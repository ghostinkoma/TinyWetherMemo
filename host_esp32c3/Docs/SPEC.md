# WetherLoggerBox — 仕様書 (SPEC)

ESP32-C3 簡易百葉箱ロガー。温度/湿度/気圧 + 雷(AS3935→CH32V003 ThunderSence, I2C 0x28)。
Web ダッシュボードは **三線メニュー(ハンバーガー)** の 6 ページ SPA。

- アーキテクチャの土台は [ARCHITECTURE.md](ARCHITECTURE.md)（RTOS: ioTask が I2C 単独所有、
  loopTask が WiFi/Web、`g_state` で受け渡し）。センサ詳細は [SENSORS.md](SENSORS.md)、
  雷/危険度は [LIGHTNING.md](LIGHTNING.md)。

## 採用ハード / バス
- ESP32-C3、I2C **SDA=GPIO8 / SCL=GPIO9**（bring-up 100kHz、安定後 400k 可）。
- AHT20(0x38) + BMP280(0x76 or **0x77**、dual-address 対応) + ThunderSence 雷ブリッジ(0x28)。
- OLED(0x3C) は任意（`WLB_OLED`）。

## Web UI 全体
- **三線メニュー**で 6 ページ切替（SPA、1 ページ HTML を ESP が配信）。
- **レスポンシブ**（スマホ/PC）。
- **ダーク**（背景ネイビー `#0b1020`、文字白）/ **ライト**（背景白、枠線ライトグリーン系
  `#5fb98c`、文字黒）。トグルで切替、`localStorage` に記憶。
- グラフは **Chart.js 4.4.1**（**ローカル同梱=CDN非依存**: `/chart.min.js` を gzip 配信）。line + リングバッファ形式。
- 追加ライブラリ方針：視認性/操作性向上に寄与する場合のみ採用。重量級 SPA FW は使わない。
- **認証**：ログイン(SHA-256+RNG, Cookie)。`/` と `/chart.min.js` と認証3種(`/api/auth,login,logout`)以外は
  401。ヘッダにログアウト、P3 にパスワード変更(SHA-256ソルト付き)。無効化は `WLB_AUTH_ENABLE 0`。
- **HTTPS(443) 併設**（自己署名 EC P-256）+ HTTP(80)。Web は `esp_http_server`(httpd) 実装。

## ページ仕様

### 1. ダッシュボード（現在値）
- 現在の **温度 / 湿度**（合成値：AHT20 優先、BMP280 は気圧）。
- **直近30分の雷頻度**（落雷/妨害波の件数と 1分あたりレート）と **危険度**。
- **危険度の定義**（CH32V003 実装時に確定＝ [LIGHTNING.md](LIGHTNING.md) 準拠）:
  距離ビン毎の減衰カウント `acc[]`（半減期5分）と重み `weight[]={20,16,10,6,4,2,1,1}`
  （近いほど大）の **積和** `danger=Σ acc[i]*weight[i]` を `WLB_LN_FULLSCALE(=100)` で
  正規化した 0..100%。レベル：`<5 安全 / <25 注意 / <50 警戒 / <75 危険 / else 厳重警戒`。
- 危険度はゲージ＋レベル色で表示。

### 2. チャート（Chart.js, ローカル同梱）
- 温度/湿度/気圧/危険度の時系列。**スコープ：ライブ / 分足 / 時間足 / 日 / 週 / 月**（年足は廃止＝データ量過大）。
- ライブ=5秒 RAM リング、分足=1分平均、時間足=1時間平均。**時足以降は FS 長期履歴**(絶対epoch, 再起動をまたぐ)
  から期間窓で取得し、未蓄積時は細かい足へフォールバックして必ず描画。真の日〜月足の長期蓄積は SQL で別途。

### 3. WiFi 接続設定
- **グループ1**：SSID 選択（`/api/wifi/scan`）＋パスワード入力、**mDNS 名**（既定 `WetherMemo`）。
  起動は **AP モード**、設定後 **STA へ切替**、**mDNS** で `http://<mdns>.local/` 到達。
- **グループ2（STA でアクセス時）**：DHCP 払い出し **IP** 表示、**STA⇄AP 切替**設定、
  本機 **SSID 名**表示（設定用 AP は **WPA2**=`WLB_AP_PASS`）。
- 設定は **LittleFS `/settings.ini`** 永続化（WiFiパスワードは **AES-256 で `passenc=` 暗号化**保存）。適用は再起動。
- 接続は同一SSIDメッシュの**最強BSSIDへロック**＋接続後の**再ローミング**（RSSI閾値割れで強APへ張替）。

### 4. 雷センサ キャリブレーション（AS3935）
akizuki **AE-AS3935** マニュアル準拠
(https://akizukidenshi.com/goodsaffix/AE-AS3935_20230208.pdf)。
- **キャリブレーション実行**ボタン（LCO 自動同調 → フラッシュ保存）＋**結果表示**
  （TUN_CAP / ok / count / target=3125）。
- **室内/室外**（AFE_GB: 室内0x12/室外0x0E）。
- **感度調整**（Watchdog Threshold WDTH 0..15）。
- **検出閾値**（Noise Floor NF_LEV 0..7 / 最小落雷数 MIN_NUM_LIGH 0,5,9,16）。
- **付近パルス除去**（Spike Rejection SREJ 0..15：大きいほど妨害波に強いが検出効率低下）。
- I2C 書込は ThunderSence ホストライブラリ経由（`recalibrate/setGain/setNoiseFloor/
  setWatchdog/setSpikeRejection/setMinLightnings/readCalib`）。Web→ioTask をコマンド
  キュー(`shared_state`)で受け渡し（I2C 単独所有を厳守）。

### 5. 温度・湿度 差分（オフセット）設定
- 温度/湿度に加算オフセット（°C / %RH、サーバ側クランプ ±20℃/±50%）。LittleFS 永続化、合成値に適用。
- 同ページ「グループ2」に各センサ実測値を表示（切り分け用）。
- （将来）SHT35/BME280 基準センサ併用時の自動オフセット学習と統合。

### 6. データ ダウンロード / 測定頻度
- **CSV ダウンロード**（`/api/csv`）。列 = **`time,temp,humi,thunder`**（ヘッダは **DL 時のみ**付与＝保存ファイルは
  ヘッダ無し）。time=**NTP日時**(JST)、temp=**AHT20のみ**(BMP280温度は冗長で不記録)、humi=AHT20湿度、
  thunder=危険度%。オフライン(NTP未同期)時は `B<起動秒>` で記録し、NTP確定時に絶対時刻へ一括置換。
  DL は 8KB 毎に `yield()`+WDT feed でセンサ/WiFi を止めない。
- **測定モデル**：**5秒ごとに1回測定** → ダッシュボード表示＋ライブ足(RAM, 30分)。その5秒値を **n個**
  ためて**最小最大を捨て平均** → **period×n(既定 5×12=60秒)ごとに1レコード(=分足)を FS へ保存**。
  これにより FS レコード数を抑え長期保存に最適化。測定周期(1..3600s)・n(1..20, 捨てる場合 n≥4)は LittleFS 永続化。
- UI: 周期入力欄に永続値を表示し、「平均サンプル数 n」の下に **FS保存間隔(=周期×n)** と **記録可能日数**
  （= ログ予算 / (86400/間隔 × 1行バイト)、上限日数でクランプ）を JS で算定表示（周期/n変更で即再計算）。
- **ダッシュボード表示は小数第2位に統一**（2桁目で四捨五入）：温度/湿度/気圧とも2桁。
- **FSデータログ = 日付名の日別ファイル再帰リング**（簡易NoSQL風, [datalog.cpp](../src/datalog.cpp)）：
  1日=1ファイル **`/g<YYYY-MM-DD>.csv`**(JST暦日, 人間可読)。**分足(period×n平均)ごとに当日ファイルへ**
  **時刻のみ**(`HH:MM:SS,temp,humi,thunder`)で追記→各行から日付を省き**約34%圧縮**(≈32B→21B)。
  DL時はファイル名の日付を各行へ付与し `YYYY-MM-DD HH:MM:SS,…` の完全日時に復元(DL形式は不変)。
  総容量(`WLB_LOG_BUDGET`=1.8MB) or 日数(`WLB_LOG_MAXDAYS`=90) 超過で **最古日ファイルを再帰削除**(単位=1日)。
  オフライン分は `/gpend.csv`(B<秒>)→NTP確定時に該当日ファイルへ振り分け。ログ消去は旧形式も一掃。
  **クリアは「データ ダウンロード → ログ消去」**(datalog 全日 + histfs を同時消去)。
- **時足の長期履歴** `/hist.bin`(15B/レコード, 絶対epoch, 最大約180日) がチャートの時足以降の источник。
- **パーティション**（[partitions.csv](../partitions.csv)）：4MB を ビルド実測(app≈1.28MB)から
  **factory(app)=0x160000(1.375MB) / spiffs(FS)=0x280000(2.5MB) / coredump=64KB**。単一app・OTAなしで
  app≈1.3MBのため factory はこれ以上縮小不可＝現状が実用上の最適(ログ予算1.8MB+histfsはFS 2.5MBに収まる)。
- 長期の日〜月足の本格蓄積は外部 SQL 送信（別途）。

## REST API（Web ⇄ ESP。HTTP:80 / HTTPS:443 で同一。`/`・`/chart.min.js`・auth3種以外は要ログイン=401）
| メソッド | パス | 用途 |
|---|---|---|
| GET | `/` | gzip SPA |
| GET | `/chart.min.js` | ローカル同梱 Chart.js (gzip) |
| GET | `/api/auth` | 認証状態 |
| POST | `/api/login` / `/api/logout` | ログイン(Cookie発行) / ログアウト |
| POST | `/api/passwd` | パスワード変更 |
| GET | `/api/now` | 現在値(温湿圧/危険度/雷カウンタ/30分頻度/bridge/scan/NTP) JSON |
| GET | `/api/history?scope=live\|min\|hour\|day\|week\|month` | 時系列 JSON |
| GET | `/api/settings` | 全設定 JSON |
| POST | `/api/wifi` / GET `/api/wifi/scan` | WiFi保存(再起動) / 近傍AP一覧 |
| POST | `/api/calib` | run/indoor/nf/wdth/srej/minnum/mask/clear (サーバ側クランプ) |
| POST | `/api/offset` | 温度/湿度オフセット |
| POST | `/api/logcfg` | 測定周期 / dropMinMax / n |
| GET | `/api/csv` | CSV ダウンロード(ヘッダはDL時のみ) |
| POST | `/api/logclear` | ログ消去 |

## 実装状況
- **完了**：6ページ SPA、現在値/雷危険度、ライブ/分足/時間足チャート + **FS長期履歴(日/週/月)**、
  WiFi(AP/STA/mDNS)+BSSIDロック+再ローミング、校正/感度(サーバ側クランプ)、オフセット、測定頻度、
  **LittleFS 永続化(設定=AES暗号)**、CSVログ(NTP時刻/オフライン救済/DL時yield)、**int16圧縮**、
  **WDT**、**RTC時刻保持**、**ログイン認証(SHA-256/RNG)**、**HTTPS(443)併設+SoftAP WPA2**、
  **Chart.jsローカル同梱**、**雷poll(389B)修正**、WiFiモデムスリープ+CPU80MHz。
- **未（別途）**：MySQL/SQL 送信、Secure Boot/Flash Encryption(eFuse=物理)、真の自動ライトスリープ、
  CH32V003 24MHz化(WCH-LinkE書込問題で保留)。
