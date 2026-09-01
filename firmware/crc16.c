/*
 * ThunderSense - crc16.c
 * CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, xorout 0x0000).
 * Bitwise (no 512B table) to save flash; ~8 shifts/byte is fine for 386B.
 * Must match the ESP32 verifier exactly.
 */
#include "crc16.h"
#include "protocol.h"   /* CRC16_POLY, CRC16_INIT */

uint16_t crc16_ccitt_update(uint16_t crc, uint8_t byte)
{
    crc ^= (uint16_t)byte << 8;
    for (int i = 0; i < 8; i++) {
        if (crc & 0x8000u)
            crc = (uint16_t)((crc << 1) ^ CRC16_POLY);
        else
            crc = (uint16_t)(crc << 1);
    }
    return crc;
}

uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = CRC16_INIT;
    while (len--)
        crc = crc16_ccitt_update(crc, *data++);
    return crc;
}
