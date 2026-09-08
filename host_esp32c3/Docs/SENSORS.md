# 環境センサ + WiFi セッション — 設計メモ

WetherLoggerBox 第2段。非ブロッキング WiFi 管理と、差し替え可能な環境センサ群。

## WiFi セッション (`wifi_session.*`)

「WiFiセッションに十分配慮」= **loop() を絶対に止めない**設計。

- 状態機械: `DISABLED → CONNECTING → CONNECTED ⇄ WAIT_RETRY`。busy-wait 無し。
- 切断は `loop()` 内で検知し、**指数バックオフ**（1s→2s→…→30s上限）で自動再接続。
  ライブラリ任せの自動再接続/persistent は無効化し、状態機械で完全掌握。
- NTP は接続後に一度だけ開始。時刻確定を `timeValid()` で通知。
- コールバック 2 本: `onConnected()`（HTTPサーバ起動等）/ `onTime(epoch)`（雷ブリッジへ `syncTime`）。
- `SSID` が空なら `DISABLED` で何もしない（オフライン単体テスト可）。

```cpp
WifiSession net;
net.onTime([](uint32_t e){ ln.syncTime(e); });
net.setNtp("pool.ntp.org", 9*3600);   // JST
net.begin(SSID, PASS);
loop: net.loop(millis());             // ブロックしない
```

## 環境センサ (共通IF `env_sensor.h`) — 同期 / ブロッキング可 (ioTask 上)

**アーキテクチャは RTOS**（決定記録は [ARCHITECTURE.md](ARCHITECTURE.md)）。センサの
ブロッキング(`delay`)は `vTaskDelay` として CPU を譲るので、**専用 `ioTask` に隔離すれば**
WiFi/loop を止めない。よってドライバは stock ライブラリをそのまま使う同期 IF に回収した。

- `begin()`: 検出・初期化（ブロック可。ioTask 内で1回）。
- `read(EnvReading&)`: 1回測定（ブロック可）。`*Valid` で有効フィールドを示す。
- I2C に触るのは ioTask だけ。上位は `g_state` のスナップショットを読む（[ARCHITECTURE.md](ARCHITECTURE.md)）。

| モジュール | センサ | 物理量 | I/F | 実装(回収先) |
|---|---|---|---|---|
| `sensor_ahtx0` | **AHT25** / AHT20/21 | 温・湿 | I2C 0x38 | Adafruit_AHTX0 |
| `sensor_bmp280` | **BMP280** | 温・気圧 | I2C 0x76/0x77 | Adafruit_BMP280 (NORMAL) |
| `sensor_bme280` | BME280 (気圧補正用) | 温・湿・気圧 | I2C 0x76/0x77 | Adafruit_BME280 (NORMAL) |
| `sensor_bme680` | BME680 | 温・湿・気圧・ガス | I2C 0x76/0x77 | Adafruit_BME680 (performReading) |
| `sensor_sht3x` | SHT35 (温度補正用) | 温・湿 | I2C 0x44/0x45 | Adafruit_SHT31 |
| `sensor_dht11` | DHT11 | 温・湿 | 1線 GPIO | DHT sensor library |
| `sensor_ds18b20` | DS18B20 | 温 | OneWire GPIO | Dallas (waitForConversion=true) |
| `sensor_s5851a` | S-5851A | 温 | I2C 0x48.. | 自前(対応lib無し) |

### ブロックはどこで吸収されるか

全ての測定ブロックは `ioTask` 内で `vTaskDelay` として譲られ、loop/WiFi は動き続ける。
参考として各ライブラリの実ブロック量（実ソース検証済み）:

| センサ | 起動時 init(1回) | 毎測定のブロック |
|---|---|---|
| AHT25 | 即 | getEvent ~80ms (delayループ=譲る) |
| BMP280 | `delay(100)` 1回 (begin末尾 L108) | readTemp/Press に delay無し(NORMAL即読) |
| BME280 | ~130ms 1回 (init) | 読取に delay無し(NORMAL即読) |
| BME680 | ~10ms 1回 | performReading ~200ms (`delay`=譲る) |
| SHT35 | reset ~15ms | 読取 ~15ms |
| DS18B20 | begin ~数ms | requestTemperatures 750ms待ち |
| DHT11 | warmup 1.5s | ~25ms ビットバング(2秒周期, noInterrupts~4ms) |
| S-5851A | 即 | レジスタ読み ~0.5ms |

> RTOS が効く条件はブロックが `delay()`(=vTaskDelay, 譲る) であること。BMP280 の
> ブロック `while(status&0x08)delay(1)`(L370) は FORCED 専用で NORMAL 運用では呼ばれない。
> DHT11 の ~25ms や OneWire の µs 級 busy-wait は短く、同格 loop を長時間飢えさせない。

### センサの選択

`src/config.h` の `#define WLB_SENSOR_xxx` を有効化したものだけがビルドされる。
無効センサはラッパ `.cpp` が `#ifdef` で空になり、そのライブラリも LDF から除外される
（採用構成のみ = Flash 75.8% / 全部有効 = 77.0%、いずれもビルド確認済み）。

アドレス/ピンも `config.h` で変更可（BMP/BME の 0x76⇔0x77、SHT の 0x44⇔0x45 等）。

### ハブ (`sensors.*`) — ioTask 内で駆動

- `begin()`: I2C を一度だけ初期化 → 有効センサを `begin()`（ブロック可）。
- `sample(out, N)`: present なセンサを 1 回ずつ `read()`（ブロック可）。
- `merged()`: **登録順=優先順**で 1 本の `EnvReading` に合成（採用の AHT25→BMP280 が先頭）。
- `byName("SHT35")` / `byName("BME280")`: リファレンスセンサを取り出し、後段の
  オフセット校正（SHT35=温度基準 / BME280=気圧基準）に使う。

ioTask が 1 秒周期で `sample()/merged()` を実行し `g_state` へ publish。ダッシュボード段の
8サンプル平均（1秒×10→最小最大を捨て `>>3`）は、この publish 値を集計するだけでよい。

## 未実装（全体像の残り）

Web UI 3タブ（Chart.js/スコープ切替）・FS記録+CSV DL・MySQL送信・雷/温度/気圧の校正UI。
いずれも loopTask 側（`g_state` を読む）に実装し、I2C には触れない（[ARCHITECTURE.md](ARCHITECTURE.md)の不変条件）。
