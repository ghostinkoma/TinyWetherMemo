/*
 * ThunderSense - nvcal.h
 * Non-volatile storage of the AS3935 LCO calibration (TUN_CAP) in the flash
 * option-byte user words (Data0/Data1). Survives power cycles; no code-page
 * erase involved. Host issues a recalibrate command to refresh it.
 * (Docs/SPEC.md §10, §17)
 */
#ifndef TS_NVCAL_H
#define TS_NVCAL_H

#include <stdint.h>

/* Load a stored TUN_CAP. Returns 1 if a valid value was found, else 0. */
int  nvcal_load(uint8_t *tuncap);

/* Persist TUN_CAP (4-bit) to option-byte Data0 with a validity magic. */
void nvcal_save(uint8_t tuncap);

#endif /* TS_NVCAL_H */
