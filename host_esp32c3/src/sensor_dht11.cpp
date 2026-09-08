#include "sensor_dht11.h"
#ifdef WLB_SENSOR_DHT11

bool SensorDHT11::begin() {
  _dev.begin();
  delay(1500);              // 電源安定待ち。IOタスク内なので vTaskDelay が譲る=無害。
  EnvReading r;
  _present = read(r);       // 初回読取で存在確認
  return _present;
}

bool SensorDHT11::read(EnvReading& out) {
  float t = _dev.readTemperature();   // ~25ms ビットバング (IOタスクが吸収)
  float h = _dev.readHumidity();
  if (isnan(t) || isnan(h)) { out.ok = false; return false; }
  out.ok = true;
  out.tempC = t;  out.tValid = true;
  out.rh    = h;  out.hValid = true;
  return true;
}

#endif
