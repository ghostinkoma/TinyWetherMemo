/*==============================================================================
  ThunderSense.cpp  -  see ThunderSense.h for the protocol and usage.
==============================================================================*/
#include "ThunderSense.h"

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) - must match the bridge.
uint16_t ThunderSense::crc16(const uint8_t* p, uint16_t n) {
  uint16_t crc = 0xFFFF;
  while (n--) {
    crc ^= (uint16_t)(*p++) << 8;
    for (uint8_t i = 0; i < 8; i++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

bool ThunderSense::begin(TwoWire& wire, uint8_t addr) {
  _w = &wire;
  _addr = addr;
#if defined(ESP32) || defined(ESP8266)
  _w->setBufferSize(TS_BUNDLE_SIZE + 8);   // fit a whole bundle in one read
#endif
  TSInfo info;
  return readInfo(info);                   // proves the bridge is answering
}

// --- low level ---------------------------------------------------------------
bool ThunderSense::writeCmd(uint8_t op, const uint8_t* args, uint8_t n) {
  _w->beginTransmission(_addr);
  _w->write(op);
  for (uint8_t i = 0; i < n; i++) _w->write(args[i]);
  return _w->endTransmission() == 0;
}

int ThunderSense::readResp(uint8_t op, uint8_t* buf, uint16_t n) {
  _w->beginTransmission(_addr);
  _w->write(op);
  if (_w->endTransmission(false) != 0) return TS_ERR_I2C;   // repeated-START
  uint16_t got = _w->requestFrom((int)_addr, (int)n);
  if (got < n) { while (_w->available()) _w->read(); return TS_ERR_I2C; }
  for (uint16_t i = 0; i < n; i++) buf[i] = _w->read();
  return (int)n;
}

// --- main poll ---------------------------------------------------------------
int ThunderSense::poll(TSEvent* out, uint8_t maxEvents) {
  uint8_t buf[TS_BUNDLE_SIZE];
  if (readResp(CMD_READ_BUNDLE, buf, TS_BUNDLE_SIZE) != TS_BUNDLE_SIZE)
    return TS_ERR_I2C;

  uint8_t status = buf[0];
  if (status & 0x80) return 0;                 // BUSY  -> retry later
  if (!(status & 0x40)) return 0;              // !DATA_OK (boot/calib/fault)

  uint8_t length = buf[1];
  uint8_t gen    = buf[2];
  if (length > TS_MAX_EVENTS) return TS_ERR_CRC;

  // CRC covers [length, gen, bins] = bytes 1..386
  uint16_t crcRecv = (uint16_t)buf[387] | ((uint16_t)buf[388] << 8);
  uint16_t crcCalc = crc16(&buf[1], 2 + TS_MAX_EVENTS * TS_BIN_SIZE);
  if (crcRecv != crcCalc) return TS_ERR_CRC;   // not acked -> safe retry

  uint8_t n = (length < maxEvents) ? length : maxEvents;
  for (uint8_t i = 0; i < n; i++) {
    const uint8_t* b = &buf[3 + i * TS_BIN_SIZE];
    out[i].type       = b[0];
    out[i].distanceKm = b[1];
    out[i].energy     = (uint32_t)b[2] | ((uint32_t)b[3] << 8) | ((uint32_t)b[4] << 16);
    out[i].epoch      = (uint32_t)b[5] | ((uint32_t)b[6] << 8) |
                        ((uint32_t)b[7] << 16) | ((uint32_t)b[8] << 24);
    out[i].ms         = (uint16_t)b[9] | ((uint16_t)b[10] << 8);
    out[i].seq        = b[11];
  }

  // ACK: only clear if we actually consumed everything (else re-read next time).
  if (n >= length) {
    uint8_t a[3] = { gen, (uint8_t)(crcRecv & 0xFF), (uint8_t)(crcRecv >> 8) };
    writeCmd(CMD_CLEAR, a, 3);
  }
  return n;
}

// --- time --------------------------------------------------------------------
bool ThunderSense::syncTime(uint32_t epoch, uint16_t ms) {
  uint8_t a[6] = {
    (uint8_t)epoch, (uint8_t)(epoch >> 8), (uint8_t)(epoch >> 16), (uint8_t)(epoch >> 24),
    (uint8_t)ms, (uint8_t)(ms >> 8)
  };
  return writeCmd(CMD_SET_TIME, a, 6);
}

// --- reads -------------------------------------------------------------------
bool ThunderSense::readStatus(TSStatus& s) {
  uint8_t b[12];
  if (readResp(CMD_READ_STATUS, b, 12) != 12) return false;
  s.raw            = b[0];
  s.state          = (TSState)(b[0] & 0x0F);
  s.busy           = b[0] & 0x80;
  s.dataOk         = b[0] & 0x40;
  s.timeValid      = b[0] & 0x20;
  s.overflow       = b[0] & 0x10;
  s.activeGen      = b[1];
  s.pending        = b[2];
  s.bootId         = b[3];
  s.lightningTotal = (uint16_t)b[4]  | ((uint16_t)b[5]  << 8);
  s.disturberTotal = (uint16_t)b[6]  | ((uint16_t)b[7]  << 8);
  s.noiseTotal     = (uint16_t)b[8]  | ((uint16_t)b[9]  << 8);
  s.lostTotal      = (uint16_t)b[10] | ((uint16_t)b[11] << 8);
  return true;
}

bool ThunderSense::readHealth(TSHealth& h) {
  uint8_t b[8];
  if (readResp(CMD_READ_HEALTH, b, 8) != 8) return false;
  h.vddMv      = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
  h.vrefintRaw = (uint16_t)b[2] | ((uint16_t)b[3] << 8);
  h.tempC10    = (int16_t)((uint16_t)b[4] | ((uint16_t)b[5] << 8));
  h.flags      = b[6];
  return true;
}

bool ThunderSense::readCalib(TSCalib& c) {
  uint8_t b[8];
  if (readResp(CMD_READ_CALIB, b, 8) != 8) return false;
  c.tuncap    = b[0];
  c.ok        = b[1];
  c.count     = (uint16_t)b[2] | ((uint16_t)b[3] << 8);
  c.target    = (uint16_t)b[4] | ((uint16_t)b[5] << 8);
  c.rcoStatus = b[6];
  c.div       = b[7];
  return true;
}

bool ThunderSense::readInfo(TSInfo& info) {
  uint8_t b[8];
  if (readResp(CMD_READ_INFO, b, 8) != 8) return false;
  info.magic[0]   = (char)b[0];
  info.magic[1]   = (char)b[1];
  info.fwMajor    = b[2];
  info.fwMinor    = b[3];
  info.recordSize = b[4];
  info.fifoDepth  = b[5];
  return true;
}

TSState ThunderSense::state() {
  TSStatus s;
  return readStatus(s) ? s.state : TS_FAULT_NACK;
}

// --- tuning / sensitivity ----------------------------------------------------
bool ThunderSense::setSensitivity(uint8_t srej, uint8_t minNum) {
  uint8_t a[2] = { (uint8_t)(srej & 0x0F), (uint8_t)(minNum & 0x03) };
  return writeCmd(CMD_SENS, a, 2);
}

bool ThunderSense::configure(bool indoor, uint8_t nf, uint8_t wdth, bool mask) {
  // AFE_GB: indoor 0x12, outdoor 0x0E.
  uint8_t a[5] = { (uint8_t)(indoor ? 0x12 : 0x0E), nf, wdth, 0 /*div /16*/,
                   (uint8_t)(mask ? 1 : 0) };
  return writeCmd(CMD_CONFIG, a, 5);
}

bool ThunderSense::recalibrate() {
  uint8_t sub = 0x01;
  return writeCmd(CMD_CMD, &sub, 1);
}

bool ThunderSense::clearStats() {
  // reg0x02 CL_STAT sequence: 1 -> 0 -> 1 on bit6, keeping SREJ/MIN_NUM.
  // Simplest: pulse via register passthrough (SREJ default 2, min 0).
  writeReg(0x02, (uint8_t)(0x40 | 0x02));   // CL_STAT=1
  writeReg(0x02, (uint8_t)(0x00 | 0x02));   // CL_STAT=0
  return writeReg(0x02, (uint8_t)(0x40 | 0x02));
}

// --- raw register passthrough ------------------------------------------------
bool ThunderSense::writeReg(uint8_t reg, uint8_t val) {
  uint8_t a[2] = { reg, val };
  return writeCmd(CMD_REG_WRITE, a, 2);
}

bool ThunderSense::readReg(uint8_t reg, uint8_t& val) {
  if (!writeCmd(CMD_REG_SELECT, &reg, 1)) return false;
  delay(3);                                 // let the bridge do the SW-I2C read
  uint8_t b[2];
  if (readResp(CMD_READ_REG, b, 2) != 2) return false;
  val = b[1];
  return true;
}

int16_t ThunderSense::readReg(uint8_t reg) {
  uint8_t v;
  return readReg(reg, v) ? (int16_t)v : -1;
}

// Read-modify-write one field: reg = (reg & ~mask) | (value & mask).
bool ThunderSense::updateReg(uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t cur;
  for (uint8_t tries = 0; tries < 3; tries++) {      // may be BUSY mid-capture
    if (readReg(reg, cur)) {
      return writeReg(reg, (uint8_t)((cur & ~mask) | (value & mask)));
    }
    delay(5);
  }
  return false;
}

// --- independent field setters ----------------------------------------------
bool ThunderSense::setGain(bool indoor) {
  // AFE_GB at [5:1]: indoor 0x12, outdoor 0x0E -> written shifted <<1.
  return updateReg(0x00, 0x3E, (uint8_t)((indoor ? 0x12 : 0x0E) << 1));
}
bool ThunderSense::setNoiseFloor(uint8_t level) {   // NF_LEV reg0x01[6:4]
  return updateReg(0x01, 0x70, (uint8_t)((level & 0x07) << 4));
}
bool ThunderSense::setWatchdog(uint8_t thr) {       // WDTH reg0x01[3:0]
  return updateReg(0x01, 0x0F, (uint8_t)(thr & 0x0F));
}
bool ThunderSense::setSpikeRejection(uint8_t srej) {// SREJ reg0x02[3:0]
  return updateReg(0x02, 0x0F, (uint8_t)(srej & 0x0F));
}
bool ThunderSense::setMinLightnings(uint8_t code) { // MIN_NUM_LIGH reg0x02[5:4]
  return updateReg(0x02, 0x30, (uint8_t)((code & 0x03) << 4));
}
bool ThunderSense::setMaskDisturbers(bool on) {     // MASK_DIST reg0x03[5]
  return updateReg(0x03, 0x20, (uint8_t)(on ? 0x20 : 0x00));
}
bool ThunderSense::setFreqDivision(uint8_t code) {  // LCO_FDIV reg0x03[7:6]
  return updateReg(0x03, 0xC0, (uint8_t)((code & 0x03) << 6));
}

int16_t ThunderSense::readDistanceKm() {            // reg0x07[5:0]
  int16_t v = readReg(0x07);
  return (v < 0) ? v : (int16_t)(v & 0x3F);
}
int16_t ThunderSense::readInterruptReason() {       // reg0x03[3:0]
  int16_t v = readReg(0x03);
  return (v < 0) ? v : (int16_t)(v & 0x0F);
}

void ThunderSense::getTotals(uint16_t& l, uint16_t& d, uint16_t& n, uint16_t& lost) {
  TSStatus s;
  if (readStatus(s)) { l = s.lightningTotal; d = s.disturberTotal;
                       n = s.noiseTotal;     lost = s.lostTotal; }
}
