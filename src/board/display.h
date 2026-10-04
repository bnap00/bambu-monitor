#pragma once
#include <Arduino.h>

enum class PanelType : uint8_t
{
    Unknown,
    SH8601_CHSC5816, // DXQ120MYB2416A (first batch)
    CO5300_CST816,   // TFD12MASBCTB4_V0_07 (later batch)
};

// Powers the panel, detects which panel revision is fitted (via the touch chip),
// and registers the LVGL display + touch input devices.
void displayInit(bool rotate180);

// Call from the UI loop; drives brightness fades.
void displayLoop();

// Brightness 0..255, faded smoothly. 0 puts the panel to sleep.
void displaySetBrightness(uint8_t level);
uint8_t displayGetBrightness();
// Apply brightness immediately (no fade), waking the panel; for when the loop is blocked.
void displayForceBrightness(uint8_t level);
bool displayIsOn();

PanelType displayPanelType();
const char *displayPanelName();

// True while a finger is on the glass (raw, independent of LVGL).
bool touchIsPressed();
uint32_t touchLastActivityMs();

// Ignore touch until millis() reaches `ms` (0 = unblock). LVGL sees "released";
// a press in progress is cancelled without producing a click.
void touchBlockUntil(uint32_t ms);
bool touchBlocked();
