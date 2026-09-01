# ThunderSense — 実機テストログ（簡易）

対象：CH32V003J4M6 + AE-AS3935。ツール：ch32fun / riscv-none-elf-gcc 14.2 / PlatformIO(ch32v) / WCH-LinkE(minichlink)。
記録：2026-09（開発セッション）。

---

## 1. ビルド（メモリ収支）

`--print-memory-usage` 実測。Flash はビン数では増えない（ビンは RAM）。

| 構成 | RAM(static) | %2KB | Flash | %16KB | 判定 |
|---|---|---|---|---|---|
| double / 32（素） | 932 B | 45.5% | 7896 B | 48.2% | ✅ |
| triple / 32（素） | 1320 B | 64.5% | — | — | ✅ |
| double / 64 | 1700 B | 83.0% | — | — | ⚠️ スタック薄 |
| triple / 64 | >2048 | — | — | — | ❌ 不可 |
| **triple / 32 ＋ 全機能**（ADC/健康度/フラッシュ校正/レジスタパススルー/感度） | **1344 B** | **65.6%** | **8988 B**(DEBUG=0) | **54.9%** | ✅ **採用** |

- `mem.h` の `_Static_assert`（安全スタック 640B 確保）が **64ビン構成をビルド時に拒否**することを確認。
- Makefile / PlatformIO 両経路で SUCCESS、警告 0。

---

## 2. 起動シーケンス（仕様配線 PA1=SCL / PA2=SDA、DEBUG=1 の SDI ログ）

```
[ThunderSense] boot
[pp] high     SCL=1 SDA=1        # push-pull 駆動 OK（ピン正常）
[pp] low      SCL=0 SDA=0
[od] released SCL=1 SDA=1        # OD 開放でプルアップに復帰（バス健全）
[scan] i2c ACK: 00 (done)        # AS3935 をアドレス 0x00 で検出
[probe] addr 00 reg00=24 ret=0   # reg0x00=0x24（AFE 既定値）、ACK
[boot] TUN_CAP from flash = 4    # 前回校正値を option-byte からロード（高速起動）
```

- **配線診断の教訓**：当初 SCL/SDA 逆結線で「全アドレス NACK／OD開放=0」。push-pull 駆動テストで
  「ピンは正常・プルアップが届いていない＝配線」と切り分け、SCL/SDA 入替で解決。
- **アドレス確定**：AS3935 = **0x00**（AE-AS3935 デモと一致）。

---

## 3. LCO 校正（初回）

```
[cal] cap= 0 cnt=.... diff=....
 ... (0..15 掃引)
[cal] BEST cap=4 cnt=.... diff=....
[boot] calibrated TUN_CAP=4, saved   # 校正成功 → option-byte 保存
```

- 手順＝DISP_LCO→SysTick ゲート(100ms)で PC4 立上りエッジ計数→目標 3125(500kHz/16)最接近を採用。
- 保存後の再書込（コードフラッシュ）でも option-byte は保持 → 次回 `TUN_CAP from flash = 4`。

---

## 4. スパーク試験（電子ライター）

雷は起こせないため電子ライターの放電で EM 妨害を発生。60 秒監視。

```
[evt] reason=4 dist=0 e=0   × 81
```

| 種別 | 件数 |
|---|---|
| **disturber (reason=4)** | **81** |
| noise (reason=1) | 0 |
| lightning (reason=8) | 0 |

- **正しい動作**：AS3935 はスパークを一貫して「人工妨害波(disturber)」と分類（雷 reason=8 とは誤判定せず）。
  → 信号検証アルゴリズム＋EXTI→capture→分類→ログの**全経路が実機動作**。
- 設計どおり disturber はビン保存せずカウンタのみ加算。距離/エネルギーは雷のみ（dist=0/e=0 は正常）。

---

## 5. 検証状況まとめ

| 項目 | 状態 |
|---|---|
| SW-I2C（AS3935 側）通信 | ✅ 実機 |
| LCO 校正・フラッシュ保存/ロード | ✅ 実機 |
| イベント捕捉（EXTI→capture→分類） | ✅ 実機（disturber 81件） |
| IWDG・起動・多重防御・フェイルセーフ | ✅ 実機 |
| ADC/健康度(VDD) | ✅ ビルド・起動（値の実測校正は未） |
| 上位 HW-I2C スレーブ（ESP32 読取・DMA-TX・CRC・CLEAR） | ⏳ **コード完成・ビルド済／実機通し未** |
| Arduino ホストライブラリ | ✅ 構文検証（g++）／⏳ 実機 ESP32 未 |

> 次段：ESP32 実機を接続し、`ThunderSense.poll()` による bundle 受信・CRC 検証・CLEAR の
> エンドツーエンド確認、および DMA チャネル(TX=CH6 想定)の実確認。
