#include "sensor_bme280.h"
#ifdef WLB_SENSOR_BME280
#include <Wire.h>

bool SensorBME280::begin() {
  _present = _dev.begin(_addr, &Wire);
  if (_present)
    _dev.setSampling(Adafruit_BME280::MODE_NORMAL,
                     Adafruit_BME280::SAMPLING_X2,   // 温度
                     Adafruit_BME280::SAMPLING_X1,   // 湿度
                     Adafruit_BME280::SAMPLING_X16,  // 気圧
                     Adafruit_BME280::FILTER_X16,
                     Adafruit_BME280::STANDBY_MS_500);
  return _present;
}

bool SensorBME280::read(EnvReading& out) {
  float t = _dev.readTemperature();
  float h = _dev.readHumidity();
  float p = _dev.readPressure();          // [Pa]
  if (isnan(t) || isnan(p)) { out.ok = false; return false; }
  out.ok = true;
  out.tempC   = t;          out.tValid = true;
  out.rh      = h;          out.hValid = !isnan(h);
  out.presHpa = p / 100.0f; out.pValid = true;
  return true;
}

#endif
