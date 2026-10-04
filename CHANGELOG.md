# Changelog

All notable changes to this project are documented here. Versions follow
[Semantic Versioning](https://semver.org/).

## [0.1.0] - 2026-10-04

First public release.

- Up to two Bambu Lab printers over LAN MQTT (TLS 8883, access code); printers may stay on Bambu Cloud.
- Dual-nozzle printers (X2D, H2D): both nozzle temperatures, active nozzle and its loaded tray; bed/chamber from the packed `device.*` fields; AMS HT units.
- Pages: two-printer overview, status (progress, ETA, layer, stage), temperatures/fans, AMS filament colours, HMS/health, system.
- Alerts with sound and a pulsing ring for finished, paused (e.g. filament runout) and failed prints, HMS faults, first layer done.
- Printer actions from the menu: pause/resume, stop (with confirmation), speed profile, chamber light, refresh.
- Controls: press the screen for the menu, double-press to switch printer, hold for home, knob to scroll, swipes.
- Auto-detects both T-Encoder-Pro panel revisions (SH8601 + CHSC5816, CO5300 + CST816).
- Qwiic sensors: AHT20, SHT3x/SHT4x, BME280/BMP280 (room climate); BH1750, VEML7700 (auto-brightness).
- AMOLED care: idle dimming, screen-off, pixel shift.
- Setup: captive portal with on-screen QR code, web settings page, `/api/status` JSON.
- Updates: ArduinoOTA (`pio run -e t-encoder-pro-ota -t upload`) and browser upload at `/update`.
- Fixed: rapid knob turns could crash LVGL 8.3.11's `lv_scr_load_anim()` and reboot the device.
