/*
 * ThunderSense - bundle.h
 * Event storage: ping-pong of two fixed Bundles (Docs/SPEC.md §2, §9).
 * Owns the two large static buffers (the SRAM budget is dominated here).
 *   producer = capture context (bundle_push)
 *   consumer = HW-I2C slave read (freeze/clear)
 * SPSC discipline: producer touches `active`, consumer touches `shipping`.
 */
#ifndef TS_BUNDLE_H
#define TS_BUNDLE_H

#include <stdint.h>
#include "protocol.h"

void bundle_init(void);

/* Producer: append one bin to the active bundle.
 * On full: drop-newest and increment lost_total (Docs/SPEC.md §7). */
void bundle_push(const EventBin *bin);

/* Consumer (on CMD_READ_BUNDLE): make a consistent shipping bundle.
 *   - if shipping is empty and active has events -> swap (gen++),
 *   - else re-serve the same (unacked) shipping (retry-safe),
 *   - write `status` (state|flags), compute crc16.
 * Returns the buffer to DMA out and its wire length. */
Bundle *bundle_freeze_for_read(uint8_t status_byte, uint16_t *wire_len);

/* ESP32 ack (CMD_CLEAR): zero-fill shipping iff (gen,crc) match the shipped one.
 * Returns 1 if cleared, 0 if no match (stale ack -> ignored). */
int bundle_clear(uint8_t gen, uint16_t crc);

/* Background clear of one DIRTY slot (CRC-first). Call from the main loop. */
void bundle_tick_clear(void);

/* Events currently held (active + shipping). For diag / STATE hints. */
uint8_t bundle_pending_count(void);

/* Read (and clear) the sticky overflow flag for the status byte. */
uint8_t bundle_take_overflow(void);

/* Monotonic totals for the diag STATUS register (Docs/SPEC.md §7). */
void bundle_get_totals(uint16_t *lightning, uint16_t *disturber,
                       uint16_t *noise, uint16_t *lost);

#endif /* TS_BUNDLE_H */
