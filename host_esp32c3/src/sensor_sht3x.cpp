#include "sensor_sht3x.h"
#ifdef WLB_SENSOR_SHT35
#include <Wire.h>

bool SensorSHT3x::begin() {
  _present = _dev.begin(_addr);
  return _present;
}

bool SensorSHT3x::read(EnvReading& out) {
  float t = _dev.readTemperature();
  float h = _dev.readHumidity();
  if (isnan(t)) { out.ok = false; return false; }
  out.ok = true;
  out.tempC = t;  out.tValid = true;
  out.rh    = h;  out.hValid = !isnan(h);
  return true;
}

#endif
