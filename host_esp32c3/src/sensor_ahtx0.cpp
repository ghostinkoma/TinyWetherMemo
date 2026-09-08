#include "sensor_ahtx0.h"
#if defined(WLB_SENSOR_AHT25) || defined(WLB_SENSOR_AHT20)
#include <Wire.h>

bool SensorAHTx0::begin() {
  _present = _dev.begin(&Wire, 0, _addr);
  return _present;
}

bool SensorAHTx0::read(EnvReading& out) {
  sensors_event_t h, t;
  if (!_dev.getEvent(&h, &t)) { out.ok = false; return false; }   // ~80ms (IOタスクが吸収)
  out.ok    = true;
  out.tempC = t.temperature;       out.tValid = true;
  out.rh    = h.relative_humidity; out.hValid = true;
  return true;
}

#endif
