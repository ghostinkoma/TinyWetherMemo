// ============================================================================
//  display_oled.cpp  -  OLED 描画 (詳細は display_oled.h)
// ============================================================================
#include "display_oled.h"
#ifdef WLB_OLED
#include <Wire.h>

#if defined(WLB_OLED_SH1106)
  #include <Adafruit_SH110X.h>
  static Adafruit_SH1106G g_oled(WLB_OLED_W, WLB_OLED_H, &Wire, -1);
  #define OLED_WHITE SH110X_WHITE
#else
  #include <Adafruit_SSD1306.h>
  static Adafruit_SSD1306 g_oled(WLB_OLED_W, WLB_OLED_H, &Wire, -1);
  #define OLED_WHITE SSD1306_WHITE
#endif

// 危険度レベルの ASCII ラベル (GFX 標準フォントに日本語なし)
static const char* kLvlAscii[5] = { "SAFE", "ADVISORY", "WARNING", "DANGER", "SEVERE" };

bool OledDisplay::begin() {
#if defined(WLB_OLED_SH1106)
  _present = g_oled.begin(WLB_OLED_ADDR, true);
#else
  _present = g_oled.begin(SSD1306_SWITCHCAPVCC, WLB_OLED_ADDR);
#endif
  if (_present) {
    g_oled.clearDisplay();
    g_oled.setTextColor(OLED_WHITE);
    g_oled.setTextSize(1);
    g_oled.setCursor(0, 0);
    g_oled.print("WetherLoggerBox");
    g_oled.display();
  }
  return _present;
}

void OledDisplay::render(const SystemSnapshot& s, const NetStatus& net) {
  if (!_present) return;
  g_oled.clearDisplay();
  g_oled.setTextColor(OLED_WHITE);

  // --- 1行目: 温度 / 湿度 ---
  g_oled.setTextSize(1);
  g_oled.setCursor(0, 0);
  if (s.env.tValid) g_oled.printf("T%.1fC", s.env.tempC); else g_oled.print("T --");
  g_oled.setCursor(70, 0);
  if (s.env.hValid) g_oled.printf("H%.0f%%", s.env.rh); else g_oled.print("H --");

  // --- 2行目: 気圧 ---
  g_oled.setCursor(0, 10);
  if (s.env.pValid) g_oled.printf("P%.1fhPa", s.env.presHpa); else g_oled.print("P --");

  // --- 危険度 (大) + レベル ---
  g_oled.setTextSize(2);
  g_oled.setCursor(0, 22);
  g_oled.printf("%u%%", s.ln.dangerPct);
  g_oled.setTextSize(1);
  g_oled.setCursor(70, 22);
  g_oled.print(s.ln.bridgeOk ? kLvlAscii[s.ln.level <= 4 ? s.ln.level : 0] : "LN?");
  g_oled.setCursor(70, 32);
  if (s.ln.nearestKm < 0)        g_oled.print("near--");
  else if (s.ln.nearestKm == 63) g_oled.print(">40km");
  else                           g_oled.printf("%dkm", s.ln.nearestKm);

  // --- 危険度バー ---
  g_oled.drawRect(0, 41, 128, 4, OLED_WHITE);
  int bw = (int)s.ln.dangerPct * 126 / 100;
  if (bw > 0) g_oled.fillRect(1, 42, bw, 2, OLED_WHITE);

  // --- 最下段: 落雷カウンタ / WiFi ---
  g_oled.setCursor(0, 48);
  g_oled.printf("L%u D%u", s.ln.lightningTotal, s.ln.disturberTotal);
  g_oled.setCursor(0, 56);
  if (net.connected) g_oled.printf("WiFi %ddBm%s", net.rssi, net.timeValid ? " T" : "");
  else               g_oled.print("WiFi ...");

  g_oled.display();   // ~25-30ms のブロック I2C 転送 (ioタスク内なので可)
}

#endif
