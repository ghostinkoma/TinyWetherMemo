#include "sensor_bme680.h"
#ifdef WLB_SENSOR_BME680
#include <Wire.h>

bool SensorBME680::begin() {
  _present = _dev.begin(_addr, &Wire);
  if (_present) {
    _dev.setTemperatureOversampling(BME680_OS_8X);
    _dev.setHumidityOversampling(BME680_OS_2X);
    _dev.setPressureOversampling(BME680_OS_4X);
    _dev.setIIRFilterSize(BME680_FILTER_SIZE_3);
    _dev.setGasHeater(320, 150);   // 320°C / 150ms
  }
  return _present;
}

bool SensorBME680::read(EnvReading& out) {
  if (!_dev.performReading()) { out.ok = false; return false; }   // ~200ms (IOタスクが吸収)
  out.ok = true;
  out.tempC   = _dev.temperature;                out.tValid = true;
  out.rh      = _dev.humidity;                   out.hValid = true;
  out.presHpa = _dev.pressure / 100.0f;          out.pValid = true;
  out.gasKohm = _dev.gas_resistance / 1000.0f;   out.gValid = (_dev.gas_resistance > 0);
  return true;
}

#endif
