# Vendored: GFX Library for Arduino 1.5.0 (LilyGO build)

Source: `libraries/Arduino_GFX-1.5.0` from
[Xinyuan-LilyGO/T-Encoder-Pro](https://github.com/Xinyuan-LilyGO/T-Encoder-Pro),
which is [moononournation/Arduino_GFX](https://github.com/moononournation/Arduino_GFX)
1.5.0 with LilyGO's additions (`Arduino_CO5300` / `Arduino_SH8601` tuned for the
T-Encoder-Pro panels, `Display_Brightness()`).

It is vendored rather than pulled from the registry because the panel init sequences
and constructor signatures differ from upstream and are what the hardware was verified with.

Local changes:
- Removed `examples/` and the U8g2 CJK font headers in `src/font/` (~17 MB, unused).
- `src/Arduino_GFX.h`: U8g2 font support is now opt-in via `ARDUINO_GFX_ENABLE_U8G2_FONTS`.

License: BSD, see `LICENSE.txt` (Adafruit GFX heritage).
