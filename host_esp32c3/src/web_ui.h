// ============================================================================
//  web_ui.h  -  ダッシュボード SPA (6ページ) + REST API。loopTask で駆動。
//  g_state を読み、Web→io は g_state のキュー/設定へ。I2C には触れない。
// ============================================================================
#ifndef WLB_WEB_UI_H
#define WLB_WEB_UI_H
#include "settings.h"

void webui_begin(WlbSettings& settings);  // WiFi(AP/STA)確立後に呼ぶ
void webui_loop();                        // 毎ループ handleClient

#endif // WLB_WEB_UI_H
