/*
 * ThunderSense - nvcal.c
 * Option-byte Data0/Data1 read/write. Data0 = TUN_CAP (4-bit), Data1 = magic.
 * Write routine adapted from the ch32fun 'optiondata' example (Mats Engstrom).
 */
#include "nvcal.h"
#include "ch32fun.h"

#define NVCAL_MAGIC  0xA5u

/* Erase-and-reprogram the 64-byte option block, keeping RDPR/USER/WRPR. */
static void flash_option_write(uint8_t data0, uint8_t data1)
{
    volatile uint16_t hold[6];
    uint32_t *hold32 = (uint32_t *)hold;
    uint32_t *ob32   = (uint32_t *)OB_BASE;
    hold32[0] = ob32[0];                        /* RDPR + USER */
    hold32[1] = (uint32_t)data0 + ((uint32_t)data1 << 16);
    hold32[2] = ob32[2];                        /* WRPR0 + WRPR1 */

    FLASH->KEYR   = FLASH_KEY1; FLASH->KEYR   = FLASH_KEY2;
    FLASH->OBKEYR = FLASH_KEY1; FLASH->OBKEYR = FLASH_KEY2;

    FLASH->CTLR |= CR_OPTER_Set;                /* option erase */
    FLASH->CTLR |= CR_STRT_Set;
    while (FLASH->STATR & FLASH_BUSY) { }
    FLASH->CTLR &= CR_OPTER_Reset;

    FLASH->CTLR |= CR_OPTPG_Set;                /* option program */
    uint16_t *ob16 = (uint16_t *)OB_BASE;
    for (unsigned i = 0; i < sizeof(hold)/sizeof(hold[0]); i++) {
        ob16[i] = hold[i];
        while (FLASH->STATR & FLASH_BUSY) { }
    }
    FLASH->CTLR &= CR_OPTPG_Reset;
    FLASH->CTLR |= CR_LOCK_Set;
}

int nvcal_load(uint8_t *tuncap)
{
    if ((OB->Data1 & 0xFF) == NVCAL_MAGIC) {
        if (tuncap) *tuncap = (uint8_t)(OB->Data0 & 0x0F);
        return 1;
    }
    return 0;
}

void nvcal_save(uint8_t tuncap)
{
    flash_option_write((uint8_t)(tuncap & 0x0F), NVCAL_MAGIC);
}
