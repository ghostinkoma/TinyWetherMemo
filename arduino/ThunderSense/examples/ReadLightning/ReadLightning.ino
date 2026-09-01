/*==============================================================================
  ReadLightning.ino  -  ESP32 example for the ThunderSense (AS3935 + CH32V003)
                        I2C lightning-sensor bridge.

  It: connects to WiFi + NTP (so events get absolute time), polls the bridge,
  and prints lightning / disturber activity plus periodic status & health.

  Wiring:  ESP32 SDA -> bridge PC1,  ESP32 SCL -> bridge PC2,  GND common.
  Library: put the ThunderSense folder in your Arduino/libraries/.
==============================================================================*/
#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include "ThunderSense.h"

// ---- user config ----
const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-pass";
const int   SDA_PIN   = 21;     // adjust for your board
const int   SCL_PIN   = 22;

ThunderSense ts;
uint8_t      lastBootId = 0;
uint32_t     lastStatusMs = 0;

static uint32_t nowEpoch() { time_t t = time(nullptr); return (uint32_t)t; }

void pushTime() {
  if (nowEpoch() > 1600000000UL) {          // NTP has a sane time
    ts.syncTime(nowEpoch(), 0);
    Serial.printf("[host] time synced: %lu\n", (unsigned long)nowEpoch());
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!ts.begin(Wire)) {
    Serial.println("[host] bridge not found - check wiring/address");
  } else {
    TSInfo info; ts.readInfo(info);
    Serial.printf("[host] bridge OK  fw %u.%u  bins=%u\n",
                  info.fwMajor, info.fwMinor, info.fifoDepth);
  }

  // WiFi + NTP so lightning gets absolute timestamps
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(250);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  delay(1500);
  pushTime();

  // Example: in a noisy spot, raise spike rejection to shed disturbers.
  // ts.setSensitivity(/*srej=*/4);
  // ts.configure(/*indoor=*/false);   // outdoor gain profile
}

void loop() {
  // 1) drain events
  TSEvent ev[TS_MAX_EVENTS];
  int n = ts.poll(ev, TS_MAX_EVENTS);
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      if (ev[i].isLightning()) {
        Serial.printf("  LIGHTNING  dist=%u km  energy=%lu  t=%lu.%03u %s\n",
                      ev[i].distanceKm, (unsigned long)ev[i].energy,
                      (unsigned long)ev[i].epoch, ev[i].ms,
                      ev[i].timeValid() ? "" : "(rel)");
      }
    }
  } else if (n < 0) {
    // TS_ERR_CRC/I2C: transient, nothing lost - will retry.
  }

  // 2) periodic status/health (every 5 s)
  if (millis() - lastStatusMs > 5000) {
    lastStatusMs = millis();
    TSStatus st; TSHealth hp;
    if (ts.readStatus(st)) {
      Serial.printf("[stat] state=%u L=%u D=%u N=%u lost=%u%s\n",
                    st.state, st.lightningTotal, st.disturberTotal,
                    st.noiseTotal, st.lostTotal, st.overflow ? " OVF" : "");
      if (st.bootId != lastBootId) {        // bridge rebooted -> re-sync time
        lastBootId = st.bootId; pushTime();
      }
    }
    if (ts.readHealth(hp))
      Serial.printf("[hlth] VDD=%u mV\n", hp.vddMv);
  }

  delay(300);   // poll cadence (bridge keeps the strongest 32 between polls)
}
