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

/* クロック: 既定 48MHz(HSI×PLL2)。実機検証済みの稼働値=安定性重視でこれを採用。
 * ―― 省電力24MHz(PLL停止=HSI直結)は実機検証済み(2026-09-13, bridge動作OK)だが、消費差は
 *    僅少(3.3V/全周辺ON: 48MHz=7.0mA vs 24MHz=5.2mA=+1.8mA。ESP32のWiFiに対し誤差)のため
 *    48MHz固定を選択。24MHzにするには下2行を有効化し config.h SYS_CLK_HZ も 24000000u にする
 *    (HW-I2C FREQ / SysTick が SYS_CLK_HZ 依存、ch32fun Delay は本値依存)。
 * #define FUNCONF_USE_PLL           0
 * #define FUNCONF_SYSTEM_CORE_CLOCK 24000000
 */

#endif /* _FUNCONFIG_H */
