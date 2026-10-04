#include "settings.h"
#include <Preferences.h>

// Optional compile-time defaults (copy include/config_local.example.h -> include/config_local.h).
// They are only used until settings are saved from the web UI.
#if __has_include("config_local.h")
#include "config_local.h"
#endif

Settings settings;

static Preferences prefs;
static const char *NS = "bambumon";

bool Settings::printersConfigured() const
{
    for (const auto &p : printers)
        if (p.enabled && p.host.length() && p.serial.length() && p.code.length())
            return true;
    return false;
}

static void applyCompileTimeDefaults()
{
#ifdef CFG_WIFI_SSID
    settings.wifiSsid = CFG_WIFI_SSID;
#endif
#ifdef CFG_WIFI_PASS
    settings.wifiPass = CFG_WIFI_PASS;
#endif
#ifdef CFG_TZ
    settings.tz = CFG_TZ;
#endif
#ifdef CFG_P1_HOST
    settings.printers[0] = {true, CFG_P1_NAME, CFG_P1_HOST, CFG_P1_SERIAL, CFG_P1_CODE};
#endif
#ifdef CFG_P2_HOST
    settings.printers[1] = {true, CFG_P2_NAME, CFG_P2_HOST, CFG_P2_SERIAL, CFG_P2_CODE};
#endif
}

void settingsLoad()
{
    applyCompileTimeDefaults();

    prefs.begin(NS, true);
    if (!prefs.isKey("saved"))
    {
        prefs.end();
        return;
    }
    Settings &s = settings;
    s.wifiSsid = prefs.getString("ssid", s.wifiSsid);
    s.wifiPass = prefs.getString("pass", s.wifiPass);
    s.hostname = prefs.getString("host", s.hostname);
    s.tz = prefs.getString("tz", s.tz);
    s.webPass = prefs.getString("webpass", s.webPass);
    for (int i = 0; i < MAX_PRINTERS; i++)
    {
        char k[8];
        PrinterConfig &p = s.printers[i];
        snprintf(k, sizeof(k), "p%den", i);
        p.enabled = prefs.getBool(k, p.enabled);
        snprintf(k, sizeof(k), "p%dname", i);
        p.name = prefs.getString(k, p.name);
        snprintf(k, sizeof(k), "p%dhost", i);
        p.host = prefs.getString(k, p.host);
        snprintf(k, sizeof(k), "p%dser", i);
        p.serial = prefs.getString(k, p.serial);
        snprintf(k, sizeof(k), "p%dcode", i);
        p.code = prefs.getString(k, p.code);
    }
    s.brightness = prefs.getUChar("bri", s.brightness);
    s.dimBrightness = prefs.getUChar("dimbri", s.dimBrightness);
    s.dimAfterS = prefs.getUShort("dims", s.dimAfterS);
    s.offAfterMin = prefs.getUShort("offmin", s.offAfterMin);
    s.stayOnWhilePrinting = prefs.getBool("stayon", s.stayOnWhilePrinting);
    s.autoBrightness = prefs.getBool("autobri", s.autoBrightness);
    s.rotate180 = prefs.getBool("rot180", s.rotate180);
    s.volume = prefs.getUChar("vol", s.volume);
    s.knobClicks = prefs.getBool("clicks", s.knobClicks);
    s.chimeOnFirstLayer = prefs.getBool("layer1", s.chimeOnFirstLayer);
    s.alarmRepeatMin = prefs.getUChar("alrep", s.alarmRepeatMin);
    s.quietStartHour = prefs.getChar("qstart", s.quietStartHour);
    s.quietEndHour = prefs.getChar("qend", s.quietEndHour);
    prefs.end();
}

void settingsSave()
{
    const Settings &s = settings;
    prefs.begin(NS, false);
    prefs.putBool("saved", true);
    prefs.putString("ssid", s.wifiSsid);
    prefs.putString("pass", s.wifiPass);
    prefs.putString("host", s.hostname);
    prefs.putString("tz", s.tz);
    prefs.putString("webpass", s.webPass);
    for (int i = 0; i < MAX_PRINTERS; i++)
    {
        char k[8];
        const PrinterConfig &p = s.printers[i];
        snprintf(k, sizeof(k), "p%den", i);
        prefs.putBool(k, p.enabled);
        snprintf(k, sizeof(k), "p%dname", i);
        prefs.putString(k, p.name);
        snprintf(k, sizeof(k), "p%dhost", i);
        prefs.putString(k, p.host);
        snprintf(k, sizeof(k), "p%dser", i);
        prefs.putString(k, p.serial);
        snprintf(k, sizeof(k), "p%dcode", i);
        prefs.putString(k, p.code);
    }
    prefs.putUChar("bri", s.brightness);
    prefs.putUChar("dimbri", s.dimBrightness);
    prefs.putUShort("dims", s.dimAfterS);
    prefs.putUShort("offmin", s.offAfterMin);
    prefs.putBool("stayon", s.stayOnWhilePrinting);
    prefs.putBool("autobri", s.autoBrightness);
    prefs.putBool("rot180", s.rotate180);
    prefs.putUChar("vol", s.volume);
    prefs.putBool("clicks", s.knobClicks);
    prefs.putBool("layer1", s.chimeOnFirstLayer);
    prefs.putUChar("alrep", s.alarmRepeatMin);
    prefs.putChar("qstart", s.quietStartHour);
    prefs.putChar("qend", s.quietEndHour);
    prefs.end();
}

void settingsFactoryReset()
{
    prefs.begin(NS, false);
    prefs.clear();
    prefs.end();
}
