// ============================================================================
//  shared_state.cpp  -  詳細は shared_state.h
// ============================================================================
#include "shared_state.h"

SharedState g_state;

void SharedState::begin() { _mtx = xSemaphoreCreateMutex(); }

void SharedState::set(const SystemSnapshot& s) {
  if (!_mtx) return;
  xSemaphoreTake(_mtx, portMAX_DELAY);
  _s = s; _s.valid = true;
  xSemaphoreGive(_mtx);
}
bool SharedState::get(SystemSnapshot& out) {
  if (!_mtx) return false;
  xSemaphoreTake(_mtx, portMAX_DELAY);
  out = _s; bool v = _s.valid;
  xSemaphoreGive(_mtx);
  return v;
}

void SharedState::requestTimeSync(uint32_t epoch) {
  if (!_mtx) return;
  xSemaphoreTake(_mtx, portMAX_DELAY);
  _pendingEpoch = epoch; _timeReq = true;
  xSemaphoreGive(_mtx);
}
bool SharedState::takeTimeSync(uint32_t& epoch) {
  if (!_mtx) return false;
  bool req = false;
  xSemaphoreTake(_mtx, portMAX_DELAY);
  if (_timeReq) { epoch = _pendingEpoch; _timeReq = false; req = true; }
  xSemaphoreGive(_mtx);
  return req;
}

void SharedState::setNet(const NetStatus& n) {
  if (!_mtx) return;
  xSemaphoreTake(_mtx, portMAX_DELAY); _net = n; xSemaphoreGive(_mtx);
}
NetStatus SharedState::getNet() {
  NetStatus n; if (!_mtx) return n;
  xSemaphoreTake(_mtx, portMAX_DELAY); n = _net; xSemaphoreGive(_mtx);
  return n;
}

void SharedState::setIoConfig(const IoConfig& c) {
  if (!_mtx) return;
  xSemaphoreTake(_mtx, portMAX_DELAY); _cfg = c; xSemaphoreGive(_mtx);
}
IoConfig SharedState::getIoConfig() {
  IoConfig c; if (!_mtx) return c;
  xSemaphoreTake(_mtx, portMAX_DELAY); c = _cfg; xSemaphoreGive(_mtx);
  return c;
}

bool SharedState::pushCmd(uint8_t op, int32_t a) {
  if (!_mtx) return false;
  bool ok = false;
  xSemaphoreTake(_mtx, portMAX_DELAY);
  if (!_cmdPending) { _cmdOp = op; _cmdA = a; _cmdPending = true; ok = true; }
  xSemaphoreGive(_mtx);
  return ok;   // false = 前のコマンド処理中
}
bool SharedState::takeCmd(uint8_t& op, int32_t& a) {
  if (!_mtx) return false;
  bool has = false;
  xSemaphoreTake(_mtx, portMAX_DELAY);
  if (_cmdPending) { op = _cmdOp; a = _cmdA; _cmdPending = false; has = true; }
  xSemaphoreGive(_mtx);
  return has;
}

void SharedState::histAppend(const HistSample& s) {
  if (!_mtx) return;
  xSemaphoreTake(_mtx, portMAX_DELAY);
  _hist[_histHead] = s;
  _histHead = (_histHead + 1) % WLB_HIST_CAP;
  if (_histCount < WLB_HIST_CAP) _histCount++;
  xSemaphoreGive(_mtx);
}
uint16_t SharedState::histCopy(HistSample* out, uint16_t maxN) {
  if (!_mtx) return 0;
  xSemaphoreTake(_mtx, portMAX_DELAY);
  uint16_t n = _histCount; if (n > maxN) n = maxN;
  // 古い順に: head から遡って n 件
  uint16_t start = (_histHead + WLB_HIST_CAP - n) % WLB_HIST_CAP;
  for (uint16_t i = 0; i < n; i++) out[i] = _hist[(start + i) % WLB_HIST_CAP];
  xSemaphoreGive(_mtx);
  return n;
}

// --- 汎用リング(tier)ヘルパ (mutex は呼び出し側で取得) ---
static void ringAppend(HistSample* ring, uint16_t& head, uint16_t& cnt, uint16_t cap, const HistSample& s) {
  ring[head] = s; head = (head + 1) % cap; if (cnt < cap) cnt++;
}
static uint16_t ringCopy(const HistSample* ring, uint16_t head, uint16_t cnt, uint16_t cap, HistSample* out, uint16_t maxN) {
  uint16_t n = cnt; if (n > maxN) n = maxN;
  uint16_t start = (head + cap - n) % cap;
  for (uint16_t i = 0; i < n; i++) out[i] = ring[(start + i) % cap];
  return n;
}

void SharedState::histAppendMin(const HistSample& s) {
  if (!_mtx) return; xSemaphoreTake(_mtx, portMAX_DELAY);
  ringAppend(_min, _minHead, _minCount, WLB_TIER_CAP, s); _minSeq++; xSemaphoreGive(_mtx);
}
uint16_t SharedState::minSeq() {
  if (!_mtx) return 0; xSemaphoreTake(_mtx, portMAX_DELAY);
  uint16_t v = _minSeq; xSemaphoreGive(_mtx); return v;
}
uint16_t SharedState::histCopyMin(HistSample* out, uint16_t maxN) {
  if (!_mtx) return 0; xSemaphoreTake(_mtx, portMAX_DELAY);
  uint16_t n = ringCopy(_min, _minHead, _minCount, WLB_TIER_CAP, out, maxN); xSemaphoreGive(_mtx); return n;
}
void SharedState::histAppendHour(const HistSample& s) {
  if (!_mtx) return; xSemaphoreTake(_mtx, portMAX_DELAY);
  ringAppend(_hour, _hourHead, _hourCount, WLB_TIER_CAP, s); _hourSeq++; xSemaphoreGive(_mtx);
}
uint16_t SharedState::hourSeq() {
  if (!_mtx) return 0; xSemaphoreTake(_mtx, portMAX_DELAY);
  uint16_t v = _hourSeq; xSemaphoreGive(_mtx); return v;
}
uint16_t SharedState::histCopyHour(HistSample* out, uint16_t maxN) {
  if (!_mtx) return 0; xSemaphoreTake(_mtx, portMAX_DELAY);
  uint16_t n = ringCopy(_hour, _hourHead, _hourCount, WLB_TIER_CAP, out, maxN); xSemaphoreGive(_mtx); return n;
}
