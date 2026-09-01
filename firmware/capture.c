/*
 * ThunderSense - capture.c
 * Multi-layer event capture with a single service entry (Docs/SPEC.md §3.5).
 *
 *   L1 EXTI on PC4 (highest prio ISR): latch the absolute event time in a few
 *      microseconds and arm a 2 ms settle window. NEVER does SW-I2C here.
 *   L2 level poll (main loop): AS3935 INT is level-held until reg0x03 is read,
 *      so a missed edge is caught by seeing IRQ still high.
 *   L4 defensive periodic read (main loop): liveness probe / stuck detection.
 *   L3 TIM1_CH4 input-capture: optional HW time latch (ENABLE_L3_TIM_CAPTURE).
 *
 * All layers funnel through capture_request() -> capture_service(); the service
 * is re-entrancy guarded so one event is captured exactly once.
 *
 * The actual AS3935 read runs in the MAIN LOOP (not the EXTI ISR): the event is
 * latched by hardware (INT held) + software (timestamp), so deferring the ~1 ms
 * SW-I2C read is safe and keeps the EXTI ISR tiny. Host reads see BUSY meanwhile.
 */
#include "capture.h"
#include "config.h"
#include "as3935.h"
#include "bundle.h"
#include "softclock.h"
#include "health.h"
#include "debug.h"
#include "ch32fun.h"

#define IRQ_LINE_BIT   (1u << AS3935_IRQ_PIN)     /* EXTI line 4 = PC4 */
#define DEFENSIVE_MS   100u

static volatile uint8_t  g_pending = 0;   /* event awaiting service            */
static volatile uint8_t  g_reading = 0;   /* SW-I2C read in progress           */
static volatile uint64_t g_capture_ms = 0;/* latched event time (earliest)     */
static volatile uint64_t g_due_ms = 0;    /* when the 2 ms settle elapses       */
static uint64_t          g_last_defensive = 0;

/* ---------------- init ---------------- */
void capture_init(void)
{
    /* PC4 as floating input for EXTI (PC4 must match config AS3935_IRQ_PIN=4). */
    funPinMode(PC4, GPIO_CFGLR_IN_FLOAT);

    /* Route EXTI line 4 to port C (AFIO->EXTICR: 2 bits/line; PC = 0b10). */
    AFIO->EXTICR &= ~(0x3u << (AS3935_IRQ_PIN * 2));
    AFIO->EXTICR |=  (0x2u << (AS3935_IRQ_PIN * 2));   /* 0b10 = port C */

    EXTI->RTENR  |= IRQ_LINE_BIT;    /* rising edge (INT goes high on event) */
    EXTI->FTENR  &= ~IRQ_LINE_BIT;
    EXTI->INTENR |= IRQ_LINE_BIT;    /* enable interrupt on line 4 */

    /* EXTI must be the highest-priority IRQ (preempts I2C/SysTick).
     * NOTE: verify the priority scheme on your ch32fun/PFIC build. */
    NVIC_SetPriority(EXTI7_0_IRQn, 0x00);   /* highest */
    NVIC_EnableIRQ(EXTI7_0_IRQn);

#if ENABLE_L3_TIM_CAPTURE
    /* Optional: TIM1_CH4 input-capture on PC4 latches the edge count in HW.
     * Left as a stub; enable only after verifying EXTI + IC coexist on PC4. */
#endif

    g_last_defensive = softclock_ms();
}

/* ---------------- L1: EXTI ISR (highest priority) ---------------- */
void EXTI7_0_IRQHandler(void) __attribute__((interrupt));
void EXTI7_0_IRQHandler(void)
{
    if (EXTI->INTFR & IRQ_LINE_BIT) {
        EXTI->INTFR = IRQ_LINE_BIT;          /* clear pending (write 1) */
        capture_request(softclock_ms());     /* latch time only; ~few us */
    }
}

/* ---------------- common entry for all layers ---------------- */
void capture_request(uint64_t t_ms)
{
    if (!g_pending) {                 /* latch earliest timestamp for this event */
        g_capture_ms = t_ms;
        g_due_ms     = t_ms + AS3935_SETTLE_MS;
        g_pending    = 1;
    }
}

/* ---------------- exclusive worker ---------------- */
void capture_service(void)
{
    if (g_reading) return;            /* re-entrancy guard */
    g_reading = 1;

    EventBin bin;
    as3935_health_t h = as3935_read_event(&bin);   /* reads reg0x03 -> clears INT */

    if (h == AS_OK) {
        uint8_t reason = bin.type & 0x0F;
        if (reason == AS_INT_L || reason == AS_INT_D || reason == AS_INT_NH) {
            uint32_t es; uint16_t ms;
            uint8_t valid = softclock_epoch_at(g_capture_ms, &es, &ms);
            bin.epoch_sec = es;
            bin.ms        = ms;
            if (valid) bin.type |= EV_TIME_VALID;
            bundle_push(&bin);        /* lightning stored; disturber/noise counted */
            DBG("[evt] reason=%u dist=%u e=%lu\n", reason, bin.distance,
                (unsigned long)((uint32_t)bin.energy[0] |
                                ((uint32_t)bin.energy[1] << 8) |
                                ((uint32_t)bin.energy[2] << 16)));
        }
        /* reason == 0 -> spurious: nothing pushed */
    }

    health_note(h);                   /* feed recovery FSM (NACK/BUS/etc.) */
    g_pending = 0;
    g_reading = 0;
}

/* ---------------- L2 + L4 (main loop) ---------------- */
void capture_tick(void)
{
    uint64_t now = softclock_ms();

    /* Pending event: service once the 2 ms settle has elapsed. */
    if (g_pending && (int64_t)(now - g_due_ms) >= 0) {
        capture_service();
        return;
    }

    /* L2: INT still high but nothing pending -> a missed edge. Grab it. */
    if (!g_pending && !g_reading && as3935_irq_level()) {
        capture_request(now);
        return;
    }

#if ENABLE_L4_DEFENSIVE
    /* L4: periodic liveness probe when idle. */
    if (!g_pending && (uint32_t)(now - g_last_defensive) >= DEFENSIVE_MS) {
        g_last_defensive = now;
        if (as3935_irq_level()) {          /* only if something is asserted */
            capture_request(now);
        }
    }
#endif
}

uint8_t capture_in_progress(void)
{
#if BUSY_WHOLE_CAPTURE
    return (uint8_t)(g_pending || g_reading);
#else
    return g_reading;
#endif
}
