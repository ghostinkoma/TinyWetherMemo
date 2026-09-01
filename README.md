# ThunderSense

**AS3935 雷センサ → CH32V003 → I2C ブリッジ** と、上位ホスト（ESP32）用ライブラリ。

秋月 **AE-AS3935**（AMS AS3935 Franklin Lightning Sensor）を **CH32V003J4M6 (SOP-8)** が受け、
雷/妨害波イベントを**リアルタイム捕捉・絶対時刻付与・強度上位32を保持**し、上位バスへ
**I2C スレーブ**として供給します。ホストは「ポーリングして読むだけ」。

> センサ本体は「500kHz 同調の AM 受信＋エンベロープ検波＋内蔵判定器」。生波形は出ず、
> 雷/妨害波の**判定結果**（距離・エネルギー）だけを返す“判定器”です。本ブリッジはそれを
> 隔離・時刻付け・バッファし、扱いやすい形にします。

---

## 特徴

- **隔離ブリッジ**：癖の強い AS3935 I2C を **ローカル SW-I2C** に閉じ込め、上位バス（温湿度センサ等と同居）を守る
- **イベント駆動捕捉**：EXTI(最優先) で発生時刻を µs で確定 → 2ms 後にレジスタ読取 → ビン化（多重防御 L1–L4）
- **絶対タイムスタンプ**：CH32V003 は RTC 無 → SysTick ソフト時計を ESP32 の NTP と対応付け
- **ロスセーフ配送**：389B 固定バンドル＋**CRC16**、**非破壊 read＋(gen,crc) 指定 CLEAR**（検証成功時のみ削除）
- **荒天対策**：満杯時は**エネルギー上位32を保持**（弱い雷を置換、ソート無し）＋単調カウンタで総数保全
- **トリプルバッファ**：fill / serve / 背景クリア を分離（クリアは CRC 先消去）
- **感度・校正の全制御**：AFE(屋内/屋外)・ノイズフロア・WDTH・SREJ・MIN_NUM・任意レジスタ R/W・
  LCO 再校正（結果ポーリング）を **ホスト I2C から**
- **フラッシュ校正保存**：LCO 校正値(TUN_CAP)を option-byte に保存→次回高速起動
- **フェイルセーフ**：センサ異常時も上位バスは生存（空バンドル＋FAULT 状態）／IWDG／SW-I2C 全ループ timeout
- **省メモリ**：2KB SRAM に triple/32＋全機能で **RAM 66% / Flash 55%**（DEBUG=0）

---

## アーキテクチャ

```
            SW-I2C(master)                    HW-I2C(slave, 0x28)
  AS3935  ───────────────►  CH32V003J4M6  ───────────────►  ESP32 (host, NTP)
   │IRQ ─EXTI(最優先)────►  capture(L1-4) → bin → bundle(triple) ──DMA──► poll
   └ 500kHz 同調アンテナ        │ SysTick 時計 / IWDG / health(VDD)
                               └ 異常時もスレーブ生存（FAULT を返す）
```

役割：**CH32V003＝即時捕捉・保持**、**ESP32＝N秒ごとにまとめて回収**。
例）1秒に0.2s間隔で5件 → 次ポーリングで5件まとめて取得。

---

## リポジトリ構成

```
ThunderSence/
├─ README.md                 # 本書
├─ TESTLOG.md                # 実機テスト結果（簡易ログ）
├─ LICENSE
├─ Docs/                     # 設計・仕様
│   ├─ README.md              # 設計根拠
│   ├─ SPEC.md                # 詳細仕様
│   ├─ I2C_REFERENCE.md       # I2C コマンド仕様
│   └─ SOP8PinOut.txt
├─ firmware/                 # CH32V003 ファーム（ch32fun / PlatformIO）
│   ├─ *.c / *.h              # モジュール（config/protocol/capture/bundle/...）
│   ├─ Makefile / platformio.ini / funconfig.h / BUILD.md
├─ arduino/ThunderSense/     # ESP32 ホストライブラリ
│   ├─ ThunderSense.h / .cpp  # ← .h を読めば使い方が分かる
│   └─ examples/ReadLightning/
└─ AE_AS3935DEMO/            # 秋月デモ(参考・三者資料)
```

---

## ハードウェア（ピン配線）

CH32V003J4M6 SOP-8（データシート準拠）。**5信号＋SWIO**でちょうど収まる。

| 物理Pin | ポート | 用途 |
|---|---|---|
| 1 | PA1 | SW-I2C **SCL** → AS3935 |
| 3 | PA2 | SW-I2C **SDA** → AS3935 |
| 5 | PC1 | HW-I2C1 **SDA**（上位バス） |
| 6 | PC2 | HW-I2C1 **SCL**（上位バス） |
| 7 | PC4 | AS3935 **IRQ**（EXTI / T1CH4 校正兼用） |
| 8 | PD1 | SWIO（書込専用） |
| 2 / 4 | VSS / VDD | GND / 3.3V |

- AS3935 モジュールに 10kΩ プルアップあり（SW-I2C 側）。外部 XTAL 不可 → 内蔵 HSI 運用。
- 予備 GPIO ゼロ（ハード電源断が要るなら TSSOP20 検討）。

---

## クイックスタート

### 1) ファーム（CH32V003）

依存：`riscv-none-elf-gcc`、[ch32fun](https://github.com/cnlohr/ch32fun)、WCH-LinkE。詳細は [firmware/BUILD.md](firmware/BUILD.md)。

```bash
# Makefile（検証済）
cd firmware
make main.bin CH32FUN=/path/to/ch32fun/ch32fun     # ビルド
# 書込は minichlink 推奨（PlatformIO同梱OpenOCDが古く未対応の環境あり）
/path/to/ch32fun/minichlink/minichlink.exe -w .../main.bin flash -b
```

PlatformIO の場合は `firmware/ch32fun` に ch32fun を symlink/コピー後 `pio run`（[BUILD.md](firmware/BUILD.md)）。
デバッグは `config.h` の `#define DEBUG 1` で SDI コンソール出力（`minichlink -T` で観測）。

### 2) ホスト（ESP32）

`arduino/ThunderSense` を Arduino の `libraries/` へ。

```cpp
#include <Wire.h>
#include "ThunderSense.h"
ThunderSense ts;
void setup(){ Wire.begin(); ts.begin(Wire); ts.syncTime(unixEpoch); }
void loop(){
  TSEvent ev[TS_MAX_EVENTS];
  int n = ts.poll(ev, TS_MAX_EVENTS);          // 受信＋CRC検証＋ACK
  for(int i=0;i<n;i++) if(ev[i].isLightning())
    Serial.printf("%u km, e=%lu\n", ev[i].distanceKm, ev[i].energy);
}
```

---

## テスト結果（要約）

実機（AS3935 センサ側）検証済み。詳細ログ → [TESTLOG.md](TESTLOG.md)。

- ビルド：Makefile / PlatformIO とも SUCCESS（**FLASH 55% / RAM 66%**、triple/32＋全機能、DEBUG=0）
- SW-I2C 通信確立、AS3935 応答（アドレス **0x00**、reg0x00=0x24）
- LCO 校正成功（**TUN_CAP=4**）→ option-byte 保存 → 次回起動でロード（高速起動）
- **スパーク試験**：電子ライターで **disturber を 81件検出**（雷ではなく妨害波と正しく分類）
- IWDG・起動シーケンス・多重防御・フェイルセーフ動作確認

**未検証**：上位 HW-I2C スレーブ側（ESP32 からの bundle 読取・DMA-TX）はコード完成・ビルド済だが実機通し確認は次段。

---

## ドキュメント

- [Docs/README.md](Docs/README.md) — 設計根拠（なぜブリッジ／スケジューリング哲学／多重防御）
- [Docs/SPEC.md](Docs/SPEC.md) — 詳細仕様（構造体・状態機械・優先度・DMA・校正・健康度・フラッシュ・メモリ収支）
- [Docs/I2C_REFERENCE.md](Docs/I2C_REFERENCE.md) — I2C コマンド仕様（全 14 コマンド）
- [arduino/ThunderSense/ThunderSense.h](arduino/ThunderSense/ThunderSense.h) — ホスト API（自己文書化）

---

## ライセンス・クレジット

- 本プロジェクトのコード：MIT（[LICENSE](LICENSE)）
- [ch32fun](https://github.com/cnlohr/ch32fun)（MIT）— CH32V003 ランタイム
- LCO 校正手順の出典：AE-AS3935 デモ / FreqCounter（Martin Nawrath, KHM LAB3, LGPL）※AVR コードは非移植、手順・目標値のみ流用
- `AE_AS3935DEMO/` は秋月電子通商の配布物（参考・三者資料）
