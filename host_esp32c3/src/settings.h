// ============================================================================
//  settings.h  -  NVS(Preferences) 永続設定。Web が編集し、loop/io が参照。
// ============================================================================
#ifndef WLB_SETTINGS_H
#define WLB_SETTINGS_H
#include <Arduino.h>
#include "config.h"     // WLB_WIFI_SSID/PASS を既定値に使う

struct WlbSettings {
  // WiFi (既定は config.h。/settings.ini があればそちらで上書き)
  char     ssid[33] = WLB_WIFI_SSID;
  char     pass[65] = WLB_WIFI_PASS;
  char     mdns[33] = "WetherMemo";
  uint8_t  staMode  = 1;            // 1=STA優先 / 0=AP運用

  // オフセット
  float    offT = 0.0f;
  float    offH = 0.0f;

  // 測定頻度
  uint16_t samplePeriodS = 5;       // 既定5秒 (ダッシュボード/ライブ足 = 1測定/5秒)
  uint8_t  avgN          = 12;      // FS集計サンプル数 (period×n=60s ごとに分足1レコード)
  uint8_t  dropMinMax    = 1;       // 最小最大を捨てる (その場合 n>=4)

  // AS3935
  uint8_t  asIndoor     = 1;        // 1=室内 / 0=室外
  uint8_t  asNoiseFloor = 2;        // NF_LEV 0..7
  uint8_t  asWatchdog   = 2;        // WDTH 0..15
  uint8_t  asSrej       = 2;        // SREJ 0..15
  uint8_t  asMinNum     = 0;        // MIN_NUM_LIGH 0..3
  uint8_t  asMaskDist   = 0;        // MASK_DIST 妨害波マスク 0/1
};

void        settings_load(WlbSettings& s);   // NVS→s (無ければ既定)
void        settings_save(const WlbSettings& s);

#endif // WLB_SETTINGS_H
