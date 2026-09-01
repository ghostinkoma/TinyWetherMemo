/*
 * ThunderSense - crc16.h
 * CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, xorout 0x0000).
 * Must match the ESP32 side exactly. (Docs/I2C_REFERENCE.md §1)
 *
 * Implementation note: prefer the bitwise version on CH32V003 to save the
 * 512-byte table in flash; switch to a table only if CRC time shows up hot.
 */
#ifndef TS_CRC16_H
#define TS_CRC16_H

#include <stdint.h>
#include <stddef.h>

/* One-shot CRC over a buffer (starts from CRC16_INIT). */
uint16_t crc16_ccitt(const uint8_t *data, size_t len);

/* Incremental update: feed one byte. Seed with CRC16_INIT.
 * Useful if a running CRC is ever kept while appending bins. */
uint16_t crc16_ccitt_update(uint16_t crc, uint8_t byte);

#endif /* TS_CRC16_H */
