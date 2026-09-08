// ============================================================================
//  sensors.h  -  環境センサ ハブ (同期集約, IOタスク上で駆動)
//
//  役割:
//   - I2C を一度だけ初期化し、config.h で有効化したセンサを begin()。
//   - sample() で全 present センサを 1 回ずつ read()(ブロック可)。
//   - merged() で採用構成(AHT25=温湿度 / BMP280=気圧)を優先し 1 本に合成。
//
//  ※ 本ハブは IOタスク内でのみ呼ぶこと(I2C 単独所有)。WiFi/Web 側は触れない。
// ============================================================================
#ifndef WLB_SENSORS_H
#define WLB_SENSORS_H

#include <Arduino.h>
#include "config.h"
#include "env_sensor.h"

#define WLB_MAX_SENSORS 8

struct SensorSample {
  const char* name;
  uint8_t     caps;
  EnvReading  r;
};

class EnvSensors {
public:
  bool begin(uint8_t sda = WLB_I2C_SDA, uint8_t scl = WLB_I2C_SCL);

  // 全 present センサを 1 回ずつ読む(ブロック可)。out[] を埋め、件数を返す。
  uint8_t sample(SensorSample* out, uint8_t maxN);

  // 直近 sample() 結果を採用優先で 1 本に合成。
  EnvReading merged() const;

  // 未検出(present=false)のセンサだけ begin() を再試行 (バス回復後の遅延検出用)。
  // 検出できたセンサ数を返す。
  uint8_t retryAbsent();

  IEnvSensor* byName(const char* nm) const;
  uint8_t     count() const { return _n; }
  IEnvSensor* at(uint8_t i) const { return (i < _n) ? _list[i] : nullptr; }

private:
  IEnvSensor*  _list[WLB_MAX_SENSORS] = {nullptr};
  uint8_t      _n = 0;
  SensorSample _last[WLB_MAX_SENSORS];
  uint8_t      _lastN = 0;
  void add(IEnvSensor* s);
};

#endif // WLB_SENSORS_H
