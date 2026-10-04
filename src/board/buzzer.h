#pragma once
#include <Arduino.h>

enum class Sound : uint8_t
{
    Tick,       // knob detent
    Click,      // button / touch confirm
    Boot,
    Connected,  // printer came online
    FirstLayer, // first layer complete
    Finished,   // print finished
    Paused,     // print paused (runout, user, etc.)
    Error,      // failure / HMS fault
};

void buzzerInit();
void buzzerLoop(); // advances non-blocking melodies

// volume 0..3; 0 mutes everything
void buzzerSetVolume(uint8_t volume);

// `force` ignores quiet hours (used for errors)
void buzzerPlay(Sound s, bool force = false);
void buzzerStop();
bool buzzerBusy();

// Set by the app from the clock; when true only forced sounds play.
void buzzerSetQuiet(bool quiet);
