# アーキテクチャ決定: RTOS採用 vs ライブラリ回収

## 背景

センサ初期化/変換の `delay()` が協調ループ(`WifiSession.loop()`)を止め、WiFi 再接続検知・
NTP・バックオフのサービスが停滞する問題。対策の方向性を2つ天秤にかけた。

- **A: RTOS採用** — 雷+センサを専用タスクに載せ、ブロッキングな stock ライブラリを
  そのまま使う。
- **B: ライブラリ回収(自前非ブロッキング)** — 各ドライバを状態機械に書き換え、
  単一スレッド協調で回す。

## 決定的な前提 (ESP32-C3)

- **シングルコア**。ただし Arduino-ESP32 の `delay()` は `vTaskDelay()` で **CPU を譲る**。
- **WiFi/TCP スタックは別タスク(高優先度)** で常時回る。
- → タスクに載せれば、ブロッキング `delay()` は WiFi を切らない。止まるのは同じタスク
  内の後続処理だけ。よって **ブロッキング処理を専用タスクに隔離すれば解決**。

## 天秤

| 観点 | A: RTOS採用 | B: 自前非ブロッキング |
|---|---|---|
| ライブラリ内部検証コスト | **不要**(どのlibもタスクに載せれば動く) | **毎回必要**(繰り返す負債) |
| 我々の保守コード | S-5851A のみ自前 | 8ドライバの状態機械を永続保守 |
| WiFi セッション | vTaskDelay が譲る→安全。起動時 init ブロックも無害化 | 起動時ブロック/ DHT11 は残る |
| 複雑さ | タスク境界 + スナップショット mutex 1個 | 単一スレッド・ロック無し |
| RAM | タスクスタック +8KB 程度 | 最小 |
| I2C 競合 | IOタスクが単独所有すれば無し | 元々単一 |

## 決定 = A (RTOS採用)

繰り返し発生する「ライブラリ内部検証」の負債(直近 BME680 でまさに支払った)を構造的に
消せることを最重視。コストは「IOタスク1本 + mutex 1個」に有界で、I2C 単独所有により
バス競合も出ない。

## タスク構成

```
  ┌────────────── ioTask (prio 1, I2C 単独所有) ───────────────┐
  │  ln.begin()/env.begin() → for(;;){                          │
  │    takeTimeSync → ln.syncTime  (I2C書込はここだけ)          │
  │    ln.tick(now)                (雷 drain + 危険度, 高頻度)   │
  │    1秒毎: env.sample()/merged() (ブロック可: Adafruit等)     │
  │    g_state.set(cur)            (mutex 保護で publish)        │
  │    vTaskDelay(20)              (譲る)                        │
  │  }                                                          │
  └────────────────────────────────────────────────────────────┘
        │ g_state (SystemSnapshot, mutex)          ▲ requestTimeSync(epoch)
        ▼                                          │
  ┌────────────── loopTask (setup/loop) ───────────────────────┐
  │  net.loop()  (WiFi 非ブロッキング + 再ローミング)           │
  │  g_state.get() を読むだけ (I2C 不可触)                       │
  │  NTP確定/60秒毎: requestTimeSync(epoch)                     │
  │  FS 書込は loopTask 単独: datalog_tick / histfs_append /    │
  │            settings_save (LittleFS 競合回避)               │
  │  NTP初確定: datalog_patch_boottime (B<秒>→絶対時刻)         │
  └────────────────────────────────────────────────────────────┘
  ┌── httpd tasks: HTTP(80) + HTTPS(443,TLS)  (ESP-IDF) ───────┐
  │  URIハンドラ = g_webMtx で全直列化 (旧WebServer相当)。      │
  │  g_state を読むだけ / FS 読取(csv,hist)。I2C は不可触。      │
  └────────────────────────────────────────────────────────────┘
  ┌────────────── WiFi/TCP task (ESP-IDF, 高優先度) ───────────┐
  └────────────────────────────────────────────────────────────┘
```

## 不変条件 (壊すと競合)

1. **I2C に触るのは ioTask だけ**。loop/WiFi/httpd は `g_state` を読むのみ。
2. 雷ブリッジへの `syncTime` は I2C。よって loop は `requestTimeSync(epoch)` で
   **依頼だけ**し、実書込は ioTask が `takeTimeSync()` で受けて行う。
3. `g_state` の読み書きは必ず mutex 経由(`set()/get()`)。
4. **FS(LittleFS) の書込は loopTask 単独**（datalog/histfs/settings）。ioTask は FS に触れない
   （LittleFS はスレッドセーフでないため）。httpd ハンドラの FS 読取(csv/hist)は `g_webMtx` で直列化。
5. **Web ハンドラは `g_webMtx` で全直列化**（HTTP/HTTPS の 2 httpd タスク間の競合と、handleNow/
   handleHistory の静的バッファ競合を防ぐ）。

## 前提(RTOSが効く条件)

ブロッキングは `delay()`(=vTaskDelay, 譲る)であること。busy-wait(`delayMicroseconds`/
`noInterrupts`)は数十µs〜数ms に限る。採用構成(AHT25=getEvent の delay ループ /
BMP280=NORMAL 即読)は満たす。DHT11 の ~25ms ビットバングや OneWire の µs 級 busy は
短く、優先度同格の loop を長時間は飢えさせない。

## OLED (オプション)

OLED も I2C なので**不変条件1に従い ioTask が描画**する(別タスク+バス mutex は採らない)。
ioTask は自分の `cur`(SystemSnapshot) を持つので、それを `WLB_OLED_MS`(既定1s)毎に
`oled.render()` へ渡すだけ。`display()` の I2C 転送 ~25-30ms も ioTask 内に閉じる。

WiFi 状態(RSSI/接続/時刻)は loopTask が持つため、**逆方向チャネル** `g_state.setNet()`
/`getNet()` で ioTask へ渡し OLED に表示する(時刻同期依頼と同じ向きの補完)。

- 有効化: `config.h` の `WLB_OLED`。SSD1306(既定) / `WLB_OLED_SH1106` で SH1106。
- ラベルは ASCII(GFX 標準フォントに日本語なし)。危険度は数値+バー+ SAFE/…/SEVERE。
- ビルド確認: OLED無 Flash75.8% / SSD1306 76.5% / SH1106+全センサ 77.7%。

## 実装ファイル

- `io_task.*`      — ioTask 本体 (env/ln/oled を所有)。I2Cバス自動リカバリ/分足・時足集計/WDT feed
- `shared_state.*` — SystemSnapshot + NetStatus + mutex + 時刻同期依頼 + 履歴リング(int16圧縮/tier)
- `wifi_session.*` — 非ブロッキング WiFi (loopTask)。BSSIDロック/再ローミング/RTC時刻復元
- `sensors.*` / `sensor_*.*` — 同期(ブロッキング可) stock ライブラリ回収
- `lightning.*`    — 雷 + 危険度(積和)。ioTask から tick。pending>0 の時だけ 389B bundle 読取
- `display_oled.*` — OLED 描画 (オプション, SSD1306/SH1106)。ioTask から render
- `web_ui.*`       — 6ページ SPA + REST。httpd(HTTP80/HTTPS443) にハンドラ登録。loopTask から begin
- `web_httpd.*`    — httpd を WebServer 風に扱うシム(HttpCtx)。既存ハンドラ流用のため
- `auth.*`         — ログイン認証 (SHA-256+RNG, Cookieセッション)
- `datalog.*`      — CSV ログ(LittleFS) + DL 配信。loopTask 単独
- `histfs.*`       — 時足の FS 長期履歴(/hist.bin, 15B/レコード)。loopTask 単独
- `settings.*`     — /settings.ini 永続化(WiFiパスは AES-256 暗号)
- `main.cpp`       — setup/loop。WDT init、省電力(CPU80MHz/モデムスリープ)、FS永続化トリガ
