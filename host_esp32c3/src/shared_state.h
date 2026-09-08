// ============================================================================
//  shared_state.h  -  IOタスク ⇄ loop/Webタスク の状態受け渡し (mutex 保護)
//
//  RTOS 境界: ioTask が I2C を単独所有し結果を publish。Web(loopTask)は読むだけ。
//  逆方向(Web→io)は「時刻同期依頼」「IO設定(周期/平均/オフセット)」「校正コマンド」を
//  キュー/スロットで渡し、実 I2C 実行は ioTask だけが行う(I2C単独所有を厳守)。
// ============================================================================
#ifndef WLB_SHARED_STATE_H
#define WLB_SHARED_STATE_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "sensors.h"     // SensorSample / EnvReading / WLB_MAX_SENSORS
#include "lightning.h"   // LnSnapshot

// --- Web→io コマンド (AS3935 校正/設定, I2C は ioが実行) ---
enum IoCmdOp : uint8_t {
  IOCMD_NONE = 0,
  IOCMD_RECAL,        // LCO再校正+保存
  IOCMD_GAIN,         // a: 1=indoor 0=outdoor
  IOCMD_NOISEFLOOR,   // a: 0..7
  IOCMD_WATCHDOG,     // a: 0..15
  IOCMD_SREJ,         // a: 0..15
  IOCMD_MINNUM,       // a: 0..3
  IOCMD_MASKDIST,     // a: 1=妨害波マスクON 0=OFF
  IOCMD_CLEARSTATS    // 統計クリア
};

// io の測定設定 (Web が設定, io が適用)
struct IoConfig {
  uint16_t samplePeriodMs = 5000;  // 測定周期
  uint8_t  avgN           = 8;     // 平均サンプル数
  uint8_t  dropMinMax     = 1;     // 最小最大を捨てる
  float    offT           = 0.0f;  // 温度オフセット
  float    offH           = 0.0f;  // 湿度オフセット
};

// AS3935 校正結果
struct CalibInfo {
  uint8_t  tuncap = 0, ok = 0;
  uint16_t count = 0, target = 3125;
  bool     valid = false;
};

// --- 1レコード圧縮: int16 固定小数点エンコード (AquaController histdb.h 準拠) ---
//   温度/湿度 : v×100        (例 28.10℃→2810 /  67.3%→6730)
//   気圧      : (v-1000)×100  (★1000hPaオフセットで int16 に収める。800..1085→-20000..+8500)
//   NaN(欠測) : WLB_NA(=INT16_MIN) を標識に使用 (有効値域は上記でこれに達しない)
constexpr int16_t WLB_NA = INT16_MIN;         // 欠測(NaN)標識
constexpr float   WLB_PRESS_BASE = 1000.0f;   // 気圧オフセット基準
inline int16_t wlbClamp16(long v) { return v > 32767 ? 32767 : (v < -32767 ? -32767 : (int16_t)v); }
inline int16_t wlbEnc2(float v)   { return isnan(v) ? WLB_NA : wlbClamp16(lroundf(v * 100.0f)); }
inline float   wlbDec2(int16_t x) { return x == WLB_NA ? NAN : x / 100.0f; }
inline int16_t wlbEncP(float hpa) { return isnan(hpa) ? WLB_NA : wlbClamp16(lroundf((hpa - WLB_PRESS_BASE) * 100.0f)); }
inline float   wlbDecP(int16_t x) { return x == WLB_NA ? NAN : WLB_PRESS_BASE + x / 100.0f; }

// 履歴 1 サンプル (チャート/30分頻度用, RAM リング) — パックで 15 バイト/レコード
struct __attribute__((packed)) HistSample {
  uint32_t epoch = 0;                       // 絶対 or 起動相対秒
  int16_t  t = WLB_NA, h = WLB_NA, p = WLB_NA; // 固定小数点(t/h=×100, p=(hPa-1000)×100)
  uint8_t  danger = 0;
  uint16_t lTotal = 0, dTotal = 0;          // 落雷/妨害波の単調カウンタ
};
static_assert(sizeof(HistSample) == 15, "HistSample は 15 バイト(圧縮)であること");
#define WLB_HIST_CAP 360   // 5秒×360 = 30分ライブ(直近30分頻度に必要)。長期はFS(Phase2)。
#define WLB_TIER_CAP 120   // 分足=120分(2時間) / 時足=120時間(5日) の代表値リング

struct SystemSnapshot {
  EnvReading   env;
  SensorSample samples[WLB_MAX_SENSORS];
  uint8_t      nSamples = 0;
  LnSnapshot   ln;
  CalibInfo    calib;
  uint8_t      scanList[16] = {0};
  uint8_t      scanCount = 0;
  char         dbg[112] = {0};     // センサ診断 (present/値) — Web表示で切り分け
  uint32_t     updatedMs = 0;
  bool         valid = false;
};

struct NetStatus {
  bool    connected = false;
  int8_t  rssi = 0;
  bool    timeValid = false;
};

class SharedState {
public:
  void begin();

  // io → loop (状態)
  void set(const SystemSnapshot& s);
  bool get(SystemSnapshot& out);

  // loop → io (時刻同期)
  void requestTimeSync(uint32_t epoch);
  bool takeTimeSync(uint32_t& epoch);

  // loop → io (WiFi状態: OLED表示)
  void setNet(const NetStatus& n);
  NetStatus getNet();

  // loop → io (測定設定)
  void setIoConfig(const IoConfig& c);
  IoConfig getIoConfig();

  // loop → io (校正コマンド: 単一保留スロット)
  bool pushCmd(uint8_t op, int32_t a);
  bool takeCmd(uint8_t& op, int32_t& a);

  // io → (履歴リング)。fine=5秒ライブ, min=分足, hour=時足
  // ★maxN/戻り値は uint16_t: WLB_HIST_CAP(360) は uint8_t だと 104 に化ける
  void histAppend(const HistSample& s);
  uint16_t histCopy(HistSample* out, uint16_t maxN);  // 古→新、件数返す
  void histAppendMin(const HistSample& s);
  uint16_t histCopyMin(HistSample* out, uint16_t maxN);
  void histAppendHour(const HistSample& s);
  uint16_t histCopyHour(HistSample* out, uint16_t maxN);
  uint16_t hourSeq();   // 時足が追加される度に増加
  uint16_t minSeq();    // 分足(period×n平均)が追加される度に増加(loopがFS永続化のトリガに使う)

private:
  SemaphoreHandle_t _mtx = nullptr;
  SystemSnapshot _s;
  uint32_t _pendingEpoch = 0; bool _timeReq = false;
  NetStatus _net;
  IoConfig  _cfg;
  uint8_t   _cmdOp = IOCMD_NONE; int32_t _cmdA = 0; bool _cmdPending = false;
  HistSample _hist[WLB_HIST_CAP]; uint16_t _histHead = 0, _histCount = 0;
  HistSample _min[WLB_TIER_CAP];  uint16_t _minHead = 0,  _minCount = 0;
  HistSample _hour[WLB_TIER_CAP]; uint16_t _hourHead = 0, _hourCount = 0;
  uint16_t   _hourSeq = 0;
  uint16_t   _minSeq = 0;
};

extern SharedState g_state;

#endif // WLB_SHARED_STATE_H
