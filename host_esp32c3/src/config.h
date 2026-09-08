// ============================================================================
//  config.h  -  WetherLoggerBox 全体設定 (ピン / 雷危険度パラメータ)
// ============================================================================
#ifndef WLB_CONFIG_H
#define WLB_CONFIG_H

#include <Arduino.h>

// ---- 個人設定オーバーライド (gitignore) --------------------------------------
//  実運用の WiFi 資格情報などは公開リポジトリに含めない。config.local.h を
//  （config.local.h.example をコピーして）用意すると、以下の既定より優先される。
//  存在しなければプレースホルダの既定値が使われる（clone直後もビルド可）。
#if __has_include("config.local.h")
  #include "config.local.h"
#endif

// ---- I2C バス (ThunderSense ブリッジ 0x28 と 環境センサが同居) --------------
//  ※ ESP32-C3 DevKitM-1 の既定 I2C。基板に合わせて変更可。
//    ThunderSense 側: ESP32 SDA -> bridge PC1 / ESP32 SCL -> bridge PC2 / GND共通
#ifndef WLB_I2C_SDA
  #define WLB_I2C_SDA   8
#endif
#ifndef WLB_I2C_SCL
  #define WLB_I2C_SCL   9
#endif
#define WLB_I2C_HZ      100000UL   // bring-up は 100kHz で堅牢に (安定後 400k に戻す)
// I2C トランザクション・タイムアウト [ms]。既定(~50ms)では雷ブリッジの 389B 束読み
// (poll/CMD_READ_BUNDLE) が 100kHz+クロックストレッチで超過し Error263 で失敗する。
// 389B≒35ms+ストレッチ を吸収できるよう延長 → 雷イベント経路(danger)を復活させる。
#define WLB_I2C_TIMEOUT_MS  400

// ---- サンプリング周期 -------------------------------------------------------
//  仕様: センサアクセスは 1 秒おき / ダッシュボード(Chart.js)は 10 秒おき。
//  雷はイベント駆動なので毎ループ drain し、危険度は 1 秒毎に減衰更新する。
#define WLB_POLL_MS         1000    // 雷ブリッジ ポーリング & 危険度更新周期
#define WLB_DASHBOARD_MS   10000    // ダッシュボード/ログ 集約周期

// ============================================================================
//  雷 危険度 (積和 / weighted sum-of-products)
// ----------------------------------------------------------------------------
//  考え方:
//   - 近い落雷 = 高リスク、遠い落雷 = 低リスク。
//   - 「統計を取る」ため、距離ビン毎の(減衰つき)発生カウントベクトル acc[] と
//     重みベクトル weight[] の 内積(=積和)  danger = Σ acc[i]*weight[i]  を危険度とする。
//   - acc[] は 1 秒毎に半減期に従って減衰 → 常に「現在の」危険度を反映する統計量。
// ============================================================================

// 距離ビン数 (AS3935 の距離推定 1..40km + 圏外 を粗くまとめる)
#define WLB_LN_BINS   8

// acc[] の半減期 [秒]。大きいほど過去の雷を長く引きずる(=なだらか)。
#define WLB_LN_HALFLIFE_S   300.0f   // 5分

// 危険度% のフルスケール。danger(積和) がこの値で 100% に張り付く。
//  例: 真上(overhead, weight=20)の落雷 5 発相当が 100%。
#define WLB_LN_FULLSCALE    100.0f

// 圏外(0x3F)判定に使う AS3935 の距離コード
#define WLB_LN_OUT_OF_RANGE 0x3F

// ============================================================================
//  WiFi / NTP
// ============================================================================
//  ※ 実運用の SSID/PASS は config.local.h(gitignore) で上書きするか、起動後に
//    設定UI(WiFiページ)から NVS/LittleFS へ保存する。以下は公開用プレースホルダ。
#ifndef WLB_WIFI_SSID
  #define WLB_WIFI_SSID  "YOUR_WIFI_SSID"   // 空なら WiFi 無効 (単体テスト)
#endif
#ifndef WLB_WIFI_PASS
  #define WLB_WIFI_PASS  "YOUR_WIFI_PASS"
#endif
#define WLB_NTP_SERVER   "ntp.nict.jp"   // AquaController 準拠 (第2/第3は wifi_session 内)
#define WLB_TZ_OFFSET_S  (9 * 3600)    // JST (+9h)

// 設定用 SoftAP。WPA2(8文字以上)。近接者による無認証再設定を防ぐ。運用前に変更推奨。
// 実運用パスは config.local.h(gitignore) で上書き可。以下は公開用の既定サンプル。
#define WLB_AP_SSID      "WetherLogger"
#ifndef WLB_AP_PASS
  #define WLB_AP_PASS    "wetherbox"      // 8文字以上必須 (空にすると open AP)。変更推奨。
#endif

// ---- ログイン認証 (SHA-256 + セッションCookie。HW SHA/RNG を使用) ----
#define WLB_AUTH_ENABLE       1           // 0=無効(全公開) / 1=有効
#define WLB_AUTH_DEFAULT_USER "wether"
#define WLB_AUTH_DEFAULT_PASS "wether"    // 初回シード。ログイン後に変更推奨
#define WLB_AUTH_SESSION_TTL_MS  86400000UL  // 24h スライディング
#define WLB_AUTH_MIN_PASS_LEN 4

// ---- 機能トグル (通常は全て1。切り分け時のみ0にして原因を特定する) ----
//  検証結果(2026-09-08): WiFiチャーン(数秒毎の再接続)は WDT/再ローム/雷poll の
//  いずれをOFFにしても継続 → これらは原因でなく、AP側/環境要因と判明。全て1へ復帰。
#define WLB_ENABLE_WDT    1        // タスクウォッチドッグ
#define WLB_ENABLE_ROAM   1        // メッシュ再ローミング
#define WLB_ENABLE_LN     1        // 雷poll(ln.tick)
#define WLB_ENABLE_SOFTAP 1        // 常設softAP(設定用)

// ---- メッシュ再ローミング (接続後もRSSIを監視し強いAPへ張り替え) ----
//  起動時スキャンが遠APを掴んだ場合の自己復帰。閾値割れ時のみスキャンし負荷を抑える。
#define WLB_ROAM_CHECK_MS   30000      // RSSI 監視間隔 [ms]
#define WLB_ROAM_RSSI_TH    (-75)      // これより弱い時だけローミング探索を行う [dBm]
#define WLB_ROAM_MARGIN     8          // 現在より +この値[dB] 以上強いBSSIDがあれば張り替え

// ---- タスクウォッチドッグ (ハング検出→パニック→自動再起動) ----
#define WLB_WDT_TIMEOUT_MS  30000      // io/loop 両タスクの feed 猶予。長いI2C/スキャンを許容

// ---- 省電力 (常時ダッシュボードと両立する範囲) ----
//  ・WiFi モデムスリープ: DTIM ビーコン間 RF を休ませる。関連付けは維持し応答も可(遅延+~DTIM)。
//    真の自動ライトスリープ(CPU停止)は ioTask の 20ms ポーリングで idle にならず本設計では効かない。
//  ・CPU 周波数: 160→80MHz で動的消費を約半減。WiFi は 80MHz 以上が必須なので 80 が下限。
// ★既定 0=モデムスリープ無効(到達性優先。AP_STA+低電圧環境での関連付け不安定/再起動後の
//   未到達を回避)。省電力を優先したい場合のみ 1(=WIFI_PS_MIN_MODEM)にする[任意オプション]。
#define WLB_WIFI_MODEM_SLEEP 0         // 0=常時ON(到達性優先, 既定) / 1=モデムスリープ(省電力)
#define WLB_CPU_MHZ          80        // 80 or 160 (WiFi使用時は80が最小)。省電力は 80 のまま維持

// ---- I2C バス自動リカバリ (稼働中のバスwedge=Error263連続 からの自己回復) ----
//  ライブスキャン(2秒毎)で 1 台も応答しない状態が連続したら、SCLを9クロック手動トグルで
//  スタックしたスレーブを解放し Wire を再初期化する。wedge中は I2C ハンマリングを停止。
#define WLB_I2C_RECOVER_SCANS  2       // wedge 連続検出回数(×2秒) でリカバリ発動
#define WLB_I2C_RECOVER_MAX    5       // リカバリ連続失敗の上限(超で断念→ブリッジ不在扱い)

// ============================================================================
//  FS 長期履歴 (時足を LittleFS に永続化 = 再起動をまたいで日/週/月足に対応)
//  15B/レコード(圧縮HistSample)。1時間1点 × CAP。時刻は絶対epoch(NTP)でスタンプ。
// ============================================================================
#define WLB_HISTFS_PATH    "/hist.bin"
#define WLB_HISTFS_OLD     "/hist.old.bin"
#define WLB_HISTFS_CAP     2160        // 90日分(24×90)。超過で1世代退避 → 最大約180日

// ---- FS データログ(CSV): 日付名の日別ファイルの再帰リング ----
//  ★ファイル名=日付 `/g<YYYY-MM-DD>.csv`(JST暦日)。行内は時刻のみ `HH:MM:SS,temp,humi,thunder`
//    → 各行から日付(11文字)を省き圧縮。DL時はファイル名の日付を付与して完全な日時へ復元。
//  オフライン分は /gpend.csv (`B<起動秒>,...`)。総容量/日数の上限超過で「最古日ファイル」を
//  再帰削除(単位=1日)。クリアは Web「データ ダウンロード → ログ消去」(histfs も同時)。
#define WLB_LOG_DAYPREFIX  "/g"        // 日別ログの接頭辞 ("/g<YYYY-MM-DD>.csv")
#define WLB_LOG_PENDING    "/gpend.csv"// NTP未同期時の一時ログ (B<起動秒>)
#define WLB_LOG_MAXDAYS    366         // 保持最大日数(≒1年)の安全上限。実質は下の容量予算が律速
#define WLB_LOG_BUDGET     1800000UL   // ログ総容量上限[byte] (FS 2.5MB のうちログ用)。実容量の律速
#define WLB_LOG_ROWBYTES   21          // 圧縮後CSV 1行の平均バイト(時刻のみ; 記録可能日数の算定用)

// ============================================================================
//  環境センサ選択
// ----------------------------------------------------------------------------
//  採用構成: AHT25 (温湿度) + BMP280 (気圧)。
//  他センサは対応済み。使うものだけ #define を有効化 (無効なものは
//  ラッパ.cpp が空になり、そのライブラリもビルド対象外になる)。
// ============================================================================
// #define WLB_SENSOR_AHT25         // AHT25 温湿度 (AHTx0共通ドライバ)
#define WLB_SENSOR_AHT20            // AHT20 温湿度 (BMP280+AHT20 モジュール)  [採用/実機]
#define WLB_SENSOR_BMP280           // BMP280 気圧+温度                       [採用/実機]
// #define WLB_SENSOR_SHT35         // SHT35 温湿度 (温度補正用リファレンス)
// #define WLB_SENSOR_DHT11         // DHT11 温湿度 (1-wire)
// #define WLB_SENSOR_DS18B20       // DS18B20 温度 (OneWire)
// #define WLB_SENSOR_S5851A        // S-5851A 温度 (I2C, LM75互換)
// #define WLB_SENSOR_BME680        // BME680 温湿度気圧+ガス
// #define WLB_SENSOR_BME280        // BME280 温湿度気圧 (気圧補正用リファレンス)

// ---- I2C アドレス (基板に合わせて変更可) -----------------------------------
#define WLB_ADDR_AHT       0x38     // AHT20/25 固定
#define WLB_ADDR_BMP280    0x76     // BMP280/BME280: SDO=GND→0x76 / VDD→0x77
#define WLB_ADDR_BME280    0x76
#define WLB_ADDR_BME680    0x76     // BME680: 0x76 / 0x77
#define WLB_ADDR_SHT35     0x44     // SHT3x: ADDR=GND→0x44 / VDD→0x45
#define WLB_ADDR_S5851A    0x48     // S-5851A: ADR で 0x48..0x4F

// ---- 非I2C センサのピン ----------------------------------------------------
#define WLB_PIN_DHT11      3        // DHT11 データ線
#define WLB_PIN_DS18B20    4        // DS18B20 1-Wire データ線 (4.7k プルアップ)

// ============================================================================
//  OLED (オプション表示)
// ----------------------------------------------------------------------------
//  I2C 接続 (センサと同じバス, 0x3C)。不変条件により描画は ioTask が行う
//  (I2C 単独所有)。SSD1306(0.96") 既定 / SH1106(1.3") は WLB_OLED_SH1106 を定義。
//  ラベルは ASCII (GFX 標準フォントに日本語なし)。
// ============================================================================
// #define WLB_OLED               // 有効化 (未定義ならビルドから除外)
// #define WLB_OLED_SH1106        // 定義で SH1106、未定義で SSD1306
#define WLB_OLED_ADDR   0x3C       // 0x3C / 0x3D
#define WLB_OLED_W      128
#define WLB_OLED_H      64
#define WLB_OLED_MS     1000       // 描画周期 [ms]

#endif // WLB_CONFIG_H
