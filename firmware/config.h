/*
 * ThunderSense - config.h
 * Build-time configuration. Target: CH32V003J4M6 (SOP-8), internal HSI (no XTAL).
 * See Docs/README.md (pins) and Docs/SPEC.md (sizes/timeouts).
 *
 * NOTE: pins are given as (port letter, pin number). The actual SDK binding
 *       (ch32fun / ch32v00x StdPeriph) is done in the corresponding .c file.
 */
#ifndef TS_CONFIG_H
#define TS_CONFIG_H

/* ---------------- Pin assignment (Docs/README.md §4.1, datasheet-verified) --------- */
/* Upper bus: HW I2C1 slave. PC1/PC2 are the DEFAULT I2C1 pins on SOP-8 (no remap). */
#define HWI2C_SDA_PORT   'C'
#define HWI2C_SDA_PIN    1        /* PC1  (physical pin 5) */
#define HWI2C_SCL_PORT   'C'
#define HWI2C_SCL_PIN    2        /* PC2  (physical pin 6) */

/* Lower bus: SW I2C master -> AS3935. PA1/PA2 are OSC pins but free under HSI. */
#define SWI2C_SCL_PORT   'A'
#define SWI2C_SCL_PIN    1        /* PA1  (physical pin 1) */
#define SWI2C_SDA_PORT   'A'
#define SWI2C_SDA_PIN    2        /* PA2  (physical pin 3) */

/* AS3935 IRQ: PC4 = EXTI4, also T1CH4 (LCO calib / L3 input-capture). */
#define AS3935_IRQ_PORT  'C'
#define AS3935_IRQ_PIN   4        /* PC4  (physical pin 7) */

/* PD1 = SWIO (physical pin 8): programming only. DO NOT use as GPIO. */

/* ---------------- I2C addresses ---------------- */
#define HWI2C_SLAVE_ADDR   0x28   /* bridge 7-bit slave addr (ensure no upper-bus conflict) */
#define AS3935_ADDR        0x00   /* verified on hardware (AE-AS3935 responds at 0x00)  */

/* ---------------- Buffer sizing (Docs/SPEC.md §2, §11) ---------------- */
/* 1バンドルに保持する落雷イベント最大数（将来の増減はここだけ変更＝布石）。
 * ★ホスト lib/ThunderSense の TS_MAX_EVENTS と必ず一致させること
 *   （バンドル転送長 = 3 + 12×N + 2 が両者の配線契約。不一致は CRC 不整合になる）。
 * RAM 収支（2KB SRAM, トリプル）: 実静的 ≈ 204 + 36×N バイト。
 * ★上限は mem.h の SRAM_STACK_MIN=640B（EXTI→2msタイマ→I2Cスレーブ→DMA のISRネスト＋SW-I2C用）
 *   に律速される。静的アサート(mem.h)が通る上限 = N=35、実マップ整合(misc≈189B)なら N≈33。
 *   40 にするには 640B 予約を下げる必要があり、その前に TS_DEBUG_STACK で実スタック高水位を測定し
 *   根拠を得ること（未測定でのN≥36は不可＝ビルドが弾く）。 */
#define MAX_EVENT_COUNT    32     /* 落雷イベント格納数（bins/bundle）。既定32。上限35(要640B予約) */
#define N_BINS             MAX_EVENT_COUNT   /* 内部エイリアス（protocol.h / bundle.c が使用） */
#define N_BUNDLES          3      /* triple buffer: fill / serve / clearing (SPEC §9) */
#define CMDBUF_DEPTH       1      /* command slots (1 is enough with Busy handshake) */

/* ---------------- Timeouts / periods (ms) (Docs/SPEC.md §14) ---------------- */
#define SWI2C_BIT_TIMEOUT_MS    1     /* per stretch/SDA-release wait; NEVER infinite */
#define AS3935_RETRY            3
#define AS3935_SETTLE_MS        2     /* IRQ high -> reg0x03 read delay (chip settle)  */
#define HEALTH_RETRY_PERIOD_MS  5000  /* background recovery cadence when FAULT         */
#define IWDG_TIMEOUT_MS         200

/* ---------------- AS3935 defaults (Docs/SPEC.md §10) ---------------- */
#define AS3935_AFE_INDOOR    0x12
#define AS3935_AFE_OUTDOOR   0x0E
#define AS3935_NF_LEV        0x02
#define AS3935_WDTH          0x02
#define AS3935_SREJ          0x02   /* reg0x02[3:0] spike rejection (default 0010) */
#define AS3935_MIN_NUM       0x00   /* reg0x02[5:4] min lightnings (0=1) */
#define AS3935_DIV           0x00   /* freq div ratio /16 */
#define AS3935_TUN_CAP       0x00   /* TODO: bench-calibrated TUN_CAP (method A, §10) */

/* ---------------- System ---------------- */
/* 既定 48MHz(実機検証済みの稼働値)。softclock/swi2c/HW-I2CスレーブFREQ は本値から自動算出。
 * 省電力24MHz化(任意)時は funconfig.h の2行と合わせ本値も 24000000u にする。 */
#define SYS_CLK_HZ            48000000u  /* HSI 48 MHz (no external crystal)             */

/* ---------------- Debug console ----------------
 * 1 = console debug ON  (uses ch32fun SDI printf; costs flash + CPU cycles)
 * 0 = console debug OFF (all DBG()/DBG_INIT() compile to nothing)
 * Use via debug.h:  DBG_INIT();  DBG("x=%d\n", x);
 * NOTE: written as 0/1 (not true/false) so `#if DEBUG` works in the preprocessor. */
#define DEBUG                0

/* ---------------- Feature toggles ---------------- */
#define ENABLE_L3_TIM_CAPTURE   0   /* TIM1_CH4 input-capture redundancy (verify EXTI+IC coexist) */
#define ENABLE_L4_DEFENSIVE     1   /* periodic defensive reg0x03 read                            */
#define BUSY_WHOLE_CAPTURE      1   /* 1: Busy for whole ~3ms; 0: only during bit-bang ~1ms       */

#endif /* TS_CONFIG_H */
