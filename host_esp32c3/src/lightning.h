// ============================================================================
//  lightning.h  -  雷センサ (ThunderSense ブリッジ) ラッパ + 危険度(積和)算出
//
//  役割:
//   - ThunderSense (AS3935→CH32V003, I2Cスレーブ0x28) を drain して雷イベント収集
//   - 距離ビン毎の減衰カウント acc[] を保持し、重みとの 内積(積和) で危険度を算出
//   - ホスト(Web/ログ)へは LnSnapshot 一発で現在値を渡す
//
//  使い方:
//     Lightning ln;
//     ln.begin();                 // I2C + ブリッジ初期化
//     ln.syncTime(unixEpoch);     // NTP 取得後 (任意, イベントに絶対時刻)
//     loop: ln.tick(millis());    // 毎ループ呼ぶ (内部で 1秒周期に間引き)
//           LnSnapshot s = ln.snapshot();
// ============================================================================
#ifndef WLB_LIGHTNING_H
#define WLB_LIGHTNING_H

#include <Arduino.h>
#include "config.h"
#include "ThunderSense.h"

// 危険度レベル (ラベルは lightning.cpp の kLevelLabel)
enum LnLevel : uint8_t {
  LN_SAFE     = 0,   // 安全
  LN_ADVISORY = 1,   // 注意
  LN_WARNING  = 2,   // 警戒
  LN_DANGER   = 3,   // 危険
  LN_SEVERE   = 4    // 厳重警戒
};

// ホストへ渡す現在状態のスナップショット
struct LnSnapshot {
  bool      bridgeOk;        // ブリッジと通信できているか
  uint8_t   state;           // TSState (READY/FAULT_* 等)
  float     dangerScore;     // 積和の生値 (Σ acc*weight)
  uint8_t   dangerPct;       // 0..100 に正規化した危険度
  LnLevel   level;           // 危険度レベル
  const char* levelLabel;    // 日本語ラベル
  int16_t   nearestKm;       // 直近ウィンドウでの最短距離 [km] (無し=-1, 圏外=63)
  uint32_t  lastStrikeEpoch; // 最後の落雷の epoch (絶対 or 起動相対)
  uint16_t  lastStrikeMs;
  // ブリッジの単調カウンタ (オーバフローしても正確)
  uint16_t  lightningTotal;
  uint16_t  disturberTotal;
  uint16_t  noiseTotal;
  uint16_t  lostTotal;
  uint8_t   bootId;          // 変化=ブリッジ再起動 → 時刻再シード合図
};

class Lightning {
public:
  bool begin(uint8_t sda = WLB_I2C_SDA, uint8_t scl = WLB_I2C_SCL);

  // 絶対時刻シード (NTP後 / bootId 変化時に呼ぶ)。未同期でも動作はする。
  bool syncTime(uint32_t unixEpoch, uint16_t ms = 0);

  // 毎ループ呼ぶ。内部で最短 WLB_POLL_MS 間隔に間引いて poll & 減衰更新する。
  void tick(uint32_t nowMs);

  // 現在状態を取得
  LnSnapshot snapshot() const { return _snap; }

  // 距離ビン毎の減衰カウント(積和の左辺ベクトル)を参照 (デバッグ/校正UI用)
  const float* bins() const { return _acc; }
  static const uint16_t* weights();   // 重みベクトル (積和の右辺)

  ThunderSense& raw() { return _ts; }   // 校正など下位APIへの直接アクセス

private:
  ThunderSense _ts;
  bool     _ok = false;
  uint32_t _lastTickMs = 0;
  uint32_t _lastStatusMs = 0;           // 軽量ステータス読みの間引き
  bool     _stValid = false;            // 直近 readStatus 成否
  TSStatus _st = {};                    // 直近ステータス(キャッシュ)

  float    _acc[WLB_LN_BINS] = {0};     // 減衰カウント (統計量)
  int16_t  _nearestKm = -1;             // ウィンドウ内最短距離
  float    _decayPerSec = 0.997f;       // begin() で半減期から算出

  LnSnapshot _snap = {};

  static uint8_t distanceToBin(uint8_t km);
  void  decay();                        // 1秒分の減衰
  void  addStrike(uint8_t km);          // 落雷1件を acc[] へ
  float computeDanger() const;          // 積和
};

#endif // WLB_LIGHTNING_H
