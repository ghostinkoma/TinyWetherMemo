# TinyWetherMemo

*English (this file) · [日本語 → README_JP.md](README_JP.md)*

An **AS3935 lightning sensor → CH32V003 → I2C bridge**, plus a host application and library for an upstream ESP32.

A **CH32V003J4M6 (SOP-8)** receives the Akizuki **AE-AS3935** (AMS AS3935 Franklin Lightning Sensor), **captures lightning / disturber events in real time, stamps them with an absolute time, keeps the strongest 32**, and serves them to the upstream bus as an **I2C slave**. The host just "polls and reads".

> The sensor itself is a "500 kHz-tuned AM receiver + envelope detector + built-in decision engine". It does not output a raw waveform — only the **classification result** (distance, energy) for lightning / disturbers. This bridge isolates, time-stamps and buffers that into an easy-to-use form.

---

## Features

- **Isolation bridge**: confines the finicky AS3935 I2C to a **local SW-I2C**, protecting the upstream bus (shared with temp/humidity sensors, etc.).
- **Event-driven capture**: EXTI (top priority) fixes the event time in µs → register read after a 2 ms settle → binning (defense-in-depth L1–L4).
- **Absolute timestamp**: the CH32V003 has no RTC → a SysTick soft clock is aligned to the ESP32's NTP.
- **Loss-safe delivery**: fixed 389 B bundle + **CRC16**, **non-destructive read + (gen, crc)-qualified CLEAR** (deleted only after the read verifies).
- **Bad-weather handling**: when full, **keeps the top 32 by energy** (weak strikes replaced, no sort) + monotonic counters preserve totals.
- **Triple buffer**: fill / serve / background-clear are separated (clear erases CRC first).
- **Full sensitivity / calibration control from the host I2C**: AFE (indoor/outdoor), noise floor, WDTH, SREJ, MIN_NUM, arbitrary register R/W, LCO recalibration (with result polling).
- **Flash-stored calibration**: the LCO tuning value (TUN_CAP) is saved to an option byte → fast start next boot.
- **Fail-safe**: the upstream bus stays alive even on sensor failure (empty bundle + FAULT state) / IWDG / timeouts on every SW-I2C loop.
- **Low memory**: with triple/32 + all features in 2 KB SRAM → **RAM 66% / Flash 55%** (DEBUG=0).

---

## Architecture

```
            SW-I2C (master)                   HW-I2C (slave, 0x28)
  AS3935  ───────────────►  CH32V003J4M6  ───────────────►  ESP32 (host, NTP)
   │IRQ ─EXTI(top prio)──►  capture(L1-4) → bin → bundle(triple) ──DMA──► poll
   └ 500 kHz-tuned antenna     │ SysTick clock / IWDG / health(VDD)
                               └ slave survives faults (returns FAULT)
```

Roles: **CH32V003 = capture & hold immediately**, **ESP32 = collect in batches every N seconds**.
e.g. 5 events at 0.2 s spacing within 1 s → fetched together on the next poll.

---

## Repository layout

```
TinyWetherMemo/
├─ README.md / README_JP.md   # this document (EN / JP)
├─ LICENSE                     # custom non-commercial license
├─ TESTLOG.md                  # bench test results (bridge)
├─ Docs/                       # design & spec
│   ├─ README.md               # design rationale
│   ├─ SPEC.md                 # detailed spec
│   ├─ I2C_REFERENCE.md        # I2C command spec
│   ├─ SOP8PinOut.txt
│   └─ TEST_LOG/               # dated on-device test logs
├─ firmware/                   # CH32V003 firmware (ch32fun / PlatformIO)  ★PIO project
│   ├─ *.c / *.h               # modules (config/protocol/capture/bundle/...)
│   ├─ Makefile / platformio.ini / funconfig.h / BUILD.md
├─ host_esp32c3/               # ESP32-C3 host app "WetherLoggerBox"  ★PIO project
│   ├─ README.md / README_JP.md   (EN / JP)
│   ├─ platformio.ini          # env:esp32c3  (pio run / pio run -t upload)
│   ├─ src/                    # main / io_task(RTOS) / sensors / lightning / wifi / oled
│   ├─ lib/ThunderSense/       # bundles this repo's arduino/ host library
│   └─ Docs/                   # ARCHITECTURE / SENSORS / LIGHTNING / ScreenShots
└─ arduino/ThunderSense/       # ESP32 host library (source of the distributable)
    ├─ ThunderSense.h / .cpp   # ← read the .h to learn the API
    └─ examples/ReadLightning/
```
> The Akizuki AE-AS3935 demo material was used only for reference during development and is **not** redistributed here (see the LICENSE third-party notices).

> **Two PlatformIO projects** live side by side: `firmware/` (CH32V003 bridge, `platform=ch32v`) and `host_esp32c3/` (ESP32-C3 logger, `platform=pioarduino`). Run `pio run` / `pio run -t upload` in each folder.

### Host app "WetherLoggerBox" — main features (details → [host_esp32c3/README.md](host_esp32c3/README.md))
- **RTOS**: ioTask owns I2C exclusively (lightning + sensors + calibration); loopTask does WiFi/Web/FS. Hand-off via `g_state` (mutex).
- **Web SPA (7 pages)**: Home / Chart / WiFi / Lightning calibration / Offset / Data / **Settings** — hamburger menu, responsive, dark/light. **Bilingual UI: English default, Japanese selectable** (in Settings). gzip-served, self-scheduling polling.
- **Chart**: live / 1-min / hourly / day / week / month (**Chart.js bundled locally = no CDN, works offline**). Hourly+ come from FS long-term history; hidden series stay hidden across live updates.
- **Auth**: login (SHA-256 + RNG, cookie) + password change; setup SoftAP is WPA2; the stored WiFi password is AES-256 encrypted.
- **WiFi**: **AP and STA are mutually exclusive** (never both up at once — LAN-exposure safety); STA falls back to AP if it cannot connect.
- **HTTPS(443) alongside HTTP(80)** (self-signed EC P-256), via `esp_http_server` (httpd).
- **Data**: CSV to LittleFS (`time,temp,humi,thunder`, NTP time / offline rescue) + long-term history persisted to FS with int16 compression.
- **Reliability / power**: task WDT, mesh BSSID lock + re-roaming, RTC time retention, WiFi modem sleep + 80 MHz CPU.
- Partitions: 4 MB → app 1.375 MB / FS(LittleFS) 2.5 MB / coredump 64 KB.

---

## Hardware (pinout)

CH32V003J4M6 SOP-8 (per datasheet). **5 signals + SWIO** fit exactly.

| Pin | Port | Use |
|---|---|---|
| 1 | PA1 | SW-I2C **SCL** → AS3935 |
| 3 | PA2 | SW-I2C **SDA** → AS3935 |
| 5 | PC1 | HW-I2C1 **SDA** (upstream bus) |
| 6 | PC2 | HW-I2C1 **SCL** (upstream bus) |
| 7 | PC4 | AS3935 **IRQ** (EXTI / also T1CH4 for calibration) |
| 8 | PD1 | SWIO (programming only) |
| 2 / 4 | VSS / VDD | GND / 3.3 V |

- The AS3935 module has a 10 kΩ pull-up (SW-I2C side). No external crystal → internal HSI.
- Zero spare GPIO (consider TSSOP20 if you need a hardware power-down line).

---

## Quick start

### 1) Firmware (CH32V003)

Dependencies: `riscv-none-elf-gcc`, [ch32fun](https://github.com/cnlohr/ch32fun), WCH-LinkE. Details in [firmware/BUILD.md](firmware/BUILD.md).

```bash
# Makefile (verified)
cd firmware
make main.bin CH32FUN=/path/to/ch32fun/ch32fun     # build
# flashing via minichlink (the OpenOCD bundled with PlatformIO is old on some setups)
/path/to/ch32fun/minichlink/minichlink.exe -w .../main.bin flash -b
```

For PlatformIO, symlink/copy ch32fun as `firmware/ch32fun`, then `pio run` (see [BUILD.md](firmware/BUILD.md)).
For debug output, `#define DEBUG 1` in `config.h` (SDI console, observe with `minichlink -T`).

Clock is **48 MHz by default** (verified on hardware). A 24 MHz (HSI-direct, PLL off) low-power option exists — see `firmware/funconfig.h` / `config.h` `SYS_CLK_HZ`; the measured saving is only ~1.8 mA (7.0 → 5.2 mA), so 48 MHz is kept for stability.

### 2) Host (ESP32)

Copy `arduino/ThunderSense` into your Arduino `libraries/`.

```cpp
#include <Wire.h>
#include "ThunderSense.h"
ThunderSense ts;
void setup(){ Wire.begin(); ts.begin(Wire); ts.syncTime(unixEpoch); }
void loop(){
  TSEvent ev[TS_MAX_EVENTS];
  int n = ts.poll(ev, TS_MAX_EVENTS);          // receive + CRC verify + ACK
  for(int i=0;i<n;i++) if(ev[i].isLightning())
    Serial.printf("%u km, e=%lu\n", ev[i].distanceKm, ev[i].energy);
}
```

---

## Test results (summary)

Verified on hardware. Detailed logs → [TESTLOG.md](TESTLOG.md) and [Docs/TEST_LOG/](Docs/TEST_LOG/).

- Build: SUCCESS with both Makefile and PlatformIO (**Flash 55% / RAM 66%**, triple/32 + all features, DEBUG=0).
- SW-I2C link established, AS3935 responds (address **0x00**, reg0x00=0x24).
- LCO calibration succeeds → TUN_CAP saved to option byte → loaded on next boot (fast start).
- Upstream HW-I2C slave verified: the ESP32-C3 reads status(12 B) / bundle(389 B, poll) with ACK.
- **Detection chain verified**: sparking an electronic lighter reliably increments the event counters through the full path (AS3935 → CH32V003 → I2C → ESP32 → Web). Note that the **classification depends on the source** — a strong/shielded electronic-lighter spark registers as *noise*, not a *disturber* (this is inherent AS3935 behavior, not a fault). See [Docs/TEST_LOG/WetherLoggerBox_2026-09-13.md](Docs/TEST_LOG/WetherLoggerBox_2026-09-13.md).
- **Data integrity under WiFi loss proven**: while the host was briefly unreachable, the CH32V003 kept capturing and the event count continued climbing; nothing was lost on recovery (the bridge buffers + FS persistence are independent of WiFi).

> ⚠ Large bundle(389 B) reads time out (Error263) under the ESP-side default I2C timeout (~50 ms), so the host uses `Wire.setTimeOut(400)` + "read the bundle only when status shows pending>0". Details in [host_esp32c3](host_esp32c3/README.md).

> ⚠ **CH32V003 flashing note**: an abnormal current on a shared power rail can disrupt WCH-LinkE programming (a real case here was traced to a **damaged host-side LDO** — replacing the module fixed both the flashing and the host's boot loops). With healthy power, WCH-LinkE v2.17 + minichlink flashes fine (`minichlink -w firmware.bin flash -b`).

---

## Documentation

- [Docs/README.md](Docs/README.md) — design rationale (why a bridge / scheduling philosophy / defense-in-depth)
- [Docs/SPEC.md](Docs/SPEC.md) — detailed spec (structs, state machines, priorities, DMA, calibration, health, flash, memory budget)
- [Docs/I2C_REFERENCE.md](Docs/I2C_REFERENCE.md) — I2C command spec (all 14 commands)
- [Docs/EVENT_STORAGE_AND_COMMANDS.md](Docs/EVENT_STORAGE_AND_COMMANDS.md) — event-buffer & command-path architecture note (why N_BINS=32; N_BINS ⟂ command path)
- [Docs/TEST_LOG/](Docs/TEST_LOG/) — dated on-device test logs
- [arduino/ThunderSense/ThunderSense.h](arduino/ThunderSense/ThunderSense.h) — host API (self-documenting)

> The deeper Docs (SPEC / ARCHITECTURE / LIGHTNING / SENSORS / I2C_REFERENCE) are currently Japanese; English versions may follow.

---

## License & credits

- Project code: a **custom non-commercial license** ([LICENSE](LICENSE)).
  Free to use / modify / **fork / redistribute for non-commercial purposes**. **No warranty; the author accepts no liability.**
  **On any redistribution or fork (modified or not), you must state the author `ghostinkoma` and this repository URL
  `https://github.com/ghostinkoma/TinyWetherMemo`** (LICENSE §3). Commercial use requires the author's permission. (Not an OSI-approved open-source license.)
- Author: ghostinkoma <ghostinkoma@gmail.com> · Repository: https://github.com/ghostinkoma/TinyWetherMemo
- [ch32fun](https://github.com/cnlohr/ch32fun) (MIT) — CH32V003 runtime (not vendored here)
- LCO calibration procedure adapted from FreqCounter (Martin Nawrath, KHM LAB3, LGPL) — AVR code not ported, only the procedure & target values reused.
- AE-AS3935 module demo material (Akizuki Denshi Tsusho) was used only for reference during development and is **not redistributed here** (obtain it from the original distributor https://akizukidenshi.com/ under their terms).
