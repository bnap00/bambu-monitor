// Bambu Monitor for the LilyGO T-Encoder-Pro
// LAN MQTT status for up to two Bambu Lab printers on a 1.2" round AMOLED.
//
// Hardware used:
//   AMOLED (SH8601 or CO5300, auto-detected) .... pages, rings, alerts, QR codes
//   Capacitive touch (CHSC5816 / CST816) ........ swipe pages/printers, tap, long-press menu
//   Rotary encoder + push switch ............... page scroll, menus, value adjust, acknowledge
//   Passive speaker ............................. finish/pause/error melodies, knob ticks
//   Qwiic port .................................. room climate + ambient light auto-brightness
//   Wi-Fi ....................................... LAN MQTT, web config, OTA, NTP
//   PSRAM ....................................... LVGL draw buffers + large MQTT payloads

#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include "board/buzzer.h"
#include "board/display.h"
#include "board/encoder.h"
#include "board/qwiic.h"
#include "net/portal.h"
#include "net/settings.h"
#include "printer/bambu.h"
#include "ui/ui.h"

static uint32_t lastInteractionMs = 0;
static uint32_t lastTouchSeenMs = 0;
static bool wakeTouchBlock = false; // finger that woke the screen is still down
static bool darkTouchBlock = false; // touch blocked because the screen is dark

// ------------------------------------------------------------ alerts

struct Watch
{
    bool baseline = false;
    PrinterState prev;
};
static Watch watch[MAX_PRINTERS];
static AlertKind alertKind = AlertKind::Finished;
static uint32_t alertSoundMs = 0;

static void raise(int idx, AlertKind kind, const String &title, const String &detail)
{
    alertKind = kind;
    alertSoundMs = millis();
    lastInteractionMs = millis(); // wake the screen
    switch (kind)
    {
    case AlertKind::Finished: buzzerPlay(Sound::Finished); break;
    case AlertKind::Paused: buzzerPlay(Sound::Paused); break;
    case AlertKind::Error: buzzerPlay(Sound::Error, true); break;
    }
    uiShowAlert(idx, kind, title, detail);
    Serial.printf("[alert] %s: %s\n", title.c_str(), detail.c_str());
}

static bool hmsKnown(const PrinterState &s, const HmsEntry &h)
{
    for (int i = 0; i < s.hmsCount; i++)
        if (s.hms[i].attr == h.attr && s.hms[i].code == h.code)
            return true;
    return false;
}

static String hmsCode(const HmsEntry &h)
{
    char b[24];
    snprintf(b, sizeof(b), "%04X_%04X_%04X_%04X", (unsigned)(h.attr >> 16), (unsigned)(h.attr & 0xFFFF),
             (unsigned)(h.code >> 16), (unsigned)(h.code & 0xFFFF));
    return b;
}

static void checkPrinter(int idx)
{
    Watch &w = watch[idx];
    PrinterState cur = bambuGet(idx);
    const PrinterState &prev = w.prev;
    String name = bambuName(idx);

    if (cur.link != prev.link)
    {
        if (cur.link == Link::AuthFailed)
            uiToast(name + ": wrong access code", 0xFF5252);
        else if (cur.link == Link::Online && prev.link != Link::Disabled)
            uiToast(name + " connected");
        else if (prev.link == Link::Online && gstateBusy(prev.state))
            uiToast(name + " went offline", 0xFFB300);
    }

    if (cur.link != Link::Online || cur.updateCount == 0 || cur.state == GState::Unknown)
    {
        w.baseline = false;
        w.prev = cur;
        return;
    }
    if (!w.baseline) // first full report after connecting: no alerts for old state
    {
        w.baseline = true;
        w.prev = cur;
        lastInteractionMs = millis();
        buzzerPlay(Sound::Connected);
        return;
    }

    GState a = prev.state, b = cur.state;
    bool alerted = false;
    if (a != b)
    {
        lastInteractionMs = millis();
        if (b == GState::Finish && gstateBusy(a))
        {
            raise(idx, AlertKind::Finished, name + " finished", cur.job);
            alerted = true;
        }
        else if (b == GState::Failed)
        {
            String d = cur.job;
            if (cur.printError)
            {
                char e[24];
                snprintf(e, sizeof(e), "\nError %04X_%04X", (unsigned)(cur.printError >> 16),
                         (unsigned)(cur.printError & 0xFFFF));
                d += e;
            }
            raise(idx, AlertKind::Error, name + " failed", d);
            alerted = true;
        }
        else if (b == GState::Pause && (a == GState::Running || a == GState::Prepare))
        {
            const char *why = stageName(cur.stage);
            raise(idx, AlertKind::Paused, name + " paused", why[0] && cur.stage != 0 ? why : "Print paused");
            alerted = true;
        }
        else if (b == GState::Running && (a == GState::Idle || a == GState::Finish || a == GState::Failed))
        {
            uiToast(name + " started");
        }
    }

    if (!alerted)
    {
        for (int i = 0; i < cur.hmsCount; i++)
        {
            const HmsEntry &h = cur.hms[i];
            if (hmsKnown(prev, h))
                continue;
            int sev = h.code >> 16;
            if (sev <= 2)
            {
                raise(idx, AlertKind::Error, name + " needs attention", "HMS " + hmsCode(h));
                alerted = true;
                break;
            }
            if (sev == 3)
                uiToast(name + ": HMS notice", 0xFFB300);
        }
    }

    if (!alerted && cur.printError && cur.printError != prev.printError && gstateBusy(cur.state))
    {
        char e[24];
        snprintf(e, sizeof(e), "Error %04X_%04X", (unsigned)(cur.printError >> 16),
                 (unsigned)(cur.printError & 0xFFFF));
        raise(idx, AlertKind::Error, name + " error", e);
    }

    if (settings.chimeOnFirstLayer && cur.state == GState::Running && prev.layer <= 1 && cur.layer >= 2 &&
        cur.totalLayers > 1)
    {
        buzzerPlay(Sound::FirstLayer);
        uiToast(name + ": first layer done");
        lastInteractionMs = millis();
    }

    w.prev = cur;
}

static void repeatAlertSound()
{
    if (!uiAlertActive() || settings.alarmRepeatMin == 0)
        return;
    if (millis() - alertSoundMs < settings.alarmRepeatMin * 60000UL)
        return;
    alertSoundMs = millis();
    if (alertKind == AlertKind::Error)
        buzzerPlay(Sound::Error, true);
    else
        buzzerPlay(alertKind == AlertKind::Finished ? Sound::Finished : Sound::Paused);
}

// ------------------------------------------------------------ power / AMOLED care

static uint8_t activeBrightness()
{
    QwiicReadings q = qwiicGet();
    if (!settings.autoBrightness || !q.hasLight || isnan(q.lux))
        return settings.brightness;
    // 0 lx -> 8 %, ~1000 lx and up -> 100 % of the configured brightness
    float f = constrain(log10f(q.lux + 1.0f) / 3.0f, 0.08f, 1.0f);
    return max<uint8_t>(6, settings.brightness * f);
}

static void managePower()
{
    uint32_t now = millis();
    uint32_t idle = now - lastInteractionMs;

    bool anyBusy = false;
    for (int i = 0; i < MAX_PRINTERS; i++)
    {
        PrinterState s = bambuGet(i);
        anyBusy |= s.link == Link::Online && gstateBusy(s.state);
    }

    uint8_t active = activeBrightness();
    uint8_t dim = min(settings.dimBrightness, active);
    uint8_t target;
    if (uiAlertActive())
        target = max<uint8_t>(active, 200);
    else if (idle < settings.dimAfterS * 1000UL)
        target = active;
    else if ((anyBusy && settings.stayOnWhilePrinting) || !settings.printersConfigured())
        target = dim;
    else if (settings.offAfterMin && idle > settings.offAfterMin * 60000UL)
        target = 0;
    else
        target = dim;

    if (target == 0 && displayIsOn())
    {
        uiBlockTouchUntil(now + 0x7FFFFFFF); // touches while dark only wake the screen
        darkTouchBlock = true;
    }
    displaySetBrightness(target);
}

static void pixelShift()
{
    // Slow orbit of a few pixels so static rings/labels don't burn into the OLED
    static const int8_t path[][2] = {{0, 0}, {3, 0}, {3, 3}, {0, 3}, {-3, 3}, {-3, 0}, {-3, -3}, {0, -3}, {3, -3}};
    static uint8_t step = 0;
    step = (step + 1) % (sizeof(path) / sizeof(path[0]));
    uiSetPixelShift(path[step][0], path[step][1]);
}

static void updateQuietHours()
{
    bool quiet = false;
    if (settings.quietStartHour >= 0 && netTimeValid())
    {
        time_t now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        int h = t.tm_hour, a = settings.quietStartHour, b = settings.quietEndHour;
        quiet = a < b ? (h >= a && h < b) : (h >= a || h < b);
    }
    buzzerSetQuiet(quiet);
}

// ------------------------------------------------------------ input

static void handleInput()
{
    uint32_t now = millis();
    int steps = encoderTakeSteps();
    KnobEvent ev = encoderTakeEvent();
    bool dark = !displayIsOn();

    // Touch: woken screen ignores the waking finger until it lifts
    uint32_t t = touchLastActivityMs();
    if (t != lastTouchSeenMs)
    {
        lastTouchSeenMs = t;
        lastInteractionMs = now;
        if (dark)
            wakeTouchBlock = true;
    }
    if (wakeTouchBlock)
    {
        if (touchIsPressed())
            uiBlockTouchUntil(now + 0x7FFFFFFF);
        else
        {
            uiBlockTouchUntil(now + 250);
            wakeTouchBlock = false;
            darkTouchBlock = false;
        }
    }
    else if (!dark && darkTouchBlock) // woken by the knob or an alert
    {
        uiBlockTouchUntil(0);
        darkTouchBlock = false;
    }

    // The whole screen is the knob's push switch: a physical press must never also
    // count as a tap, so touch is cancelled while the switch is down and briefly after.
    if (encoderButtonHeld() && !wakeTouchBlock && !darkTouchBlock)
        uiBlockTouchUntil(now + 350);

    if (steps == 0 && ev == KnobEvent::None)
        return;
    lastInteractionMs = now;
    if (dark && !uiAlertActive())
        return; // first knob action only wakes the screen

    if (ev == KnobEvent::VeryLongPress)
    {
        buzzerPlay(Sound::Click, true);
        netStartSetupAp();
        uiShowSetup();
        uiToast("Setup Wi-Fi: " + netApSsid());
        return;
    }
    uiOnKnob(steps);
    uiOnKnobEvent(ev);
}

// ------------------------------------------------------------ main

void setup()
{
    Serial.begin(115200);
    delay(50);
    Serial.println("\n[boot] Bambu Monitor for T-Encoder-Pro");

    settingsLoad();
    buzzerInit();
    buzzerSetVolume(settings.volume);
    encoderInit();
    displayInit(settings.rotate180);
    uiInit();
    lv_timer_handler();
    displaySetBrightness(settings.brightness);

    qwiicInit();
    netBegin();
    bambuBegin();

    buzzerPlay(Sound::Boot);
    lastInteractionMs = millis();
    Serial.printf("[boot] heap %u, psram %u\n", ESP.getFreeHeap(), ESP.getFreePsram());
}

void loop()
{
    static uint32_t tAlerts = 0, tPower = 0, tShift = 0, tQuiet = 0, tLog = 0;
    uint32_t now = millis();

    handleInput();
    lv_timer_handler();
    displayLoop();
    buzzerLoop();
    netLoop();
    uiLoop();

    if (netTakeSettingsChanged())
    {
        buzzerSetVolume(settings.volume);
        for (auto &w : watch)
            w = Watch();
        uiRebuild();
    }

    if (now - tAlerts > 400)
    {
        tAlerts = now;
        for (int i = 0; i < MAX_PRINTERS; i++)
            if (bambuEnabled(i))
                checkPrinter(i);
        repeatAlertSound();
    }
    if (now - tPower > 200)
    {
        tPower = now;
        managePower();
    }
    if (now - tShift > 90000)
    {
        tShift = now;
        pixelShift();
    }
    if (now - tQuiet > 30000)
    {
        tQuiet = now;
        updateQuietHours();
    }
    if (now - tLog > 60000)
    {
        tLog = now;
        Serial.printf("[stat] heap %u (min %u), psram %u, wifi %s\n", ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                      ESP.getFreePsram(), netIp().c_str());
    }

    delay(4);
}
