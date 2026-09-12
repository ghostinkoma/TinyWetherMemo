# WetherLoggerBox (ESP32-C3 host)

*English (this file) · [日本語 → README_JP.md](README_JP.md)*

A small weather-shelter logger. It measures **temperature / humidity / pressure + lightning** and shows/configures everything from a Web dashboard (a 7-page SPA). Lightning comes from the AS3935 → CH32V003 **ThunderSense** bridge (I2C slave 0x28).

- Spec: [Docs/SPEC.md](Docs/SPEC.md) · Architecture: [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md)
- Sensors: [Docs/SENSORS.md](Docs/SENSORS.md) · Lightning / risk: [Docs/LIGHTNING.md](Docs/LIGHTNING.md)

## Hardware / wiring (shared I2C bus)
| Signal | ESP32-C3 |
|---|---|
| SDA | GPIO8 |
| SCL | GPIO9 |

On the same I2C bus: **AHT20(0x38)** + **BMP280(0x76/0x77)** + **ThunderSense lightning(0x28)** (+ optional OLED 0x3C).
Bring-up at 100 kHz (`config.h WLB_I2C_HZ`). Common GND, 3.3 V required.
> CH32V003 wiring/firmware are in the parent repo: [../firmware](../firmware) and [../README.md](../README.md).

## Build / flash (PlatformIO)
```
pio run                                  # build
pio run -t upload --upload-port COMxx    # flash (port is environment-specific)
```
Platform is **pioarduino (arduino-esp32 core 3.x)**. The ESP32-C3 native USB is unstable for `pio device monitor` (miniterm/pyserial) on Windows, so **verify behavior over the Web** instead.
> ⚠ Before flashing, confirm the target with `esptool read_mac` — never overwrite an unrelated ESP32.
> On Windows, if `pio` is not on PATH, run `%USERPROFILE%\.platformio\penv\Scripts\pio.exe` directly.

## First-time setup (secrets — needed right after clone)
Real credentials / private keys are **not** committed. The following are `.gitignore`d; create them per-machine after cloning (the build still works without them, using placeholders / a public sample).
1. **WiFi credentials**: copy [`src/config.local.h.example`](src/config.local.h.example) to **`src/config.local.h`** and fill in your SSID / password (and `WLB_AP_PASS` if wanted). It is auto-included from `config.h` and overrides the defaults. If absent, you can also enter WiFi from the settings page after boot.
2. **TLS cert/key (HTTPS)**: run `powershell -ExecutionPolicy Bypass -File tools\gen_cert.ps1` to generate a device-unique **`src/cert_pem.h`** (needs openssl). If absent it falls back to the public dummy [`src/cert_pem_sample.h`](src/cert_pem_sample.h) (**that key is public = no protection**; always regenerate).
3. **Default login**: `wether/wether` (config.h `WLB_AUTH_DEFAULT_*`) is a first-boot seed → **change it after logging in**. The SoftAP default pass `wetherbox` (`WLB_AP_PASS`) should also be changed before deployment.

## Web UI layout / structure (reference)
The dashboard is a **single SPA (HTML+CSS+JS combined)**. Layout & delivery:

| Element | Where | Role |
|---|---|---|
| **SPA source** | `PAGE[]` in [`src/web_ui.cpp`](src/web_ui.cpp) (a `R"HTML(...)HTML"` raw string) | **the single source of truth — edit here** |
| **Served data** | `WLB_PAGE_GZ[]` in [`src/web_page_gz.h`](src/web_page_gz.h) | gzip of the source, auto-generated; `handleRoot` serves it with `Content-Encoding: gzip` |
| REST API | `handle*` in `src/web_ui.cpp` | endpoints below |

**Regenerate the gzip (run after editing the HTML)** — keep **UTF-8** to avoid cp932 mojibake:
`powershell -ExecutionPolicy Bypass -File tools\gen_page_gz.ps1` (extracts the `R"HTML(...)HTML"` body, gzips as UTF-8, writes `web_page_gz.h`, and round-trip verifies). Then rebuild.
> Reading with `Get-Content -Raw` (default cp932) mangles Japanese — always use UTF-8.

**UI language**: English by default, Japanese selectable in the **Settings** page (stored in the browser). See `web_ui.cpp` `I18N`.

**REST endpoints**: `/` (gzip SPA) · `/chart.min.js` (**locally bundled Chart.js**; gzip, no CDN) · `/api/auth`·`/api/login`·`/api/logout`·`/api/passwd` (auth) · `/api/now` · `/api/history?scope=` (**live/min/hour/day/week/month**; hourly+ from FS long-term history) · `/api/settings` · `/api/wifi`·`/api/wifi/scan` · `/api/calib` · `/api/offset` · `/api/logcfg` · `/api/csv` (header added only on download) · `/api/logclear`. **Everything except `/`, `/chart.min.js` and the 3 auth endpoints requires login (401).**

## Security (using ESP32 HW security peripherals)
- **Login auth** [`src/auth.cpp`](src/auth.cpp): salted password hash with **SHA-256 (HW)**, salt / 128-bit session token from **HW RNG (`esp_random`)**. Cookie session (24 h sliding). Default `WLB_AUTH_DEFAULT_USER/PASS` = `wether/wether` (first-boot seed, **change after login**). Password change on the Settings/account UI (revokes all sessions). Disable with `WLB_AUTH_ENABLE 0`.
- **Settings password encryption** [`src/settings.cpp`](src/settings.cpp): the WiFi password is stored **AES-256-CBC (HW)** as `passenc=` (no plaintext). Key = SHA-256(STA MAC + fixed salt) = device-specific; IV is random each time. ⚠ Without flash encryption (eFuse) the key is MAC-derivable = obfuscation-grade; true secrecy needs Secure Boot / Flash Encryption (eFuse, physical, irreversible).
- **Setup SoftAP is WPA2** (`WLB_AP_PASS`, ≥8 chars) — prevents unauthenticated nearby reconfiguration.
- **HTTPS (TLS)**: the web server was ported to `esp_http_server` (httpd), running **HTTP(80) and HTTPS(443) together** ([web_httpd.h](src/web_httpd.h) is a WebServer-style shim reusing existing handlers). Cert is **self-signed EC P-256 (ECDSA)** ([cert_pem.h](src/cert_pem.h); TLS via mbedtls/HW). ⚠ Self-signed → the browser warns "not secure" each time; real trust needs your own domain + ACME, or an internal-CA cert.

## WiFi mode (AP or STA — mutually exclusive)
Following AquaController, **AP and STA are never up at the same time** (running both would expose the upstream LAN through the AP). On boot: `staMode=STA` with an SSID → **STA only**; SSID empty / AP selected → **AP only**. If STA cannot connect within `WLB_STA_FALLBACK_MS`, it **falls back to AP** so the device stays reachable for reconfiguration.

## Usage (Web dashboard)
1. If not configured (or in AP mode), a **SoftAP `WetherLogger` (WPA2, default pass `wetherbox`)** comes up → connect and open **http://192.168.4.1/**.
2. On the **WiFi** page, save your SSID / password / mDNS name (default **WetherMemo**) and boot mode = STA → reboot connects as STA (AP is not kept up).
3. Then reach it on the same LAN via **http://WetherMemo.local/** (mDNS) or the assigned IP.

### 7 pages (hamburger menu / responsive / dark-light toggle ◐ / EN·JP)
1. **Home**: temperature / humidity / pressure, last-30-min lightning frequency, **risk** (weighted sum; [LIGHTNING.md](Docs/LIGHTNING.md)).
2. **Chart**: Chart.js time series. **live / 1-min / hourly / day / week / month**. Hourly+ come from FS long-term history (absolute epoch); falls back to a finer resolution when not yet accumulated, so it always draws. **Chart.js is bundled locally, so charts work without internet.** Hidden series stay hidden across live updates.
3. **WiFi**: SSID pick + password, mDNS name, boot mode (AP/STA). In STA it shows the assigned IP and the device's own AP SSID / current mode.
4. **Lightning calibration**: LCO recalibration + result, indoor/outdoor, NF_LEV, WDTH, SREJ, MIN_NUM (per the AE-AS3935 manual). See [Docs/TEST_LOG](../Docs/TEST_LOG/) for a spark-test calibration note.
5. **Temp/Humidity offset**: additive correction on temperature / humidity.
6. **Data / logging**: measurement period (default 5 s), drop-min/max + n-average (n≥4 when dropping), **CSV download**. CSV columns = `time,temp,humi,thunder` (header added only on download). time = NTP datetime, temp = **AHT20 only**, thunder = risk%. BMP280 (pressure) is redundant and not logged to CSV.
7. **Settings**: UI language (English / 日本語).

Settings persist in **LittleFS `/settings.ini`**. Calibration/sensitivity reach the AS3935 via a Web→ioTask queue (I2C stays single-owner).

## Data persistence / time
- **CSV log** (daily-ring, date-named files, time-only rows). Offline (NTP not synced) rows are written as `B<boot-seconds>` and converted to absolute time once NTP locks (`datalog_patch_boottime()`).
- **FS long-term history** [`src/histfs.cpp`](src/histfs.cpp): the per-average value is appended to `/hist.bin` at 15 B/record with **absolute epoch**, so day/week/month charts survive reboots. On boot the tail is restored into the RAM ring (`histfs_seed`). FS writes are **loopTask only**.
- The RAM history ring uses **int16 fixed-point compression (15 B/record)**: temp/humidity ×100, pressure `(hPa-1000)×100`.

## Reliability / power / time
- **WiFi**: at boot, scan and **lock onto the strongest-RSSI BSSID** of the same-SSID mesh. **Mesh re-roaming** (`maybeRoam`): while connected, watch RSSI and only when `< WLB_ROAM_RSSI_TH` (-75 dB) rescan and switch to a BSSID at least `WLB_ROAM_MARGIN` (8 dB) stronger. Feature toggles `WLB_ENABLE_*` (config.h).
- **Task WDT** (`WLB_WDT_TIMEOUT_MS`=30 s): watches io/loop tasks, panics → auto-reboot on hang, saves a coredump.
- **RTC time retention** ([`src/wifi_session.cpp`](src/wifi_session.cpp)): the ESP32-C3 RTC keeps time across a CPU soft reset (WDT reboot etc.). If NTP was synced before, it becomes timeValid immediately after reboot (no waiting for NTP); a real power loss resets to 1970 so it doesn't misfire.
- **Large CSV downloads** `yield()` + feed the WDT every ~8 KB, so WiFi/NTP and the ioTask (sensors) keep running during a download.
- **★ WiFi-loss resilience**: capture/count/FS-logging run over local I2C and are **independent of WiFi**, so a WiFi drop does not lose lightning events (proven on hardware — see [../Docs/TEST_LOG/WetherLoggerBox_2026-09-13.md](../Docs/TEST_LOG/WetherLoggerBox_2026-09-13.md)).
- ⚠ **Debug note**: opening the ESP32-C3 USB serial port resets the C3, so verify a running device over HTTP only.

## Partitions ([partitions.csv](partitions.csv))
From measured build (app ≈ 1.3 MB): **factory(app)=1.375 MB / spiffs(FS)=2.5 MB / coredump=64 KB**. A 1 MB app is too small for the binary. Changing offsets re-formats LittleFS on next boot.

## Implementation status
- **Done**: 7-page SPA, current values / lightning risk, live/1-min/hourly chart + **FS long-term history (day/week/month)**, WiFi (AP/STA/mDNS, **AP-STA exclusive** + STA→AP fallback) + re-roaming, calibration/sensitivity (server-side clamp), offset, measurement frequency, LittleFS persistence, CSV log (NTP time / offline rescue / yield on download), int16 compression, WDT, **RTC time retention**, **login auth (SHA-256/RNG) + password change**, **settings AES encryption**, **SoftAP WPA2**, **Chart.js bundled locally**, **bilingual UI (EN default / JP)**, chart series-visibility kept across updates.
- **Lightning poll (389 B) fix**: the bundle read (CMD_READ_BUNDLE) exceeded the default I2C timeout (~50 ms) → Error263. Fixed with (1) `Wire.setTimeOut(WLB_I2C_TIMEOUT_MS=400)`, (2) read the bundle only when a 12 B status shows `pending>0` (zero large reads while idle). Verified idle-error = 0 on hardware.
- **Power**: ESP32 = WiFi modem sleep (`WIFI_PS_MIN_MODEM`) + 80 MHz CPU (160→80). CH32V003 = 48 MHz default; a 24 MHz HSI-direct option exists (bridge verified working, ~1.8 mA saving — 48 MHz kept for stability).
- **HTTPS(443) alongside HTTP(80)**: httpd port + self-signed EC P-256. Verified on hardware.
- **Not done (separate)**: MySQL/SQL upload, Secure Boot / Flash Encryption (eFuse, physical), true automatic light sleep (needs duty-cycle design).
