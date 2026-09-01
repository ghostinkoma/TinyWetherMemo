/*==============================================================================
  ThunderSense.h  -  Arduino/ESP32 host library for the AS3935 -> CH32V003 -> I2C
                     lightning-sensor bridge ("ThunderSense").

  WHAT THIS IS
  ------------
  The CH32V003 bridge captures AS3935 lightning/disturber events in real time,
  timestamps them, and buffers the strongest strikes. Your ESP32 (this library)
  just POLLS it over I2C and gets clean, timestamped events - no interrupt
  wrangling, no AS3935 register juggling on the host.

  WIRING (host side)
  ------------------
    ESP32 SDA  ->  bridge PC1 (I2C SDA)
    ESP32 SCL  ->  bridge PC2 (I2C SCL)
    GND common. Bridge runs at 3.3 V. I2C pull-ups are on the AS3935 module.

  60-SECOND USAGE
  ---------------
    #include <Wire.h>
    #include "ThunderSense.h"
    ThunderSense ts;

    void setup() {
      Serial.begin(115200);
      Wire.begin();                 // ESP32: Wire.begin(sdaPin, sclPin);
      ts.begin(Wire);               // (also raises the I2C RX buffer to fit a bundle)
      ts.syncTime(myUnixEpoch, 0);  // give it NTP time so events get absolute stamps
    }

    void loop() {
      TSEvent ev[TS_MAX_EVENTS];
      int n = ts.poll(ev, TS_MAX_EVENTS);        // reads + verifies CRC + ACKs
      for (int i = 0; i < n; i++) {
        if (ev[i].isLightning())
          Serial.printf("LIGHTNING %u km, energy %lu, t=%lu.%03u\n",
                        ev[i].distanceKm, (unsigned long)ev[i].energy,
                        (unsigned long)ev[i].epoch, ev[i].ms);
      }
      delay(500);                                 // poll period is up to you
    }

  HOW POLLING WORKS (so you can trust it)
  ---------------------------------------
    poll() does the full, loss-safe handshake for you:
      1. read the fixed-size bundle  (cmd 0x10)
      2. if BUSY -> return 0 (bridge is mid-capture; just call again)
      3. verify the CRC16 over [length,gen,bins]
      4. copy out the events
      5. ACK with CLEAR(gen,crc)  (cmd 0x20)  <- only now does the bridge drop them
    If the read is corrupted, poll() does NOT ack, so nothing is lost - the same
    events come back on the next poll.

  NOTES
  -----
  * Only LIGHTNING events carry distance/energy. Disturbers/noise are counted
    (see readStatus()/getTotals()) but not delivered as events.
  * A bundle holds up to TS_MAX_EVENTS(=32) strongest strikes since the last ACK.
    Poll often enough that you do not overflow (readStatus().overflow tells you).
  * Requires an I2C host that can read ~389 bytes in one transaction (ESP32 OK;
    classic AVR Uno's 32-byte Wire buffer is too small).
==============================================================================*/
#ifndef THUNDERSENSE_H
#define THUNDERSENSE_H

#include <Arduino.h>
#include <Wire.h>

// ---- defaults ---------------------------------------------------------------
#define TS_DEFAULT_ADDR   0x28    // bridge 7-bit I2C slave address
#define TS_MAX_EVENTS     32      // bins per bundle (bridge N_BINS)
#define TS_BIN_SIZE       12      // wire bytes per event
#define TS_BUNDLE_SIZE    (3 + TS_MAX_EVENTS * TS_BIN_SIZE + 2)  // 389

// ---- return codes (poll / low-level) ----------------------------------------
enum {
  TS_ERR_I2C   = -1,   // transaction failed / short read
  TS_ERR_CRC   = -2,   // CRC mismatch (not acked; will retry next poll)
  TS_ERR_BUSY  = -3    // (poll returns 0 for busy; this is for low-level calls)
};

// ---- bridge lifecycle state (low nibble of the status byte) -----------------
enum TSState : uint8_t {
  TS_BOOT        = 0,
  TS_CALIBRATING = 1,
  TS_READY       = 2,
  TS_BUSY_CAPTURE= 3,
  TS_RECOVERING  = 4,
  TS_FAULT_NACK  = 5,   // AS3935 not responding
  TS_FAULT_STUCK = 6,   // SDA/IRQ stuck
  TS_FAULT_CALIB = 7    // LCO calibration failed
};

// ---- AS3935 interrupt reason (event type) -----------------------------------
enum TSEventType : uint8_t {
  TS_EV_NOISE     = 0x01,
  TS_EV_DISTURBER = 0x04,
  TS_EV_LIGHTNING = 0x08
};

// ---- one lightning event ----------------------------------------------------
struct TSEvent {
  uint8_t  type;        // TSEventType in [3:0]; bit7 = time was valid
  uint8_t  distanceKm;  // 0x3F = out of range, 0x01 = overhead, 0 = n/a
  uint32_t energy;      // 20-bit relative energy (no physical unit)
  uint32_t epoch;       // absolute Unix seconds (or boot-relative if !timeValid)
  uint16_t ms;          // 0..999
  uint8_t  seq;         // rolling sequence number (gap detection)

  bool isLightning() const { return (type & 0x0F) == TS_EV_LIGHTNING; }
  bool isDisturber() const { return (type & 0x0F) == TS_EV_DISTURBER; }
  bool isNoise()     const { return (type & 0x0F) == TS_EV_NOISE; }
  bool timeValid()   const { return (type & 0x80) != 0; }
};

// ---- bridge status / diagnostics (cmd 0x01) ---------------------------------
struct TSStatus {
  uint8_t  raw;              // the status byte
  TSState  state;
  bool     busy;            // mid-capture; retry
  bool     dataOk;          // bundle is usable
  bool     timeValid;       // clock synced via syncTime()
  bool     overflow;        // events were dropped since last ACK
  uint8_t  activeGen;
  uint8_t  pending;         // events waiting to be read
  uint8_t  bootId;          // changes on bridge reset (re-sync time when it does)
  uint16_t lightningTotal;  // monotonic (survive overflow)
  uint16_t disturberTotal;
  uint16_t noiseTotal;
  uint16_t lostTotal;
};

// ---- supply / health (cmd 0x04) ---------------------------------------------
// NOTE: the CH32V003 has NO calibrated die-temp sensor. vddMv is the real,
// useful number (brownout watch). tempC10 is a proxy and is 0 = not provided.
struct TSHealth {
  uint16_t vddMv;
  uint16_t vrefintRaw;
  int16_t  tempC10;      // 0.1 C; 0 = not available
  uint8_t  flags;        // bit0 = temp is a proxy only
};

// ---- LCO tuning result (cmd 0x06) -------------------------------------------
struct TSCalib {
  uint8_t  tuncap;   // 0..15 chosen tuning capacitor
  uint8_t  ok;       // 1 = antenna within +-3.5% of 500 kHz
  uint16_t count;    // measured LCO count over the gate
  uint16_t target;   // ideal count (3125)
  uint8_t  rcoStatus;
  uint8_t  div;      // LCO_FDIV used
};

// ---- firmware info (cmd 0x03) -----------------------------------------------
struct TSInfo {
  char     magic[2];   // 'T','S'
  uint8_t  fwMajor, fwMinor;
  uint8_t  recordSize; // 12
  uint8_t  fifoDepth;  // 32
};

//==============================================================================
class ThunderSense {
public:
  // Bind to an I2C bus. On ESP32 this also enlarges the Wire RX buffer so a
  // full bundle fits in one read. Returns false if the bridge INFO can't be read.
  bool begin(TwoWire& wire = Wire, uint8_t addr = TS_DEFAULT_ADDR);

  // ---- main loop call -------------------------------------------------------
  // Poll for new events. Reads the bundle, verifies CRC, copies up to
  // maxEvents into out[], and ACKs the bridge. Give an array of TS_MAX_EVENTS.
  //   >0 : number of events returned
  //    0 : nothing new / bridge BUSY / not READY (check state() if unsure)
  //   <0 : TS_ERR_* (nothing acked; safe to retry)
  int poll(TSEvent* out, uint8_t maxEvents);

  // ---- time --------------------------------------------------------------
  // Seed the bridge clock with absolute Unix time so events get absolute
  // stamps. Call once after NTP, and again whenever status().bootId changes.
  bool syncTime(uint32_t unixEpoch, uint16_t ms = 0);

  // ---- reads -------------------------------------------------------------
  bool readStatus(TSStatus& out);   // 0x01
  bool readHealth(TSHealth& out);   // 0x04
  bool readCalib (TSCalib&  out);   // 0x06
  bool readInfo  (TSInfo&   out);   // 0x03
  TSState state();                  // convenience: just the lifecycle state

  // ---- sensor tuning / sensitivity ---------------------------------------
  // These set ALL relevant AS3935 registers at once (one command each):
  bool setSensitivity(uint8_t srej, uint8_t minNum = 0);          // 0x42 (reg0x02)
  bool configure(bool indoor, uint8_t noiseFloor = 2, uint8_t watchdog = 2,
                 bool maskDisturbers = false);                     // 0x40 (reg0x00/01/03)

  // ---- INDEPENDENT setters (change ONE field, keep the rest) --------------
  // Each is a read-modify-write over the raw register passthrough, so you can
  // tweak a single knob at runtime by polling from the host.
  //
  //   AS3935 register / field map (see AE-AS3935 datasheet):
  //     reg0x00 [5:1] AFE_GB  gain   (indoor 0x12 / outdoor 0x0E), [0] PWD
  //     reg0x01 [6:4] NF_LEV  noise floor (0..7), [3:0] WDTH watchdog (0..15)
  //     reg0x02 [5:4] MIN_NUM_LIGH,  [3:0] SREJ spike rejection, [6] CL_STAT
  //     reg0x03 [7:6] LCO_FDIV (antenna tuning div - the BRIDGE owns this),
  //             [5] MASK_DIST, [3:0] INT (read-only interrupt reason)
  //     reg0x07 [5:0] DISTANCE estimate (km; 0x3F out of range, 0x01 overhead)

  bool setGain(bool indoor);              // AFE_GB indoor/outdoor            (任意)
  bool setNoiseFloor(uint8_t level0to7);  // NF_LEV threshold                 (任意)
  bool setWatchdog(uint8_t thr0to15);     // WDTH watchdog threshold          (任意)
  bool setSpikeRejection(uint8_t srej);   // SREJ only (keeps MIN_NUM)        (任意)
  bool setMinLightnings(uint8_t code0to3);// MIN_NUM_LIGH (0=1,1=5,2=9,3=16)  (任意)
  bool setMaskDisturbers(bool on);        // MASK_DIST                        (任意)

  // Antenna tuning frequency-division ratio (LCO_FDIV). The BRIDGE manages this
  // during calibration; you normally never touch it. Exposed only for experts -
  // changing it will invalidate the current calibration.
  bool setFreqDivision(uint8_t code0to3); // 0=/16,1=/32,2=/64,3=/128 (bridge-owned)

  // Distance / interrupt readouts direct from the sensor (also delivered per
  // event via TSEvent; these read the live register).
  int16_t readDistanceKm();               // reg0x07 [5:0]; <0 on I2C error
  int16_t readInterruptReason();          // reg0x03 [3:0]; TSEventType or 0

  // Kick off an antenna re-calibration (~1.6 s) and persist it to the bridge's
  // flash. Poll state() until READY, then readCalib() for the result.
  bool recalibrate();                                              // 0x41/0x01
  bool clearStats();                                               // reg0x02 CL_STAT

  // ---- raw AS3935 register passthrough (advanced) ------------------------
  bool    writeReg(uint8_t reg, uint8_t val);   // 0x43  (any AS3935 register)
  bool    readReg (uint8_t reg, uint8_t& val);  // 0x44 select -> 0x05 read
  int16_t readReg (uint8_t reg);                // convenience: <0 on error

  // ---- convenience -------------------------------------------------------
  void getTotals(uint16_t& lightning, uint16_t& disturber,
                 uint16_t& noise, uint16_t& lost);

  // low-level command opcodes (documented in Docs/I2C_REFERENCE.md)
  enum : uint8_t {
    CMD_READ_STATUS=0x01, CMD_READ_TIME=0x02, CMD_READ_INFO=0x03,
    CMD_READ_HEALTH=0x04, CMD_READ_REG=0x05,  CMD_READ_CALIB=0x06,
    CMD_READ_BUNDLE=0x10, CMD_CLEAR=0x20,     CMD_SET_TIME=0x30,
    CMD_CONFIG=0x40, CMD_CMD=0x41, CMD_SENS=0x42,
    CMD_REG_WRITE=0x43, CMD_REG_SELECT=0x44
  };

private:
  TwoWire* _w   = &Wire;
  uint8_t  _addr = TS_DEFAULT_ADDR;

  static uint16_t crc16(const uint8_t* p, uint16_t n);  // CCITT-FALSE 0x1021/0xFFFF
  bool     writeCmd(uint8_t op, const uint8_t* args, uint8_t n);
  int      readResp(uint8_t op, uint8_t* buf, uint16_t n);  // returns bytes read
  bool     updateReg(uint8_t reg, uint8_t mask, uint8_t value); // read-modify-write
};

#endif // THUNDERSENSE_H
