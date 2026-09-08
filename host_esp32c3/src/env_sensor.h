// ============================================================================
//  env_sensor.h  -  環境センサ共通インターフェース (同期 / ブロッキング可)
//
//  ■アーキテクチャ (RTOS採用)
//    センサは「IOタスク」上でのみ動く。IOタスクは I2C バスを単独所有し、雷ブリッジ
//    poll と全センサ read を担う。ブロッキング(delay)は FreeRTOS の vTaskDelay として
//    CPU を譲るので、WiFi/TCP タスクや loopTask を止めない。よってセンサドライバは
//    stock ライブラリ(Adafruit 等, 内部で delay するもの)をそのまま使ってよい
//    ＝各ライブラリの内部を非ブロッキング化・検証する保守コストを負わない。
//
//    上位(WiFi/Web)は共有スナップショット(shared_state)を読むだけで、I2C には触れない。
//    →バス競合なし・ロックは状態受け渡しの mutex 1 個のみ。
//
//  begin()/read() は IOタスク内で呼ばれる前提。ブロックしてよい。
// ============================================================================
#ifndef WLB_ENV_SENSOR_H
#define WLB_ENV_SENSOR_H

#include <Arduino.h>
#include "config.h"

// センサが提供する物理量 (ビットフラグ)
enum EnvCap : uint8_t {
  CAP_TEMP = 0x01,   // 温度 [°C]
  CAP_HUM  = 0x02,   // 相対湿度 [%RH]
  CAP_PRES = 0x04,   // 気圧 [hPa]
  CAP_GAS  = 0x08    // ガス抵抗 [kΩ] (BME680)
};

// 1 回の測定結果。*Valid が false のフィールドは無効(未対応/読取失敗)。
struct EnvReading {
  bool  ok      = false;   // 通信自体が成功したか
  float tempC   = NAN;  bool tValid = false;
  float rh      = NAN;  bool hValid = false;
  float presHpa = NAN;  bool pValid = false;
  float gasKohm = NAN;  bool gValid = false;
};

// --- 全センサ共通の抽象基底 (同期) ------------------------------------------
class IEnvSensor {
public:
  virtual ~IEnvSensor() {}

  // 初期化。成功で true。IOタスク内で呼ばれる(ブロック可)。
  virtual bool begin() = 0;

  // 1 回測定して out を埋める。ブロック可(IOタスク内)。out.ok=通信成否。
  virtual bool read(EnvReading& out) = 0;

  virtual const char* name() const = 0;
  virtual uint8_t caps() const = 0;

  bool present() const { return _present; }

protected:
  bool _present = false;
};

#endif // WLB_ENV_SENSOR_H
