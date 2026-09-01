/*
 * ThunderSense - health.h
 * Sensor/bus fault detection & recovery state machine (Docs/SPEC.md §14).
 * Owns the reported ts_state_t (READY / RECOVERING / FAULT_*). Fail-safe:
 * even in FAULT the HW-I2C slave stays alive and serves empty valid bundles.
 */
#ifndef TS_HEALTH_H
#define TS_HEALTH_H

#include <stdint.h>
#include "protocol.h"
#include "as3935.h"

void health_init(void);

/* Feed a driver result (from as3935_* / swi2c_*). Escalates fault state. */
void health_note(as3935_health_t h);

/* Background recovery FSM: bus-recover -> re-init -> re-calibrate, paced by
 * HEALTH_RETRY_PERIOD_MS. Call from the main loop. */
void health_tick(void);

/* Current state for the status byte's STATE nibble. */
ts_state_t health_state(void);

/* Convenience: 1 if currently in any FAULT_* state. */
uint8_t health_is_fault(void);

/* Set a lifecycle state directly (BOOT/CALIBRATING/READY) from startup/main. */
void health_set_state(ts_state_t s);

#endif /* TS_HEALTH_H */
