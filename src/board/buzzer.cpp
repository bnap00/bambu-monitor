#include "buzzer.h"
#include "board_pins.h"

struct Note
{
    uint16_t hz; // 0 = rest
    uint16_t ms;
};

#define END {0, 0}

static const Note mTick[] = {{4200, 4}, END};
static const Note mClick[] = {{2600, 12}, END};
static const Note mBoot[] = {{1319, 70}, {1568, 70}, {2093, 110}, END};
static const Note mConnected[] = {{1760, 50}, {0, 30}, {2349, 70}, END};
static const Note mFirstLayer[] = {{2093, 80}, {0, 60}, {2093, 80}, END};
static const Note mFinished[] = {{1047, 120}, {1319, 120}, {1568, 120}, {2093, 260}, {0, 80},
                                 {1568, 120}, {2093, 380}, END};
static const Note mPaused[] = {{1568, 180}, {0, 80}, {1175, 260}, END};
static const Note mError[] = {{2794, 160}, {0, 70}, {2794, 160}, {0, 70}, {2794, 160}, {0, 250},
                              {2217, 400}, END};

static const Note *current = nullptr;
static uint8_t idx = 0;
static uint32_t noteEndMs = 0;
static uint8_t vol = 2;
static bool quiet = false;

static uint8_t dutyForVolume()
{
    // Passive speaker behind a transistor: lower duty cycle = quieter
    static const uint8_t duty[] = {0, 6, 32, 128};
    return duty[min<uint8_t>(vol, 3)];
}

static void startNote(const Note &n)
{
    if (n.hz == 0)
    {
        ledcWrite(PIN_BUZZER, 0);
    }
    else
    {
        ledcChangeFrequency(PIN_BUZZER, n.hz, 8);
        ledcWrite(PIN_BUZZER, dutyForVolume());
    }
    noteEndMs = millis() + n.ms;
}

void buzzerInit()
{
    ledcAttach(PIN_BUZZER, 2000, 8);
    ledcWrite(PIN_BUZZER, 0);
}

void buzzerSetVolume(uint8_t v)
{
    vol = v;
}

void buzzerSetQuiet(bool q)
{
    quiet = q;
}

void buzzerPlay(Sound s, bool force)
{
    if (vol == 0)
        return;
    if (quiet && !force)
        return;
    // Don't let UI ticks interrupt an alert melody
    if (current && (s == Sound::Tick || s == Sound::Click) && current != mTick && current != mClick)
        return;

    switch (s)
    {
    case Sound::Tick: current = mTick; break;
    case Sound::Click: current = mClick; break;
    case Sound::Boot: current = mBoot; break;
    case Sound::Connected: current = mConnected; break;
    case Sound::FirstLayer: current = mFirstLayer; break;
    case Sound::Finished: current = mFinished; break;
    case Sound::Paused: current = mPaused; break;
    case Sound::Error: current = mError; break;
    }
    idx = 0;
    startNote(current[0]);
}

void buzzerStop()
{
    current = nullptr;
    ledcWrite(PIN_BUZZER, 0);
}

bool buzzerBusy()
{
    return current != nullptr;
}

void buzzerLoop()
{
    if (!current || (int32_t)(millis() - noteEndMs) < 0)
        return;
    idx++;
    if (current[idx].ms == 0)
    {
        buzzerStop();
        return;
    }
    startNote(current[idx]);
}
