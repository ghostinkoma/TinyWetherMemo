/*
 * ThunderSense - softclock.c
 * SysTick software clock (no RTC on CH32V003).
 *
 * Design: SysTick free-runs as a 32-bit up-counter at HCLK (STRE=0, so CNT
 * wraps at 2^32, never reloaded). A "walking compare" advances CMP by one
 * millisecond of ticks each interrupt, giving a periodic 1 ms tick while CNT
 * stays monotonic for wrap-safe microsecond delays.
 *   - softclock_ms()        : 64-bit ms since boot (lock-free torn-read guard)
 *   - softclock_now()       : absolute epoch after SET_TIME seeding
 *   - softclock_delay_us()  : busy delay via CNT deltas (used by SW-I2C)
 * (Docs/SPEC.md §6)  Framework: ch32fun.
 */
#include "softclock.h"
#include "config.h"
#include "ch32fun.h"

#define TICKS_PER_MS   (SYS_CLK_HZ / 1000u)   /* 48000 @ 48 MHz */
#define TICKS_PER_US   (SYS_CLK_HZ / 1000000u)/* 48    @ 48 MHz */

/* SysTick CTLR bits (CH32V003 QingKe-V2) */
#define STK_CTLR_STE    (1u << 0)   /* counter enable        */
#define STK_CTLR_STIE   (1u << 1)   /* interrupt enable      */
#define STK_CTLR_STCLK  (1u << 2)   /* 1 = HCLK, 0 = HCLK/8  */
#define STK_CTLR_STRE   (1u << 3)   /* 1 = auto-reload       */
#define STK_SR_CNTIF    (1u << 0)   /* compare flag          */

static volatile uint64_t g_ms      = 0;      /* ms since boot            */
static volatile uint32_t g_epoch   = 0;      /* Unix seconds at sync     */
static volatile uint32_t g_epoch_ms_base = 0;/* g_ms low when synced     */
static volatile uint8_t  g_valid   = 0;      /* 1 after SET_TIME         */

void softclock_init(void)
{
    SysTick->SR  = 0;
    SysTick->CNT = 0;
    SysTick->CMP = TICKS_PER_MS;              /* first tick 1 ms out      */
    /* free-run (STRE=0), HCLK clock, interrupt on compare, enable */
    SysTick->CTLR = STK_CTLR_STE | STK_CTLR_STIE | STK_CTLR_STCLK;
    NVIC_EnableIRQ(SysTick_IRQn);
}

/* Periodic 1 ms interrupt: advance the compare, bump ms. */
void SysTick_Handler(void) __attribute__((interrupt));
void SysTick_Handler(void)
{
    SysTick->SR = 0;                          /* clear compare flag       */
    /* Catch up if a higher-priority ISR delayed us past >1 tick, so the
     * ms count and the walking compare never fall behind CNT. */
    do {
        SysTick->CMP += TICKS_PER_MS;
        g_ms++;
    } while ((int32_t)(SysTick->CNT - SysTick->CMP) >= 0);
}

uint64_t softclock_ms(void)
{
    /* Lock-free 64-bit read: retry until two reads agree (guards ISR tear). */
    uint64_t a, b;
    do { a = g_ms; b = g_ms; } while (a != b);
    return a;
}

void softclock_set_epoch(uint32_t epoch_sec, uint16_t ms)
{
    uint64_t now = softclock_ms();
    /* Anchor: epoch_sec.ms corresponds to this instant (now ms since boot). */
    g_epoch          = epoch_sec;
    g_epoch_ms_base  = (uint32_t)(now - ms);  /* ms-of-boot that maps to epoch_sec.000 */
    g_valid          = 1;
}

uint8_t softclock_epoch_at(uint64_t ms_since_boot, uint32_t *epoch_sec, uint16_t *ms)
{
    if (g_valid) {
        uint64_t elapsed = ms_since_boot - g_epoch_ms_base;  /* ms since epoch.000 */
        *epoch_sec = g_epoch + (uint32_t)(elapsed / 1000u);
        *ms        = (uint16_t)(elapsed % 1000u);
        return 1;
    }
    /* not synced yet: report boot-relative */
    *epoch_sec = (uint32_t)(ms_since_boot / 1000u);
    *ms        = (uint16_t)(ms_since_boot % 1000u);
    return 0;
}

uint8_t softclock_now(uint32_t *epoch_sec, uint16_t *ms)
{
    return softclock_epoch_at(softclock_ms(), epoch_sec, ms);
}

uint8_t softclock_time_valid(void)
{
    return g_valid;
}

void softclock_delay_us(uint32_t us)
{
    uint32_t start = SysTick->CNT;
    uint32_t ticks = us * TICKS_PER_US;
    /* 32-bit unsigned subtraction is wrap-safe against CNT rollover. */
    while ((uint32_t)(SysTick->CNT - start) < ticks) { /* spin */ }
}
