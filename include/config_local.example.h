#pragma once
// Optional: copy to include/config_local.h (gitignored) to bake in defaults so the
// device works right after flashing. Anything saved later from the web UI wins.

#define CFG_WIFI_SSID "my-wifi"
#define CFG_WIFI_PASS "my-wifi-password"
#define CFG_TZ "IST-5:30" // POSIX TZ string

// Printer 1. Settings > WLAN on the printer shows the IP and access code;
// the serial is under Settings > Device (or in Bambu Studio > Device).
#define CFG_P1_NAME "P1S"
#define CFG_P1_HOST "192.168.1.50"
#define CFG_P1_SERIAL "01P00A000000000"
#define CFG_P1_CODE "12345678"

// Printer 2 (remove these lines for a single printer)
#define CFG_P2_NAME "A1"
#define CFG_P2_HOST "192.168.1.51"
#define CFG_P2_SERIAL "03919A000000000"
#define CFG_P2_CODE "87654321"
