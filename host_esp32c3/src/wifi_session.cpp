// ============================================================================
//  wifi_session.cpp  -  非ブロッキング WiFi セッション管理 (詳細は .h)
// ============================================================================
#include "wifi_session.h"
#include <WiFi.h>
#include <time.h>
#include "config.h"

void WifiSession::begin(const char* ssid, const char* pass) {
  _ssid = ssid;
  _pass = pass;
  if (!_ssid || !_ssid[0]) { _state = ST_DISABLED; return; }

  // セッション制御は自前で行う: フラッシュへの資格情報保存は無効、
  // ライブラリの自動再接続も無効化し、状態機械で完全に掌握する。
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WLB_WIFI_MODEM_SLEEP ? true : false);   // 省電力: モデムスリープ(config)

  // ★RTC 活用: ESP32-C3 の RTC タイマは CPU ソフトリセット(WDT再起動等)を跨いで時刻を保持する。
  //   前回 NTP 同期済なら再起動直後から正しい時刻が使えるので、NTP を待たず即 timeValid にし、
  //   イベント/CSV のタイムスタンプ欠落を防ぐ(電源断では 1970 に戻るので誤検出しない)。
  time_t rt = time(nullptr);
  if (rt > 1700000000L) {
    _timeValid = true;
    if (_onTime) _onTime((uint32_t)rt);
    Serial.printf("[net] RTC retained time -> valid immediately (%ld)\n", (long)rt);
  }

  _backoffMs = 1000;
  startConnect(millis());
}

void WifiSession::startConnect(uint32_t now) {
  WiFi.disconnect(false, true);   // 前回セッションを掃除 (STAは維持)

  // ★メッシュ対策(AquaController net.cpp 準拠): 同一SSIDが複数AP存在するので、
  //   スキャンして最強RSSIのBSSID+chへロック接続する。既定の WiFi.begin(ssid,pass) は
  //   遠いAPを掴むことがある(実測 -82dB)。近いAP(<2m, >=-70dB想定)へ確実に繋ぐ。
  int best = -999; int32_t ch = 0; uint8_t bssid[6]; bool locked = false;
  uint8_t sameCount = 0;
  int n = WiFi.scanNetworks(false /*async*/, true /*hidden*/, false /*passive*/, 250);
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) != _ssid) continue;
    sameCount++;
    if (WiFi.RSSI(i) > best) {                     // 同一SSIDで最強RSSIのBSSIDを選ぶ
      best = WiFi.RSSI(i); ch = WiFi.channel(i);
      uint8_t* b = WiFi.BSSID(i); if (b) memcpy(bssid, b, 6); locked = true;
    }
  }
  WiFi.scanDelete();
  if (locked) {
    Serial.printf("[net] lock BSSID %02X:%02X:%02X:%02X:%02X:%02X ch=%ld rssi=%d (same-SSID APs=%u)\n",
                  bssid[0],bssid[1],bssid[2],bssid[3],bssid[4],bssid[5],(long)ch,best,sameCount);
    WiFi.begin(_ssid, _pass, ch, bssid);   // 最強APへロック
  } else {
    WiFi.begin(_ssid, _pass);              // 見つからなければ従来通り
  }
  _connectStartMs = now;
  _state = ST_CONNECTING;
}

void WifiSession::enterWaitRetry(uint32_t now) {
  WiFi.disconnect(false, true);
  _retryAtMs = now + _backoffMs;
  _backoffMs = min(_backoffMs * 2, kBackoffMax);   // 指数バックオフ
  _ntpStarted = false;
  _state = ST_WAIT_RETRY;
}

void WifiSession::loop(uint32_t now) {
  switch (_state) {
    case ST_DISABLED:
    case ST_IDLE:
      break;

    case ST_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        _backoffMs = 1000;                 // 成功でバックオフをリセット
        _state = ST_CONNECTED;
        if (_onConnected) _onConnected();
        _ntpStarted = false;
        checkNtp(now);
      } else if (now - _connectStartMs > kConnectTimeout) {
        enterWaitRetry(now);               // タイムアウト → バックオフ
      }
      break;

    case ST_CONNECTED:
      if (WiFi.status() != WL_CONNECTED) { // 切断検知 → 再接続へ
        _timeValid = false;
        enterWaitRetry(now);
      } else {
        checkNtp(now);
#if WLB_ENABLE_ROAM
        maybeRoam(now);                    // 弱ければ強いAPへ張り替え(メッシュ自己復帰)
#endif
      }
      break;

    case ST_WAIT_RETRY:
      if ((int32_t)(now - _retryAtMs) >= 0) startConnect(now);
      break;
  }
}

// NTP: 接続後に一度 configTime、その後 数秒おきに時刻確定を確認。
void WifiSession::checkNtp(uint32_t now) {
  if (!_ntpStarted) {
    configTime(_gmt, _dst, _ntp1, "time.google.com", "time.nist.gov");
    _ntpStarted = true;
    _lastNtpCheckMs = now;
    return;
  }
  if (_timeValid) return;
  if (now - _lastNtpCheckMs < 1000) return;
  _lastNtpCheckMs = now;

  time_t t = time(nullptr);
  if (t > 1600000000L) {        // 2020-09 以降なら NTP 確定とみなす
    _timeValid = true;
    if (_onTime) _onTime((uint32_t)t);
  }
}

#if WLB_ENABLE_ROAM
// 接続後のメッシュ再ローミング: RSSIが閾値割れの時だけ同一SSIDを再スキャンし、
// 現在より有意に強い別BSSIDがあれば張り替える。起動時に遠APを掴んでも自己復帰する。
// (スキャンは弱信号時のみ・低頻度なので通常の応答性には影響しない)
void WifiSession::maybeRoam(uint32_t now) {
  if (now - _lastRoamCheckMs < WLB_ROAM_CHECK_MS) return;
  _lastRoamCheckMs = now;

  int cur = WiFi.RSSI();
  if (cur == 0 || cur >= WLB_ROAM_RSSI_TH) return;   // 十分強い → 何もしない

  int best = -999; int32_t ch = 0; uint8_t bssid[6]; bool found = false;
  int n = WiFi.scanNetworks(false /*async*/, true /*hidden*/, false /*passive*/, 150);
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == _ssid && WiFi.RSSI(i) > best) {
      best = WiFi.RSSI(i); ch = WiFi.channel(i);
      memcpy(bssid, WiFi.BSSID(i), 6); found = true;
    }
  }
  WiFi.scanDelete();
  if (!found || best < cur + WLB_ROAM_MARGIN) return; // 有意に強い候補なし

  uint8_t* c = WiFi.BSSID();
  if (c && memcmp(c, bssid, 6) == 0) return;          // 既に最強APに接続済み

  Serial.printf("[net] roam %ddB -> %02X:%02X:%02X:%02X:%02X:%02X ch=%ld %ddB\n",
                cur, bssid[0],bssid[1],bssid[2],bssid[3],bssid[4],bssid[5],(long)ch,best);
  WiFi.begin(_ssid, _pass, ch, bssid);               // 強いAPへ張り替え
  _connectStartMs = now;
  _state = ST_CONNECTING;                             // 以降 loop が再接続を進める
}
#endif  // WLB_ENABLE_ROAM

uint32_t WifiSession::epoch() const {
  if (!_timeValid) return 0;
  return (uint32_t)time(nullptr);
}

int WifiSession::rssi() const {
  return isConnected() ? WiFi.RSSI() : 0;
}

const char* WifiSession::stateStr() const {
  switch (_state) {
    case ST_DISABLED:   return "DISABLED";
    case ST_IDLE:       return "IDLE";
    case ST_CONNECTING: return "CONNECTING";
    case ST_CONNECTED:  return "CONNECTED";
    case ST_WAIT_RETRY: return "WAIT_RETRY";
  }
  return "?";
}
