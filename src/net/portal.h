#pragma once
#include <Arduino.h>

enum class NetMode : uint8_t
{
    Connecting, // STA, waiting for an IP
    Online,     // STA connected
    SetupAP,    // access point + captive portal for first-time setup
};

void netBegin();
void netLoop();

NetMode netMode();
String netIp();     // STA IP, or AP IP in setup mode
String netApSsid(); // setup AP name
bool netTimeValid();

// Starts the setup access point (keeps STA running if configured).
void netStartSetupAp();

// Set when the web UI changed settings that the UI/buzzer should pick up.
bool netTakeSettingsChanged();
