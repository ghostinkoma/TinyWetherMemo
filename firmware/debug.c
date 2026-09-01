/*
 * ThunderSense - debug.c
 * Debug console backend. Entire file is empty when DEBUG==0.
 */
#include "debug.h"

#if DEBUG
#include "ch32fun.h"

void ts_dbg_init(void)
{
    /* ch32fun routes printf() to the SDI debug channel (WCH-LinkE terminal).
     * Requires FUNCONF_USE_DEBUGPRINTF=1 in funconfig.h.
     * SDIePrintf uses no GPIO, so it does not cost us a pin. */
    SetupDebugPrintf();
    DBG("\n[ThunderSense] debug console up (clk=%luHz)\n", (unsigned long)SYS_CLK_HZ);
}
#endif
