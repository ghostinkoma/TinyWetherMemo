/*
 * ThunderSense - main.c
 * Startup + main loop for the AS3935 -> CH32V003 -> ESP32 I2C bridge.
 * (Docs/README.md, Docs/SPEC.md)
 *
 * Boot:   BOOT -> CALIBRATING -> READY.  LCO calibration is loaded from flash
 *         (option bytes) when available; else run once and persisted.
 * Loop:   capture layers, background bundle clear, host command, recovery FSM,
 *         periodic health sample, publish state, kick IWDG.
 * Priority: EXTI(capture L1) > SysTick > I2C slave > main loop (SPEC §3).
 */
#include "config.h"
#include "mem.h"
#include "debug.h"
#include "softclock.h"
#include "swi2c.h"
#include "as3935.h"
#include "bundle.h"
#include "capture.h"
#include "i2c_slave.h"
#include "health.h"
#include "adc.h"
#include "nvcal.h"
#include "ch32fun.h"

/* ---- Independent Watchdog (IWDG) ---- */
static void iwdg_start(void)
{
    /* LSI ~128 kHz, prescaler /32 -> 4 kHz -> reload = timeout_ms * 4. */
    IWDG->CTLR = 0x5555;               /* enable register access */
    IWDG->PSCR = 3;                    /* /32 */
    IWDG->RLDR = (IWDG_TIMEOUT_MS * 4) & 0x0FFF;
    IWDG->CTLR = 0xAAAA;               /* reload */
    IWDG->CTLR = 0xCCCC;               /* start */
}
static inline void iwdg_kick(void) { IWDG->CTLR = 0xAAAA; }

/* Override the weak hook in as3935.c: keep IWDG alive during the ~1.6 s
 * host-triggered LCO recalibration sweep. */
void ts_wdt_kick(void) { IWDG->CTLR = 0xAAAA; }

int main(void)
{
    SystemInit();
    funGpioInitAll();

    DBG_INIT();
    DBG("\n[ThunderSense] boot\n");

    softclock_init();
    adc_init();
    bundle_init();
    swi2c_init();
    health_init();

#if DEBUG
    /* --- pin-driver test (push-pull, pull-up-independent) --- SCL=PA1, SDA=PA2 */
    funPinMode(PA1, GPIO_CFGLR_OUT_10Mhz_PP);
    funPinMode(PA2, GPIO_CFGLR_OUT_10Mhz_PP);
    funDigitalWrite(PA1, FUN_HIGH); funDigitalWrite(PA2, FUN_HIGH);
    softclock_delay_us(100);
    DBG("[pp] high     SCL=%d SDA=%d (expect 1 1 = driver OK)\n",
        funDigitalRead(PA1), funDigitalRead(PA2));
    funDigitalWrite(PA1, FUN_LOW); funDigitalWrite(PA2, FUN_LOW);
    softclock_delay_us(100);
    DBG("[pp] low      SCL=%d SDA=%d (expect 0 0)\n",
        funDigitalRead(PA1), funDigitalRead(PA2));
    /* --- open-drain + external pull-up test --- */
    funPinMode(PA1, GPIO_CFGLR_OUT_10Mhz_OD);
    funPinMode(PA2, GPIO_CFGLR_OUT_10Mhz_OD);
    funDigitalWrite(PA1, FUN_HIGH); funDigitalWrite(PA2, FUN_HIGH);
    softclock_delay_us(100);
    DBG("[od] released SCL=%d SDA=%d (1 1 = pull-ups reach these pins)\n",
        funDigitalRead(PA1), funDigitalRead(PA2));

    /* One-time SW-I2C bus scan: which 7-bit addresses ACK? (diagnose AS3935) */
    DBG("[scan] i2c ACK:");
    for (uint8_t a = 0; a < 0x78; a++) {
        if (swi2c_write(a, 0, 0) == SWI2C_OK) DBG(" %02x", a);
    }
    DBG(" (done)\n");
    /* Probe likely AS3935 addresses: read reg0x00, show value + ret. */
    for (uint8_t a = 0; a <= 0x03; a++) {
        uint8_t v = 0xEE;
        swi2c_ret_t r = swi2c_read(a, 0x00, &v, 1);
        DBG("[probe] addr %02x reg00=%02x ret=%d\n", a, v, (int)r);
    }
#endif

    /* Upper-bus slave up early so the host can see states during calibration. */
    i2c_slave_init(HWI2C_SLAVE_ADDR);
    i2c_slave_set_state(STATE_BOOT);

    /* Sensor setup, then calibration: reuse the flash-stored TUN_CAP if present
     * (fast boot), otherwise calibrate once and persist. */
    health_set_state(STATE_CALIBRATING);
    i2c_slave_set_state(STATE_CALIBRATING);

    as3935_health_t h = as3935_setup();
    if (h == AS_OK) {
        uint8_t cap;
        if (nvcal_load(&cap)) {
            as3935_apply_tuncap(cap);
            DBG("[boot] TUN_CAP from flash = %u\n", cap);
            health_set_state(STATE_READY);
        } else {
            h = as3935_calibrate();
            if (h == AS_OK) {
                nvcal_save(as3935_last_tuncap());
                DBG("[boot] calibrated TUN_CAP=%u, saved\n", as3935_last_tuncap());
                health_set_state(STATE_READY);
            } else {
                health_note(h);
            }
        }
    } else {
        health_note(h);
        DBG("[boot] sensor setup failed h=%u\n", h);
    }

    capture_init();
    iwdg_start();

    uint64_t last_health = 0;
    for (;;) {
        capture_tick();                /* L2/L4 + deferred service            */
        bundle_tick_clear();           /* background zero-clear (CRC-first)   */
        i2c_slave_process_cmd();       /* execute queued host command         */
        health_tick();                 /* background recovery when faulted    */

        uint64_t now = softclock_ms();
        if ((uint32_t)(now - last_health) >= 1000u) {   /* ~1 Hz health sample */
            last_health = now;
            i2c_slave_set_health(adc_vdd_mv(), adc_read(8));
            extern volatile uint32_t g_i2c_addr_hits, g_i2c_resets;   /* diag */
            DBG("[i2c] addrHits=%lu resets=%lu\n",
                (unsigned long)g_i2c_addr_hits, (unsigned long)g_i2c_resets);
        }

        i2c_slave_set_state(health_state());
        if (i2c_slave_stuck()) i2c_slave_reset();

        iwdg_kick();
    }
}
