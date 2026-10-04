#pragma once
#include <Arduino.h>

#define MAX_PRINTERS 2

struct PrinterConfig
{
    bool enabled = false;
    String name;   // shown on screen, e.g. "X1C"
    String host;   // printer IP on the LAN
    String serial; // printer serial number (MQTT topic)
    String code;   // 8-char LAN access code from the printer screen
};

struct Settings
{
    String wifiSsid;
    String wifiPass;
    String hostname = "bambu-monitor";
    String tz = "UTC0"; // POSIX TZ string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3"
    String webPass;     // optional HTTP basic-auth password for the web UI (user "admin")

    PrinterConfig printers[MAX_PRINTERS];

    // Display
    uint8_t brightness = 180;   // active brightness 1..255
    uint8_t dimBrightness = 30; // idle brightness
    uint16_t dimAfterS = 60;    // dim after this many seconds without interaction
    uint16_t offAfterMin = 15;  // screen off after N minutes when no printer is busy (0 = never)
    bool stayOnWhilePrinting = true;
    bool autoBrightness = true; // uses a Qwiic light sensor when one is present
    bool rotate180 = false;

    // Sound
    uint8_t volume = 2;           // 0 = mute .. 3 = loud
    bool knobClicks = true;       // tick sound on knob detents
    bool chimeOnFirstLayer = true;
    uint8_t alarmRepeatMin = 2;   // re-sound unacknowledged alerts every N minutes (0 = once)
    int8_t quietStartHour = 23;   // -1 disables quiet hours
    int8_t quietEndHour = 7;

    bool printersConfigured() const;
};

extern Settings settings;

void settingsLoad();
void settingsSave();
void settingsFactoryReset();
