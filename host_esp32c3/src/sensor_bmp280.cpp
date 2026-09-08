#include "sensor_bmp280.h"
#ifdef WLB_SENSOR_BMP280
#include <Wire.h>

bool SensorBMP280::begin() {
  // AHT20+BMP280 コンボは 0x76/0x77 の個体差 + 中華クローンで chipID が 0x58 以外の
  // ことがある。両アドレス × (正規 chipID → 実 chipID 再試行) で確実に検出する
  // (AquaController の同一ロット実装に準拠)。
  const uint8_t addrs[2] = { _addr, (uint8_t)((_addr == 0x76) ? 0x77 : 0x76) };
  for (uint8_t i = 0; i < 2 && !_present; i++) {
    uint8_t a = addrs[i];
    bool ok = _dev.begin(a);                       // まず正規 (chipID 0x58)
    if (!ok) {                                     // クローン: reg0xD0(ID)を読んで再試行
      Wire.beginTransmission(a); Wire.write(0xD0);
      if (Wire.endTransmission(false) == 0 && Wire.requestFrom((int)a, 1) == 1) {
        int id = Wire.read();
        if (id > 0) ok = _dev.begin(a, (uint8_t)id);
      }
    }
    if (ok) { _addr = a; _present = true; }
  }
  if (_present)
    _dev.setSampling(Adafruit_BMP280::MODE_NORMAL,
                     Adafruit_BMP280::SAMPLING_X2,   // 温度
                     Adafruit_BMP280::SAMPLING_X16,  // 気圧
                     Adafruit_BMP280::FILTER_X16,
                     Adafruit_BMP280::STANDBY_MS_500);
  return _present;
}

bool SensorBMP280::read(EnvReading& out) {
  float t = _dev.readTemperature();
  float p = _dev.readPressure();          // [Pa]
  if (isnan(t) || isnan(p)) { out.ok = false; return false; }
  out.ok = true;
  out.tempC   = t;          out.tValid = true;
  out.presHpa = p / 100.0f; out.pValid = true;
  return true;
}

#endif
