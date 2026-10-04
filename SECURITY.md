# Security

## Things to know

- **Printer access codes and your Wi-Fi password are stored unencrypted** in the ESP32's NVS
  flash. Anyone with physical access to the device can read them with a USB cable.
- **The web settings page has no password by default.** Anyone on your LAN can change settings or
  upload firmware. Set a password under *Security* on the settings page; it is also used for
  ArduinoOTA uploads (`upload_flags = --auth=<password>`). Basic auth over plain HTTP is not
  encrypted, so treat your LAN as the trust boundary.
- The setup access point (`BambuMonitor-XXXX`) is open. It only runs on first boot, when Wi-Fi
  can't connect shortly after boot, or when you start it from the menu or by holding the knob 8 s.
- The printer's MQTT TLS certificate is self-signed and is not verified.
- Saved passwords and access codes are never sent back to the browser.

## Reporting a vulnerability

Please use GitHub's private vulnerability reporting (*Security → Report a vulnerability*) rather
than a public issue.
