// ============================================================================
//  sensor_ds18b20.h  -  DS18B20 温度 (Maxim 1-Wire, Dallas/OneWire)
//  変換 750ms は waitForConversion(true) でブロック待ち → IOタスクが吸収。
//  データ線に 4.7kΩ プルアップ。温度のみ。
// ============================================================================
#ifndef WLB_SENSOR_DS18B20_H
#define WLB_SENSOR_DS18B20_H
#include "config.h"
#ifdef WLB_SENSOR_DS18B20

#include "env_sensor.h"
#include <OneWire.h>
#include <DallasTemperature.h>

class SensorDS18B20 : public IEnvSensor {
public:
  explicit SensorDS18B20(uint8_t pin = WLB_PIN_DS18B20)
    : _pin(pin), _ow(pin), _dev(&_ow) {}
  bool begin() override;
  bool read(EnvReading& out) override;
  const char* name() const override { return "DS18B20"; }
  uint8_t caps() const override { return CAP_TEMP; }
private:
  uint8_t           _pin;
  OneWire           _ow;
  DallasTemperature _dev;
};

#endif
#endif // WLB_SENSOR_DS18B20_H
