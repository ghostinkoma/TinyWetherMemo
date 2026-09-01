/*
 * ThunderSense - protocol.h
 * I2C wire protocol shared (conceptually) with the ESP32 host.
 * Keep in exact sync with Docs/I2C_REFERENCE.md and Docs/SPEC.md §2/§4.
 */
#ifndef TS_PROTOCOL_H
#define TS_PROTOCOL_H

#include <stdint.h>
#include "config.h"

/* ---------------- Event bin: 12 bytes fixed (SPEC §2.1) ---------------- */
typedef struct __attribute__((packed)) {
    uint8_t  type;        /* [7]=time_valid, [3:0]=reason (see EV_*)          */
    uint8_t  distance;    /* km. 0x3F=out of range, 0x01=overhead, 0x00=n/a   */
    uint8_t  energy[3];   /* 20-bit LE (reg6[3:0]:reg5:reg4)                  */
    uint32_t epoch_sec;   /* LE. absolute if time_valid, else boot-relative   */
    uint16_t ms;          /* LE 0..999                                        */
    uint8_t  seq;         /* rolling event counter                            */
} EventBin;
_Static_assert(sizeof(EventBin) == 12, "EventBin must be exactly 12 bytes");

/* event reason values (AS3935 reg 0x03 low nibble) + time flag.
 * MUST match the hardware / demo: NH=0x01, D=0x04, L=0x08 (== AS_INT_* in as3935.h). */
#define EV_NOISE       0x01
#define EV_DISTURBER   0x04
#define EV_LIGHTNING   0x08
#define EV_TIME_VALID  0x80

/* ---------------- Bundle wire layout (SPEC §2.2) ---------------- */
typedef struct __attribute__((packed)) {
    uint8_t  status;             /* live status byte; NOT covered by CRC        */
    uint8_t  length;             /* number of valid bins, 0..N_BINS             */
    uint8_t  gen;                /* generation id (++ per swap, 0..255)         */
    EventBin bins[N_BINS];       /* unused entries are 0x00                     */
    uint16_t crc16;             /* CRC over [length, gen, bins] (see below)    */
} Bundle;

#define BUNDLE_WIRE_SIZE   (sizeof(Bundle))                          /* 391 @ N_BINS=32 */
#define BUNDLE_CRC_SPAN    (2u + (uint16_t)sizeof(EventBin)*N_BINS)  /* length+gen+bins */
#define BUNDLE_CRC_OFFSET  1u                                        /* CRC starts at &length */

/* ---------------- status byte bit map (SPEC §4.2) ---------------- */
#define ST_BUSY        (1u<<7)   /* mirror of STATE==BUSY_CAPTURE; rest invalid */
#define ST_DATA_OK     (1u<<6)   /* 1 = bundle usable (STATE==READY)            */
#define ST_TIME_VALID  (1u<<5)
#define ST_OVERFLOW    (1u<<4)
#define ST_STATE_MASK  0x0Fu

/* ---------------- STATE enum (status low nibble) (SPEC §4.1) ---------------- */
typedef enum {
    STATE_BOOT         = 0,
    STATE_CALIBRATING  = 1,
    STATE_READY        = 2,
    STATE_BUSY_CAPTURE = 3,
    STATE_RECOVERING   = 4,
    STATE_FAULT_NACK   = 5,
    STATE_FAULT_STUCK  = 6,
    STATE_FAULT_CALIB  = 7,
    /* 8..15 reserved */
} ts_state_t;
#define STATE_IS_FAULT(s)  ((s) >= STATE_FAULT_NACK)

/* ---------------- command opcodes (I2C_REFERENCE §3) ---------------- */
#define CMD_READ_BUNDLE  0x10   /* R: Bundle (391B, DMA-TX)                */
#define CMD_READ_STATUS  0x01   /* R: diag status (12B)                    */
#define CMD_READ_TIME    0x02   /* R: epoch(4)+ms(2)                       */
#define CMD_READ_INFO    0x03   /* R: 8B info                              */
#define CMD_READ_HEALTH  0x04   /* R: HealthData (VDD / rough temp proxy)  */
#define CMD_READ_REG     0x05   /* R: [reg,val] last register selected      */
#define CMD_READ_CALIB   0x06   /* R: CalibInfo (LCO tuning result)         */
#define CMD_CLEAR        0x20   /* W: gen, crc_lo, crc_hi                  */
#define CMD_SET_TIME     0x30   /* W: epoch(4 LE)+ms(2 LE)                 */
#define CMD_CONFIG       0x40   /* W: afe,nf,wdth,div,mask                 */
#define CMD_CMD          0x41   /* W: subcmd (0x01 recal+save / 0x02 flush / 0xA5 reset) */
#define CMD_SENS         0x42   /* W: srej, min_num_ligh (reg0x02 sensitivity) */
#define CMD_REG_WRITE    0x43   /* W: reg, val  (write ANY AS3935 register) */
#define CMD_REG_SELECT   0x44   /* W: reg       (read reg -> cache for 0x05) */

/* ---------------- CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) ---------------- */
#define CRC16_POLY   0x1021u
#define CRC16_INIT   0xFFFFu

/* ---------------- diag STATUS register (0x01) layout, 12B (I2C_REFERENCE §5) ------ */
typedef struct __attribute__((packed)) {
    uint8_t  status;            /* same encoding as bundle.status */
    uint8_t  active_gen;
    uint8_t  ship_len;
    uint8_t  boot_id;
    uint16_t lightning_total;   /* LE, monotonic */
    uint16_t disturber_total;
    uint16_t noise_total;
    uint16_t lost_total;
} DiagStatus;
_Static_assert(sizeof(DiagStatus) == 12, "DiagStatus must be 12 bytes");

/* ---------------- health register (0x04), 8B (Docs/SPEC.md §16) --------------
 * NOTE: CH32V003 has no calibrated die-temp sensor. vdd_mv (via Vrefint) is the
 * reliable metric; temp_c10 is a rough proxy (0 = not provided). */
typedef struct __attribute__((packed)) {
    uint16_t vdd_mv;         /* supply voltage estimate (mV)   */
    uint16_t vrefint_raw;    /* raw ADC of Vrefint (ch8)       */
    int16_t  temp_c10;       /* rough die-temp proxy, 0.1 C; 0 = n/a */
    uint8_t  flags;          /* bit0 = temp is a proxy only    */
    uint8_t  reserved;
} HealthData;
_Static_assert(sizeof(HealthData) == 8, "HealthData must be 8 bytes");

/* ---------------- LCO calibration result (0x06), 8B ---------------- */
typedef struct __attribute__((packed)) {
    uint8_t  tuncap;         /* chosen TUN_CAP (0..15) */
    uint8_t  ok;             /* 1 = within +-3.5% tolerance */
    uint16_t count;          /* measured LCO count over the gate */
    uint16_t target;         /* target count (3125) */
    uint8_t  rco_status;     /* reg0x3A<<0 hi nibble | reg0x3B (SRCO/TRCO done/nok) */
    uint8_t  div;            /* LCO_FDIV used (reg0x03[7:6]) */
} CalibInfo;
_Static_assert(sizeof(CalibInfo) == 8, "CalibInfo must be 8 bytes");

#endif /* TS_PROTOCOL_H */
