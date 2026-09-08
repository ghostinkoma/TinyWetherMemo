// ============================================================================
//  sensor_bmp280.h  -  BMP280 気圧+温度  [採用]  (Adafruit_BMP280)
//  I2C 0x76(SDO=GND) / 0x77(SDO=VDD)。
// ============================================================================
#ifndef WLB_SENSOR_BMP280_H
#define WLB_SENSOR_BMP280_H
#include "config.h"
#ifdef WLB_SENSOR_BMP280

#include "env_sensor.h"
#include <Adafruit_BMP280.h>

class SensorBMP280 : public IEnvSensor {
public:
  explicit SensorBMP280(uint8_t addr = WLB_ADDR_BMP280) : _addr(addr) {}
  bool begin() override;
  bool read(EnvReading& out) override;
  const char* name() const override { return "BMP280"; }
  uint8_t caps() const override { return CAP_TEMP | CAP_PRES; }
private:
  uint8_t _addr;
  Adafruit_BMP280 _dev;
};

#endif
#endif // WLB_SENSOR_BMP280_H
