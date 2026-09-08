// ============================================================================
//  sensor_sht3x.h  -  SHT35 温湿度 (温度補正用リファレンス, Adafruit_SHT31)
//  SHT30/31/35 共通。SHT35 は高確度グレード。I2C 0x44 / 0x45。
// ============================================================================
#ifndef WLB_SENSOR_SHT3X_H
#define WLB_SENSOR_SHT3X_H
#include "config.h"
#ifdef WLB_SENSOR_SHT35

#include "env_sensor.h"
#include <Adafruit_SHT31.h>

class SensorSHT3x : public IEnvSensor {
public:
  explicit SensorSHT3x(uint8_t addr = WLB_ADDR_SHT35) : _addr(addr) {}
  bool begin() override;
  bool read(EnvReading& out) override;
  const char* name() const override { return "SHT35"; }
  uint8_t caps() const override { return CAP_TEMP | CAP_HUM; }
private:
  uint8_t _addr;
  Adafruit_SHT31 _dev;
};

#endif
#endif // WLB_SENSOR_SHT3X_H
