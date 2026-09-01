/*
 * ThunderSense - adc.h
 * Minimal ADC for the internal Vrefint -> VDD (supply / brownout) health metric.
 *
 * NOTE: the CH32V003 has NO calibrated die-temperature sensor (only Vrefint on
 * ch8 and Vcalint on ch9). "Die temperature" is therefore reported only as a
 * rough proxy; VDD (supply) is the reliable, useful number. (Docs/SPEC.md §16)
 */
#ifndef TS_ADC_H
#define TS_ADC_H

#include <stdint.h>

void     adc_init(void);
uint16_t adc_read(uint8_t ch);     /* single conversion, 12-bit */
uint16_t adc_vdd_mv(void);         /* VDD estimate via Vrefint (mV) */

#endif /* TS_ADC_H */
