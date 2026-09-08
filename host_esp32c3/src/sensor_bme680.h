// ============================================================================
//  sensor_bme680.h  -  BME680 温湿度気圧+ガス (Adafruit_BME680)
//  I2C 0x76 / 0x77。performReading() のブロック(~200ms)は IOタスクが吸収。
// ============================================================================
#ifndef WLB_SENSOR_BME680_H
#define WLB_SENSOR_BME680_H
#include "config.h"
#ifdef WLB_SENSOR_BME680

#include "env_sensor.h"
#include <Adafruit_BME680.h>

class SensorBME680 : public IEnvSensor {
public:
  explicit SensorBME680(uint8_t addr = WLB_ADDR_BME680) : _addr(addr) {}
  bool begin() override;
  bool read(EnvReading& out) override;
  const char* name() const override { return "BME680"; }
  uint8_t caps() const override { return CAP_TEMP | CAP_HUM | CAP_PRES | CAP_GAS; }
private:
  uint8_t _addr;
  Adafruit_BME680 _dev;
};

#endif
#endif // WLB_SENSOR_BME680_H
