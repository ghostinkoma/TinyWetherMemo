/*
 * ThunderSense - bundle.c  (TRIPLE buffer)
 * Three fixed Bundles rotate through roles (Docs/SPEC.md §2, §7, §9):
 *   FILLING  - producer appends here (bundle_push)
 *   SERVING  - frozen snapshot the host reads; retained (re-requestable) until
 *              acked, then marked DIRTY (its data lingers as history)
 *   DIRTY    - scheduled for background clear (bundle_tick_clear), CRC-erased
 *              FIRST then bins, so an interrupted clear can never look valid
 *   CLEAN    - idle pool, ready to become the next FILLING
 *
 * The third buffer lets the (slow) zero-clear run fully decoupled from fill and
 * serve. SPSC: producer touches FILLING only; freeze/clear touch the others.
 * Index swaps run under a short critical section vs. a preempting push.
 */
#include "bundle.h"
#include "config.h"
#include "crc16.h"
#include <string.h>

enum { SLOT_CLEAN = 0, SLOT_FILLING, SLOT_SERVING, SLOT_DIRTY };
#define SERVE_NONE 0xFFu

static Bundle           g_bundle[N_BUNDLES];
static volatile uint8_t g_slot[N_BUNDLES];     /* per-slot role */
static volatile uint8_t g_fill  = 0;           /* FILLING index */
static volatile uint8_t g_serve = SERVE_NONE;  /* SERVING index or NONE */
static volatile uint8_t g_gen   = 0;
static volatile uint8_t g_seq   = 0;
static volatile uint8_t g_overflow = 0;

static volatile uint16_t g_lightning = 0, g_disturber = 0, g_noise = 0, g_lost = 0;

/* ---- RISC-V critical section (save/restore mstatus.MIE) ---- */
static inline uint32_t irq_save(void)
{
    uint32_t m;
    __asm__ volatile ("csrr %0, mstatus" : "=r"(m));
    __asm__ volatile ("csrci mstatus, 8");
    return m;
}
static inline void irq_restore(uint32_t m)
{
    if (m & 0x8u) __asm__ volatile ("csrsi mstatus, 8");
}

static inline uint32_t bin_energy(const EventBin *b)
{
    return (uint32_t)b->energy[0] | ((uint32_t)b->energy[1] << 8)
         | ((uint32_t)b->energy[2] << 16);
}

static uint8_t find_clean(void)
{
    for (uint8_t i = 0; i < N_BUNDLES; i++)
        if (g_slot[i] == SLOT_CLEAN) return i;
    return 0xFF;
}

/* Clear a slot: CRC first (invalidate), then bins, then meta. (SPEC §9) */
static void clear_slot(uint8_t i)
{
    Bundle *b = &g_bundle[i];
    b->crc16 = 0;                       /* 1) erase CRC -> never looks valid */
    memset(b->bins, 0, sizeof(b->bins));/* 2) erase bins                     */
    b->length = 0; b->gen = 0; b->status = 0;
    g_slot[i] = SLOT_CLEAN;
}

void bundle_init(void)
{
    memset(g_bundle, 0, sizeof(g_bundle));
    for (uint8_t i = 0; i < N_BUNDLES; i++) g_slot[i] = SLOT_CLEAN;
    g_slot[0] = SLOT_FILLING;
    g_fill = 0; g_serve = SERVE_NONE;
    g_gen = g_seq = g_overflow = 0;
    g_lightning = g_disturber = g_noise = g_lost = 0;
}

/* Producer: totals count all; only lightning stored; top-N-by-energy on full. */
void bundle_push(const EventBin *bin)
{
    uint8_t reason = bin->type & 0x0F;
    if      (reason & EV_DISTURBER) { g_disturber++; return; }
    else if (reason & EV_NOISE)     { g_noise++;     return; }
    else if (!(reason & EV_LIGHTNING)) { return; }
    g_lightning++;

    uint32_t s = irq_save();
    Bundle *b = &g_bundle[g_fill];
    if (b->length < N_BINS) {
        EventBin *dst = &b->bins[b->length];
        *dst = *bin; dst->seq = g_seq++;
        b->length++;
    } else {
        uint32_t ne = bin_energy(bin);
        uint16_t mi = 0; uint32_t me = bin_energy(&b->bins[0]);
        for (uint16_t i = 1; i < N_BINS; i++) {
            uint32_t e = bin_energy(&b->bins[i]);
            if (e < me) { me = e; mi = i; }
        }
        if (ne > me) { b->bins[mi] = *bin; b->bins[mi].seq = g_seq++; }
        g_lost++; g_overflow = 1;
    }
    irq_restore(s);
}

/* Background clear of one DIRTY slot. Call from the main loop. */
void bundle_tick_clear(void)
{
    for (uint8_t i = 0; i < N_BUNDLES; i++) {
        if (g_slot[i] == SLOT_DIRTY) { clear_slot(i); return; }
    }
}

Bundle *bundle_freeze_for_read(uint8_t status_byte, uint16_t *wire_len)
{
    uint8_t idx;

    if (g_serve != SERVE_NONE) {
        idx = g_serve;                      /* retry the same frozen snapshot */
    } else if (g_bundle[g_fill].length > 0) {
        uint32_t s = irq_save();
        idx = g_fill;
        g_slot[idx] = SLOT_SERVING;
        g_serve = idx;
        g_bundle[idx].gen = ++g_gen;
        uint8_t c = find_clean();
        if (c == 0xFF) {                    /* no clean pool: clear a DIRTY now */
            irq_restore(s);
            bundle_tick_clear();
            s = irq_save();
            c = find_clean();
        }
        if (c != 0xFF) { g_slot[c] = SLOT_FILLING; g_fill = c; }
        irq_restore(s);
    } else {
        idx = g_fill;                       /* nothing pending: serve empty */
        g_bundle[idx].length = 0;
        g_bundle[idx].gen    = g_gen;
    }

    Bundle *b = &g_bundle[idx];
    if (g_overflow) status_byte |= ST_OVERFLOW;
    b->status = status_byte;
    b->crc16  = crc16_ccitt((const uint8_t *)&b->length, BUNDLE_CRC_SPAN);
    if (wire_len) *wire_len = (uint16_t)BUNDLE_WIRE_SIZE;
    return b;
}

int bundle_clear(uint8_t gen, uint16_t crc)
{
    if (g_serve == SERVE_NONE) return 0;
    Bundle *b = &g_bundle[g_serve];
    if (b->gen != gen || b->crc16 != crc) return 0;    /* stale ack */

    uint32_t s = irq_save();
    g_slot[g_serve] = SLOT_DIRTY;          /* retain as history; clear later */
    g_serve = SERVE_NONE;
    g_overflow = 0;
    irq_restore(s);
    return 1;
}

uint8_t bundle_pending_count(void)
{
    uint16_t n = g_bundle[g_fill].length;
    if (g_serve != SERVE_NONE) n += g_bundle[g_serve].length;
    return (n > 255) ? 255 : (uint8_t)n;
}

uint8_t bundle_take_overflow(void) { return g_overflow; }

void bundle_get_totals(uint16_t *lightning, uint16_t *disturber,
                       uint16_t *noise, uint16_t *lost)
{
    if (lightning) *lightning = g_lightning;
    if (disturber) *disturber = g_disturber;
    if (noise)     *noise     = g_noise;
    if (lost)      *lost      = g_lost;
}
