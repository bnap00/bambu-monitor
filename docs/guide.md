# Bambu Monitor guide

Everything the [README](../README.md) skips.

- [Hardware](#hardware)
- [Printer compatibility](#printer-compatibility)
- [Installing](#installing)
- [First-time setup](#first-time-setup)
- [Controls](#controls)
- [Screens](#screens)
- [Alerts and sound](#alerts-and-sound)
- [Screen brightness and burn-in](#screen-brightness-and-burn-in)
- [Qwiic sensors](#qwiic-sensors)
- [Updating](#updating)
- [Status API](#status-api)
- [Troubleshooting](#troubleshooting)
- [How the code is laid out](#how-the-code-is-laid-out)
- [Roadmap](#roadmap)

## Hardware

[LilyGO T-Encoder-Pro](https://lilygo.cc/products/t-encoder-pro): ESP32-S3R8, 16 MB flash,
8 MB PSRAM, 1.2" 390×390 round AMOLED with touch, rotary knob whose push switch is the whole
screen, a small speaker and a Qwiic port.

LilyGO shipped two panel revisions. Both are detected at boot (the System page and
`/api/status` show which one you have):

| Panel | Touch | Notes |
|---|---|---|
| SH8601 | CHSC5816 | earlier units |
| CO5300 | CST816 | later units |

How each part is used:

| Hardware | Used for |
|---|---|
| AMOLED | pages, progress rings, alerts, setup QR codes |
| Touch | swipes, tapping a printer on the overview, dragging the brightness arc |
| Knob | page / menu scrolling, adjusting values |
| Push switch (GPIO0) | menu, switch printer, home, dismiss alerts |
| Speaker (GPIO17) | alert melodies and knob ticks |
| Qwiic (SDA 16, SCL 15) | optional climate and light sensors |
| Wi-Fi | LAN MQTT, settings page, OTA, NTP clock |

GPIO18 is labelled "LED" on the schematic but isn't populated on V1.0 boards.

## Printer compatibility

The monitor uses each printer's local MQTT broker (port 8883, user `bblp`, password = LAN
access code). That works while the printer stays connected to Bambu Cloud.

| Printer | Status |
|---|---|
| A1 | Tested on hardware |
| X2D | Tested on hardware (status). Dual nozzle, packed temperatures and AMS HT are covered by the parser test |
| A1 mini, P1P, P1S, P2S, X1C, X1E, H2D, H2S, H2C | Expected to work, same protocol. [Reports welcome](../.github/ISSUE_TEMPLATE/printer_support.md) |

**Control commands** (pause, resume, stop, speed, light) may be rejected by newer printer
firmware unless *LAN Only Mode* and *Developer Mode* are enabled on the printer. Monitoring
works either way.

## Installing

**Browser:** open <https://bnap00.github.io/bambu-monitor/> in Chrome or Edge, plug the device
in with a USB-C *data* cable and click *Install*.

**esptool:** download `bambu-monitor-<version>-factory.bin` from the
[releases](https://github.com/bnap00/bambu-monitor/releases) and run

```bash
esptool.py --chip esp32s3 write_flash 0x0 bambu-monitor-<version>-factory.bin
```

**From source:**

```bash
pip install platformio
git clone https://github.com/bnap00/bambu-monitor && cd bambu-monitor
pio run -e t-encoder-pro -t upload
```

If the serial port doesn't show up, hold the knob down while plugging the cable in. That
starts the ESP32's download mode.

To skip the setup portal during development, copy `include/config_local.example.h` to
`include/config_local.h` and fill in Wi-Fi and printer details before building.

## First-time setup

1. After flashing, the screen shows a QR code. Scan it, or join the Wi-Fi network
   `BambuMonitor-XXXX`.
2. The settings page opens on its own (otherwise go to `http://192.168.4.1`).
3. Enter your Wi-Fi details and, for each printer:
   - **IP address** and **LAN access code**: on the printer, *Settings → WLAN / Network*
   - **Serial number**: *Settings → Device*, or *Bambu Studio → Device*
4. Save. The monitor restarts and connects.

Give the printers and the monitor fixed IPs (a DHCP reservation in your router). The settings
page lives at `http://bambu-monitor.local/` or the monitor's IP; the System page shows both
plus a QR code. To get back into setup mode: *menu → Device → Wi-Fi setup*, or hold the knob
for 8 seconds.

## Controls

The whole screen is the knob's push button.

| Do this | Result |
|---|---|
| Turn the knob | next / previous page; scroll menus; change values |
| Press the screen | menu for the current page |
| Double-press | same page, other printer |
| Hold (0.7 s) | back to the overview |
| Hold (8 s) | Wi-Fi setup mode |
| Swipe left / right | next / previous page |
| Swipe up / down | other printer |
| Tap a printer on the overview | open it |
| Any press during an alert | dismiss it |

While the switch is pressed, touch is ignored, so a press never also counts as a tap.

**Printer menu:** pause / resume, speed (silent → ludicrous), stop (asks again), chamber light,
refresh, and *Device* (brightness, volume, knob clicks, test alert, Wi-Fi setup, restart).

## Screens

- **Overview** (with two printers): one ring per printer, percent, time left, clock, room climate.
- **Status:** progress ring, time left, ETA, layer, current stage (e.g. *Filament runout*), job name.
- **Temperatures:** nozzle and bed with target bars, chamber, fans, speed profile, printer Wi-Fi.
  Dual-nozzle printers show the active nozzle large and the other one below it.
- **Filament:** AMS trays in their real colours with type and remaining %, the active tray
  outlined, AMS humidity and temperature, AMS HT units, external spool.
- **Health:** HMS codes with severity, print error code, printer IP.
- **System:** clock, Wi-Fi, sensors, firmware version, QR code to the settings page.

Colours: green printing · blue preparing · amber paused · teal finished · red failed/error ·
grey idle/offline.

## Alerts and sound

| Event | Alert |
|---|---|
| Print finished | teal screen + melody |
| Paused (runout, user, error) | amber screen + melody, with the reason |
| Failed / serious HMS fault / print error | red screen + alarm, ignores quiet hours |
| First layer done | short chime + toast |
| Printer connected / offline | toast |

Unacknowledged alerts repeat every 2 minutes (configurable). During quiet hours
(23:00–07:00 by default) only failures make sound. Volume has four levels including mute.

## Screen brightness and burn-in

- Full brightness while you use it, dimmed after 60 s.
- Stays dimmed while a printer is busy; turns off after 15 minutes when everything is idle.
- Alerts and state changes wake it. The first touch or knob turn on a dark screen only wakes it.
- All content moves a few pixels every 90 seconds to spread OLED wear.

All timings are on the settings page.

## Qwiic sensors

Plug in any of these; they're detected automatically, even after boot:

| Sensor | Shows |
|---|---|
| AHT20 / AHT21, SHT3x, SHT4x, BME280 | room temperature and humidity on the overview |
| BMP280 | room temperature (and pressure in the API) |
| BH1750, VEML7700 | ambient light, drives auto-brightness |

## Updating

- **Over Wi-Fi from source:**
  `pio run -e t-encoder-pro-ota -t upload --upload-port <monitor-ip>`
  (`bambu-monitor.local` works too if mDNS reaches your machine)
- **From a browser:** open `http://<monitor-ip>/update` and upload
  `bambu-monitor-<version>-ota.bin` from a release.

The screen shows a progress ring during updates. If you set a web password, OTA needs it too:
add `upload_flags = --auth=<password>` to the `t-encoder-pro-ota` environment.

## Status API

`GET /api/status` returns JSON: firmware version, uptime, Wi-Fi signal, panel type, free memory,
Qwiic readings, and for each printer its link state, print state, stage, progress, remaining
time, layers, job name, temperatures (both nozzles on dual-nozzle printers) and HMS count.

## Troubleshooting

| Symptom | Try |
|---|---|
| Printer shows "Wrong access code" | Re-enter the code shown on the printer screen. If it's correct, enable LAN Only Mode on that printer |
| Printer stays "Offline" | Check the IP (use a DHCP reservation); the printer and monitor must be on the same network |
| Pause/stop does nothing | Printer firmware rejects LAN control; enable LAN Only + Developer Mode |
| `bambu-monitor.local` doesn't resolve | Use the IP from the System page |
| No serial port when flashing | Use a data cable; hold the knob while plugging in |
| Settings page asks for a password you forgot | Hold the knob 8 s for setup mode. The setup network doesn't ask for it |

For bug reports, include the `/api/status` output (see the
[issue template](../.github/ISSUE_TEMPLATE/bug_report.md)).

## How the code is laid out

```
include/board_pins.h        pin map (from the V1.0 schematic)
include/lv_conf.h           LVGL 8.3 config (PSRAM heap)
src/board/                  display + touch (both revisions), knob, speaker, Qwiic sensors
src/printer/bambu.*         one MQTT connection per printer, commands
src/printer/bambu_parse.*   report parsing (delta merge, dual nozzle, AMS HT)
src/net/                    Wi-Fi, captive portal, settings page, OTA, NVS settings
src/ui/ui.*                 LVGL pages, menus, alerts
src/main.cpp                alert rules, power management, input routing
test/parse/                 parser unit test (runs on a PC)
test/sim/                   renders the real UI on a PC + fast-knob stress test
lib/Arduino_GFX/            vendored LilyGO build of Arduino_GFX (see VENDORED.md)
```

More in [CONTRIBUTING.md](../CONTRIBUTING.md).

## Roadmap

- Camera snapshots on A1 / P1 (JPEG stream on port 6000)
- More than two printers
- Per-nozzle filament view on dual-nozzle printers
