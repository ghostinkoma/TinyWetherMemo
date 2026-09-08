/*
 * ThunderSense - funconfig.h  (ch32fun build-time options)
 * Required by ch32fun. Kept minimal.
 */
#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

#define CH32V003                 1
/* SDI printf backend for the debug console (used only when config.h DEBUG=1;
 * unused code is garbage-collected by the linker when DEBUG=0). */
#define FUNCONF_USE_DEBUGPRINTF  1

/* クロック: 既定は ch32fun 標準の 48MHz(HSI×PLL2)。実機はこれで稼働。
 * ―― 省電力オプション(保留): 下2行を有効化すると PLL停止で HSI直結 24MHz 駆動になり、
 *    雷IRQは常時捕捉のまま消費を下げられる。config.h の SYS_CLK_HZ も 24000000 に合わせること。
 *    ※ WCH-LinkE(v2.17)×同梱minichlink の書込非互換で未反映(WCH-LinkUtility等で書込可)。
 * #define FUNCONF_USE_PLL           0
 * #define FUNCONF_SYSTEM_CORE_CLOCK 24000000
 */

#endif /* _FUNCONFIG_H */
