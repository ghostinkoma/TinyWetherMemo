// ============================================================================
//  wifi_session.h  -  ESP32-C3 用 非ブロッキング WiFi セッション管理
//
//  設計方針 (「WiFiセッションに十分配慮」):
//   - loop() を 絶対にブロックしない。connect 待ちの busy-loop を作らない
//     (雷 drain / センサ 1秒サンプルを止めないため)。
//   - 切断は WiFi イベントで検知し、指数バックオフで自動再接続。
//   - 状態を明確な機械 (DISABLED/IDLE/CONNECTING/CONNECTED/WAIT_RETRY) で管理。
//   - NTP は接続完了後に一度だけ開始、時刻確定を timeValid() で通知。
//   - コールバック 2 本だけ公開: onConnected / onTime。
//       * onConnected: IP 取得直後 (HTTPサーバ起動など)
//       * onTime:      NTP で絶対時刻が確定 (雷ブリッジへ syncTime 等)
//   - 資格情報や再接続は WiFi ライブラリ任せにせず自前制御 (persistent=off)。
//
//  使い方:
//     WifiSession net;
//     net.onTime([](uint32_t epoch){ ln.syncTime(epoch); });
//     net.begin(SSID, PASS);
//     loop: net.loop(millis());   // 毎ループ。ブロックしない。
// ============================================================================
#ifndef WLB_WIFI_SESSION_H
#define WLB_WIFI_SESSION_H

#include <Arduino.h>
#include <functional>

class WifiSession {
public:
  enum State : uint8_t {
    ST_DISABLED = 0,   // begin() 未呼び出し / 資格情報なし
    ST_IDLE,           // 未接続・再接続待ちでもない
    ST_CONNECTING,     // 接続試行中 (非ブロッキング)
    ST_CONNECTED,      // IP 取得済み
    ST_WAIT_RETRY      // 失敗/切断 → バックオフ待ち
  };

  using ConnectedCb = std::function<void()>;
  using TimeCb      = std::function<void(uint32_t epoch)>;

  // 資格情報を渡して開始 (接続はブロックせず loop() で進む)。
  void begin(const char* ssid, const char* pass);

  // 毎ループ呼ぶ。now は millis()。状態遷移・再接続・NTP を進める。
  void loop(uint32_t now);

  // --- 状態参照 ---
  State state()      const { return _state; }
  bool  isConnected()const { return _state == ST_CONNECTED; }
  bool  timeValid()  const { return _timeValid; }
  uint32_t epoch()   const;                 // 現在の UNIX 秒 (未確定なら 0)
  int   rssi()       const;                 // dBm (未接続は 0)
  const char* stateStr() const;

  // --- コールバック登録 ---
  void onConnected(ConnectedCb cb) { _onConnected = cb; }
  void onTime(TimeCb cb)           { _onTime = cb; }

  // NTP 設定 (begin より前に。既定 pool.ntp.org / JST)
  void setNtp(const char* server1, long gmtOffsetSec = 9 * 3600,
              int daylightSec = 0) {
    _ntp1 = server1; _gmt = gmtOffsetSec; _dst = daylightSec;
  }

private:
  State    _state = ST_DISABLED;
  const char* _ssid = nullptr;
  const char* _pass = nullptr;

  // バックオフ (指数, 上限あり)
  uint32_t _retryAtMs   = 0;
  uint32_t _backoffMs   = 1000;      // 初回 1s → ×2 … 上限
  static const uint32_t kBackoffMax = 30000;
  uint32_t _connectStartMs = 0;
  static const uint32_t kConnectTimeout = 15000;

  // NTP
  const char* _ntp1 = "pool.ntp.org";
  long _gmt = 9 * 3600;    // JST
  int  _dst = 0;
  bool _ntpStarted = false;
  bool _timeValid  = false;
  uint32_t _lastNtpCheckMs = 0;

  ConnectedCb _onConnected = nullptr;
  TimeCb      _onTime      = nullptr;

  // メッシュ再ローミング (接続後もRSSIを監視し強いAPへ張り替え)
  uint32_t _lastRoamCheckMs = 0;

  void startConnect(uint32_t now);
  void enterWaitRetry(uint32_t now);
  void checkNtp(uint32_t now);
  void maybeRoam(uint32_t now);
};

#endif // WLB_WIFI_SESSION_H
