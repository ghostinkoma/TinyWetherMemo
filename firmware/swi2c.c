/*
 * ThunderSense - swi2c.c
 * Bit-banged I2C MASTER to the AS3935. Open-drain pins with external pull-ups.
 *   SCL = PA1 (physical pin 1),  SDA = PA2 (physical pin 3)  -- must match config.h
 *
 * SAFETY (Docs/SPEC.md §14.1): EVERY wait on SCL/SDA is bounded by
 * SWI2C_BIT_TIMEOUT_MS. A hung sensor can NEVER block the CPU (that would
 * starve the HW-I2C slave and hang the whole upper bus). Framework: ch32fun.
 */
#include "swi2c.h"
#include "config.h"
#include "softclock.h"
#include "ch32fun.h"

/* Pin tokens (ch32fun). Keep in sync with config.h SWI2C_* definitions. */
#define SCL_PIN   PA1
#define SDA_PIN   PA2

#define SWI2C_HALF_US   10u                /* ~50 kHz (breadboard-robust)        */
#define STRETCH_LIMIT   (SWI2C_BIT_TIMEOUT_MS * 1000u)  /* us before giving up   */

#define HALF()    softclock_delay_us(SWI2C_HALF_US)

/* Open-drain: write HIGH releases the line (pull-up), write LOW drives it. */
static inline void scl_rel(void) { funDigitalWrite(SCL_PIN, FUN_HIGH); }
static inline void scl_low(void) { funDigitalWrite(SCL_PIN, FUN_LOW);  }
static inline void sda_rel(void) { funDigitalWrite(SDA_PIN, FUN_HIGH); }
static inline void sda_low(void) { funDigitalWrite(SDA_PIN, FUN_LOW);  }
static inline int  sda_read(void){ return funDigitalRead(SDA_PIN); }
static inline int  scl_read(void){ return funDigitalRead(SCL_PIN); }

void swi2c_init(void)
{
    /* Open-drain outputs; idle released (high via pull-ups). */
    funPinMode(SCL_PIN, GPIO_CFGLR_OUT_10Mhz_OD);
    funPinMode(SDA_PIN, GPIO_CFGLR_OUT_10Mhz_OD);
    scl_rel();
    sda_rel();
}

/* Release SCL and wait for it to actually go high (clock stretching), bounded. */
static swi2c_ret_t scl_high_sync(void)
{
    uint32_t t = 0;
    scl_rel();
    while (!scl_read()) {
        softclock_delay_us(1);
        if (++t > STRETCH_LIMIT) return SWI2C_TIMEOUT;
    }
    return SWI2C_OK;
}

static swi2c_ret_t i2c_start(void)
{
    sda_rel(); HALF();
    if (scl_high_sync() != SWI2C_OK) return SWI2C_TIMEOUT;
    HALF();
    sda_low(); HALF();          /* SDA falls while SCL high = START */
    scl_low(); HALF();
    return SWI2C_OK;
}

static swi2c_ret_t i2c_rstart(void)   /* repeated START */
{
    sda_rel(); HALF();
    if (scl_high_sync() != SWI2C_OK) return SWI2C_TIMEOUT;
    HALF();
    sda_low(); HALF();
    scl_low(); HALF();
    return SWI2C_OK;
}

static swi2c_ret_t i2c_stop(void)
{
    sda_low(); HALF();
    if (scl_high_sync() != SWI2C_OK) return SWI2C_TIMEOUT;
    HALF();
    sda_rel(); HALF();          /* SDA rises while SCL high = STOP */
    return SWI2C_OK;
}

/* Write one bit. */
static swi2c_ret_t wr_bit(int bit)
{
    if (bit) sda_rel(); else sda_low();
    HALF();
    if (scl_high_sync() != SWI2C_OK) return SWI2C_TIMEOUT;
    HALF();
    scl_low();
    return SWI2C_OK;
}

/* Read one bit (SDA released so the slave drives it). */
static swi2c_ret_t rd_bit(int *bit)
{
    sda_rel(); HALF();
    if (scl_high_sync() != SWI2C_OK) return SWI2C_TIMEOUT;
    *bit = sda_read();
    HALF();
    scl_low();
    return SWI2C_OK;
}

/* Send a byte, return SWI2C_OK if ACKed, SWI2C_NACK if not, or TIMEOUT. */
static swi2c_ret_t wr_byte(uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        if (wr_bit(b & 0x80) != SWI2C_OK) return SWI2C_TIMEOUT;
        b <<= 1;
    }
    int ack;
    if (rd_bit(&ack) != SWI2C_OK) return SWI2C_TIMEOUT;
    return ack ? SWI2C_NACK : SWI2C_OK;    /* ACK = SDA low = 0 */
}

/* Read a byte; send ACK (more to come) or NACK (last). */
static swi2c_ret_t rd_byte(uint8_t *out, int ack)
{
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) {
        int bit;
        if (rd_bit(&bit) != SWI2C_OK) return SWI2C_TIMEOUT;
        v = (uint8_t)((v << 1) | (bit & 1));
    }
    if (wr_bit(ack ? 0 : 1) != SWI2C_OK) return SWI2C_TIMEOUT;  /* ACK=0 */
    *out = v;
    return SWI2C_OK;
}

swi2c_ret_t swi2c_write(uint8_t addr, const uint8_t *buf, size_t n)
{
    swi2c_ret_t r;
    if ((r = i2c_start()) != SWI2C_OK) return r;
    if ((r = wr_byte((uint8_t)(addr << 1))) != SWI2C_OK) { i2c_stop(); return r; }
    for (size_t i = 0; i < n; i++) {
        if ((r = wr_byte(buf[i])) != SWI2C_OK) { i2c_stop(); return r; }
    }
    return i2c_stop();
}

swi2c_ret_t swi2c_read(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n)
{
    swi2c_ret_t r;
    if ((r = i2c_start()) != SWI2C_OK) return r;
    if ((r = wr_byte((uint8_t)(addr << 1))) != SWI2C_OK) { i2c_stop(); return r; }
    if ((r = wr_byte(reg))                  != SWI2C_OK) { i2c_stop(); return r; }
    if ((r = i2c_rstart())                  != SWI2C_OK) { i2c_stop(); return r; }
    if ((r = wr_byte((uint8_t)((addr << 1) | 1))) != SWI2C_OK) { i2c_stop(); return r; }
    for (size_t i = 0; i < n; i++) {
        int last = (i == n - 1);
        if ((r = rd_byte(&buf[i], !last)) != SWI2C_OK) { i2c_stop(); return r; }
    }
    return i2c_stop();
}

swi2c_ret_t swi2c_bus_recover(void)
{
    /* Free a slave that is holding SDA low: pulse SCL up to 9 times. */
    sda_rel();
    for (int i = 0; i < 9 && !sda_read(); i++) {
        scl_low(); HALF();
        if (scl_high_sync() != SWI2C_OK) return SWI2C_TIMEOUT;
        HALF();
    }
    /* Issue a STOP to resync the bus. */
    sda_low(); HALF();
    if (scl_high_sync() != SWI2C_OK) return SWI2C_TIMEOUT;
    HALF();
    sda_rel(); HALF();
    return sda_read() ? SWI2C_OK : SWI2C_BUSERR;   /* SDA released? */
}
