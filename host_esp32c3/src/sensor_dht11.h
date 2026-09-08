// ============================================================================
//  sensor_dht11.h  -  DHT11 温湿度 (1線シリアル, GPIO, DHT sensor library)
//  読取のブロック(~25ms/2秒周期)は IOタスクが吸収。分解能は粗い(温度1°C/湿度1%RH)。
// ============================================================================
#ifndef WLB_SENSOR_DHT11_H
#define WLB_SENSOR_DHT11_H
#include "config.h"
#ifdef WLB_SENSOR_DHT11

#include "env_sensor.h"
#include <DHT.h>

class SensorDHT11 : public IEnvSensor {
public:
  explicit SensorDHT11(uint8_t pin = WLB_PIN_DHT11) : _pin(pin), _dev(pin, DHT11) {}
  bool begin() override;
  bool read(EnvReading& out) override;
  const char* name() const override { return "DHT11"; }
  uint8_t caps() const override { return CAP_TEMP | CAP_HUM; }
private:
  uint8_t _pin;
  DHT     _dev;
};

#endif
#endif // WLB_SENSOR_DHT11_H
