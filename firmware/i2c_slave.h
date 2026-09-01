/*
 * ThunderSense - i2c_slave.h
 * HW I2C1 slave on the upper bus + DMA-TX for the bundle (Docs/SPEC.md §8).
 * Control plane: incoming write-commands are copied into a command buffer in
 * the ISR and executed later in the main loop (Docs/SPEC.md §5) so the
 * lightning capture always keeps priority.
 */
#ifndef TS_I2C_SLAVE_H
#define TS_I2C_SLAVE_H

#include <stdint.h>
#include "protocol.h"

/* Command buffer: a host write-command captured in the ISR, run in main loop. */
typedef struct {
    volatile uint8_t pending;   /* 1 = a command awaits execution */
    uint8_t opcode;
    uint8_t arg[6];             /* e.g. CLEAR(gen,crc_lo,crc_hi) / SET_TIME(...) */
} CmdBuf;

/* Init HW I2C1 as slave at addr; set up DMA1 TX channel for bundle sends.
 * (DMA1_CH6 = I2C1_TX assumed; confirm against RM.) */
void i2c_slave_init(uint8_t addr);

/* I2C1 event/error ISR body. On a read command it arms the DMA source;
 * on a write command it stores opcode+args into the command buffer. */
void i2c_slave_isr(void);

/* Main loop: execute one pending command (CLEAR/SET_TIME/CONFIG/CMD). */
void i2c_slave_process_cmd(void);

/* State glue: the slave asks these when building the status byte / arming DMA. */
void    i2c_slave_set_state(ts_state_t state);   /* from health/main */

/* Publish the latest health sample (from the main loop; no ADC in the ISR). */
void    i2c_slave_set_health(uint16_t vdd_mv, uint16_t vrefint_raw);

/* HW peripheral stuck detection + recovery (Docs/SPEC.md §14.2 F). */
uint8_t i2c_slave_stuck(void);   /* 1 if BUSY flag latched too long */
void    i2c_slave_reset(void);   /* I2C1 SWRST + re-init */

#endif /* TS_I2C_SLAVE_H */
