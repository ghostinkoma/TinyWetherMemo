// ============================================================================
//  WetherLoggerBox  -  main.cpp   (RTOS + Web ダッシュボード)
//
//  loopTask: WiFi(AP+STA) + Web(6ページSPA)。g_state を読み書きするだけで I2C不可触。
//  ioTask  : I2C 単独所有 (雷+センサ+校正+履歴)。詳細は Docs/ARCHITECTURE.md / SPEC.md
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include "esp_task_wdt.h"
#include "config.h"
#include "settings.h"
#include "wifi_session.h"
#include "shared_state.h"
#include "io_task.h"
#include "web_ui.h"
#include "datalog.h"
#include "histfs.h"
#include "auth.h"
#include "server_sync.h"

// ★loopTask のスタックを拡大。サーバ連携の TLS(HTTPS)クライアント(mbedTLS)を
//   loopTask で実行するため、既定8KBでは handshake 中にスタックオーバーフロー→
//   クラッシュ/再起動ループになる(オンデバイスHTTPSサーバを10KBにしているのと同理由)。
SET_LOOP_TASK_STACK_SIZE(24 * 1024);

WifiSession  net;
WlbSettings  g_settings;
static uint32_t lastTimePush = 0;
static bool     g_bootPatched = false;   // NTP初確定時に起動相対ログを絶対時刻へ置換(1回)
static uint16_t g_lastLogSeq = 0;        // FSへ永続化した分足シーケンス
static bool     g_logSeqInit = false;

// ---- WiFi 動作モード (AP と STA を排他運用: AquaController 準拠) ----
bool            g_apMode = false;        // 現在 AP 単独運用か (web_ui が参照)
static bool     g_wantSta = false;       // 起動時に STA を選択したか
static bool     g_everConnected = false; // STA が一度でも接続できたか
static uint32_t g_wifiBootMs = 0;        // STA 起動時刻 (フォールバック判定用)

// AP 単独運用へ (STA は上げない)。WiFi を一旦 OFF にしてから AP を確実に起動する。
static void startApMode() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF); delay(100);
  WiFi.mode(WIFI_AP);  delay(100);
  WiFi.setSleep(false);
  bool ok = (strlen(WLB_AP_PASS) >= 8) ? WiFi.softAP(WLB_AP_SSID, WLB_AP_PASS)   // WPA2
                                       : WiFi.softAP(WLB_AP_SSID);                // open
  g_apMode = true;
  Serial.printf("[net] AP-only '%s' (WPA2=%d) ok=%d http://%s/\n",
                WLB_AP_SSID, strlen(WLB_AP_PASS) >= 8, ok, WiFi.softAPIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(WLB_CPU_MHZ);   // 省電力: 160→80MHz (WiFi は80MHz以上必須)
#if WLB_ENABLE_WDT
  // ★WDT を最優先で初期化 (ioTask が自身を add する前に)。ハング→パニック→自動再起動。
  esp_task_wdt_config_t twdt = { .timeout_ms = WLB_WDT_TIMEOUT_MS, .idle_core_mask = (1u<<0), .trigger_panic = true };
  if (esp_task_wdt_init(&twdt) == ESP_ERR_INVALID_STATE) esp_task_wdt_reconfigure(&twdt);
  esp_task_wdt_add(NULL);   // loopTask を監視
#endif
  delay(300);
  Serial.println("\n=== WetherLoggerBox ===");

  settings_load(g_settings);
  g_state.begin();       // 共有状態 mutex
  datalog_begin();       // LittleFS (実データのトレース記録)
  wauth::begin();        // ログイン認証 (SHA-256/RNG; /auth.ini)。LittleFS mount 後に
  histfs_seed();         // FS長期履歴(時足)をRAM時足リングへ復元 → 再起動後も日/週/月足が即描画
  iotask_start();        // IOタスク (雷+センサ, I2C所有)

  // WiFi: AP と STA を同時に上げない (AquaController 準拠 = 上位LANの露出を避ける)。
  //   staMode==STA かつ SSID 有り → STA 単独。それ以外 (AP 指定/SSID 空) → AP 単独。
  //   STA が WLB_STA_FALLBACK_MS 内に一度も繋がらなければ AP へフォールバック(到達性確保)。
  net.onTime([](uint32_t epoch){ g_state.requestTimeSync(epoch); });
  net.setNtp(WLB_NTP_SERVER, WLB_TZ_OFFSET_S);

  g_wantSta = (g_settings.staMode == 1) && (g_settings.ssid[0] != '\0');
  if (g_wantSta) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(WLB_WIFI_MODEM_SLEEP ? true : false);   // true=WIFI_PS_MIN_MODEM(省電力)/false=常時ON
    net.begin(g_settings.ssid, g_settings.pass);           // STA 単独 (softAP は上げない)
    g_wifiBootMs = millis();
    Serial.printf("[net] STA-only -> '%s' (AP は起動しない)\n", g_settings.ssid);
  } else {
    startApMode();                                          // AP 単独 (STA は起動しない)
  }

  srv::begin(&g_settings);   // SQLサーバ連携 (スタンドアロン時は通信しない)
  webui_begin(g_settings);   // Web(:80) + mDNS(settings.mdns)
}

void loop() {
  uint32_t now = millis();
#if WLB_ENABLE_WDT
  esp_task_wdt_reset();          // WDT feed (loopTask)
#endif
  if (!g_apMode) net.loop(now);      // AP 単独運用中は STA セッションを回さない (排他)
  webui_loop();

  if (net.isConnected()) g_everConnected = true;

  // STA が一度も繋がらないまま WLB_STA_FALLBACK_MS 経過 → AP へフォールバック (到達性確保)。
  // AP へ切替後は STA セッションを停止し、AP と STA を同時に上げない状態を保つ。
#if WLB_ENABLE_SOFTAP
  if (g_wantSta && !g_apMode && !g_everConnected &&
      (uint32_t)(now - g_wifiBootMs) > WLB_STA_FALLBACK_MS) {
    Serial.println("[net] STA が接続できないため AP へフォールバック");
    net.disable();                   // STA を停止 (排他)
    startApMode();                   // AP 単独へ
  }
#endif

  NetStatus ns; ns.connected = net.isConnected();
  ns.rssi = (int8_t)net.rssi(); ns.timeValid = net.timeValid();
  g_state.setNet(ns);

  // サーバ対応かつ未登録なら、接続後に自動 enroll (UI操作なしで実測値を流し始める)。
  static uint32_t lastEnroll = 0;
  if (srv::enabled() && !srv::hasToken() && net.isConnected() &&
      (lastEnroll == 0 || (uint32_t)(now - lastEnroll) > 30000)) {
    lastEnroll = now;
    String e; srv::enroll(e);
#if WLB_ENABLE_WDT
    esp_task_wdt_reset();
#endif
  }

  if (net.timeValid() && now - lastTimePush >= 60000) {
    lastTimePush = now; g_state.requestTimeSync(net.epoch());
  }

  // ★NTP 初確定時: 起動相対(B<秒>)で記録済みのログを絶対時刻へ一括置換 (オフライン対応)。
  if (net.timeValid() && !g_bootPatched) {
    uint32_t bootEpoch = net.epoch() - now / 1000;   // 起動時刻(絶対) = 現在NTP - 稼働秒
    datalog_patch_boottime(bootEpoch);
    g_bootPatched = true;
  }

  // FS 保存: ioTask が新しい分足(period×n平均)を追加したら CSV + 分足バイナリ(histfs) へ
  // 追記 (loopTask 単独 = FS競合回避)。時刻は分足レコードの epoch(絶対 or 起動相対) を使用。
  uint16_t seq = g_state.minSeq();
  if (!g_logSeqInit) { g_lastLogSeq = seq; g_logSeqInit = true; }   // 起動直後(seed分)はスキップ
  else if (seq != g_lastLogSeq) {
    g_lastLogSeq = seq;
    HistSample tail[WLB_TIER_CAP];
    uint16_t n = g_state.histCopyMin(tail, WLB_TIER_CAP);
    if (n > 0) {
      HistSample ls = tail[n-1];                       // 最新の分足レコード
      datalog_append(ls.epoch, wlbDec2(ls.t), wlbDec2(ls.h), ls.danger);   // CSV(全期間, 日別リング)
      if (ls.epoch > 1700000000UL) histfs_append(ls);  // 分足バイナリ(絶対epochのみ, チャート用)
      // サーバ対応時: 同じ分足1点を SQLサーバへ PUSH (未接続/時刻未確定/未登録は内部でスキップ)。
      if (srv::enabled() && srv::hasToken() && net.isConnected()) {
        srv::push(ls);
#if WLB_ENABLE_WDT
        esp_task_wdt_reset();                           // TLS 送信での遅延を吸収
#endif
      }
    }
  }

  delay(5);
}
