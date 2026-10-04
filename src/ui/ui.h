#pragma once
#include <Arduino.h>
#include "board/encoder.h"

enum class AlertKind : uint8_t
{
    Finished,
    Paused,
    Error,
};

void uiInit();
void uiRebuild(); // rebuild page list after printer settings changed
void uiLoop();    // refreshes the visible page (rate-limited internally)

// Physical inputs, already filtered by the app for wake-up handling
void uiOnKnob(int steps);
void uiOnKnobEvent(KnobEvent e);

// Full-screen alert that stays until acknowledged (knob or touch)
void uiShowAlert(int printer, AlertKind kind, const String &title, const String &detail);
bool uiAlertActive();
void uiDismissAlert();

// Small transient message
void uiToast(const String &text, uint32_t rgb = 0x3DDC84);

// AMOLED burn-in mitigation: shift all content by a few pixels
void uiSetPixelShift(int dx, int dy);

// Jump straight to the Wi-Fi / web setup page
void uiShowSetup();

// Ignore touch input until this millis() timestamp (used after waking the screen)
void uiBlockTouchUntil(uint32_t ms);

// Firmware update screen, drawn immediately (the main loop is blocked during OTA).
// pct 0..100, -1 = failed (overlay removed), -2 = progress unknown.
void uiOtaProgress(int pct);
