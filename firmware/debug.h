/*
 * ThunderSense - debug.h
 * Zero-cost debug console gate. When DEBUG==0 every DBG()/DBG_INIT() expands
 * to nothing, so neither printf nor its arguments are emitted (no flash, no CPU).
 * Backend: ch32fun SDI printf over the single-wire debug link (no extra pin).
 */
#ifndef TS_DEBUG_H
#define TS_DEBUG_H

#include "config.h"

#if DEBUG
  #include <stdio.h>
  void ts_dbg_init(void);              /* set up ch32fun debug printf */
  #define DBG_INIT()   ts_dbg_init()
  #define DBG(...)     printf(__VA_ARGS__)
#else
  #define DBG_INIT()   ((void)0)
  #define DBG(...)     ((void)0)
#endif

#endif /* TS_DEBUG_H */
