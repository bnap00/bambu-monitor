# Third-party software

Bambu Monitor's own code is MIT licensed (see `LICENSE`). Firmware builds include:

| Component | Use | License |
|---|---|---|
| [GFX Library for Arduino](https://github.com/moononournation/Arduino_GFX) 1.5.0, LilyGO build (vendored in `lib/Arduino_GFX`) | AMOLED QSPI drivers (SH8601, CO5300) | BSD, `lib/Arduino_GFX/LICENSE.txt` |
| [LVGL](https://github.com/lvgl/lvgl) 8.3.11 | UI toolkit | MIT |
| Montserrat font (bundled with LVGL) | UI text | SIL Open Font License 1.1 |
| Font Awesome symbols (bundled with LVGL) | UI icons | SIL OFL 1.1 / MIT |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) 7 | MQTT payload parsing | MIT |
| [PubSubClient](https://github.com/knolleary/pubsubclient) 2.8 | MQTT client | MIT |
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) 3.1 | Framework | LGPL-2.1 |
| [ESP-IDF](https://github.com/espressif/esp-idf) 5.3 (via the Arduino core) | SDK, mbedTLS, lwIP, FreeRTOS | Apache-2.0 and component licenses |
| [pioarduino platform-espressif32](https://github.com/pioarduino/platform-espressif32) | Build platform | Apache-2.0 |

The web flasher page loads [ESP Web Tools](https://github.com/esphome/esp-web-tools) (Apache-2.0).

Hardware details (pin map, panel/touch variants) were taken from LilyGO's
[T-Encoder-Pro](https://github.com/Xinyuan-LilyGO/T-Encoder-Pro) schematic and examples.
Bambu Lab MQTT field names follow community documentation such as
[OpenBambuAPI](https://github.com/Doridian/OpenBambuAPI) and
[ha-bambulab](https://github.com/greghesp/ha-bambulab); no code was copied from them.

"Bambu Lab" is a trademark of its owner. This project is not affiliated with or endorsed
by Bambu Lab or LilyGO.
