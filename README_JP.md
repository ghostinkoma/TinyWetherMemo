# TinyWetherMemo（日本語）

*[English → README.md](README.md) · 日本語（本ファイル）*

**AS3935 雷センサ → CH32V003 → I2C ブリッジ** と、上位ホスト（ESP32）用アプリ／ライブラリ。

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
- **感度・校正の全制御**：AFE(屋内/屋外)・ノイズフロア・WDTH・SREJ・MIN_NUM・任意レジスタ R/W・LCO 再校正を **ホスト I2C から**
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
TinyWetherMemo/
├─ README.md / README_JP.md   # 本書 (英 / 日)
├─ LICENSE                     # 独自の非商用ライセンス
├─ TESTLOG.md                  # 実機テスト結果（ブリッジ）
├─ Docs/                       # 設計・仕様
│   ├─ README.md               # 設計根拠
│   ├─ SPEC.md                 # 詳細仕様
│   ├─ I2C_REFERENCE.md        # I2C コマンド仕様
│   ├─ SOP8PinOut.txt
│   └─ TEST_LOG/               # 日付別の実機テストログ
├─ firmware/                   # CH32V003 ファーム（ch32fun / PlatformIO）  ★PIOプロジェクト
│   ├─ *.c / *.h               # モジュール（config/protocol/capture/bundle/...）
│   ├─ Makefile / platformio.ini / funconfig.h / BUILD.md
├─ host_esp32c3/               # ESP32-C3 ホストアプリ「WetherLoggerBox」  ★PIOプロジェクト
│   ├─ README.md / README_JP.md   (英 / 日)
│   ├─ platformio.ini          # env:esp32c3  (pio run / pio run -t upload)
│   ├─ src/                    # main / io_task(RTOS) / sensors / lightning / wifi / oled
│   ├─ lib/ThunderSense/       # 本リポジトリ arduino/ 版ホストライブラリを同梱
│   └─ Docs/                   # ARCHITECTURE / SENSORS / LIGHTNING / ScreenShots
└─ arduino/ThunderSense/       # ESP32 ホストライブラリ（配布用の元）
    ├─ ThunderSense.h / .cpp   # ← .h を読めば使い方が分かる
    └─ examples/ReadLightning/
```
> 秋月 AE-AS3935 のデモ資料は開発時の参考のみで、本リポジトリには再配布しません（[LICENSE](LICENSE) 三者表記）。

> **2 つの PlatformIO プロジェクト**が同居: `firmware/`（CH32V003 ブリッジ, `platform=ch32v`）と
> `host_esp32c3/`（ESP32-C3 ロガー, `platform=pioarduino`）。各フォルダで `pio run` / `pio run -t upload`。

### ホストアプリ WetherLoggerBox の主機能（詳細 → [host_esp32c3/README_JP.md](host_esp32c3/README_JP.md)）
- **RTOS**: ioTask が I2C 単独所有（雷+センサ+校正）、loopTask が WiFi/Web/FS。`g_state`(mutex) で受渡し。
- **Web SPA（7ページ）**: ホーム/チャート/WiFi/雷校正/オフセット/データ/**設定** — 三線メニュー・レスポンシブ・ダーク/ライト。**UIは英語既定・日本語切替可**（設定ページ）。gzip 配信・自己スケジュールポーリング。
- **チャート**: ライブ/分足/時間足/日/週/月（**Chart.js ローカル同梱=CDN非依存・オフライン描画可**）。時足以降は FS 長期履歴。非表示にした系列はライブ更新をまたいで維持。
- **認証**: ログイン(SHA-256+RNG, Cookie) + パスワード変更、設定用 SoftAP は WPA2、保存WiFiパスは AES-256 暗号化。
- **WiFi**: **AP と STA は排他**（同時に上げない＝LAN露出対策）。STA が接続できなければ AP へフォールバック。
- **HTTPS(443) 併設**（自己署名 EC P-256）+ HTTP(80)。`esp_http_server`(httpd) 実装。
- **データ**: LittleFS へ CSV(`time,temp,humi,thunder`, NTP時刻/オフライン救済) + 長期履歴を FS 永続化。int16 圧縮。
- **信頼性/省電力**: タスクWDT、メッシュ BSSID ロック+再ローミング、RTC 時刻保持、WiFiモデムスリープ + CPU 80MHz。
- パーティション: 4MB を app 1.375MB / FS(LittleFS) 2.5MB / coredump 64KB。

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

クロックは **既定 48MHz**（実機検証値）。24MHz(HSI直結・PLL停止)の省電力オプションもあり（`firmware/funconfig.h` / `config.h` `SYS_CLK_HZ`）。ただし消費差は約1.8mA(7.0→5.2mA)と僅少のため、安定重視で 48MHz を採用。

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

実機検証済み。詳細ログ → [TESTLOG.md](TESTLOG.md) / [Docs/TEST_LOG/](Docs/TEST_LOG/)。

- ビルド：Makefile / PlatformIO とも SUCCESS（**Flash 55% / RAM 66%**、triple/32＋全機能、DEBUG=0）
- SW-I2C 通信確立、AS3935 応答（アドレス **0x00**、reg0x00=0x24）
- LCO 校正成功 → option-byte 保存 → 次回起動でロード（高速起動）
- 上位 HW-I2C スレーブ検証：ESP32-C3 から status(12B) / bundle(389B, poll) の読取・ACK が通る。
- **検知チェーン検証済み**：電子ライターのスパークで、全経路（AS3935→CH32V003→I2C→ESP32→Web）のイベントカウンタが確実に増加。ただし**分類は源に依存**し、強い/遮蔽型ライターのスパークは*妨害波*ではなく*ノイズ*として計数される（AS3935の仕様どおりで不具合ではない）。→ [Docs/TEST_LOG/WetherLoggerBox_2026-09-13.md](Docs/TEST_LOG/WetherLoggerBox_2026-09-13.md)
- **WiFi断でもデータ完全性**：ホストが一時到達不可の間も CH32V003 は捕捉を続け、イベント計数は増加継続。復帰時に欠落なし（ブリッジのバッファ＋FS永続は WiFi 非依存）。

> ⚠ 大容量 bundle(389B) 読取は ESP 側の既定 I2C タイムアウト(~50ms)で timeout(Error263) するため、
> ホスト側で `Wire.setTimeOut(400)` + 「status で pending>0 の時だけ bundle を読む」最適化を実施（詳細は [host_esp32c3](host_esp32c3/README_JP.md)）。

> ⚠ **CH32V003 書込み注意**：共有電源系の**異常電流**が WCH-LinkE 書込みを妨げることがある（本件の実例は**ホスト側 LDO の破損**が原因で、モジュール交換で書込みもブートループも解消）。給電が正常なら WCH-LinkE v2.17 + minichlink で問題なく書込可（`minichlink -w firmware.bin flash -b`）。

---

## ドキュメント

- [Docs/README.md](Docs/README.md) — 設計根拠（なぜブリッジ／スケジューリング哲学／多重防御）
- [Docs/SPEC.md](Docs/SPEC.md) — 詳細仕様（構造体・状態機械・優先度・DMA・校正・健康度・フラッシュ・メモリ収支）
- [Docs/I2C_REFERENCE.md](Docs/I2C_REFERENCE.md) — I2C コマンド仕様（全 14 コマンド）
- [Docs/EVENT_STORAGE_AND_COMMANDS_JP.md](Docs/EVENT_STORAGE_AND_COMMANDS_JP.md) — イベントバッファ & コマンド経路のアーキノート（なぜN_BINS=32か／N_BINS ⟂ コマンド経路）
- [Docs/TEST_LOG/](Docs/TEST_LOG/) — 日付別の実機テストログ
- [arduino/ThunderSense/ThunderSense.h](arduino/ThunderSense/ThunderSense.h) — ホスト API（自己文書化）

> 詳細Docs（SPEC / ARCHITECTURE / LIGHTNING / SENSORS / I2C_REFERENCE）は現状日本語です。英語版は追って整備予定。

---

## ライセンス・クレジット

- 本プロジェクトのコード：**独自の非商用ライセンス**（[LICENSE](LICENSE)）。
  **非商用に限り**利用・改変・**フォーク・再配布は自由**。**無保証・作者は一切の責任を負わない**。
  **改変の有無を問わず配布・フォーク時は、原作者 `ghostinkoma` と本リポジトリ URL
  `https://github.com/ghostinkoma/TinyWetherMemo` の明記を義務**とします（[LICENSE](LICENSE) 第3条）。
  商用利用は作者へ個別許諾を（OSI 承認のオープンソースではありません）。
- 作者: ghostinkoma <ghostinkoma@gmail.com> ／ リポジトリ: https://github.com/ghostinkoma/TinyWetherMemo
- [ch32fun](https://github.com/cnlohr/ch32fun)（MIT）— CH32V003 ランタイム（本リポジトリには非同梱）
- LCO 校正手順の出典：FreqCounter（Martin Nawrath, KHM LAB3, LGPL）※AVR コードは非移植、手順・目標値のみ流用
- AE-AS3935 モジュールのデモ資料（秋月電子通商）は開発時の参考のみ。**本リポジトリには再配布しません**（原配布元 https://akizukidenshi.com/ の条件に従い入手のこと）。
