// ============================================================================
//  lightning.cpp  -  雷センサ + 危険度(積和)算出  (詳細は lightning.h)
// ============================================================================
#include "lightning.h"
#include <Wire.h>
#include <math.h>

// ---- 距離ビンの重みベクトル (積和の右辺) -----------------------------------
//  bin0=真上 が最大、遠ざかるほど小さく。圏外(距離不明)は検出はしたので最小の1。
//    bin:  0(<=1km) 1(<=5) 2(<=10) 3(<=15) 4(<=20) 5(<=30) 6(<=40) 7(圏外)
static const uint16_t kWeight[WLB_LN_BINS] = { 20, 16, 10, 6, 4, 2, 1, 1 };

// ビン上限 [km] (この値以下なら当該ビン)。圏外(0x3F)は distanceToBin で別扱い。
static const uint8_t  kBinMaxKm[WLB_LN_BINS] = { 1, 5, 10, 15, 20, 30, 40, 255 };

// 危険度レベルのラベル
static const char* kLevelLabel[5] = { "安全", "注意", "警戒", "危険", "厳重警戒" };

const uint16_t* Lightning::weights() { return kWeight; }

// AS3935 の距離コード(km) → ビン番号
uint8_t Lightning::distanceToBin(uint8_t km) {
  if (km == WLB_LN_OUT_OF_RANGE || km == 0) return WLB_LN_BINS - 1;  // 圏外/不明
  for (uint8_t i = 0; i < WLB_LN_BINS - 1; i++)
    if (km <= kBinMaxKm[i]) return i;
  return WLB_LN_BINS - 2;   // 40km 超は最遠の実距離ビンへ
}

bool Lightning::begin(uint8_t sda, uint8_t scl) {
  Wire.begin(sda, scl);
  Wire.setClock(WLB_I2C_HZ);

  // 半減期 -> 1秒あたり減衰係数:  decay = 0.5 ^ (1/halflife)
  _decayPerSec = powf(0.5f, 1.0f / WLB_LN_HALFLIFE_S);

  _ok = _ts.begin(Wire);          // INFO 読めれば true
  _snap.bridgeOk = _ok;
  _snap.state    = _ok ? TS_READY : TS_FAULT_NACK;
  _snap.level    = LN_SAFE;
  _snap.levelLabel = kLevelLabel[LN_SAFE];
  _snap.nearestKm  = -1;
  return _ok;
}

bool Lightning::syncTime(uint32_t unixEpoch, uint16_t ms) {
  if (!_ok) return false;
  return _ts.syncTime(unixEpoch, ms);
}

void Lightning::decay() {
  _nearestKm = -1;    // ウィンドウを再評価するため一旦クリア
  for (uint8_t i = 0; i < WLB_LN_BINS; i++) {
    _acc[i] *= _decayPerSec;
    if (_acc[i] < 0.001f) _acc[i] = 0.0f;
    // まだ有意なカウントが残るビンのうち最も近い実距離を nearestKm に反映
    if (_acc[i] > 0.05f && i < WLB_LN_BINS - 1)
      if (_nearestKm < 0 || kBinMaxKm[i] < _nearestKm) _nearestKm = kBinMaxKm[i];
  }
}

void Lightning::addStrike(uint8_t km) {
  _acc[distanceToBin(km)] += 1.0f;
  _snap.lastStrikeEpoch = 0;   // 呼び出し側で上書きされる
}

// 積和: danger = Σ acc[i] * weight[i]
float Lightning::computeDanger() const {
  float d = 0.0f;
  for (uint8_t i = 0; i < WLB_LN_BINS; i++) d += _acc[i] * (float)kWeight[i];
  return d;
}

void Lightning::tick(uint32_t nowMs) {
  if (!_ok) return;

  // --- (A) 100ms毎: 軽量ステータス(12B, 信頼できる)を読み、保留イベントがある時だけ
  //         389B 束読み(poll)でドレインする。待機中は大容量読みをしない = Error263 根絶。
  //         (poll は失敗時 非ACK なのでイベントは失われず次回リトライされる)
  if (nowMs - _lastStatusMs >= 100) {
    _lastStatusMs = nowMs;
    TSStatus st;
    if (_ts.readStatus(st)) {
      _stValid = true; _st = st;                       // snapshot 用にキャッシュ
      if (st.pending > 0 && st.dataOk && !st.busy) {
        TSEvent ev[TS_MAX_EVENTS];
        int n = _ts.poll(ev, TS_MAX_EVENTS);
        for (int i = 0; i < n; i++) {
          if (!ev[i].isLightning()) continue;          // disturber/noise は総数のみ
          addStrike(ev[i].distanceKm);
          _snap.lastStrikeEpoch = ev[i].epoch;
          _snap.lastStrikeMs    = ev[i].ms;
          uint8_t km = ev[i].distanceKm;
          if (km != WLB_LN_OUT_OF_RANGE && km != 0)
            if (_nearestKm < 0 || (int16_t)km < _nearestKm) _nearestKm = km;
        }
      }
    } else {
      _stValid = false;
    }
  }

  // --- (B) 1秒周期の減衰 + スナップショット更新 ---------------------------
  if (nowMs - _lastTickMs < WLB_POLL_MS) return;
  _lastTickMs = nowMs;

  decay();

  float danger = computeDanger();
  int pct = (int)lroundf(danger / WLB_LN_FULLSCALE * 100.0f);
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;

  LnLevel lvl;
  if      (pct < 5)  lvl = LN_SAFE;
  else if (pct < 25) lvl = LN_ADVISORY;
  else if (pct < 50) lvl = LN_WARNING;
  else if (pct < 75) lvl = LN_DANGER;
  else               lvl = LN_SEVERE;

  _snap.dangerScore = danger;
  _snap.dangerPct   = (uint8_t)pct;
  _snap.level       = lvl;
  _snap.levelLabel  = kLevelLabel[lvl];
  _snap.nearestKm   = _nearestKm;

  // ブリッジの単調カウンタ/状態を取り込む (直近 (A) で読んだキャッシュを使用)
  if (_stValid) {
    _snap.bridgeOk       = true;
    _snap.state          = _st.state;
    _snap.lightningTotal = _st.lightningTotal;
    _snap.disturberTotal = _st.disturberTotal;
    _snap.noiseTotal     = _st.noiseTotal;
    _snap.lostTotal      = _st.lostTotal;
    _snap.bootId         = _st.bootId;
  } else {
    _snap.bridgeOk = false;
    _snap.state    = TS_FAULT_NACK;
  }
}
