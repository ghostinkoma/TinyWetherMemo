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

WifiSession  net;
WlbSettings  g_settings;
static uint32_t lastTimePush = 0;
static bool     g_bootPatched = false;   // NTP初確定時に起動相対ログを絶対時刻へ置換(1回)
static uint16_t g_lastLogSeq = 0;        // FSへ永続化した分足シーケンス
static bool     g_logSeqInit = false;

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

  // WiFi: NTP確定で I2C書込(syncTime)は IO へ依頼
  net.onTime([](uint32_t epoch){ g_state.requestTimeSync(epoch); });
  net.setNtp(WLB_NTP_SERVER, WLB_TZ_OFFSET_S);
  net.begin(g_settings.ssid, g_settings.pass);   // 空/AP運用なら STA は DISABLED

#if WLB_ENABLE_SOFTAP
  // SoftAP 常設 (設定用・PWなし)。STA が繋がらなくても必ず到達可能。
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(WLB_WIFI_MODEM_SLEEP ? true : false);   // true=WIFI_PS_MIN_MODEM(省電力)/false=常時ON
  if (strlen(WLB_AP_PASS) >= 8) WiFi.softAP(WLB_AP_SSID, WLB_AP_PASS);   // WPA2
  else                          WiFi.softAP(WLB_AP_SSID);                 // open (PW未設定時)
  Serial.printf("[net] AP '%s' (WPA2=%d) http://%s/\n", WLB_AP_SSID, strlen(WLB_AP_PASS)>=8, WiFi.softAPIP().toString().c_str());
#else
  // 切り分け: softAP を無効化し STA 単独運用 (同一ch同居 deauth 仮説の検証)。
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  Serial.println("[net] STA-only (softAP disabled)");
#endif

  webui_begin(g_settings);   // Web(:80) + mDNS(settings.mdns)
}

void loop() {
  uint32_t now = millis();
#if WLB_ENABLE_WDT
  esp_task_wdt_reset();          // WDT feed (loopTask)
#endif
  net.loop(now);
  webui_loop();

  NetStatus ns; ns.connected = net.isConnected();
  ns.rssi = (int8_t)net.rssi(); ns.timeValid = net.timeValid();
  g_state.setNet(ns);

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
    }
  }

  delay(5);
}
