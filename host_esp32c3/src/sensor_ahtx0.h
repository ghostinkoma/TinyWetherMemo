// ============================================================================
//  sensor_ahtx0.h  -  AHT25 / AHT20 (AHTx0系) 温湿度  [AHT25=採用]
//  stock ライブラリ Adafruit_AHTX0 に回収 (ブロッキングは IOタスクが吸収)。
//  I2C 0x38 固定。AHT20/21/25 共通。
// ============================================================================
#ifndef WLB_SENSOR_AHTX0_H
#define WLB_SENSOR_AHTX0_H
#include "config.h"
#if defined(WLB_SENSOR_AHT25) || defined(WLB_SENSOR_AHT20)

#include "env_sensor.h"
#include <Adafruit_AHTX0.h>

class SensorAHTx0 : public IEnvSensor {
public:
  explicit SensorAHTx0(const char* nm = "AHT25", uint8_t addr = WLB_ADDR_AHT)
    : _name(nm), _addr(addr) {}
  bool begin() override;
  bool read(EnvReading& out) override;
  const char* name() const override { return _name; }
  uint8_t caps() const override { return CAP_TEMP | CAP_HUM; }
private:
  const char* _name;
  uint8_t     _addr;
  Adafruit_AHTX0 _dev;
};

#endif
#endif // WLB_SENSOR_AHTX0_H
