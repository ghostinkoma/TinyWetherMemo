# WetherLoggerBox (ESP32-C3 ホスト)

簡易百葉箱ロガー。**温度/湿度/気圧 + 雷** を測定し、Web ダッシュボード(6ページ SPA)で
表示・設定する。雷は AS3935→CH32V003 **ThunderSence** ブリッジ(I2C スレーブ 0x28)から取得。

- 仕様: [Docs/SPEC.md](Docs/SPEC.md) ／ アーキ: [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md)
- センサ: [Docs/SENSORS.md](Docs/SENSORS.md) ／ 雷・危険度: [Docs/LIGHTNING.md](Docs/LIGHTNING.md)

## ハード / 配線 (I2C 共有バス)
| 信号 | ESP32-C3 |
|---|---|
| SDA | GPIO8 |
| SCL | GPIO9 |

同一 I2C バス上: **AHT20(0x38)** + **BMP280(0x76/0x77)** + **ThunderSence 雷(0x28)** (+任意 OLED 0x3C)。
bring-up は 100kHz(`config.h WLB_I2C_HZ`)。GND 共通・3.3V 必須。
> CH32V003 側配線・ファームは親リポジトリ [../firmware](../firmware) と [../README.md](../README.md)。

## ビルド / 書き込み (PlatformIO)
```
pio run                                  # ビルド
pio run -t upload --upload-port COM13    # 書込 (ポートは環境依存。COM7/8 は AquaController)
```
プラットフォームは **pioarduino (arduino-esp32 コア3.x)**。ESP32-C3 のネイティブ USB は
`pio device monitor` (miniterm/pyserial) が Windows で不安定なため、**動作確認は Web** で行う。
> Windows で `pio` が PATH に無い場合: `%USERPROFILE%\.platformio\penv\Scripts\pio.exe` を直接実行。

## 初回セットアップ (秘密情報 — clone 直後に必要)
公開リポジトリに実資格情報/秘密鍵は含めない。以下は **`.gitignore` 済み**で、clone 後に各自用意する
(未用意でもプレースホルダ/公開サンプルでビルドは通る)。
1. **WiFi 資格情報**: [`src/config.local.h.example`](src/config.local.h.example) を
   **`src/config.local.h`** にコピーし、自分の SSID / パスワード(必要なら `WLB_AP_PASS`)を記入。
   `config.h` から自動 include され既定より優先される。未作成時は起動後に WiFi 設定ページからも投入可。
2. **TLS 証明書/鍵 (HTTPS)**: `powershell -ExecutionPolicy Bypass -File tools\gen_cert.ps1` で
   デバイス固有の **`src/cert_pem.h`** を生成(要 openssl)。未生成時は公開ダミー
   [`src/cert_pem_sample.h`](src/cert_pem_sample.h) へ自動フォールバック(**この鍵は公開=保護価値なし**、必ず再生成)。
3. **既定ログイン**: `wether/wether`(config.h `WLB_AUTH_DEFAULT_*`) は初回シード → **ログイン後に必ず変更**。
   SoftAP パス既定 `wetherbox`(`WLB_AP_PASS`) も運用前に変更推奨。

## WebUI 配置 / 構成 (リファレンス)
ダッシュボードは **単一 SPA (HTML+CSS+JS 一体)**。配置と配信は次のとおり:

| 要素 | 実体 | 役割 |
|---|---|---|
| **SPA 原本** | [`src/web_ui.cpp`](src/web_ui.cpp) 内 `PAGE[]` (`R"HTML(...)HTML"` 生文字列) | **編集はここが唯一の真実(source of truth)** |
| **配信データ** | [`src/web_page_gz.h`](src/web_page_gz.h) `WLB_PAGE_GZ[]` | 原本を gzip 圧縮した自動生成物。`handleRoot` が `Content-Encoding: gzip` で配信 |
| REST API | `src/web_ui.cpp` の `handle*` | 後述のエンドポイント |

**gzip 再生成手順 (HTML を編集したら必ず実行)** — cp932 誤読による文字化けを避けるため **UTF-8 厳守**:
1. `web_ui.cpp` の `R"HTML(` … `)HTML"` の中身を **UTF-8** で読み出す (`[IO.File]::ReadAllText(path,[Text.Encoding]::UTF8)`)。
2. `System.IO.Compression.GZipStream(Optimal)` で圧縮。
3. `WLB_PAGE_GZ_LEN` と `WLB_PAGE_GZ[] PROGMEM = {0x..}` を `web_page_gz.h` へ出力。
4. 再ビルド。`gunzip` ラウンドトリップで日本語(例「時間足」)が壊れていないか確認。
> `Get-Content -Raw`(既定 cp932) で読むと日本語が化ける。必ず `-Encoding UTF8` / `ReadAllText(...,UTF8)`。

**REST エンドポイント**: `/`(gzip SPA) ／ `/chart.min.js`(**ローカル同梱 Chart.js**; gzip, CDN非依存) ／
`/api/auth`・`/api/login`・`/api/logout`・`/api/passwd`(認証) ／ `/api/now` ／
`/api/history?scope=`(**live/min/hour/day/week/month**; 時足以降は FS 長期履歴) ／ `/api/settings` ／
`/api/wifi`・`/api/wifi/scan` ／ `/api/calib` ／ `/api/offset` ／ `/api/logcfg` ／
`/api/csv`(DL 時のみヘッダ付与) ／ `/api/logclear`。**`/`と`/chart.min.js`と認証3種以外は要ログイン(401)**。

## セキュリティ (ESP32 HW セキュリティ・ペリフェラル活用)
- **ログイン認証** [`src/auth.cpp`](src/auth.cpp): **SHA-256(HW)** でソルト付きパスワードハッシュ、
  **RNG(HW `esp_random`)** でソルト/セッショントークン(128bit)。Cookie セッション(24h スライディング)。
  既定 `WLB_AUTH_DEFAULT_USER/PASS`(config.h)=`wether/wether` → 初回シード、**ログイン後に変更推奨**。
  P3「アカウント」でパスワード変更(変更で全セッション失効)。無効化は `WLB_AUTH_ENABLE 0`。
- **設定パスワード暗号化** [`src/settings.cpp`](src/settings.cpp): WiFi パスワードを **AES-256-CBC(HW)** で
  `passenc=` として保存(平文非保持)。鍵=SHA-256(STA MAC + 固定salt)=デバイス固有。IV は毎回 RNG。
  ※フラッシュ暗号化(eFuse)なしでは鍵はMAC由来で導出可能=難読化グレード。真の機密化は Secure Boot/
  Flash Encryption(eFuse=物理・不可逆)が必要。RSA(HW)は HTTPS/Secure Boot 導入時に活用余地。
- **設定用 SoftAP は WPA2**(`WLB_AP_PASS`, 8文字以上)。無認証な近接再設定を防止。
- **HTTPS(TLS)**: Web サーバを `esp_http_server`(httpd) へ移植し、**HTTP(80) と HTTPS(443) を併設**
  ([web_httpd.h](src/web_httpd.h) が WebServer 風シムで既存ハンドラを流用、両サーバに同一ハンドラ登録)。
  証明書は **自己署名 EC P-256(ECDSA)** ([cert_pem.h](src/cert_pem.h); TLSは mbedtls/HW支援)。
  ⚠ 自己署名のためブラウザは毎回「安全でない接続」警告を出す(LAN機器はCA発行不可)。真の信頼には
  独自ドメイン+ACME か、内部CAの証明書配布が必要。RSA-2048 も選択可だがハンドシェイクが重いため EC を採用。

## 使い方 (Web ダッシュボード)
1. 起動直後は **SoftAP `WetherLogger`(WPA2, 既定パス `wetherbox`)** が常設 → スマホ等で接続し **http://192.168.4.1/**。
2. 「3 WiFi設定」で自宅 SSID/パスワード/mDNS名(既定 **WetherMemo**)を保存 → 再起動で STA 接続。
3. 以後は同一 LAN から **http://WetherMemo.local/** (mDNS) または払い出し IP で到達。

### 6 ページ (三線メニュー / レスポンシブ / ダーク・ライト切替 ◐)
1. **ダッシュボード**: 温度・湿度・気圧、直近30分の雷頻度、**危険度**(積和; [LIGHTNING.md](Docs/LIGHTNING.md))。
2. **チャート**: Chart.js 時系列。**ライブ/分足/時間足/日/週/月**(年足は廃止=データ量過大)。
   時間足以降は FS 長期履歴(絶対epoch)から取得し、未蓄積時は細かい足へフォールバックして必ず描画。
   ※Chart.js は CDN 取得のため**チャート表示はインターネット接続(STA)が必要**。
3. **WiFi設定**: SSID 選択+PW、mDNS 名、AP/STA。STA 時は払い出し IP・本機 AP SSID 表示。
4. **雷キャリブレーション**: LCO 再校正+結果、室内/室外、NF_LEV、WDTH、SREJ、最小落雷数
   (AE-AS3935 マニュアル準拠)。
5. **温湿度オフセット**: 温度/湿度に加算補正。
6. **データ/測定頻度**: 測定周期(既定5秒)、最小最大除外+n平均(除外時 n≥4)、**CSV DL**。
   CSV 列 = `time,temp,humi,thunder`(ヘッダは DL 時のみ付与)。time=NTP日時、temp=**AHT20のみ**、
   thunder=危険度%。BMP280(気圧)は冗長のため CSV 非記録。

設定は **LittleFS `/settings.ini`** 永続化。校正/感度は Web→ioTask のキュー経由で AS3935 へ反映
(I2C 単独所有を厳守)。

## データ永続化 / 時刻
- **CSV ログ** `/log.csv`(900KB で `/log.old.csv` へ1世代退避)。オフライン(NTP未同期)時は
  `B<起動秒>` で記録し、NTP 確定時に `datalog_patch_boottime()` が絶対時刻へ一括置換。
- **FS 長期履歴** [`src/histfs.cpp`](src/histfs.cpp): 時足(1時間代表値)を `/hist.bin` に 15B/レコードで
  追記(`WLB_HISTFS_CAP`=2160≒90日, 超過で1世代退避)。**絶対epoch**で保存し、日/週/月足は再起動を
  またいで描画。起動時に末尾を RAM 時足リングへ復元(`histfs_seed`)。FS 書込は **loopTask 単独**。
- 履歴の RAM リングは **int16 固定小数点圧縮 (15B/レコード)**: 温湿度×100 / 気圧 `(hPa-1000)×100`。

## 信頼性 / 電源 / 時刻
- **WiFi 接続**: 起動時スキャンで同一SSID メッシュの**最強RSSI BSSID へロック接続**。
  **メッシュ再ローミング** (`maybeRoam`): 接続後も RSSI を監視し `< WLB_ROAM_RSSI_TH`(-75dB) の時のみ
  再スキャンし、+`WLB_ROAM_MARGIN`(8dB) 以上強い BSSID へ張り替え。機能トグル `WLB_ENABLE_*`(config.h)。
- **タスクWDT** (`WLB_WDT_TIMEOUT_MS`=30s): io/loop 両タスク監視、ハング時パニック→自動再起動。coredump 保存。
- **RTC 時刻保持** ([`src/wifi_session.cpp`](src/wifi_session.cpp) begin): ESP32-C3 の RTC は CPU ソフトリセット
  (WDT再起動等)を跨いで時刻保持。前回 NTP 同期済なら再起動直後に `time()>閾値` を検出して即 timeValid とし、
  NTP を待たずイベント/CSV に正しい時刻を付与(電源断では 1970 に戻り誤検出しない)。
- **大容量CSV配信**は 8KB 毎に `yield()`+WDT feed し、DL 中も WiFi/NTP と ioTask(センサ)を止めない。
- ⚠ **デバッグ注意**: COM13 の SerialPort を開くと C3 がリセットされる。稼働中の検証は HTTP のみで行う。

## パーティション ([partitions.csv](partitions.csv))
ビルド実測(app≒1.12MB)から算定: **factory(app)=1.375MB / spiffs(FS)=2.5MB / coredump=64KB**。
1MB app はバイナリが収まらないため不可。オフセット変更時は次回起動で LittleFS が再フォーマットされる。

## 実装状況
- **完了**: 6ページ SPA、現在値/雷危険度、ライブ/分足/時間足チャート+**FS長期履歴(日/週/月)**、
  WiFi(AP/STA/mDNS)+再ローミング、校正/感度(サーバ側クランプ)、オフセット、測定頻度、LittleFS 永続化、
  CSV ログ(NTP時刻/オフライン救済/DL時yield)、int16圧縮、WDT、**RTC時刻保持**、
  **ログイン認証(SHA-256/RNG)+パスワード変更**、**設定AES暗号化**、**SoftAP WPA2**、**Chart.js ローカル同梱**。
- **雷イベント経路(poll)修正済**: 389B束読み(CMD_READ_BUNDLE)が既定I2Cタイムアウト(~50ms)超過で
  Error263になっていた → (1) `Wire.setTimeOut(WLB_I2C_TIMEOUT_MS=400)` で延長、(2) 12BのreadStatusで
  `pending>0` の時だけ束読みするよう最適化(待機中は大容量読みゼロ=Error263根絶)。実機で待機中エラー0を確認。
- **省電力**: ESP32=WiFiモデムスリープ(`WIFI_PS_MIN_MODEM`)+CPU 80MHz(160→80)。応答~100ms維持を実測。
  CH32V003=PLL停止でHSI直結 **48→24MHz**(スリープはせず雷IRQ常時捕捉/[firmware/funconfig.h](../firmware/funconfig.h))。
- **HTTPS(443) 併設**: httpd 移植 + 自己署名 EC P-256。HTTP(80)も維持。実機で HTTP/HTTPS 両疎通確認。
- **未 (別途)**: MySQL/SQL 送信、Secure Boot/Flash Encryption(eFuse=物理)、真の自動ライトスリープ(要デューティ設計)。
