#include "sensor_s5851a.h"
#ifdef WLB_SENSOR_S5851A
#include <Wire.h>

bool SensorS5851A::begin() {
  Wire.beginTransmission(_addr);
  _present = (Wire.endTransmission() == 0);   // ACK があれば存在
  return _present;
}

bool SensorS5851A::read(EnvReading& out) {
  Wire.beginTransmission(_addr);
  Wire.write(0x00);                           // 温度レジスタ選択
  if (Wire.endTransmission(false) != 0) { out.ok = false; return false; }
  if (Wire.requestFrom((int)_addr, 2) != 2) { out.ok = false; return false; }
  uint8_t msb = Wire.read();
  uint8_t lsb = Wire.read();
  // LM75 系: 上位有効・下位未使用ビットは0。 temp = (int16)(msb<<8|lsb)/256
  int16_t raw = (int16_t)(((uint16_t)msb << 8) | lsb);
  out.ok    = true;
  out.tempC = raw / 256.0f;  out.tValid = true;
  return true;
}

#endif
