#include "sensor_ds18b20.h"
#ifdef WLB_SENSOR_DS18B20

bool SensorDS18B20::begin() {
  _dev.begin();
  _dev.setWaitForConversion(true);   // 750ms ブロック待ち (IOタスクが吸収)
  _present = (_dev.getDeviceCount() > 0);
  return _present;
}

bool SensorDS18B20::read(EnvReading& out) {
  _dev.requestTemperatures();               // 変換起動 + 完了まで待つ
  float t = _dev.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C || isnan(t)) { out.ok = false; return false; }
  out.ok = true;
  out.tempC = t;  out.tValid = true;
  return true;
}

#endif
