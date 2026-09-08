# WetherLoggerBox 実機テストログ — 2026-09-08

対象: ESP32-C3 ホスト「WetherLoggerBox」(`host_esp32c3/`) + CH32V003 雷ブリッジ(`firmware/`)。
実機: STA `192.168.1.35`(DHCP) / mDNS 既定 `WetherMemo` / SoftAP `WetherLogger`(WPA2)。
検証方法: PowerShell `Invoke-WebRequest`(認証は `-SessionVariable` で Cookie 保持、HTTPS は自己署名を
バイパス) と in-app ブラウザ。書込は COM13(ESP32) / WCH-LinkE(CH32)。

> ⚠ 教訓: **COM13 の SerialPort を開くと ESP32-C3 がリセットされる**(DTR/RTS)。稼働中の状態確認は
> HTTP/ping のみで行う(serial を開くと再起動し「WiFiチャーン」に誤認する)。起動ログ取得時のみ serial。

---

## 1. チャート 分足/時間足 追加
- ボタン: `ライブ/分足/時間足/日/週/月`(年足は後に廃止)。
- `/api/history?scope=min` 起動70秒後: `{"age":[0],"temp":[28.62],"hum":[65.94],"pres":[1000],"danger":[0.00]}` → **分足に実データ蓄積を確認**。

## 2. 1レコード圧縮 (int16 固定小数点, AquaController histdb 準拠)
- `HistSample` を packed 15B 化(温湿×100 / 気圧(hPa-1000)×100 / NaN=INT16_MIN)。`static_assert(sizeof==15)` 通過。
- **RAM 22.5% → 18.9%**(約12KB削減)。
- 往復検証: `/api/now` 実測 `28.54℃/66.3%/999.5hPa` に対し圧縮リング経由 `/api/history` が `temp:28.54, hum:66.29` と
  **小数第2位まで一致** → エンコード/デコード正常。

## 3. 既存バグ修正 (圧縮作業中に発見)
- **histCopy uint8_t オーバーフロー**: `WLB_HIST_CAP(360)` を uint8_t 引数に渡し 104 に化ける警告 → maxN/戻り値/
  ループを uint16_t に統一。ビルド警告消滅。
- **d30/l30 アンダーフロー**: 雷ブリッジ再起動でカウンタが0に戻ると `h[新]-h[古]` が負→uint16化け(65535)。巻き戻り
  クランプを追加。実機 `d30=65530 → d30=1`(D=1と整合) に是正、P1「妨害波N件」正常表示。

## 4. CSV 仕様変更 (NTP時刻 / AHT20のみ / ヘッダDL時のみ)
- 列 `time,temp,humi,thunder`。ヘッダは DL 時のみ付与(保存ファイルはヘッダ無し)。
- 実機 DL 先頭:
  ```
  time,temp,humi,thunder
  2026-09-08 06:01:35,26.78,77.4,0
  2026-09-08 06:01:40,26.76,77.4,0
  ```
  → NTP日時 / temp=AHT20のみ / 気圧(BMP280)非記録 / 5秒周期、を確認。

## 5. FS 長期履歴 / パーティション再設計 / WDT / 再ローミング / オフラインCSV / RTC
- パーティション: factory(app) 1.375MB / spiffs(FS) 2.5MB / coredump 64KB。ビルド RAM18.9%/Flash28% → 収まり確認。
- `/api/history?scope=hour/day/week/month` は FS `hist.bin`(時足) → 未蓄積時は分足/ライブへフォールバックし常に描画。
- WDT(30s): 7分以上連続稼働・再起動ループ無しを serial で確認(`TWDT already initialized`→reconfigure)。
- 起動ログ: `[io] sensors=2 bridge=ok` / `[net] lock BSSID … rssi=-62` / `[web] up`。
- **「WiFiチャーン」は計測アーティファクト**と判明: serial を開く度に C3 がリセットされ「lock BSSID 連発/HTTP NG」に
  見えていた。ネットワークのみ(serial非接触)では **HTTP 10/10 OK, RSSI-61, NTP同期, 温度26.94→27.00 連続** と完全安定。
  → 物理電源再投入で不調状態クリア。切り分けで WDT/再ローム/雷poll/softAP を各OFF にしてもチャーンは出ず(=いずれも無罪)。

## 6. 認証 (SHA-256 + RNG, Cookie) / AES設定暗号 / SoftAP WPA2
- 未認証 `/api/now` → **401**。`/api/auth` → `{"enabled":1,"authed":0}`。
- ログイン `wether/wether` → `{ok:1}` → 認証後 `/api/now` OK(t=27.12, rssi-60)。ヘッダにログアウト、P3にパスワード変更。
- ブラウザ: 未認証はログインオーバーレイ(データ`--`)→ログイン成功でダッシュボード実データ表示・ログアウトボタン出現。
- 設定 `/settings.ini` の WiFi パスは AES-256 で `passenc=` 保存(平文非保持)。

## 7. HTTPS(443) 併設 (自己署名 EC P-256) + HTTP(80)
- Web を `esp_http_server`(httpd) へ移植、`web_httpd.h`(HttpCtx) で既存ハンドラ流用、両サーバに同一ハンドラ登録、
  `g_webMtx` で直列化。証明書 EC P-256(ECDSA, openssl生成)。
- 実機:
  - HTTP(80): 未認証401 → login → `/api/now` OK。
  - **HTTPS(443)**: login → `/api/now` OK(t=27.32, bridge=1)、`/` gzip SPA 200/23111B。
- ブラウザ: ログイン→チャート描画(ローカル Chart.js)まで確認。

## 8. Chart.js ローカル同梱 (CDN非依存)
- `/chart.min.js` = 200 / **200807B** / `Content-Encoding: gzip`(埋込 69KB)。ブラウザでチャート描画(CDNブロック環境でも可)。

## 9. 省電力 (ESP モデムスリープ + CPU80MHz / CH32 24MHz)
- ESP: `WIFI_PS_MIN_MODEM` + `setCpuFrequencyMhz(80)`。実機 HTTP 応答 **平均100ms/最大119ms**、RSSI-64〜-68、bridge=1、NTP有効。
- CH32: PLL停止 HSI直結 24MHz へ変更しビルド成功(RAM66%/Flash55%)。**ただし書込未反映**(§11)。→ 保留オプション化、既定48MHzに整合。

## 10. 雷 poll(389B) 修正 = 雷イベント経路復活
- 症状: `_ts.poll()` の 389B 束読み(`CMD_READ_BUNDLE`)が Error263(timeout) で失敗、12B の `readStatus` は成功 → danger 常時0。
- 原因: arduino-esp32 Wire 既定 I2C タイムアウト(~50ms) < 100kHz×389B+クロックストレッチ。
- 修正: (1)`Wire.setTimeOut(400)`、(2)`readStatus` で `pending>0` の時だけ束読み(待機中は大容量読みゼロ)。
- 実機:
  - 待機中 Error263: **84回/15s → 0回/18s**(根絶)。
  - **スパーク試験**: スパーク中も **Error263=0**、妨害波 **D=13** 増加(jam無し, bridge=1) → **389B poll 成功・イベント流通OK**。
  - ※スパークは AS3935 が「妨害波(disturber)」に分類 → D増加/L・danger=0 が正解(danger は落雷分類のみ上昇)。

## 11. CH32V003 書込み問題 (WCH-LinkE v2.17)
- `minichlink -l/-a`(読取/halt) は成功: `LinkE version 2.17` / `Part UUID 90-db-ab-cd-fa-21-bd-79` / 16KB → SWIO配線・通信は正常。
- `-w`(書込) は `Interface Setup` 後 `Error sending WCH command (on recv): 81 08 06 16 00 00 00 00 01` で停止(ハング)。
  USB抜き差し・halt先行・明示アドレス0x08000000・110s待機でも同一。**24MHz/48MHz 両ビルドで同一** → バイナリ無関係。
- `pio run -t upload`(openocd wch-link) は `WLink Open Error`(開けず)。
- バイナリ健全性: 先頭 `6f 10 80 75` = RISC-V JAL 命令(CH32V003の正しいリセット形式)、8964B → **ビルド異常なし**。
- 結論: **WCH-LinkE v2.17 × 同梱 minichlink の書込プロトコル非互換**。回避 = WCH-LinkUtility(公式GUI)で
  `firmware.bin`→`0x08000000`、または minichlink 最新版。→ **24MHz化は保留**。

## 12. ビルド確認 (最終)
- ESP32-C3: `pio run` SUCCESS — RAM 19.0% / Flash 31.9%(1.275MB / 枠1.375MB)。
- CH32V003: `pio run` SUCCESS — RAM 66.2% / Flash 55.0%(9008B, 48MHz既定)。

## 既知の未解決 / 保留
- CH32V003 24MHz 化: 書込ツール問題で保留(§11)。
- 到達性: 稼働中は安定するが、**再起動後にメッシュ遠APを掴む**ことがある(物理電源再投入で復帰)。恒久策の再ローミングは実装済
  だが RSSI 閾値(-75)割れ時のみ発火のため、強信号での AP 側 deauth には効かない。
- 長期(日〜月)の本格蓄積・MySQL/SQL 送信は別途。
