// Host-side renderer: runs the real src/ui/ui.cpp against LVGL with fake printer
// data and writes screenshots of every page to out/*.ppm.
#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include "board_pins.h"
#include "board/buzzer.h"
#include "board/display.h"
#include "board/qwiic.h"
#include "net/portal.h"
#include "net/settings.h"
#include "printer/bambu.h"
#include "ui/ui.h"

static uint32_t fakeMs = 0;
extern "C" uint32_t millis() { return fakeMs; }
EspClass ESP;
WiFiStub WiFi;
Settings settings;

bool Settings::printersConfigured() const { return true; }
void settingsSave() {}

void buzzerPlay(Sound, bool) {}
void buzzerStop() {}
void buzzerSetVolume(uint8_t) {}

void displaySetBrightness(uint8_t) {}
void displayForceBrightness(uint8_t) {}
static uint32_t simBlock = 0;
void touchBlockUntil(uint32_t ms) { simBlock = ms; }
bool touchBlocked() { return simBlock && (int32_t)(millis() - simBlock) < 0; }
const char *displayPanelName() { return "CO5300 + CST816"; }

static bool climate = true;
QwiicReadings qwiicGet()
{
    QwiicReadings q;
    q.hasClimate = climate;
    q.tempC = 24.3f;
    q.humidity = 46;
    q.climateSensor = "SHT4x";
    q.hasLight = true;
    q.lux = 180;
    q.lightSensor = "BH1750";
    return q;
}

static NetMode mode = NetMode::Online;
NetMode netMode() { return mode; }
String netIp() { return mode == NetMode::SetupAP ? "192.168.4.1" : "192.168.1.42"; }
String netApSsid() { return "BambuMonitor-3A5C"; }
bool netTimeValid() { return true; }
void netStartSetupAp() {}

static PrinterState fake[2];
static bool enabled[2] = {true, true};
PrinterState bambuGet(int i) { return fake[i]; }
bool bambuEnabled(int i) { return enabled[i]; }
String bambuName(int i) { return i == 0 ? "P1S" : "A1"; }
void bambuCommand(int, PrinterCmd, int) {}
const char *gstateName(GState s)
{
    static const char *n[] = {"Unknown", "Idle", "Preparing", "Printing", "Paused", "Finished", "Failed", "Slicing"};
    return n[(int)s];
}
bool gstateBusy(GState s) { return s == GState::Prepare || s == GState::Running || s == GState::Pause; }
const char *stageName(int s) { return s == 6 ? "Filament runout" : s == 7 ? "Heating hotend" : ""; }

static void tray(AmsTray &t, const char *type, uint32_t rgba, int remain)
{
    t.present = true;
    strcpy(t.type, type);
    t.rgba = rgba;
    t.remain = remain;
}

static void setupFakes()
{
    PrinterState &a = fake[0];
    a.link = Link::Online;
    a.updateCount = 10;
    a.state = GState::Running;
    a.stage = 0;
    a.percent = 47;
    a.remainingMin = 83;
    a.layer = 120;
    a.totalLayers = 300;
    strcpy(a.job, "Benchy_0.2mm_PLA_P1S_1h50m");
    a.nozzle = 219.6;
    a.nozzleTarget = 220;
    a.bed = 54;
    a.bedTarget = 60;
    a.partFan = 100;
    a.auxFan = 40;
    a.chamberFan = 20;
    a.speedLevel = 2;
    a.wifiDbm = -48;
    a.amsCount = 1;
    a.ams[0].present = true;
    a.ams[0].humidityLevel = 4;
    a.ams[0].humidityPct = 21;
    a.ams[0].tempC = 27.5;
    tray(a.ams[0].trays[0], "PLA", 0xF2F2F2FF, 80);
    tray(a.ams[0].trays[1], "PETG", 0x161616FF, 35);
    tray(a.ams[0].trays[2], "PLA", 0xE5372BFF, 100);
    a.trayNow = 2;
    a.hmsCount = 1;
    a.hms[0] = {0x05000200, 0x00030002};

    PrinterState &b = fake[1];
    b.link = Link::Online;
    b.updateCount = 10;
    b.state = GState::Pause;
    b.stage = 6;
    b.percent = 82;
    b.remainingMin = 12;
    b.layer = 210;
    b.totalLayers = 256;
    strcpy(b.job, "cable_clips");
    b.nozzle = 180;
    b.nozzleTarget = 220;
    b.bed = 65;
    b.bedTarget = 65;
    b.partFan = 0;
    b.speedLevel = 3;
    b.wifiDbm = -61;
    b.printError = 0x07008011;
    tray(b.external, "PLA", 0x2E7D32FF, -1);
    b.trayNow = 254;
}

// ---- framebuffer

static uint16_t fb[LCD_WIDTH * LCD_HEIGHT];

static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *px)
{
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++)
            fb[y * LCD_WIDTH + x] = (px++)->full;
    lv_disp_flush_ready(d);
}

static void run(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 10)
    {
        fakeMs += 10;
        uiLoop();
        lv_timer_handler();
    }
}

static void shot(const char *name)
{
    run(900);
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
    lv_refr_now(nullptr);
    char path[128];
    snprintf(path, sizeof path, "out/%s.ppm", name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", LCD_WIDTH, LCD_HEIGHT);
    for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++)
    {
        uint16_t c = fb[i];
        uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 0x3F) << 2), (uint8_t)((c & 0x1F) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("wrote %s\n", path);
}

int main()
{
    settings.printers[0].host = "192.168.1.50";
    settings.printers[1].host = "192.168.1.51";
    settings.wifiSsid = "HomeNet";
    setupFakes();

    lv_init();
    static lv_disp_draw_buf_t db;
    static lv_color_t buf[LCD_WIDTH * LCD_HEIGHT];
    lv_disp_draw_buf_init(&db, buf, nullptr, LCD_WIDTH * LCD_HEIGHT);
    static lv_disp_drv_t dd;
    lv_disp_drv_init(&dd);
    dd.hor_res = LCD_WIDTH;
    dd.ver_res = LCD_HEIGHT;
    dd.flush_cb = flush;
    dd.draw_buf = &db;
    lv_disp_drv_register(&dd);

    uiInit();
    const char *names[] = {"01_overview", "02_p1s_status", "03_p1s_temps", "04_p1s_ams", "05_p1s_health",
                           "06_a1_status", "07_a1_temps", "08_a1_ams", "09_a1_health", "10_system"};
    for (int i = 0; i < 10; i++)
    {
        if (i)
        {
            run(400);
            uiOnKnob(1);
        }
        shot(names[i]);
    }

    uiOnKnob(2); // wrap to P1S status
    run(400);
    uiOnKnobEvent(KnobEvent::Click); // press the screen: menu
    shot("11_menu_printer");
    uiOnKnobEvent(KnobEvent::DoubleClick);

    uiOnKnob(2); // P1S filament for the single-AMS layout
    shot("11b_p1s_ams_single");
    uiShowAlert(0, AlertKind::Finished, "P1S finished", "Benchy_0.2mm_PLA_P1S_1h50m");
    shot("12_alert_finished");
    uiDismissAlert();
    uiShowAlert(1, AlertKind::Error, "A1 needs attention", "HMS 0300_0100_0001_0007");
    shot("13_alert_error");
    uiDismissAlert();
    run(500);
    uiToast("A1: first layer done");
    shot("14_toast");

    uiShowSetup(); // system page (no setup page while printers exist)
    run(400);
    uiOnKnobEvent(KnobEvent::Click); // device menu
    run(300);
    uiOnKnobEvent(KnobEvent::Click); // Brightness
    shot("15_adjust_brightness");
    uiOnKnobEvent(KnobEvent::Click);

    fake[0].state = GState::Idle;
    fake[1].link = Link::AuthFailed;
    uiOnKnobEvent(KnobEvent::LongPress); // hold: home
    run(300);
    shot("16_overview_idle_authfail");

    enabled[0] = enabled[1] = false;
    mode = NetMode::SetupAP;
    uiRebuild();
    shot("17_setup_ap");
    return 0;
}
