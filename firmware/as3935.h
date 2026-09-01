/*
 * ThunderSense - as3935.h
 * AS3935 driver over SW-I2C. Register subset per AE_AS3935DEMO / demo manual.
 * (Docs/SPEC.md §10)
 */
#ifndef TS_AS3935_H
#define TS_AS3935_H

#include <stdint.h>
#include "protocol.h"

/* Driver/health result codes (feed to health module). */
typedef enum {
    AS_OK = 0,
    AS_NACK,        /* no ACK (wiring/power/dead)          */
    AS_BUS,         /* SW-I2C bus error / SDA stuck        */
    AS_STUCK_IRQ,   /* INT stays high after read+clear     */
    AS_CALIB_FAIL   /* LCO frequency out of range          */
} as3935_health_t;

/* ---------------- Register map (subset) ---------------- */
#define AS_REG_AFE       0x00   /* [5:1]=AFE_GB, [0]=PWD                      */
#define AS_REG_NF        0x01   /* [6:4]=NF_LEV, [3:0]=WDTH                   */
#define AS_REG_INT       0x03   /* [3:0]=int reason, [5]=MASK_DIST, [7:6]=DIV */
#define AS_REG_ENERGY_L  0x04
#define AS_REG_ENERGY_M  0x05
#define AS_REG_ENERGY_H  0x06   /* [3:0] used                                */
#define AS_REG_DIST      0x07   /* [5:0] distance km                         */
#define AS_REG_TUN       0x08   /* [3:0]=TUN_CAP, [7]=DISP_LCO ...           */
#define AS_REG_CALIB_RCO 0x3A
#define AS_REG_CALIB_SRCO 0x3B

/* Interrupt reason values (reg0x03 low nibble). */
#define AS_INT_NH   0x01   /* noise level too high */
#define AS_INT_D    0x04   /* disturber detected   */
#define AS_INT_L    0x08   /* lightning            */

/* Distance sentinels (reg0x07). */
#define AS_DIST_OVERHEAD  0x01
#define AS_DIST_OUTRANGE  0x3F

as3935_health_t as3935_init(void);        /* setup + calibrate */
as3935_health_t as3935_setup(void);       /* preset + config only (no calibrate) */
as3935_health_t as3935_calibrate(void);   /* LCO tuning sweep; stores chosen TUN_CAP */
as3935_health_t as3935_apply_tuncap(uint8_t tuncap);  /* apply a stored value directly */
uint8_t         as3935_last_tuncap(void); /* TUN_CAP chosen by the last calibrate */

/* Generic register access (host passthrough) and sensitivity helpers. */
as3935_health_t as3935_write_reg(uint8_t reg, uint8_t val);
as3935_health_t as3935_read_reg(uint8_t reg, uint8_t *val);
as3935_health_t as3935_set_sensitivity(uint8_t srej, uint8_t min_num_ligh); /* reg0x02 */

/* Last LCO calibration result (for host tuning readback). */
uint16_t as3935_cal_count(void);   /* measured LCO count over the gate */
uint16_t as3935_cal_target(void);  /* target count (3125) */
uint8_t  as3935_cal_ok(void);      /* 1 = within tolerance */
as3935_health_t as3935_config(uint8_t afe, uint8_t nf, uint8_t wdth,
                              uint8_t div, uint8_t mask_dist);

/* Service one event after IRQ: read reg0x03 (clears INT), classify, and on
 * lightning read energy/distance. Fills bin->type/distance/energy only;
 * the caller (capture) fills the timestamp/seq. */
as3935_health_t as3935_read_event(EventBin *bin);

/* Raw IRQ pin level, for the L2 level-poll fallback. */
uint8_t as3935_irq_level(void);

#endif /* TS_AS3935_H */
