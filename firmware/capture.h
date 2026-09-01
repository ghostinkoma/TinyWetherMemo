/*
 * ThunderSense - capture.h
 * Multi-layer event capture with a single service entry (Docs/SPEC.md §3.5).
 *   L1 EXTI (highest prio) -> latch absolute time, arm 2ms one-shot
 *   L2 level poll  (main loop) -> catch missed edges (INT is level-held)
 *   L3 TIM1_CH4 input-capture (optional) -> HW time latch
 *   L4 defensive periodic reg0x03 read (last resort)
 * All layers funnel into capture_request()/capture_service() so an event is
 * captured exactly once (no double count).
 */
#ifndef TS_CAPTURE_H
#define TS_CAPTURE_H

#include <stdint.h>

/* Configure EXTI on the IRQ pin (L1) and optional TIM1_CH4 IC (L3). */
void capture_init(void);

/* Common entry for ALL layers. Latches the earliest timestamp for this event.
 * Safe from ISR context (called by L1 EXTI). t_ms = softclock_ms() at detect. */
void capture_request(uint64_t t_ms);

/* Run L2 (level poll) + L4 (defensive) checks. Call from the main loop. */
void capture_tick(void);

/* Exclusive one-shot worker: reads AS3935 and pushes a bin. Driven by the
 * 2ms one-shot ISR and/or the main loop. Re-entrancy guarded internally. */
void capture_service(void);

/* 1 while an event is being captured -> drives status BUSY / STATE_BUSY_CAPTURE. */
uint8_t capture_in_progress(void);

#endif /* TS_CAPTURE_H */
