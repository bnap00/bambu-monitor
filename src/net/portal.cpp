#include "portal.h"
#include "settings.h"
#include "printer/bambu.h"
#include "board/display.h"
#include "board/qwiic.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoOTA.h>
#include "ui/ui.h"
#include <ArduinoJson.h>
#include <time.h>

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

static WebServer server(80);
static DNSServer dns;
static bool apActive = false;
static bool settingsChanged = false;
static String apSsid;
static uint32_t staStartMs = 0;
static bool servicesStarted = false;

// ------------------------------------------------------------ helpers

static String esc(const String &s)
{
    String o;
    o.reserve(s.length() + 8);
    for (char c : s)
    {
        switch (c)
        {
        case '&': o += F("&amp;"); break;
        case '<': o += F("&lt;"); break;
        case '>': o += F("&gt;"); break;
        case '"': o += F("&quot;"); break;
        default: o += c;
        }
    }
    return o;
}

static bool authorized()
{
    if (settings.webPass.isEmpty() || apActive)
        return true;
    if (server.authenticate("admin", settings.webPass.c_str()))
        return true;
    server.requestAuthentication(BASIC_AUTH, "Bambu Monitor");
    return false;
}

static String field(const char *label, const char *name, const String &value, const char *type = "text",
                    const char *extra = "")
{
    String s = F("<label>");
    s += label;
    s += F("<input name=\"");
    s += name;
    s += F("\" type=\"");
    s += type;
    s += F("\" value=\"");
    s += esc(value);
    s += "\" ";
    s += extra;
    s += F("></label>");
    return s;
}

static String check(const char *label, const char *name, bool on)
{
    String s = F("<label class=c><input type=checkbox name=\"");
    s += name;
    s += '"';
    if (on)
        s += F(" checked");
    s += '>';
    s += label;
    s += F("</label>");
    return s;
}

static const char STYLE[] PROGMEM = R"CSS(
<meta name=viewport content="width=device-width,initial-scale=1">
<style>
body{font-family:system-ui,sans-serif;background:#0b0b0c;color:#e8e8e8;max-width:640px;margin:auto;padding:16px}
h1{font-size:1.4em}h2{font-size:1.05em;margin-top:28px;color:#3ddc84;border-bottom:1px solid #222;padding-bottom:4px}
label{display:block;margin:10px 0 2px;font-size:.9em;color:#aaa}
input[type=text],input[type=password],input[type=number]{width:100%;box-sizing:border-box;padding:9px;border-radius:8px;border:1px solid #333;background:#16161a;color:#fff;font-size:1em}
label.c{display:flex;gap:8px;align-items:center;color:#ddd}
button{margin-top:22px;padding:12px 22px;border:0;border-radius:10px;background:#3ddc84;color:#000;font-weight:600;font-size:1em}
.note{color:#888;font-size:.85em}.row{display:flex;gap:10px}.row>*{flex:1}
a{color:#3ddc84}code{background:#16161a;padding:2px 5px;border-radius:4px}
</style>)CSS";

// ------------------------------------------------------------ pages

static void handleRoot()
{
    if (!authorized())
        return;
    const Settings &s = settings;
    String h;
    h.reserve(9000);
    h += F("<!doctype html><html><head><title>Bambu Monitor</title>");
    h += FPSTR(STYLE);
    h += F("</head><body><h1>Bambu Monitor</h1><p class=note>v" FW_VERSION " &middot; LilyGO T-Encoder-Pro &middot; panel ");
    h += displayPanelName();
    h += F(" &middot; <a href=/api/status>status JSON</a> &middot; <a href=/update>firmware update</a></p>");
    h += F("<form method=post action=/save>");

    h += F("<h2>Wi-Fi</h2>");
    h += field("SSID", "ssid", s.wifiSsid);
    h += field("Password (leave blank to keep)", "pass", "", "password");
    h += field("Hostname", "host", s.hostname);
    h += field("Timezone (POSIX TZ, e.g. IST-5:30 or CET-1CEST,M3.5.0,M10.5.0/3)", "tz", s.tz);

    for (int i = 0; i < MAX_PRINTERS; i++)
    {
        const PrinterConfig &p = s.printers[i];
        String n = String(i);
        h += F("<h2>Printer ");
        h += i + 1;
        h += F("</h2>");
        h += check("Enabled", ("p" + n + "en").c_str(), p.enabled);
        h += field("Name on screen", ("p" + n + "name").c_str(), p.name, "text", "maxlength=12");
        h += field("IP address", ("p" + n + "host").c_str(), p.host);
        h += field("Serial number", ("p" + n + "ser").c_str(), p.serial);
        h += field(p.code.length() ? "LAN access code (set; blank keeps it)" : "LAN access code",
                   ("p" + n + "code").c_str(), "", "password", "maxlength=16");
    }
    h += F("<p class=note>On the printer: Settings &rarr; WLAN shows IP + access code. "
           "Serial: Settings &rarr; Device. The printer can stay connected to Bambu Cloud.</p>");

    h += F("<h2>Display</h2><div class=row>");
    h += field("Brightness (1-255)", "bri", String(s.brightness), "number", "min=1 max=255");
    h += field("Dimmed brightness", "dimbri", String(s.dimBrightness), "number", "min=1 max=255");
    h += F("</div><div class=row>");
    h += field("Dim after (s)", "dims", String(s.dimAfterS), "number", "min=5 max=3600");
    h += field("Screen off after (min, 0=never)", "offmin", String(s.offAfterMin), "number", "min=0 max=1440");
    h += F("</div>");
    h += check("Keep screen on (dimmed) while printing", "stayon", s.stayOnWhilePrinting);
    h += check("Auto brightness from a Qwiic light sensor (BH1750 / VEML7700)", "autobri", s.autoBrightness);
    h += check("Rotate 180&deg; (USB-C on top)", "rot180", s.rotate180);

    h += F("<h2>Sound</h2><div class=row>");
    h += field("Volume (0-3)", "vol", String(s.volume), "number", "min=0 max=3");
    h += field("Repeat alarms every (min, 0=once)", "alrep", String(s.alarmRepeatMin), "number", "min=0 max=60");
    h += F("</div>");
    h += check("Knob click sounds", "clicks", s.knobClicks);
    h += check("Chime when the first layer completes", "layer1", s.chimeOnFirstLayer);
    h += F("<div class=row>");
    h += field("Quiet hours start (-1=off)", "qstart", String(s.quietStartHour), "number", "min=-1 max=23");
    h += field("Quiet hours end", "qend", String(s.quietEndHour), "number", "min=0 max=23");
    h += F("</div><p class=note>During quiet hours only failures/errors make sound.</p>");

    h += F("<h2>Security</h2>");
    h += field("Web UI password (user: admin, blank = none)", "webpass", "", "password");
    h += check("Clear web password", "clearweb", false);

    h += F("<button type=submit>Save</button></form>");
    h += F("<form method=post action=/reboot><button style='background:#333;color:#fff'>Reboot</button></form>");
    h += F("</body></html>");
    server.send(200, "text/html", h);
}

static int argInt(const char *name, int def, int lo, int hi)
{
    if (!server.hasArg(name) || server.arg(name).isEmpty())
        return def;
    return constrain(server.arg(name).toInt(), lo, hi);
}

static void handleSave()
{
    if (!authorized())
        return;
    Settings &s = settings;
    String oldSsid = s.wifiSsid, oldPass = s.wifiPass, oldHost = s.hostname;
    bool oldRot = s.rotate180;

    s.wifiSsid = server.arg("ssid");
    s.wifiSsid.trim();
    if (server.arg("pass").length())
        s.wifiPass = server.arg("pass");
    if (server.arg("host").length())
        s.hostname = server.arg("host");
    if (server.arg("tz").length())
        s.tz = server.arg("tz");

    for (int i = 0; i < MAX_PRINTERS; i++)
    {
        PrinterConfig &p = s.printers[i];
        String n = String(i);
        p.enabled = server.hasArg("p" + n + "en");
        p.name = server.arg("p" + n + "name");
        p.host = server.arg("p" + n + "host");
        p.serial = server.arg("p" + n + "ser");
        p.name.trim();
        p.host.trim();
        p.serial.trim();
        p.serial.toUpperCase();
        String code = server.arg("p" + n + "code");
        code.trim();
        if (code.length())
            p.code = code;
    }

    s.brightness = argInt("bri", s.brightness, 1, 255);
    s.dimBrightness = argInt("dimbri", s.dimBrightness, 1, 255);
    s.dimAfterS = argInt("dims", s.dimAfterS, 5, 3600);
    s.offAfterMin = argInt("offmin", s.offAfterMin, 0, 1440);
    s.stayOnWhilePrinting = server.hasArg("stayon");
    s.autoBrightness = server.hasArg("autobri");
    s.rotate180 = server.hasArg("rot180");
    s.volume = argInt("vol", s.volume, 0, 3);
    s.alarmRepeatMin = argInt("alrep", s.alarmRepeatMin, 0, 60);
    s.knobClicks = server.hasArg("clicks");
    s.chimeOnFirstLayer = server.hasArg("layer1");
    s.quietStartHour = argInt("qstart", s.quietStartHour, -1, 23);
    s.quietEndHour = argInt("qend", s.quietEndHour, 0, 23);
    if (server.hasArg("clearweb"))
        s.webPass = "";
    else if (server.arg("webpass").length())
        s.webPass = server.arg("webpass");

    settingsSave();

    bool needRestart = s.wifiSsid != oldSsid || s.wifiPass != oldPass || s.hostname != oldHost ||
                       s.rotate180 != oldRot || apActive;
    String h = F("<!doctype html><html><head>");
    h += FPSTR(STYLE);
    if (!needRestart)
        h += F("<meta http-equiv=refresh content='2;url=/'>");
    h += F("</head><body><h1>Saved</h1><p>");
    h += needRestart ? F("Restarting to apply Wi-Fi / display changes&hellip;") : F("Applied.");
    h += F("</p></body></html>");
    server.send(200, "text/html", h);

    if (needRestart)
    {
        delay(800);
        ESP.restart();
    }
    setenv("TZ", s.tz.c_str(), 1);
    tzset();
    bambuReconfigure();
    settingsChanged = true;
}

static const char *linkName(Link l)
{
    switch (l)
    {
    case Link::Disabled: return "disabled";
    case Link::Connecting: return "connecting";
    case Link::Online: return "online";
    case Link::AuthFailed: return "auth_failed";
    default: return "offline";
    }
}

static void handleStatus()
{
    if (!authorized())
        return;
    JsonDocument doc;
    doc["version"] = FW_VERSION;
    doc["uptime_s"] = millis() / 1000;
    doc["ip"] = netIp();
    doc["rssi"] = WiFi.RSSI();
    doc["panel"] = displayPanelName();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["free_psram"] = ESP.getFreePsram();
    QwiicReadings q = qwiicGet();
    JsonObject qo = doc["qwiic"].to<JsonObject>();
    JsonArray addrs = qo["addresses"].to<JsonArray>();
    for (uint8_t i = 0; i < q.foundCount; i++)
    {
        char b[6];
        snprintf(b, sizeof(b), "0x%02X", q.found[i]);
        addrs.add(b);
    }
    if (q.hasClimate)
    {
        qo["climate_sensor"] = q.climateSensor;
        qo["temp_c"] = q.tempC;
        if (!isnan(q.humidity))
            qo["humidity"] = q.humidity;
        if (!isnan(q.pressureHpa))
            qo["pressure_hpa"] = q.pressureHpa;
    }
    if (q.hasLight)
    {
        qo["light_sensor"] = q.lightSensor;
        qo["lux"] = q.lux;
    }
    JsonArray arr = doc["printers"].to<JsonArray>();
    for (int i = 0; i < MAX_PRINTERS; i++)
    {
        PrinterState st = bambuGet(i);
        JsonObject o = arr.add<JsonObject>();
        o["name"] = bambuName(i);
        o["link"] = linkName(st.link);
        if (st.link == Link::Disabled)
            continue;
        o["state"] = gstateName(st.state);
        o["stage"] = stageName(st.stage);
        o["percent"] = st.percent;
        o["remaining_min"] = st.remainingMin;
        o["layer"] = st.layer;
        o["total_layers"] = st.totalLayers;
        o["job"] = st.job;
        o["nozzle"] = st.nozzle;
        if (st.nozzleCount > 1)
        {
            o["nozzle_left"] = st.nozzleL;
            o["nozzle_right"] = st.nozzleR;
            o["active_nozzle"] = st.activeNozzle == 1 ? "left" : "right";
        }
        o["bed"] = st.bed;
        if (!isnan(st.chamber))
            o["chamber"] = st.chamber;
        o["hms_count"] = st.hmsCount;
        o["print_error"] = st.printError;
        o["updates"] = st.updateCount;
    }
    String out;
    serializeJsonPretty(doc, out);
    server.send(200, "application/json", out);
}

static void handleUpdatePage()
{
    if (!authorized())
        return;
    String h = F("<!doctype html><html><head><title>Update</title>");
    h += FPSTR(STYLE);
    h += F("</head><body><h1>Firmware update</h1><p class=note>Upload <code>.pio/build/t-encoder-pro/firmware.bin</code></p>"
           "<form method=post action=/update enctype=multipart/form-data><input type=file name=fw accept=.bin>"
           "<button>Upload &amp; flash</button></form><p><a href=/>back</a></p></body></html>");
    server.send(200, "text/html", h);
}

static void handleUpdateDone()
{
    if (!authorized())
        return;
    bool ok = !Update.hasError();
    server.send(ok ? 200 : 500, "text/plain", ok ? "Update OK, rebooting..." : Update.errorString());
    if (ok)
    {
        delay(600);
        ESP.restart();
    }
}

static void handleUpdateUpload()
{
    if (!settings.webPass.isEmpty() && !apActive && !server.authenticate("admin", settings.webPass.c_str()))
        return;
    HTTPUpload &up = server.upload();
    if (up.status == UPLOAD_FILE_START)
    {
        Serial.printf("[ota] receiving %s\n", up.filename.c_str());
        Update.begin(UPDATE_SIZE_UNKNOWN);
        uiOtaProgress(0);
    }
    else if (up.status == UPLOAD_FILE_WRITE)
    {
        Update.write(up.buf, up.currentSize);
        uiOtaProgress(-2); // size unknown: spinner-style update
    }
    else if (up.status == UPLOAD_FILE_END)
    {
        Update.end(true);
        Serial.printf("[ota] done, %u bytes, %s\n", up.totalSize, Update.hasError() ? "FAILED" : "ok");
    }
}

static void handleReboot()
{
    if (!authorized())
        return;
    server.send(200, "text/plain", "Rebooting...");
    delay(500);
    ESP.restart();
}

static void handleNotFound()
{
    if (apActive) // captive portal: send everything to the setup page
    {
        server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
        server.send(302, "text/plain", "");
        return;
    }
    server.send(404, "text/plain", "not found");
}

// ------------------------------------------------------------ wifi

void netStartSetupAp()
{
    if (apActive)
        return;
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "BambuMonitor-%02X%02X", mac[4], mac[5]);
    apSsid = ssid;
    WiFi.mode(settings.wifiSsid.length() ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAP(ssid);
    dns.start(53, "*", WiFi.softAPIP());
    apActive = true;
    Serial.printf("[net] setup AP '%s' at %s\n", ssid, WiFi.softAPIP().toString().c_str());
}

static void startServices()
{
    if (servicesStarted)
        return;
    servicesStarted = true;
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/update", HTTP_GET, handleUpdatePage);
    server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
    server.on("/reboot", HTTP_POST, handleReboot);
    server.onNotFound(handleNotFound);
    server.begin();
}

void netBegin()
{
    WiFi.persistent(false);
    WiFi.setHostname(settings.hostname.c_str());
    WiFi.setAutoReconnect(true);

    setenv("TZ", settings.tz.c_str(), 1);
    tzset();

    if (settings.wifiSsid.isEmpty())
    {
        netStartSetupAp();
    }
    else
    {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false); // lower MQTT latency; the display dominates power anyway
        WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
        staStartMs = millis();
    }
    startServices();
}

// Network OTA for `pio run -e t-encoder-pro-ota -t upload` (espota, UDP/TCP 3232).
// Uses the web UI password when one is set.
static void startArduinoOta()
{
    static bool started = false;
    if (started)
        return;
    started = true;
    ArduinoOTA.setHostname(settings.hostname.c_str());
    if (settings.webPass.length())
        ArduinoOTA.setPassword(settings.webPass.c_str());
    ArduinoOTA.setRebootOnSuccess(true);
    ArduinoOTA.onStart([] {
        Serial.println("[ota] start");
        uiOtaProgress(0);
    });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        static int last = -1;
        int pct = total ? done * 100 / total : 0;
        if (pct != last)
        {
            last = pct;
            uiOtaProgress(pct);
        }
    });
    ArduinoOTA.onEnd([] {
        Serial.println("[ota] done, rebooting");
        uiOtaProgress(100);
    });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.printf("[ota] error %u\n", e);
        uiOtaProgress(-1);
    });
    ArduinoOTA.begin();
    Serial.printf("[ota] ready: %s.local:3232\n", settings.hostname.c_str());
}

void netLoop()
{
    static bool wasOnline = false;
    bool online = WiFi.status() == WL_CONNECTED;

    if (online && !wasOnline)
    {
        Serial.printf("[net] connected, IP %s\n", WiFi.localIP().toString().c_str());
        configTzTime(settings.tz.c_str(), "pool.ntp.org", "time.google.com");
        MDNS.begin(settings.hostname.c_str());
        MDNS.addService("http", "tcp", 80);
        startArduinoOta();
    }
    wasOnline = online;

    // Can't join the configured network for 45 s after boot: offer the setup AP as well
    if (!online && !apActive && settings.wifiSsid.length() && millis() - staStartMs > 45000 &&
        millis() < 120000)
        netStartSetupAp();

    if (apActive)
        dns.processNextRequest();
    server.handleClient();
    if (online)
        ArduinoOTA.handle();
}

NetMode netMode()
{
    if (WiFi.status() == WL_CONNECTED)
        return NetMode::Online;
    if (apActive)
        return NetMode::SetupAP;
    return NetMode::Connecting;
}

String netIp()
{
    if (WiFi.status() == WL_CONNECTED)
        return WiFi.localIP().toString();
    if (apActive)
        return WiFi.softAPIP().toString();
    return "";
}

String netApSsid()
{
    return apSsid;
}

bool netTimeValid()
{
    return time(nullptr) > 1700000000;
}

bool netTakeSettingsChanged()
{
    bool c = settingsChanged;
    settingsChanged = false;
    return c;
}
