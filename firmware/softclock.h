/*
 * ThunderSense - softclock.h
 * SysTick-based software clock (CH32V003 has no RTC). Provides:
 *   - monotonic ms since boot,
 *   - absolute time seeded by the ESP32 via SET_TIME (NTP correlation),
 *   - us busy-delay for the SW-I2C bit-bang (so no TIM is consumed).
 * (Docs/SPEC.md §6)
 */
#ifndef TS_SOFTCLOCK_H
#define TS_SOFTCLOCK_H

#include <stdint.h>

/* Start SysTick (1 ms tick ISR increments the 64-bit ms accumulator). */
void softclock_init(void);

/* Monotonic milliseconds since boot. Safe to call from ISR. */
uint64_t softclock_ms(void);

/* Seed absolute time (CMD_SET_TIME). Marks time as valid. */
void softclock_set_epoch(uint32_t epoch_sec, uint16_t ms);

/* Fill absolute time now. Returns 1 if time is valid, 0 if not yet synced
 * (if not synced, epoch_sec/ms are boot-relative). */
uint8_t softclock_now(uint32_t *epoch_sec, uint16_t *ms);

/* Convert a specific ms-since-boot timestamp (e.g. latched at the IRQ edge)
 * to absolute epoch. Returns 1 if time is valid, else boot-relative. */
uint8_t softclock_epoch_at(uint64_t ms_since_boot, uint32_t *epoch_sec, uint16_t *ms);

/* 1 once SET_TIME has been received. */
uint8_t softclock_time_valid(void);

/* Busy delay using the SysTick VAL register (for SW-I2C timing). */
void softclock_delay_us(uint32_t us);

#endif /* TS_SOFTCLOCK_H */
