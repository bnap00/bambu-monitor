# Contributing

Thanks for helping! Bug reports, printer-compatibility reports and pull requests are welcome.

## Building

```bash
pip install platformio
pio run -e t-encoder-pro                 # build
pio run -e t-encoder-pro -t upload       # flash over USB
pio run -e t-encoder-pro-ota -t upload --upload-port <device-ip>   # flash over Wi-Fi
pio device monitor                       # serial log (115200)
```

Optional: copy `include/config_local.example.h` to `include/config_local.h` (gitignored)
to bake in Wi-Fi and printer settings for development.

## UI work without hardware

`test/sim` compiles the real `src/ui/ui.cpp` against LVGL on your PC with fake printer data:

```bash
pio run -e t-encoder-pro      # once, fetches LVGL
test/sim/build.sh
cd test/sim && ./sim && python3 sheets.py    # screenshots in out/
ASAN_OPTIONS=detect_leaks=0 ./stress         # fast-knob regression test
```

Please attach before/after screenshots for UI changes.

## Tests

There's no CI, so please run these before opening a pull request:

```bash
pio run -e t-encoder-pro          # firmware builds
test/parse/run.sh                 # MQTT report parser (A1 deltas, X2D dual nozzle, AMS HT)
test/sim/build.sh && (cd test/sim && ./sim && ASAN_OPTIONS=detect_leaks=0 ./stress)
```

## Releasing (maintainers)

`scripts/release.sh 0.2.0` builds, runs the parser test, tags, creates the GitHub release with
the factory/OTA images and updates the browser flasher on the `gh-pages` branch. Add the
version's section to `CHANGELOG.md` first; its text becomes the release notes.

## Printer support

Bambu printers differ in what they report (P1/A1 send deltas; X1/H2/X2 send full reports with
some fields in new places). If something shows `--` or looks wrong for your model, please open
an issue with your printer model and firmware version, and if you can, a captured report:

```bash
# needs mosquitto-clients; the access code is on the printer under Settings > WLAN
mosquitto_sub -h <printer-ip> -p 8883 --insecure --cafile /etc/ssl/certs/ca-certificates.crt \
  -u bblp -P <access-code> -t 'device/<serial>/report' -C 3
```

Remove your serial number before posting.

## Code style

- Match the surrounding code: 4-space indent, braces on their own lines, short focused comments.
- Keep the main loop non-blocking; network work lives in the `bambu` task on core 0.
- Don't call `lv_scr_load_anim()` with a non-zero time (see the comment in `showPage()`).
- Report parsing lives in `src/printer/bambu_parse.cpp`; add a case to `test/parse` when you
  teach it a new printer's fields.
