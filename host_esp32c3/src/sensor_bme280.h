// ============================================================================
//  sensor_bme280.h  -  BME280 温湿度気圧 (気圧補正用リファレンス, Adafruit_BME280)
//  I2C 0x76 / 0x77。
// ============================================================================
#ifndef WLB_SENSOR_BME280_H
#define WLB_SENSOR_BME280_H
#include "config.h"
#ifdef WLB_SENSOR_BME280

#include "env_sensor.h"
#include <Adafruit_BME280.h>

class SensorBME280 : public IEnvSensor {
public:
  explicit SensorBME280(uint8_t addr = WLB_ADDR_BME280) : _addr(addr) {}
  bool begin() override;
  bool read(EnvReading& out) override;
  const char* name() const override { return "BME280"; }
  uint8_t caps() const override { return CAP_TEMP | CAP_HUM | CAP_PRES; }
private:
  uint8_t _addr;
  Adafruit_BME280 _dev;
};

#endif
#endif // WLB_SENSOR_BME280_H
