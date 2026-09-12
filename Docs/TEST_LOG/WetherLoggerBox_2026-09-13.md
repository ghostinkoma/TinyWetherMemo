# WetherLoggerBox 実機テストログ — 2026-09-13

CH32V003 24/48MHz 書込・クロックA/B・雷センサ スパーク試験・**WiFi断でも雷ログが残る**ことの実証。
関連: [2026-09-08 ログ](WetherLoggerBox_2026-09-08.md) / [SPEC](../../host_esp32c3/Docs/SPEC.md) / [LIGHTNING](../../host_esp32c3/Docs/LIGHTNING.md)。

---

## 1. テスト条件

### 供試体 (DUT)
| 要素 | 内容 |
|---|---|
| 雷ブリッジ | **CH32V003 (ThunderSence)** / 書込器 = WCH-LinkE v2.17 **COM9** / minichlink |
| ホスト | **ESP32-C3 (新モジュール)** / **COM17** / MAC `7c:4f:ad:4b:eb:50` / STA IP **192.168.1.38** |
| 旧ホスト | ESP32-C3 MAC `e0:72:a1:22:eb:78` = **LDO破損・異常電流により引退**(下記) |
| センサ | AS3935 (akizuki AE-AS3935) — I2Cスキャンで 0x28(ブリッジ)/0x38(AHT20)/0x77(BMP280) 検出 |
| 電源 | USB (旧機はLDO破損で不安定、新機は正常) |

### ファーム / クロック
- CH32V003: **48MHz(HSI×PLL2)既定** ⇔ **24MHz(PLL停止=HSI直結)** を funconfig.h(`FUNCONF_USE_PLL 0`+`FUNCONF_SYSTEM_CORE_CLOCK 24000000`) と config.h(`SYS_CLK_HZ`) の対で切替。ビルド: 48MHz=Flash 55.0%(9008B) / 24MHz=Flash 54.7%(8964B), RAM 66%。
- ESP32-C3: host_esp32c3 ホストアプリ (pioarduino / arduino-esp32 3.x)。書込 Hash verified。

### LCO校正 (アンテナ同調)
- `valid=1, TUN_CAP=6, count=3126 / target=3125`(24MHz時)＝ほぼ完璧に500kHz同調。TUN_CAPはoption-byte保存→起動時ロード。

### 雷センサ感度スイープ (AS3935, `/api/calib` で live 変更)
| # | AFE_GB | NF_LEV | WDTH | SREJ | 備考 |
|---|---|---|---|---|---|
| 基準 | 室内0x12 | 2 | 2 | 2 | データシート既定+室内 |
| — | 室内 | 4 / 5 | 2 | 2 | ノイズ床↑ |
| — | 室外0x0E | 5 / 7(max) | 2 | 2 | 低利得+ノイズ床↑ |
| — | 室外 | 2 | 2 | 2 | 低利得+高感度 |
| — | 室内 | 0(最低=最高感度) | 1 | 2 | 遮蔽源向け最大感度 |
| 最終 | 室内0x12 | 2 | 2 | 2 | 48MHz運用時の設定 |

### 妨害波源
- **内燃構造式(遮蔽型)電子ライター**。スパークがバーナー室内で発生し外部EMI漏れが少ない。距離 5cm〜1m を可変。

---

## 2. 試験ログ / 観測

### 2.1 書込み (真因: 旧ESP32 LDO破損)
- 以前ハングしていた WCH-LinkE v2.17 × minichlink が、今回 `Detected CH32V003 → Image written → 読み戻し完全一致` で**初成功**。
- **旧ESP32モジュールのLDO破損による異常電流**が、同一電源系のWCH書込みを妨げ(`Error sending WCH command`)、旧ESP32自身もブラウンアウトでブートループ(起動バナー連発)していたと判明。**モジュール交換で解決**。「v2.17非互換」説は誤りと確定。

### 2.2 クロック 24 vs 48MHz
- **24MHz実機動作OK**: bridge=1 安定(5/5 poll)、I2Cスキャン全検出＝HW-I2Cスレーブ通信正常。
- **消費電流(データシート CH32V003DS0, Run/Flash, 3.3V, 全周辺ON)**: 48MHz=**7.0mA** / 24MHz=**5.2mA** = 差 **+1.8mA のみ**(ESP32のWiFi 数十〜250mAに対し誤差)。
- → **48MHz固定を採用**(検証済み・安定、消費差は無視可)。

### 2.3 スパーク試験 (24MHz→48MHz A/B)
- 全感度(NF_LEV 0〜7 / 室内・室外 / WDTH 1・2)・両クロックで、スパークは **N(ノイズ, INT_NH)** として計数され、**D(妨害波)=0・L(落雷)=0 のまま**。
- N の代表推移(48MHz, 室内/NF2): `1 → 6 → 25 → 57 → 79 → (WiFi断) → 100 → 121`(単調増加=毎スパーク計数)。
- **結論**: クロックは分類に無関係(24=48で同一挙動)。内燃遮蔽ライターのスパークは AS3935 には「妨害波」ではなく「ノイズ」として映る(強い広帯域インパルスで、disturber=500kHz近傍の人工連続源パターンに一致しない)。**機器不具合ではなく源のRF特性**。真のdisturber確認は調光器/ブラシモーター/SMPS/蛍光灯など典型源が要。
- 連続バーストはノイズ床を張り続け INT_NH に固定されるため、構造上さらにdisturberへ進めない。

---

## 3. ★重要実証: WiFi断でも雷ログは失われない

試験中 **06:02:43 にホストがネットワーク応答不能(到達不可)** となり、**06:04:12 に自己復帰**した。この間、

- **N カウンタは 79 → 100 へ増加し続けた**＝WiFi/HTTP が落ちている間も、**AS3935→CH32→(local HW-I2C)→ESP32 の捕捉・計数は継続**。
- 復帰後、蓄積カウントは**欠落なく**反映された。

### なぜ残るか (設計上の裏付け)
- **CH32V003ブリッジが一次バッファ**: イベントをトリプルバッファ+エネルギー上位32保持+**単調カウンタ**で保持し、ホストが読むまで**非破壊**で取り置く(CRC16付き389B束、検証成功時のみCLEAR)。WiFiもESP32再起動も関与しない。
- **ESP32は local I2C で回収し LittleFS(histfs/datalog)へ永続化**。この経路は**WiFiに非依存**(NTP時刻だけは、未同期時 `B<起動秒>` で記録→NTP確定時に絶対時刻へ一括変換)。
- したがって **WiFi断は「遠隔可視化の一時停止」に留まり、雷イベントの捕捉・計数・FS記録は途切れない**。データ完全性がフィールドで実証された。

> 注: 途中の応答不能は、至近スパークの強EMI or 給電揺れによる一時的WiFiドロップと推定。CH32計数継続=capture経路は影響を受けていない。

---

## 4. 最終状態 / 宿題
- **CH32V003 = 48MHz** 書込済み(読み戻し一致)。ESP32-C3 = ホストアプリ稼働(192.168.1.38, HTTP/HTTPS 200, 認証有効)。
- AS3935感度 = 室内0x12/NF2/WDTH2/SREJ2(テスト最終値)。**屋外百葉箱運用なら AFE=室外(0x0E)推奨**。
- **軽微な宿題**: 48MHzでの **LCO再校正 count が 0/3125**(ok=0)。ただし option-byte の TUN_CAP=6 はロード・適用済み(valid=1)＝アンテナ同調済みで実害なし。count更新ルーチン(as3935.c/capture.c のLCO計数ゲート)は要点検。

## 5. 再現手順 (要点)
1. CH32書込: `pio run`(firmware/) → `minichlink -w .pio/build/genericCH32V003J4M6/firmware.bin flash -b` → `-r` で読み戻し比較。24MHz化は funconfig.h 2行+config.h `SYS_CLK_HZ` を対で 24000000 に。
2. ESP32書込: `pio run -t upload --upload-port COMxx`(host_esp32c3/)。**書込前に `esptool read_mac` で MAC を確認**(AquaController機を絶対に上書きしない)。
3. 感度変更: ログイン後 `POST /api/calib op=indoor|nf|wdth|srej val=..`。現在値は `/api/settings`、雷カウンタは `/api/now`(D=妨害波/N=ノイズ/L=落雷/danger%)。
4. 機能試験: 電子ライターを近接スパーク→ダッシュボードのカウンタ増加で全経路OK(近接スパークはN分類が正常)。
