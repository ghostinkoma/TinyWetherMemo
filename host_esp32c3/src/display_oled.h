// ============================================================================
//  display_oled.h  -  OLED 表示 (オプション, I2C SSD1306 / SH1106)
//
//  不変条件: I2C に触るのは ioTask だけ → OLED も ioTask が描画する。
//  ioTask が持つ SystemSnapshot(cur) と NetStatus を受けて 1 画面を描く。
//  ラベルは ASCII (GFX 標準フォントに日本語なし)。
//  WLB_OLED 未定義ならクラスごとビルド対象外。
// ============================================================================
#ifndef WLB_DISPLAY_OLED_H
#define WLB_DISPLAY_OLED_H
#include "config.h"
#ifdef WLB_OLED

#include "shared_state.h"

class OledDisplay {
public:
  bool begin();
  bool present() const { return _present; }
  // 現在値を 1 画面描画 (ブロック I2C 転送 ~25-30ms, ioタスク内なので可)。
  void render(const SystemSnapshot& s, const NetStatus& net);
private:
  bool _present = false;
};

#endif
#endif // WLB_DISPLAY_OLED_H
