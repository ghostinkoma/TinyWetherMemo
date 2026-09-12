# WetherLoggerBox（ESP32-C3 ホスト・日本語）

*[English → README.md](README.md) · 日本語（本ファイル）*

簡易百葉箱ロガー。**温度/湿度/気圧 + 雷** を測定し、Web ダッシュボード(7ページ SPA)で
表示・設定する。雷は AS3935→CH32V003 **ThunderSense** ブリッジ(I2C スレーブ 0x28)から取得。

- 仕様: [Docs/SPEC.md](Docs/SPEC.md) ／ アーキ: [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md)
- センサ: [Docs/SENSORS.md](Docs/SENSORS.md) ／ 雷・危険度: [Docs/LIGHTNING.md](Docs/LIGHTNING.md)

## ハード / 配線 (I2C 共有バス)
| 信号 | ESP32-C3 |
|---|---|
| SDA | GPIO8 |
| SCL | GPIO9 |

同一 I2C バス上: **AHT20(0x38)** + **BMP280(0x76/0x77)** + **ThunderSense 雷(0x28)** (+任意 OLED 0x3C)。
bring-up は 100kHz(`config.h WLB_I2C_HZ`)。GND 共通・3.3V 必須。
> CH32V003 側配線・ファームは親リポジトリ [../firmware](../firmware) と [../README.md](../README.md)。

## ビルド / 書き込み (PlatformIO)
```
pio run                                  # ビルド
pio run -t upload --upload-port COMxx    # 書込 (ポートは環境依存)
```
プラットフォームは **pioarduino (arduino-esp32 コア3.x)**。ESP32-C3 のネイティブ USB は
`pio device monitor` (miniterm/pyserial) が Windows で不安定なため、**動作確認は Web** で行う。
> ⚠ 書込前に `esptool read_mac` で対象MACを確認すること（無関係なESP32を絶対に上書きしない）。
> Windows で `pio` が PATH に無い場合: `%USERPROFILE%\.platformio\penv\Scripts\pio.exe` を直接実行。

## 初回セットアップ (秘密情報 — clone 直後に必要)
公開リポジトリに実資格情報/秘密鍵は含めない。以下は **`.gitignore` 済み**で、clone 後に各自用意する
(未用意でもプレースホルダ/公開サンプルでビルドは通る)。
1. **WiFi 資格情報**: [`src/config.local.h.example`](src/config.local.h.example) を **`src/config.local.h`** に
   コピーし SSID / パスワード(必要なら `WLB_AP_PASS`)を記入。`config.h` から自動 include され既定より優先。
   未作成でも起動後に WiFi 設定ページから投入可。
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

**gzip 再生成 (HTML を編集したら必ず実行)** — cp932 誤読による文字化けを避けるため **UTF-8 厳守**:
`powershell -ExecutionPolicy Bypass -File tools\gen_page_gz.ps1`（`R"HTML(...)HTML"` の中身を抽出→UTF-8 gzip→`web_page_gz.h` 出力→ラウンドトリップ検証）。その後 再ビルド。
> `Get-Content -Raw`(既定 cp932) で読むと日本語が化ける。必ず UTF-8 で扱う。

**UI言語**: 英語既定・**設定ページで日本語切替**（ブラウザに保存）。実装は `web_ui.cpp` の `I18N`。

**REST エンドポイント**: `/`(gzip SPA) ／ `/chart.min.js`(**ローカル同梱 Chart.js**; gzip, CDN非依存) ／
`/api/auth`・`/api/login`・`/api/logout`・`/api/passwd`(認証) ／ `/api/now` ／
`/api/history?scope=`(**live/min/hour/day/week/month**; 時足以降は FS 長期履歴) ／ `/api/settings` ／
`/api/wifi`・`/api/wifi/scan` ／ `/api/calib` ／ `/api/offset` ／ `/api/logcfg` ／
`/api/csv`(DL 時のみヘッダ付与) ／ `/api/logclear`。**`/`と`/chart.min.js`と認証3種以外は要ログイン(401)**。

## セキュリティ (ESP32 HW セキュリティ・ペリフェラル活用)
- **ログイン認証** [`src/auth.cpp`](src/auth.cpp): **SHA-256(HW)** でソルト付きハッシュ、**RNG(HW `esp_random`)** で
  ソルト/セッショントークン(128bit)。Cookie セッション(24h スライディング)。既定 `wether/wether`(初回シード、**変更推奨**)。
  設定/アカウントUIでパスワード変更(全セッション失効)。無効化は `WLB_AUTH_ENABLE 0`。
- **設定パスワード暗号化** [`src/settings.cpp`](src/settings.cpp): WiFiパスを **AES-256-CBC(HW)** で `passenc=` 保存(平文非保持)。
  鍵=SHA-256(STA MAC + 固定salt)=デバイス固有、IV は毎回 RNG。⚠ フラッシュ暗号化(eFuse)なしでは鍵はMAC由来で
  導出可能=難読化グレード。真の機密化は Secure Boot/Flash Encryption(eFuse=物理・不可逆)が必要。
- **設定用 SoftAP は WPA2**(`WLB_AP_PASS`, 8文字以上)。無認証な近接再設定を防止。
- **HTTPS(TLS)**: Web を `esp_http_server`(httpd) へ移植し **HTTP(80) と HTTPS(443) を併設**
  ([web_httpd.h](src/web_httpd.h) が WebServer 風シムで既存ハンドラ流用)。証明書は **自己署名 EC P-256**
  ([cert_pem.h](src/cert_pem.h); TLSは mbedtls/HW)。⚠ 自己署名のためブラウザ警告あり。真の信頼には独自ドメイン+ACME 等が必要。

## WiFi モード (AP または STA = 排他)
AquaController 準拠で **AP と STA を同時に上げない**(併存すると上位LANをAP経由で晒すため)。起動時、
`staMode=STA` かつ SSID 有り → **STA 単独**、SSID 空/AP 指定 → **AP 単独**。STA が `WLB_STA_FALLBACK_MS`
以内に接続できなければ **AP へフォールバック**し、設定用に到達性を確保する。

## 使い方 (Web ダッシュボード)
1. 未設定(または AP モード)時は **SoftAP `WetherLogger`(WPA2, 既定パス `wetherbox`)** が起動 → 接続し **http://192.168.4.1/**。
2. 「WiFi」ページで SSID/パスワード/mDNS名(既定 **WetherMemo**)と起動モード=STA を保存 → 再起動で STA 接続(APは上げない)。
3. 以後は同一 LAN から **http://WetherMemo.local/** (mDNS) または払い出し IP で到達。

### 7 ページ (三線メニュー / レスポンシブ / ダーク・ライト切替 ◐ / 英・日)
1. **ホーム**: 温度・湿度・気圧、直近30分の雷頻度、**危険度**(積和; [LIGHTNING.md](Docs/LIGHTNING.md))。
2. **チャート**: Chart.js 時系列。**ライブ/分足/時間足/日/週/月**。時足以降は FS 長期履歴(絶対epoch)から取得し、
   未蓄積時は細かい足へフォールバックして必ず描画。**Chart.js はローカル同梱なのでオフラインでも描画可**。非表示系列はライブ更新をまたいで維持。
3. **WiFi**: SSID 選択+PW、mDNS 名、起動モード(AP/STA)。STA 時は払い出し IP・本機 AP SSID・現在モードを表示。
4. **雷キャリブレーション**: LCO 再校正+結果、室内/室外、NF_LEV、WDTH、SREJ、最小落雷数(AE-AS3935 準拠)。
   スパーク試験の校正メモは [Docs/TEST_LOG](../Docs/TEST_LOG/) 参照。
5. **温湿度オフセット**: 温度/湿度に加算補正。
6. **データ/測定頻度**: 測定周期(既定5秒)、最小最大除外+n平均(除外時 n≥4)、**CSV DL**。CSV 列 = `time,temp,humi,thunder`
   (ヘッダは DL 時のみ)。time=NTP日時、temp=**AHT20のみ**、thunder=危険度%。BMP280(気圧)は冗長のため CSV 非記録。
7. **設定**: UI 言語(English / 日本語)。

設定は **LittleFS `/settings.ini`** 永続化。校正/感度は Web→ioTask のキュー経由で AS3935 へ反映(I2C 単独所有を厳守)。

## データ永続化 / 時刻
- **CSV ログ**(日別リング・日付名ファイル・時刻のみ行)。オフライン(NTP未同期)時は `B<起動秒>` で記録し、
  NTP 確定時に `datalog_patch_boottime()` が絶対時刻へ一括置換。
- **FS 長期履歴** [`src/histfs.cpp`](src/histfs.cpp): 平均値を `/hist.bin` に 15B/レコード・**絶対epoch**で追記→
  日/週/月足は再起動をまたいで描画。起動時に末尾を RAM リングへ復元(`histfs_seed`)。FS 書込は **loopTask 単独**。
- 履歴の RAM リングは **int16 固定小数点圧縮 (15B/レコード)**: 温湿度×100 / 気圧 `(hPa-1000)×100`。

## 信頼性 / 電源 / 時刻
- **WiFi 接続**: 起動時スキャンで同一SSID メッシュの**最強RSSI BSSID へロック接続**。**メッシュ再ローミング**
  (`maybeRoam`): 接続後も RSSI を監視し `< WLB_ROAM_RSSI_TH`(-75dB) の時のみ再スキャンし、+`WLB_ROAM_MARGIN`(8dB)
  以上強い BSSID へ張り替え。機能トグル `WLB_ENABLE_*`(config.h)。
- **タスクWDT** (`WLB_WDT_TIMEOUT_MS`=30s): io/loop 両タスク監視、ハング時パニック→自動再起動。coredump 保存。
- **RTC 時刻保持** ([`src/wifi_session.cpp`](src/wifi_session.cpp)): CPU ソフトリセット(WDT再起動等)を跨いで時刻保持。
  前回 NTP 同期済なら再起動直後に即 timeValid(電源断では 1970 に戻り誤検出しない)。
- **大容量CSV配信**は 8KB 毎に `yield()`+WDT feed し、DL 中も WiFi/NTP と ioTask(センサ)を止めない。
- **★ WiFi断への耐性**: 捕捉・計数・FS記録は local I2C 経由で **WiFi非依存**。WiFiが落ちても雷イベントは失われない
  (実機実証 → [../Docs/TEST_LOG/WetherLoggerBox_2026-09-13.md](../Docs/TEST_LOG/WetherLoggerBox_2026-09-13.md))。
- ⚠ **デバッグ注意**: ESP32-C3 の USB シリアルを開くと C3 がリセットされる。稼働中の検証は HTTP のみで行う。

## パーティション ([partitions.csv](partitions.csv))
ビルド実測(app≒1.3MB)から算定: **factory(app)=1.375MB / spiffs(FS)=2.5MB / coredump=64KB**。
1MB app はバイナリが収まらないため不可。オフセット変更時は次回起動で LittleFS が再フォーマットされる。

## 実装状況
- **完了**: 7ページ SPA、現在値/雷危険度、ライブ/分足/時間足チャート+**FS長期履歴(日/週/月)**、
  WiFi(AP/STA/mDNS, **AP-STA排他**+STA→APフォールバック)+再ローミング、校正/感度(サーバ側クランプ)、オフセット、
  測定頻度、LittleFS 永続化、CSV ログ(NTP時刻/オフライン救済/DL時yield)、int16圧縮、WDT、**RTC時刻保持**、
  **ログイン認証(SHA-256/RNG)+パスワード変更**、**設定AES暗号化**、**SoftAP WPA2**、**Chart.js ローカル同梱**、
  **多言語UI(英語既定/日本語)**、チャート系列の非表示保持。
- **雷poll(389B)修正済**: 束読み(CMD_READ_BUNDLE)が既定I2Cタイムアウト(~50ms)超過で Error263 → (1) `Wire.setTimeOut(400)`、
  (2) 12BのreadStatusで `pending>0` の時だけ束読み(待機中は大容量読みゼロ)。実機で待機中エラー0を確認。
- **省電力**: ESP32=WiFiモデムスリープ(`WIFI_PS_MIN_MODEM`)+CPU 80MHz。CH32V003=既定48MHz(24MHz HSI直結オプション有=
  ブリッジ動作確認済・約1.8mA差、安定重視で48MHz採用)。
- **HTTPS(443) 併設**: httpd 移植 + 自己署名 EC P-256。実機で HTTP/HTTPS 両疎通確認。
- **未 (別途)**: MySQL/SQL 送信、Secure Boot/Flash Encryption(eFuse=物理)、真の自動ライトスリープ(要デューティ設計)。
