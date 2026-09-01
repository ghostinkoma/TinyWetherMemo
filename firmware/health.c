/*
 * ThunderSense - health.c
 * Sensor/bus fault detection & recovery FSM (Docs/SPEC.md §14).
 *
 * Escalation:  AS_NACK/AS_BUS  -> retry, then bus-recover, then re-init/re-cal,
 *              and if still failing -> FAULT_* state. Recovery retries run in
 *              the background paced by HEALTH_RETRY_PERIOD_MS.
 * Fail-safe: even while FAULT, the HW-I2C slave keeps answering (empty valid
 *            bundles + FAULT state), so the upper bus never dies.
 */
#include "health.h"
#include "config.h"
#include "swi2c.h"
#include "as3935.h"
#include "softclock.h"
#include "debug.h"

static ts_state_t g_state = STATE_BOOT;
static uint8_t    g_fail_count = 0;
static uint64_t   g_next_retry_ms = 0;

void health_init(void)
{
    g_state = STATE_BOOT;
    g_fail_count = 0;
    g_next_retry_ms = 0;
}

/* Map the worst observed condition into a state; enter RECOVERING on repeated
 * transient errors before declaring a hard FAULT. */
void health_note(as3935_health_t h)
{
    switch (h) {
    case AS_OK:
        if (g_state != STATE_BUSY_CAPTURE) g_state = STATE_READY;
        g_fail_count = 0;
        break;
    case AS_CALIB_FAIL:
        g_state = STATE_FAULT_CALIB;
        break;
    case AS_STUCK_IRQ:
        g_state = STATE_FAULT_STUCK;
        break;
    case AS_NACK:
    case AS_BUS:
    default:
        if (++g_fail_count >= AS3935_RETRY) {
            g_state = (h == AS_BUS) ? STATE_FAULT_STUCK : STATE_FAULT_NACK;
        } else if (g_state == STATE_READY || g_state == STATE_BOOT) {
            g_state = STATE_RECOVERING;
        }
        break;
    }
}

void health_tick(void)
{
    if (!health_is_fault() && g_state != STATE_RECOVERING) return;

    uint64_t now = softclock_ms();
    if ((int64_t)(now - g_next_retry_ms) < 0) return;   /* pace retries */
    g_next_retry_ms = now + HEALTH_RETRY_PERIOD_MS;

    DBG("[health] recovery attempt (state=%u)\n", g_state);
    g_state = STATE_RECOVERING;

    /* A -> bus recover, B -> re-init (+re-calibrate). */
    swi2c_bus_recover();
    as3935_health_t h = as3935_init();      /* includes calibrate() */

    if (h == AS_OK) {
        g_state = STATE_READY;
        g_fail_count = 0;
        DBG("[health] recovered\n");
    } else if (h == AS_CALIB_FAIL) {
        g_state = STATE_FAULT_CALIB;
    } else {
        g_state = STATE_FAULT_NACK;
    }
}

ts_state_t health_state(void) { return g_state; }

uint8_t health_is_fault(void) { return STATE_IS_FAULT(g_state) ? 1 : 0; }

/* Let the rest of the system set lifecycle states (BOOT/CALIBRATING/READY). */
void health_set_state(ts_state_t s) { g_state = s; }
