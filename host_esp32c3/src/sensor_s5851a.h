// ============================================================================
//  sensor_s5851a.h  -  S-5851A 温度 (ABLIC, I2C / LM75互換)
//  対応 stock ライブラリが無いため自前(最小 Wire 直叩き)。ADR で 0x48..0x4F。
// ============================================================================
#ifndef WLB_SENSOR_S5851A_H
#define WLB_SENSOR_S5851A_H
#include "config.h"
#ifdef WLB_SENSOR_S5851A

#include "env_sensor.h"

class SensorS5851A : public IEnvSensor {
public:
  explicit SensorS5851A(uint8_t addr = WLB_ADDR_S5851A) : _addr(addr) {}
  bool begin() override;
  bool read(EnvReading& out) override;
  const char* name() const override { return "S-5851A"; }
  uint8_t caps() const override { return CAP_TEMP; }
private:
  uint8_t _addr;
};

#endif
#endif // WLB_SENSOR_S5851A_H
