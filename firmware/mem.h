/*
 * ThunderSense - mem.h
 * Memory POLICY, not a runtime allocator.
 *
 * On the CH32V003 (2 KB SRAM, no OS) dynamic allocation is forbidden:
 *   - NO malloc/free, NO heap, NO pool allocator.
 *   - All buffers are `static`, sized at compile time (see bundle.c, i2c_slave.c).
 * This header centralizes the SRAM budget guard so an over-large static
 * footprint fails the BUILD (not silently overflow at runtime), plus an
 * optional stack high-water-mark for debugging.  (Docs/SPEC.md §11, §2.3)
 */
#ifndef TS_MEM_H
#define TS_MEM_H

#include <stdint.h>
#include "protocol.h"

/* ---------------- SRAM budget guard (CH32V003 = 2048 bytes) ---------------- */
#define SRAM_TOTAL        2048u

/* Rough static footprint: the two ping-pong bundles dominate. Bump the "misc"
 * term as modules add globals; keep the assert honest against the .map file. */
#define SRAM_STATIC_EST   ((uint32_t)BUNDLE_WIRE_SIZE * N_BUNDLES + 128u)

/* Minimum stack we insist on keeping free for ISR nesting
 * (EXTI -> 2ms timer -> I2C slave -> DMA) plus SW-I2C frames. */
#define SRAM_STACK_MIN    640u

_Static_assert(SRAM_STATIC_EST + SRAM_STACK_MIN <= SRAM_TOTAL,
    "Static footprint leaves too little stack: reduce N_BINS or trim globals");

/* ---------------- Stack high-water-mark (debug only) ---------------- */
#if defined(TS_DEBUG_STACK)
void     mem_stack_paint(void);   /* fill unused stack with 0xA5 early at boot */
uint16_t mem_stack_used(void);    /* scan the pattern -> peak stack usage (bytes) */
#endif

#endif /* TS_MEM_H */
