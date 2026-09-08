// ============================================================================
//  sensors.cpp  -  環境センサ ハブ (詳細は sensors.h)
// ============================================================================
#include "sensors.h"
#include <Wire.h>

#include "sensor_ahtx0.h"
#include "sensor_bmp280.h"
#include "sensor_bme280.h"
#include "sensor_bme680.h"
#include "sensor_sht3x.h"
#include "sensor_dht11.h"
#include "sensor_ds18b20.h"
#include "sensor_s5851a.h"

// --- 静的インスタンス (有効なものだけ)。登録順 = 合成の優先順(採用が先頭) ----
#ifdef WLB_SENSOR_AHT25
  static SensorAHTx0  s_aht25("AHT25", WLB_ADDR_AHT);
#endif
#ifdef WLB_SENSOR_AHT20
  static SensorAHTx0  s_aht20("AHT20", WLB_ADDR_AHT);
#endif
#ifdef WLB_SENSOR_BMP280
  static SensorBMP280 s_bmp280;
#endif
#ifdef WLB_SENSOR_BME280
  static SensorBME280 s_bme280;
#endif
#ifdef WLB_SENSOR_BME680
  static SensorBME680 s_bme680;
#endif
#ifdef WLB_SENSOR_SHT35
  static SensorSHT3x  s_sht35;
#endif
#ifdef WLB_SENSOR_DS18B20
  static SensorDS18B20 s_ds18b20;
#endif
#ifdef WLB_SENSOR_S5851A
  static SensorS5851A s_s5851a;
#endif
#ifdef WLB_SENSOR_DHT11
  static SensorDHT11  s_dht11;
#endif

void EnvSensors::add(IEnvSensor* s) {
  if (_n < WLB_MAX_SENSORS) _list[_n++] = s;
}

bool EnvSensors::begin(uint8_t sda, uint8_t scl) {
  Wire.begin(sda, scl);
  Wire.setClock(WLB_I2C_HZ);

#ifdef WLB_SENSOR_AHT25
  add(&s_aht25);
#endif
#ifdef WLB_SENSOR_AHT20
  add(&s_aht20);
#endif
#ifdef WLB_SENSOR_BMP280
  add(&s_bmp280);
#endif
#ifdef WLB_SENSOR_BME280
  add(&s_bme280);
#endif
#ifdef WLB_SENSOR_BME680
  add(&s_bme680);
#endif
#ifdef WLB_SENSOR_SHT35
  add(&s_sht35);
#endif
#ifdef WLB_SENSOR_DS18B20
  add(&s_ds18b20);
#endif
#ifdef WLB_SENSOR_S5851A
  add(&s_s5851a);
#endif
#ifdef WLB_SENSOR_DHT11
  add(&s_dht11);
#endif

  bool any = false;
  for (uint8_t i = 0; i < _n; i++)
    if (_list[i]->begin()) any = true;
  return any;
}

uint8_t EnvSensors::sample(SensorSample* out, uint8_t maxN) {
  uint8_t k = 0;
  for (uint8_t i = 0; i < _n && k < maxN; i++) {
    if (!_list[i]->present()) continue;
    out[k].name = _list[i]->name();
    out[k].caps = _list[i]->caps();
    _list[i]->read(out[k].r);           // ブロック可 (IOタスク)
    k++;
  }
  _lastN = (k < WLB_MAX_SENSORS) ? k : WLB_MAX_SENSORS;
  for (uint8_t i = 0; i < _lastN; i++) _last[i] = out[i];
  return k;
}

EnvReading EnvSensors::merged() const {
  EnvReading m;
  for (uint8_t i = 0; i < _lastN; i++) {
    const EnvReading& r = _last[i].r;
    if (!r.ok) continue;
    if (!m.tValid && r.tValid) { m.tempC   = r.tempC;   m.tValid = true; }
    if (!m.hValid && r.hValid) { m.rh      = r.rh;      m.hValid = true; }
    if (!m.pValid && r.pValid) { m.presHpa = r.presHpa; m.pValid = true; }
    if (!m.gValid && r.gValid) { m.gasKohm = r.gasKohm; m.gValid = true; }
  }
  m.ok = m.tValid || m.hValid || m.pValid;
  return m;
}

uint8_t EnvSensors::retryAbsent() {
  uint8_t present = 0;
  for (uint8_t i = 0; i < _n; i++) {
    if (!_list[i]->present()) _list[i]->begin();   // 再init (I2C回復後に拾える)
    if (_list[i]->present()) present++;
  }
  return present;
}

IEnvSensor* EnvSensors::byName(const char* nm) const {
  for (uint8_t i = 0; i < _n; i++)
    if (strcmp(_list[i]->name(), nm) == 0) return _list[i];
  return nullptr;
}
