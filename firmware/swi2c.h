/*
 * ThunderSense - swi2c.h
 * Bit-banged I2C MASTER to the AS3935 (PA1=SCL, PA2=SDA).
 *
 * CRITICAL (Docs/SPEC.md §14.1): every wait loop (clock-stretch, SDA release)
 * MUST be bounded by SWI2C_BIT_TIMEOUT_MS. A hung AS3935 must never block the
 * CPU, or the HW-I2C slave stops responding and the whole upper bus hangs.
 */
#ifndef TS_SWI2C_H
#define TS_SWI2C_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    SWI2C_OK = 0,
    SWI2C_NACK,      /* device did not ACK            */
    SWI2C_TIMEOUT,   /* stretch/SDA wait exceeded     */
    SWI2C_BUSERR     /* bus stuck / arbitration issue */
} swi2c_ret_t;

void        swi2c_init(void);

/* Write n bytes to addr (buf[0] is usually the register). */
swi2c_ret_t swi2c_write(uint8_t addr, const uint8_t *buf, size_t n);

/* Set register pointer then repeated-START read of n bytes. */
swi2c_ret_t swi2c_read(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);

/* Recovery: pulse SCL up to 9 times to free a slave holding SDA low, then STOP.
 * Returns SWI2C_OK if the bus is released. (Docs/SPEC.md §14.2 A) */
swi2c_ret_t swi2c_bus_recover(void);

#endif /* TS_SWI2C_H */
