/*
 * ThunderSense - as3935.c
 * AS3935 driver over SW-I2C.
 *
 * LCO calibration is a CH32V003 re-implementation of the proven Akizuki demo
 * flow (AE_AS3935DEMO) which used Martin Nawrath's FreqCounter (AVR Timer1
 * hardware counter + Timer2 gate). That AVR register code is NOT portable, so
 * we keep only the *method and target values*:
 *   - DISP_LCO on, sweep TUN_CAP 0..15, output = LCO/16 (~31.25 kHz @ 500 kHz),
 *   - gate = 100 ms  ->  target = 500000/16 * 0.1 = 3125 counts,
 *   - pick the cap with the smallest |count - 3125|; fail if outside +-3.5%.
 * The "hardware counter" becomes a SysTick-gated polled edge counter on PC4
 * (PC4=T1CH4 cannot be a TIM external-clock source on this part; polling at
 *  48 MHz easily catches a 31 kHz edge). Ref: FreqCounter.cpp (KHM LAB3).
 * (Docs/SPEC.md §10)
 */
#include "as3935.h"
#include "config.h"
#include "swi2c.h"
#include "softclock.h"
#include "debug.h"
#include "ch32fun.h"

#define AS3935_IRQ   PC4              /* ch32fun token; matches config AS3935_IRQ_* */

/* LCO calibration constants (from the proven demo) */
#define CAL_GATE_MS    100u
#define CAL_TARGET     3125u         /* 500kHz / 16 over 100 ms */
#define CAL_TOLERANCE  110u          /* ~3.5% of 3125           */

#define AS_TUN_DISP_LCO  0x80        /* reg0x08 bit7: output LCO on IRQ pin */

static uint8_t  g_tuncap = AS3935_TUN_CAP;   /* last applied/chosen TUN_CAP */
static uint16_t g_cal_count = 0;             /* last measured LCO count      */
static uint8_t  g_cal_ok = 0;                /* last calibration in tolerance */

/* Weak watchdog-kick hook: calibrate() calls this each sweep step so a
 * host-triggered recalibration (~1.6 s) does not trip the IWDG. main.c
 * overrides it to reload the watchdog. */
__attribute__((weak)) void ts_wdt_kick(void) { }

/* ---- small helpers ---- */
static as3935_health_t map_ret(swi2c_ret_t r)
{
    switch (r) {
        case SWI2C_OK:   return AS_OK;
        case SWI2C_NACK: return AS_NACK;
        default:         return AS_BUS;   /* TIMEOUT / BUSERR */
    }
}

static swi2c_ret_t as_wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    swi2c_ret_t r = swi2c_write(AS3935_ADDR, b, 2);
    softclock_delay_us(2000);        /* AS3935 needs settle after a write */
    return r;
}

static swi2c_ret_t as_rd(uint8_t reg, uint8_t *v)
{
    return swi2c_read(AS3935_ADDR, reg, v, 1);
}

uint8_t as3935_irq_level(void)
{
    return (uint8_t)funDigitalRead(AS3935_IRQ);
}

/* SysTick-gated polled rising-edge counter on the IRQ pin (FreqCounter port). */
static uint32_t lco_count(uint32_t gate_ms)
{
    uint32_t cnt = 0;
    int prev = as3935_irq_level();
    uint64_t t0 = softclock_ms();
    while ((uint32_t)(softclock_ms() - t0) < gate_ms) {
        int now = as3935_irq_level();
        if (now && !prev) cnt++;      /* rising edge */
        prev = now;
    }
    return cnt;
}

as3935_health_t as3935_config(uint8_t afe, uint8_t nf, uint8_t wdth,
                              uint8_t div, uint8_t mask_dist)
{
    swi2c_ret_t r;
    if ((r = as_wr(AS_REG_AFE, (uint8_t)(afe << 1))) != SWI2C_OK) return map_ret(r);
    if ((r = as_wr(AS_REG_NF,  (uint8_t)((nf << 4) | (wdth & 0x0F)))) != SWI2C_OK) return map_ret(r);
    /* reg0x03: [7:6]=div, [5]=mask_dist. Low nibble (int reason) is read-only. */
    uint8_t r3 = (uint8_t)((div << 6) | (mask_dist ? 0x20 : 0x00));
    if ((r = as_wr(AS_REG_INT, r3)) != SWI2C_OK) return map_ret(r);
    return AS_OK;
}

as3935_health_t as3935_calibrate(void)
{
    /* PC4 as plain input to read the LCO output during the sweep. */
    funPinMode(AS3935_IRQ, GPIO_CFGLR_IN_FLOAT);

    /* Force div = /16 (reg0x03 = 0) so target 3125 holds during calibration. */
    if (as_wr(AS_REG_INT, 0x00) != SWI2C_OK) return AS_NACK;

    uint32_t best_diff = 0xFFFFFFFFu, best_cnt = 0;
    uint8_t  best_cap = 0;

    for (uint8_t cap = 0; cap < 16; cap++) {
        ts_wdt_kick();                            /* keep IWDG alive during sweep */
        if (as_wr(AS_REG_TUN, (uint8_t)(AS_TUN_DISP_LCO | cap)) != SWI2C_OK)
            return AS_NACK;
        softclock_delay_us(3000);                 /* let the LCO settle */
        uint32_t cnt  = lco_count(CAL_GATE_MS);
        uint32_t diff = (cnt > CAL_TARGET) ? (cnt - CAL_TARGET) : (CAL_TARGET - cnt);
        DBG("[cal] cap=%2u cnt=%4lu diff=%4lu\n",
            cap, (unsigned long)cnt, (unsigned long)diff);
        if (diff < best_diff) { best_diff = diff; best_cap = cap; best_cnt = cnt; }
    }

    /* Apply the chosen cap with DISP_LCO OFF (IRQ returns to interrupt duty). */
    if (as_wr(AS_REG_TUN, best_cap) != SWI2C_OK) return AS_NACK;
    g_tuncap    = best_cap;
    g_cal_count = (uint16_t)best_cnt;
    g_cal_ok    = (best_diff <= CAL_TOLERANCE) ? 1 : 0;
    DBG("[cal] BEST cap=%u cnt=%lu diff=%lu (tol=%u)\n",
        best_cap, (unsigned long)best_cnt, (unsigned long)best_diff, CAL_TOLERANCE);

    return (best_diff > CAL_TOLERANCE) ? AS_CALIB_FAIL : AS_OK;
}

as3935_health_t as3935_setup(void)
{
    /* Direct commands from the demo: PRESET_DEFAULT (0x3C) and CALIB_RCO (0x3D). */
    if (as_wr(0x3C, 0x96) != SWI2C_OK) return AS_NACK;
    if (as_wr(0x3D, 0x96) != SWI2C_OK) return AS_NACK;

    as3935_health_t h = as3935_config(AS3935_AFE_INDOOR, AS3935_NF_LEV,
                                      AS3935_WDTH, AS3935_DIV, /*mask*/0);
    if (h != AS_OK) return h;
    return as3935_set_sensitivity(AS3935_SREJ, AS3935_MIN_NUM);   /* reg0x02 */
}

as3935_health_t as3935_init(void)
{
    as3935_health_t h = as3935_setup();
    if (h != AS_OK) return h;
    return as3935_calibrate();
}

as3935_health_t as3935_apply_tuncap(uint8_t tuncap)
{
    tuncap &= 0x0F;
    if (as_wr(AS_REG_TUN, tuncap) != SWI2C_OK) return AS_NACK;
    g_tuncap = tuncap;
    return AS_OK;
}

uint8_t as3935_last_tuncap(void) { return g_tuncap; }

as3935_health_t as3935_write_reg(uint8_t reg, uint8_t val)
{
    return map_ret(as_wr(reg, val));
}

as3935_health_t as3935_read_reg(uint8_t reg, uint8_t *val)
{
    return map_ret(as_rd(reg, val));
}

/* reg0x02: [5:4]=MIN_NUM_LIGH, [3:0]=SREJ (CL_STAT bit6 left 0). */
as3935_health_t as3935_set_sensitivity(uint8_t srej, uint8_t min_num_ligh)
{
    uint8_t v = (uint8_t)(((min_num_ligh & 0x03) << 4) | (srej & 0x0F));
    return map_ret(as_wr(0x02, v));            /* reg0x02 CL_STAT/MIN_NUM/SREJ */
}

uint16_t as3935_cal_count(void)  { return g_cal_count; }
uint16_t as3935_cal_target(void) { return CAL_TARGET; }
uint8_t  as3935_cal_ok(void)     { return g_cal_ok; }

as3935_health_t as3935_read_event(EventBin *bin)
{
    uint8_t reason;
    swi2c_ret_t r = as_rd(AS_REG_INT, &reason);   /* reading reg0x03 clears INT */
    if (r != SWI2C_OK) return map_ret(r);

    reason &= 0x0F;
    bin->type = reason;                           /* caller ORs EV_TIME_VALID  */
    bin->energy[0] = bin->energy[1] = bin->energy[2] = 0;
    bin->distance  = 0;

    if (reason == AS_INT_L) {
        uint8_t e[4];                             /* regs 0x04,0x05,0x06,0x07 */
        r = swi2c_read(AS3935_ADDR, AS_REG_ENERGY_L, e, 4);
        if (r != SWI2C_OK) return map_ret(r);
        bin->energy[0] = e[0];
        bin->energy[1] = e[1];
        bin->energy[2] = (uint8_t)(e[2] & 0x0F);  /* 20-bit total */
        bin->distance  = (uint8_t)(e[3] & 0x3F);  /* 0x3F=out of range, 0x01=overhead */
    }
    /* reason == 0 -> spurious/unknown: caller should not push a bin. */
    return AS_OK;
}
