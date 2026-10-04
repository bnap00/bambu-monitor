#include "ui.h"
#include "board_pins.h"
#include "board/buzzer.h"
#include "board/display.h"
#include "board/qwiic.h"
#include "net/portal.h"
#include "net/settings.h"
#include "printer/bambu.h"
#include <WiFi.h>
#include <functional>
#include <lvgl.h>
#include <time.h>
#include <vector>

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// ============================================================ palette

static const uint32_t C_BG = 0x000000;
static const uint32_t C_TRACK = 0x1A1A1E;
static const uint32_t C_TEXT = 0xF2F2F2;
static const uint32_t C_DIM = 0x8A8A90;
static const uint32_t C_FAINT = 0x4A4A50;
static const uint32_t C_PRINT = 0x3DDC84;
static const uint32_t C_PREP = 0x4FC3F7;
static const uint32_t C_PAUSE = 0xFFB300;
static const uint32_t C_DONE = 0x26E0C8;
static const uint32_t C_FAIL = 0xFF5252;
static const uint32_t C_IDLE = 0x9E9E9E;
static const uint32_t C_OFF = 0x45454B;

static lv_color_t col(uint32_t rgb)
{
    return lv_color_hex(rgb);
}

static uint32_t stateRgb(const PrinterState &s)
{
    if (s.link == Link::AuthFailed)
        return C_FAIL;
    if (s.link != Link::Online)
        return C_OFF;
    if (s.state == GState::Failed || (s.printError && s.state != GState::Pause))
        return C_FAIL;
    switch (s.state)
    {
    case GState::Running: return C_PRINT;
    case GState::Prepare:
    case GState::Slicing: return C_PREP;
    case GState::Pause: return C_PAUSE;
    case GState::Finish: return C_DONE;
    default: return C_IDLE;
    }
}

static String linkText(const PrinterState &s)
{
    switch (s.link)
    {
    case Link::Disabled: return "Not configured";
    case Link::Connecting: return "Connecting...";
    case Link::AuthFailed: return "Wrong access code";
    case Link::Offline: return "Offline";
    default: return gstateName(s.state);
    }
}

static String fmtDuration(int minutes)
{
    if (minutes <= 0)
        return "<1m";
    char b[16];
    if (minutes >= 60)
        snprintf(b, sizeof(b), "%dh %02dm", minutes / 60, minutes % 60);
    else
        snprintf(b, sizeof(b), "%dm", minutes);
    return b;
}

static String fmtEta(int minutes)
{
    if (!netTimeValid())
        return "";
    time_t now = time(nullptr);
    time_t eta = now + minutes * 60;
    struct tm a, b;
    localtime_r(&now, &a);
    localtime_r(&eta, &b);
    char buf[32];
    if (a.tm_yday == b.tm_yday)
        strftime(buf, sizeof(buf), "Done at %H:%M", &b);
    else if ((b.tm_yday == a.tm_yday + 1) || (a.tm_yday >= 364 && b.tm_yday == 0))
        strftime(buf, sizeof(buf), "Done tomorrow %H:%M", &b);
    else
        strftime(buf, sizeof(buf), "Done %a %H:%M", &b);
    return buf;
}

static String fmtTemp(float v)
{
    if (isnan(v))
        return "--";
    return String((int)lroundf(v)) + "\xC2\xB0"; // °
}

static void setText(lv_obj_t *l, const String &t)
{
    if (strcmp(lv_label_get_text(l), t.c_str()) != 0)
        lv_label_set_text(l, t.c_str());
}

static void setTextColor(lv_obj_t *o, uint32_t rgb)
{
    lv_obj_set_style_text_color(o, col(rgb), 0);
}

// ============================================================ widget helpers

static lv_obj_t *mkLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t rgb, int y, const char *txt = "")
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col(rgb), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, txt);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    return l;
}

// 300-degree gauge ring with the opening at the bottom (keeps room for page dots)
static lv_obj_t *mkRing(lv_obj_t *parent, int size, int width, uint32_t rgb)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_set_size(a, size, size);
    lv_obj_center(a);
    lv_arc_set_rotation(a, 120);
    lv_arc_set_bg_angles(a, 0, 300);
    lv_arc_set_range(a, 0, 100);
    lv_arc_set_value(a, 0);
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, col(C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, col(rgb), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    return a;
}

static lv_obj_t *mkBar(lv_obj_t *parent, int w, int y)
{
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_set_size(b, w, 8);
    lv_obj_align(b, LV_ALIGN_CENTER, 0, y);
    lv_bar_set_range(b, 0, 1000);
    lv_obj_set_style_bg_color(b, col(C_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 4, LV_PART_INDICATOR);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
    return b;
}

static lv_obj_t *mkScreen(lv_obj_t **rootOut)
{
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, col(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Content root: shifted for burn-in protection, transparent to input
    lv_obj_t *root = lv_obj_create(scr);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    *rootOut = root;
    return scr;
}

// ============================================================ pages

enum class PageKind : uint8_t
{
    Overview,
    Status,
    Temps,
    Ams,
    Health,
    System,
    Setup,
};

struct OverviewW
{
    lv_obj_t *ring[2], *name[2], *pct[2], *sub[2], *clock, *room;
};
struct StatusW
{
    lv_obj_t *ring, *name, *stage, *pct, *remain, *eta, *layer, *job, *temps;
};
struct TempsW
{
    lv_obj_t *nozLabel, *noz, *nozBar, *bed, *bedBar, *chamber, *fans, *speed;
};
struct AmsW
{
    lv_obj_t *row[2], *unit[2], *tray[2][4], *type[2][4], *pct[2][4], *ext, *extLbl, *empty;
};
struct HealthW
{
    lv_obj_t *headline, *body;
};
struct SystemW
{
    lv_obj_t *clock, *date, *wifi, *qwiic, *panel, *qr, *url;
    String qrData;
};
struct SetupW
{
    lv_obj_t *qr, *l1, *l2;
    String qrData;
};

struct Page
{
    PageKind kind;
    int printer;
    lv_obj_t *scr;
    lv_obj_t *root;
    void *w;
};

static std::vector<Page> pages;
static int current = 0;
static lv_obj_t *dots = nullptr;
static int shiftX = 0, shiftY = 0;
static uint32_t lastRefreshMs = 0;
static uint32_t lastNavMs = 0;
static bool gestureFired = false;

static void showPage(int idx, int dir);
static void openPrinterMenu(int printer);
static void openSystemMenu();
static void openOverviewMenu();

// ---------------------------------------------------- touch handling (shared)

static bool touchAllowed()
{
    return !touchBlocked();
}

static int pageFor(PageKind kind, int printer)
{
    for (size_t i = 0; i < pages.size(); i++)
        if (pages[i].kind == kind && pages[i].printer == printer)
            return i;
    return -1;
}

static int otherPrinterPage(int dir)
{
    const Page &p = pages[current];
    if (p.printer < 0)
        return -1;
    for (int k = 1; k < MAX_PRINTERS; k++)
    {
        int other = (p.printer + dir * k + MAX_PRINTERS) % MAX_PRINTERS;
        int idx = pageFor(p.kind, other);
        if (idx >= 0)
            return idx;
    }
    return -1;
}

static void onScreenEvent(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (!touchAllowed())
        return;

    if (code == LV_EVENT_PRESSED)
    {
        gestureFired = false;
    }
    else if (code == LV_EVENT_GESTURE)
    {
        gestureFired = true;
        lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
        int n = pages.size();
        if (d == LV_DIR_LEFT)
            showPage((current + 1) % n, 1);
        else if (d == LV_DIR_RIGHT)
            showPage((current - 1 + n) % n, -1);
        else if (d == LV_DIR_TOP || d == LV_DIR_BOTTOM)
        {
            int idx = otherPrinterPage(d == LV_DIR_TOP ? 1 : -1);
            if (idx >= 0)
                showPage(idx, d == LV_DIR_TOP ? 2 : -2);
        }
    }
    else if (code == LV_EVENT_SHORT_CLICKED && !gestureFired)
    {
        const Page &p = pages[current];
        if (p.kind == PageKind::Overview)
        {
            // Tap the top half for printer 1, bottom half for printer 2
            lv_point_t pt;
            lv_indev_get_point(lv_indev_get_act(), &pt);
            int target = pt.y < LCD_HEIGHT / 2 ? 0 : 1;
            int idx = pageFor(PageKind::Status, target);
            if (idx >= 0)
            {
                buzzerPlay(Sound::Click);
                showPage(idx, 1);
            }
        }
    }
}

static Page &addPage(PageKind kind, int printer)
{
    Page p{kind, printer, nullptr, nullptr, nullptr};
    p.scr = mkScreen(&p.root);
    lv_obj_add_event_cb(p.scr, onScreenEvent, LV_EVENT_ALL, nullptr);
    pages.push_back(p);
    return pages.back();
}

// ---------------------------------------------------- builders

static void buildOverview()
{
    Page &p = addPage(PageKind::Overview, -1);
    auto *w = new OverviewW();
    p.w = w;
    lv_obj_t *r = p.root;

    w->ring[0] = mkRing(r, 384, 14, C_PRINT);
    w->ring[1] = mkRing(r, 346, 14, C_PRINT);
    w->clock = mkLabel(r, &lv_font_montserrat_24, C_DIM, -122);

    for (int i = 0; i < 2; i++)
    {
        int y = i == 0 ? -52 : 42;
        w->name[i] = mkLabel(r, &lv_font_montserrat_20, C_TEXT, y - 16);
        lv_obj_align(w->name[i], LV_ALIGN_CENTER, -58, y - 6);
        w->pct[i] = mkLabel(r, &lv_font_montserrat_36, C_TEXT, y);
        lv_obj_align(w->pct[i], LV_ALIGN_CENTER, 58, y - 6);
        w->sub[i] = mkLabel(r, &lv_font_montserrat_16, C_DIM, y + 26);
    }
    lv_obj_t *sep = lv_obj_create(r);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, 200, 1);
    lv_obj_set_style_bg_color(sep, col(C_FAINT), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_align(sep, LV_ALIGN_CENTER, 0, -4);

    w->room = mkLabel(r, &lv_font_montserrat_16, C_DIM, 112);
}

static void buildStatus(int printer)
{
    Page &p = addPage(PageKind::Status, printer);
    auto *w = new StatusW();
    p.w = w;
    lv_obj_t *r = p.root;

    w->ring = mkRing(r, 380, 18, C_PRINT);
    w->name = mkLabel(r, &lv_font_montserrat_20, C_TEXT, -138, bambuName(printer).c_str());
    w->stage = mkLabel(r, &lv_font_montserrat_16, C_DIM, -108);
    lv_label_set_long_mode(w->stage, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(w->stage, 250);
    w->pct = mkLabel(r, &lv_font_montserrat_48, C_TEXT, -52);
    w->remain = mkLabel(r, &lv_font_montserrat_24, C_TEXT, 2);
    w->eta = mkLabel(r, &lv_font_montserrat_16, C_DIM, 32);
    w->layer = mkLabel(r, &lv_font_montserrat_16, C_DIM, 58);
    w->job = mkLabel(r, &lv_font_montserrat_16, C_TEXT, 90);
    lv_label_set_long_mode(w->job, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(w->job, 240);
    w->temps = mkLabel(r, &lv_font_montserrat_16, C_DIM, 122);
}

static void buildTemps(int printer)
{
    Page &p = addPage(PageKind::Temps, printer);
    auto *w = new TempsW();
    p.w = w;
    lv_obj_t *r = p.root;

    mkLabel(r, &lv_font_montserrat_20, C_TEXT, -140, (bambuName(printer) + " " LV_SYMBOL_CHARGE).c_str());
    w->nozLabel = mkLabel(r, &lv_font_montserrat_14, C_DIM, -108, "NOZZLE");
    w->noz = mkLabel(r, &lv_font_montserrat_28, C_TEXT, -82);
    w->nozBar = mkBar(r, 250, -58);
    mkLabel(r, &lv_font_montserrat_14, C_DIM, -34, "BED");
    w->bed = mkLabel(r, &lv_font_montserrat_28, C_TEXT, -8);
    w->bedBar = mkBar(r, 270, 16);
    w->chamber = mkLabel(r, &lv_font_montserrat_20, C_TEXT, 46);
    w->fans = mkLabel(r, &lv_font_montserrat_16, C_DIM, 80);
    w->speed = mkLabel(r, &lv_font_montserrat_16, C_DIM, 110);
}

static void buildAms(int printer)
{
    Page &p = addPage(PageKind::Ams, printer);
    auto *w = new AmsW();
    p.w = w;
    lv_obj_t *r = p.root;

    mkLabel(r, &lv_font_montserrat_20, C_TEXT, -140, (bambuName(printer) + " filament").c_str());
    for (int u = 0; u < 2; u++)
    {
        lv_obj_t *row = lv_obj_create(r);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 290, 104);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(row, LV_ALIGN_CENTER, 0, u == 0 ? -58 : 48);
        w->row[u] = row;
        const int y = 0;
        w->unit[u] = mkLabel(row, &lv_font_montserrat_14, C_DIM, y - 42);
        for (int t = 0; t < 4; t++)
        {
            int x = -99 + t * 66;
            lv_obj_t *c = lv_obj_create(row);
            lv_obj_remove_style_all(c);
            lv_obj_set_size(c, 46, 46);
            lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(c, col(C_FAINT), 0);
            lv_obj_set_style_border_width(c, 2, 0);
            lv_obj_align(c, LV_ALIGN_CENTER, x, y - 6);
            lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
            w->tray[u][t] = c;
            w->pct[u][t] = lv_label_create(c);
            lv_obj_set_style_text_font(w->pct[u][t], &lv_font_montserrat_12, 0);
            lv_obj_center(w->pct[u][t]);
            w->type[u][t] = mkLabel(row, &lv_font_montserrat_12, C_DIM, y + 28);
            lv_obj_align(w->type[u][t], LV_ALIGN_CENTER, x, y + 28);
        }
    }
    w->ext = lv_obj_create(r);
    lv_obj_remove_style_all(w->ext);
    lv_obj_set_size(w->ext, 30, 30);
    lv_obj_set_style_radius(w->ext, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(w->ext, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(w->ext, 2, 0);
    lv_obj_align(w->ext, LV_ALIGN_CENTER, -40, 122);
    lv_obj_clear_flag(w->ext, LV_OBJ_FLAG_CLICKABLE);
    w->extLbl = mkLabel(r, &lv_font_montserrat_14, C_DIM, 122);
    lv_obj_align(w->extLbl, LV_ALIGN_LEFT_MID, 195 - 15, 122);
    w->empty = mkLabel(r, &lv_font_montserrat_16, C_DIM, -10, "No AMS detected");
}

static void buildHealth(int printer)
{
    Page &p = addPage(PageKind::Health, printer);
    auto *w = new HealthW();
    p.w = w;
    lv_obj_t *r = p.root;

    mkLabel(r, &lv_font_montserrat_20, C_TEXT, -140, (bambuName(printer) + " health").c_str());
    w->headline = mkLabel(r, &lv_font_montserrat_24, C_PRINT, -96);
    w->body = mkLabel(r, &lv_font_montserrat_16, C_TEXT, 0);
    lv_obj_set_width(w->body, 290);
    lv_obj_align(w->body, LV_ALIGN_TOP_MID, 0, 130);
    lv_label_set_long_mode(w->body, LV_LABEL_LONG_WRAP);
}

static lv_obj_t *mkQr(lv_obj_t *parent, int size, int y)
{
    lv_obj_t *qr = lv_qrcode_create(parent, size, lv_color_black(), lv_color_white());
    lv_obj_set_style_border_color(qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(qr, 5, 0);
    lv_obj_align(qr, LV_ALIGN_CENTER, 0, y);
    lv_obj_clear_flag(qr, LV_OBJ_FLAG_CLICKABLE);
    return qr;
}

static void buildSystem()
{
    Page &p = addPage(PageKind::System, -1);
    auto *w = new SystemW();
    p.w = w;
    lv_obj_t *r = p.root;

    w->clock = mkLabel(r, &lv_font_montserrat_48, C_TEXT, -118);
    w->date = mkLabel(r, &lv_font_montserrat_16, C_DIM, -80);
    w->wifi = mkLabel(r, &lv_font_montserrat_14, C_DIM, -52);
    w->qwiic = mkLabel(r, &lv_font_montserrat_14, C_DIM, -30);
    w->qr = mkQr(r, 92, 34);
    w->url = mkLabel(r, &lv_font_montserrat_14, C_TEXT, 98);
    w->panel = mkLabel(r, &lv_font_montserrat_12, C_FAINT, 122);
}

static void buildSetup()
{
    Page &p = addPage(PageKind::Setup, -1);
    auto *w = new SetupW();
    p.w = w;
    lv_obj_t *r = p.root;

    mkLabel(r, &lv_font_montserrat_20, C_TEXT, -140, "Setup");
    w->l1 = mkLabel(r, &lv_font_montserrat_14, C_DIM, -108);
    w->qr = mkQr(r, 130, -10);
    w->l2 = mkLabel(r, &lv_font_montserrat_16, C_TEXT, 100);
    lv_obj_set_width(w->l2, 270);
    lv_label_set_long_mode(w->l2, LV_LABEL_LONG_WRAP);
}

// ---------------------------------------------------- refreshers

static void refreshOverview(OverviewW *w)
{
    for (int i = 0; i < 2; i++)
    {
        PrinterState s = bambuGet(i);
        uint32_t c = stateRgb(s);
        bool busy = gstateBusy(s.state) || s.state == GState::Finish;
        lv_arc_set_value(w->ring[i], s.link == Link::Online && busy ? s.percent : 0);
        lv_obj_set_style_arc_color(w->ring[i], col(c), LV_PART_INDICATOR);
        setText(w->name[i], bambuName(i));
        setTextColor(w->name[i], c);
        if (s.link == Link::Online && busy)
        {
            setText(w->pct[i], String(s.percent) + "%");
            String sub = s.state == GState::Finish ? String("Finished") : fmtDuration(s.remainingMin) + " left";
            if (s.state == GState::Pause)
                sub = "Paused";
            setText(w->sub[i], sub);
        }
        else
        {
            setText(w->pct[i], s.link == Link::Online ? "--" : LV_SYMBOL_WIFI);
            setText(w->sub[i], linkText(s));
        }
        setTextColor(w->pct[i], s.link == Link::Online ? C_TEXT : C_OFF);
    }

    if (netTimeValid())
    {
        time_t now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        char b[8];
        strftime(b, sizeof(b), "%H:%M", &t);
        setText(w->clock, b);
    }
    else
    {
        setText(w->clock, netMode() == NetMode::Online ? "" : LV_SYMBOL_WIFI " ...");
    }

    QwiicReadings q = qwiicGet();
    if (q.hasClimate && !isnan(q.tempC))
    {
        String t = String(q.tempC, 1) + "\xC2\xB0" "C";
        if (!isnan(q.humidity))
            t += "  " + String((int)lroundf(q.humidity)) + "%";
        setText(w->room, LV_SYMBOL_HOME " " + t);
    }
    else
    {
        setText(w->room, "");
    }
}

static void refreshStatus(int printer, StatusW *w)
{
    PrinterState s = bambuGet(printer);
    uint32_t c = stateRgb(s);
    lv_obj_set_style_arc_color(w->ring, col(c), LV_PART_INDICATOR);
    setText(w->name, bambuName(printer));
    setTextColor(w->name, c);

    if (s.link != Link::Online)
    {
        lv_arc_set_value(w->ring, 0);
        setText(w->stage, "");
        setText(w->pct, s.link == Link::AuthFailed ? LV_SYMBOL_WARNING : LV_SYMBOL_WIFI);
        setTextColor(w->pct, s.link == Link::AuthFailed ? C_FAIL : C_OFF);
        setText(w->remain, linkText(s));
        setText(w->eta, s.link == Link::Disabled ? "" : settings.printers[printer].host);
        setText(w->layer, s.link == Link::AuthFailed ? "Check LAN access code" : "");
        setText(w->job, "");
        setText(w->temps, "");
        return;
    }

    bool busy = gstateBusy(s.state);
    bool showJob = busy || s.state == GState::Finish || s.state == GState::Failed;
    lv_arc_set_value(w->ring, showJob ? s.percent : 0);
    setTextColor(w->pct, C_TEXT);

    String stage = gstateName(s.state);
    const char *sn = stageName(s.stage);
    if (busy && sn[0] && s.stage != 0)
        stage = sn;
    setText(w->stage, stage);
    setTextColor(w->stage, c);

    if (showJob)
    {
        setText(w->pct, String(s.percent) + "%");
        if (busy)
        {
            setText(w->remain, fmtDuration(s.remainingMin) + " left");
            setText(w->eta, fmtEta(s.remainingMin));
        }
        else
        {
            setText(w->remain, s.state == GState::Finish ? "Finished" : "Failed");
            setText(w->eta, "");
        }
        if (s.totalLayers > 0)
            setText(w->layer, "Layer " + String(s.layer) + " / " + String(s.totalLayers));
        else
            setText(w->layer, "");
        setText(w->job, s.job);
    }
    else
    {
        setText(w->pct, LV_SYMBOL_OK);
        setText(w->remain, "Ready");
        setText(w->eta, "");
        setText(w->layer, "");
        setText(w->job, "");
    }
    String t = s.nozzleCount > 1
                   ? "L " + fmtTemp(s.nozzleL) + "  R " + fmtTemp(s.nozzleR) + "   " LV_SYMBOL_DOWN " " + fmtTemp(s.bed)
                   : LV_SYMBOL_UP " " + fmtTemp(s.nozzle) + "   " LV_SYMBOL_DOWN " " + fmtTemp(s.bed);
    if (!isnan(s.chamber) && s.chamber > 0)
        t += "   " LV_SYMBOL_HOME " " + fmtTemp(s.chamber);
    setText(w->temps, t);
}

static void setTempBar(lv_obj_t *bar, float v, float target, float maxv)
{
    int val = isnan(v) ? 0 : constrain((int)(v / maxv * 1000), 0, 1000);
    lv_bar_set_value(bar, val, LV_ANIM_ON);
    bool heating = !isnan(target) && target > 0 && fabsf(target - v) > 3;
    uint32_t c = isnan(target) || target <= 0 ? C_IDLE : heating ? C_PAUSE : C_PRINT;
    lv_obj_set_style_bg_color(bar, col(c), LV_PART_INDICATOR);
}

static void refreshTemps(int printer, TempsW *w)
{
    PrinterState s = bambuGet(printer);
    auto withTarget = [](float v, float t) {
        String r = fmtTemp(v);
        if (!isnan(t) && t > 0)
            r += " / " + fmtTemp(t);
        return r;
    };
    setText(w->noz, withTarget(s.nozzle, s.nozzleTarget));
    setText(w->bed, withTarget(s.bed, s.bedTarget));
    setTempBar(w->nozBar, s.nozzle, s.nozzleTarget, 320);
    setTempBar(w->bedBar, s.bed, s.bedTarget, 120);
    String extra = isnan(s.chamber) || s.chamber <= 0 ? String("") : "Chamber " + fmtTemp(s.chamber);
    if (s.nozzleCount > 1)
    {
        // Big number = active nozzle; the idle one goes on the line below
        bool left = s.activeNozzle == 1;
        setText(w->nozLabel, left ? "LEFT NOZZLE (ACTIVE)" : "RIGHT NOZZLE (ACTIVE)");
        String other = (left ? "Right " : "Left ") + withTarget(left ? s.nozzleR : s.nozzleL,
                                                              left ? s.nozzleRTarget : s.nozzleLTarget);
        extra = extra.length() ? other + "   " + extra : other;
    }
    else
    {
        setText(w->nozLabel, "NOZZLE");
    }
    setText(w->chamber, extra);

    String fans;
    auto addFan = [&](const char *n, int v) {
        if (v < 0)
            return;
        if (fans.length())
            fans += "  ";
        fans += n;
        fans += " " + String(v) + "%";
    };
    addFan("Part", s.partFan);
    addFan("Aux", s.auxFan);
    addFan("Chmb", s.chamberFan);
    setText(w->fans, fans.length() ? LV_SYMBOL_REFRESH " " + fans : String(""));

    static const char *speeds[] = {"", "Silent", "Standard", "Sport", "Ludicrous"};
    String sp = s.speedLevel >= 1 && s.speedLevel <= 4 ? String("Speed: ") + speeds[s.speedLevel] : String("");
    if (s.wifiDbm)
        sp += (sp.length() ? "   " : "") + String(LV_SYMBOL_WIFI " ") + s.wifiDbm + " dBm";
    setText(w->speed, sp);
}

static void styleTray(lv_obj_t *c, lv_obj_t *pctLbl, const AmsTray &t, bool active)
{
    if (t.present)
    {
        uint32_t rgb = t.rgba >> 8;
        lv_obj_set_style_bg_color(c, col(rgb), 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        uint8_t r = rgb >> 16, g = rgb >> 8, b = rgb;
        bool light = (r * 299 + g * 587 + b * 114) / 1000 > 140;
        lv_obj_set_style_text_color(pctLbl, light ? lv_color_black() : lv_color_white(), 0);
        setText(pctLbl, t.remain >= 0 ? String(t.remain) : String(""));
    }
    else
    {
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(pctLbl, col(C_FAINT), 0);
        setText(pctLbl, "-");
    }
    lv_obj_set_style_border_color(c, active ? lv_color_white() : col(C_FAINT), 0);
    lv_obj_set_style_border_width(c, active ? 4 : 2, 0);
}

static void refreshAms(int printer, AmsW *w)
{
    PrinterState s = bambuGet(printer);
    // Up to two AMS units fit; AMS HT units may live in any slot
    int slots[2] = {-1, -1}, found = 0;
    for (int i = 0; i < MAX_AMS && found < 2; i++)
        if (s.ams[i].present)
            slots[found++] = i;
    int units = 0;
    for (int u = 0; u < 2; u++)
    {
        int slot = slots[u];
        bool show = slot >= 0;
        const AmsUnit &unit = s.ams[show ? slot : 0];
        units += show;
        auto vis = [show](lv_obj_t *o) {
            if (show)
                lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        };
        vis(w->row[u]);
        for (int t = 0; t < 4; t++)
        {
            if (!show)
                continue;
            bool used = t < unit.trayCount;
            int x = unit.trayCount == 1 ? 0 : -99 + t * 66;
            for (lv_obj_t *o : {w->tray[u][t], w->type[u][t]})
            {
                if (used)
                    lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
                else
                    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_align(w->tray[u][t], LV_ALIGN_CENTER, x, -6);
            lv_obj_align(w->type[u][t], LV_ALIGN_CENTER, x, 28);
            styleTray(w->tray[u][t], w->pct[u][t], unit.trays[t], s.trayNow == slot * 4 + t);
            setText(w->type[u][t], unit.trays[t].present ? unit.trays[t].type : "");
        }
        if (show)
        {
            String info = unit.hwId >= 128 ? String("AMS HT ") + String(unit.hwId - 127)
                                           : "AMS " + String(unit.hwId + 1);
            if (unit.humidityPct >= 0)
                info += "  " + String(unit.humidityPct) + "% RH";
            else if (unit.humidityLevel >= 0)
                info += "  humidity lvl " + String(unit.humidityLevel);
            if (!isnan(unit.tempC) && unit.tempC > 0)
                info += "  " + fmtTemp(unit.tempC);
            setText(w->unit[u], info);
        }
    }
    lv_obj_align(w->row[0], LV_ALIGN_CENTER, 0, units == 1 ? -14 : -58);
    if (units == 0)
        lv_obj_clear_flag(w->empty, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(w->empty, LV_OBJ_FLAG_HIDDEN);

    bool extActive = s.trayNow == 254;
    if (s.external.present)
    {
        lv_obj_set_style_bg_color(w->ext, col(s.external.rgba >> 8), 0);
        lv_obj_set_style_bg_opa(w->ext, LV_OPA_COVER, 0);
    }
    else
    {
        lv_obj_set_style_bg_opa(w->ext, LV_OPA_TRANSP, 0);
    }
    lv_obj_set_style_border_color(w->ext, extActive ? lv_color_white() : col(C_FAINT), 0);
    setText(w->extLbl, String("Ext ") + (s.external.present ? s.external.type : "-"));
}

static void refreshHealth(int printer, HealthW *w)
{
    PrinterState s = bambuGet(printer);
    String body;
    int worst = 5;
    for (int i = 0; i < s.hmsCount; i++)
    {
        const HmsEntry &h = s.hms[i];
        int sev = h.code >> 16; // 1 fatal, 2 serious, 3 common, 4 info
        worst = min(worst, sev);
        char b[48];
        static const char *sevName[] = {"", "FATAL", "SERIOUS", "WARN", "INFO"};
        snprintf(b, sizeof(b), "%s %04X_%04X_%04X_%04X\n", sev >= 1 && sev <= 4 ? sevName[sev] : "?",
                 (unsigned)(h.attr >> 16), (unsigned)(h.attr & 0xFFFF), (unsigned)(h.code >> 16),
                 (unsigned)(h.code & 0xFFFF));
        body += b;
    }
    if (s.printError)
    {
        char b[40];
        snprintf(b, sizeof(b), "Print error %04X_%04X\n", (unsigned)(s.printError >> 16),
                 (unsigned)(s.printError & 0xFFFF));
        body += b;
        worst = min(worst, 2);
    }

    if (s.link != Link::Online)
    {
        setText(w->headline, linkText(s));
        setTextColor(w->headline, stateRgb(s));
    }
    else if (worst <= 2)
    {
        setText(w->headline, LV_SYMBOL_WARNING " Attention");
        setTextColor(w->headline, C_FAIL);
    }
    else if (worst <= 4 && s.hmsCount)
    {
        setText(w->headline, LV_SYMBOL_BELL " Notices");
        setTextColor(w->headline, C_PAUSE);
    }
    else
    {
        setText(w->headline, LV_SYMBOL_OK " All good");
        setTextColor(w->headline, C_PRINT);
    }

    if (body.isEmpty())
        body = "No HMS messages\n";
    else
        body += "Look up codes at\nwiki.bambulab.com\n";
    body += "\n" + settings.printers[printer].host;
    if (s.wifiDbm)
        body += "  " LV_SYMBOL_WIFI " " + String(s.wifiDbm) + " dBm";
    setText(w->body, body);
}

static void updateQr(lv_obj_t *qr, String &cache, const String &data)
{
    if (cache == data)
        return;
    cache = data;
    lv_qrcode_update(qr, data.c_str(), data.length());
}

static void refreshSystem(SystemW *w)
{
    if (netTimeValid())
    {
        time_t now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        char b[32];
        strftime(b, sizeof(b), "%H:%M", &t);
        setText(w->clock, b);
        strftime(b, sizeof(b), "%A, %d %b", &t);
        setText(w->date, b);
    }
    else
    {
        setText(w->clock, "--:--");
        setText(w->date, "waiting for time");
    }

    if (netMode() == NetMode::Online)
        setText(w->wifi, LV_SYMBOL_WIFI " " + settings.wifiSsid + "  " + String(WiFi.RSSI()) + " dBm");
    else
        setText(w->wifi, netMode() == NetMode::SetupAP ? "Setup AP: " + netApSsid() : String("Wi-Fi connecting..."));

    QwiicReadings q = qwiicGet();
    String qs;
    if (q.hasClimate && !isnan(q.tempC))
    {
        qs = String(q.climateSensor) + " " + String(q.tempC, 1) + "\xC2\xB0";
        if (!isnan(q.humidity))
            qs += " " + String((int)lroundf(q.humidity)) + "%";
    }
    if (q.hasLight && !isnan(q.lux))
        qs += (qs.length() ? "  " : "") + String(q.lightSensor) + " " + String((int)q.lux) + " lx";
    if (qs.isEmpty())
        qs = q.foundCount ? "Qwiic: " + String(q.foundCount) + " unknown device(s)" : String("Qwiic: no sensor");
    setText(w->qwiic, qs);

    String ip = netIp();
    String url = ip.length() ? "http://" + ip + "/" : String("");
    if (url.length())
    {
        lv_obj_clear_flag(w->qr, LV_OBJ_FLAG_HIDDEN);
        updateQr(w->qr, w->qrData, url);
    }
    else
    {
        lv_obj_add_flag(w->qr, LV_OBJ_FLAG_HIDDEN);
    }
    setText(w->url, url.length() ? settings.hostname + ".local  " + ip : String(""));
    setText(w->panel, String("v" FW_VERSION "  ") + displayPanelName() + "  up " + String(millis() / 3600000) + "h");
}

static void refreshSetup(SetupW *w)
{
    if (netMode() == NetMode::SetupAP && netIp() == WiFi.softAPIP().toString())
    {
        setText(w->l1, "1. Scan to join Wi-Fi");
        updateQr(w->qr, w->qrData, "WIFI:S:" + netApSsid() + ";T:nopass;;");
        setText(w->l2, "Join " + netApSsid() + "\nthen open 192.168.4.1");
    }
    else if (netMode() == NetMode::Online)
    {
        String url = "http://" + netIp() + "/";
        setText(w->l1, "Scan to configure printers");
        updateQr(w->qr, w->qrData, url);
        setText(w->l2, url + "\n" + settings.hostname + ".local");
    }
    else
    {
        setText(w->l1, "Connecting to Wi-Fi...");
        setText(w->l2, settings.wifiSsid + "\nHold knob 8 s for setup mode");
    }
}

static void refreshPage(Page &p)
{
    switch (p.kind)
    {
    case PageKind::Overview: refreshOverview((OverviewW *)p.w); break;
    case PageKind::Status: refreshStatus(p.printer, (StatusW *)p.w); break;
    case PageKind::Temps: refreshTemps(p.printer, (TempsW *)p.w); break;
    case PageKind::Ams: refreshAms(p.printer, (AmsW *)p.w); break;
    case PageKind::Health: refreshHealth(p.printer, (HealthW *)p.w); break;
    case PageKind::System: refreshSystem((SystemW *)p.w); break;
    case PageKind::Setup: refreshSetup((SetupW *)p.w); break;
    }
}

// ---------------------------------------------------- page dots + navigation

static void rebuildDots()
{
    if (dots)
        lv_obj_del(dots);
    dots = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(dots);
    lv_obj_set_size(dots, 200, 12);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 6, 0);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_CLICKABLE);
    if (pages.size() < 2)
        return;
    for (size_t i = 0; i < pages.size(); i++)
    {
        lv_obj_t *d = lv_obj_create(dots);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 7, 7);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, col(C_FAINT), 0);
    }
}

static void updateDots()
{
    if (!dots)
        return;
    uint32_t n = lv_obj_get_child_cnt(dots);
    for (uint32_t i = 0; i < n; i++)
    {
        lv_obj_t *d = lv_obj_get_child(dots, i);
        const Page &p = pages[i];
        uint32_t c = C_FAINT;
        if ((int)i == current)
            c = p.printer >= 0 ? stateRgb(bambuGet(p.printer)) : C_TEXT;
        if (c == C_OFF && (int)i == current)
            c = C_TEXT;
        lv_obj_set_style_bg_color(d, col(c), 0);
        lv_obj_set_size(d, (int)i == current ? 10 : 7, (int)i == current ? 10 : 7);
    }
}

// Page transitions are done by sliding the page's content root, never with
// lv_scr_load_anim(): in LVGL 8.3.11 loading a screen while a screen animation is
// still running dereferences NULL (lv_disp.c: scr_to_load is cleared by
// scr_load_internal() and then used), which rebooted the device on fast knob spins.
static void slideX(void *o, int32_t v) { lv_obj_set_x((lv_obj_t *)o, v); }
static void slideY(void *o, int32_t v) { lv_obj_set_y((lv_obj_t *)o, v); }
static void fadeIn(void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, v, 0); }

static void resetRoot(lv_obj_t *root)
{
    lv_anim_del(root, nullptr);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_opa(root, LV_OPA_COVER, 0);
}

static void showPage(int idx, int dir)
{
    if (pages.empty())
        return;
    idx = constrain(idx, 0, (int)pages.size() - 1);
    int prev = current;
    bool changed = idx != current;
    current = idx;
    refreshPage(pages[current]);

    if (prev >= 0 && prev < (int)pages.size())
        resetRoot(pages[prev].root);
    lv_obj_t *root = pages[current].root;
    resetRoot(root);
    lv_scr_load(pages[current].scr); // immediate load: no pending LVGL screen animation

    uint32_t now = millis();
    if (changed && now - lastNavMs > 200) // fast spinning skips the animation
    {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, root);
        lv_anim_set_time(&a, 160);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        if (dir != 0)
        {
            int d = (dir > 0 ? 1 : -1) * 60;
            lv_anim_set_values(&a, d, 0);
            lv_anim_set_exec_cb(&a, abs(dir) == 2 ? slideY : slideX);
            lv_anim_start(&a);
        }
        lv_anim_set_values(&a, LV_OPA_20, LV_OPA_COVER);
        lv_anim_set_exec_cb(&a, fadeIn);
        lv_anim_start(&a);
    }
    lastNavMs = now;
    updateDots();
}

static void buildPages()
{
    lv_obj_t *blank = lv_obj_create(nullptr); // keep something loaded while deleting
    lv_obj_set_style_bg_color(blank, col(C_BG), 0);
    lv_scr_load(blank);
    for (Page &p : pages)
    {
        lv_obj_del(p.scr);
        switch (p.kind)
        {
        case PageKind::Overview: delete (OverviewW *)p.w; break;
        case PageKind::Status: delete (StatusW *)p.w; break;
        case PageKind::Temps: delete (TempsW *)p.w; break;
        case PageKind::Ams: delete (AmsW *)p.w; break;
        case PageKind::Health: delete (HealthW *)p.w; break;
        case PageKind::System: delete (SystemW *)p.w; break;
        case PageKind::Setup: delete (SetupW *)p.w; break;
        }
    }
    pages.clear();

    int enabled = 0;
    for (int i = 0; i < MAX_PRINTERS; i++)
        enabled += bambuEnabled(i);

    if (enabled == 0)
    {
        buildSetup();
    }
    else
    {
        if (enabled > 1)
            buildOverview();
        for (int i = 0; i < MAX_PRINTERS; i++)
        {
            if (!bambuEnabled(i))
                continue;
            buildStatus(i);
            buildTemps(i);
            buildAms(i);
            buildHealth(i);
        }
    }
    buildSystem();

    for (Page &p : pages)
    {
        lv_obj_set_style_translate_x(p.root, shiftX, 0);
        lv_obj_set_style_translate_y(p.root, shiftY, 0);
    }
    rebuildDots();
    current = 0;
    lv_scr_load(pages[0].scr);
    lv_obj_del(blank);
    refreshPage(pages[0]);
    updateDots();
}

// ============================================================ overlays

// ---- menu (roller driven by knob or touch)

struct MenuItem
{
    String label;
    std::function<void()> action;
};

static lv_obj_t *menuObj = nullptr;
static lv_obj_t *menuRoller = nullptr;
static std::vector<MenuItem> menuItems;

static void menuClose()
{
    if (menuObj)
        lv_obj_del_async(menuObj);
    menuObj = nullptr;
    menuRoller = nullptr;
}

static void menuExecute()
{
    if (!menuRoller)
        return;
    uint16_t sel = lv_roller_get_selected(menuRoller);
    std::function<void()> act = sel < menuItems.size() ? menuItems[sel].action : nullptr;
    menuClose();
    buzzerPlay(Sound::Click);
    if (act)
        act();
}

static lv_obj_t *mkOverlay(uint8_t opa)
{
    lv_obj_t *o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_center(o);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, col(C_BG), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE); // swallow touches beneath
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void menuOpen(const String &title, std::vector<MenuItem> items, int selected = 0)
{
    menuClose();
    menuItems = std::move(items);
    menuObj = mkOverlay(LV_OPA_COVER);
    mkLabel(menuObj, &lv_font_montserrat_20, C_DIM, -100, title.c_str());

    String opts;
    for (size_t i = 0; i < menuItems.size(); i++)
    {
        if (i)
            opts += "\n";
        opts += menuItems[i].label;
    }
    menuRoller = lv_roller_create(menuObj);
    lv_roller_set_options(menuRoller, opts.c_str(), LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(menuRoller, 3);
    lv_roller_set_selected(menuRoller, selected, LV_ANIM_OFF);
    lv_obj_set_width(menuRoller, 280);
    lv_obj_align(menuRoller, LV_ALIGN_CENTER, 0, 8);
    lv_obj_set_style_bg_opa(menuRoller, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(menuRoller, 0, 0);
    lv_obj_set_style_text_font(menuRoller, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(menuRoller, col(C_FAINT), 0);
    lv_obj_set_style_text_align(menuRoller, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(menuRoller, &lv_font_montserrat_24, LV_PART_SELECTED);
    lv_obj_set_style_text_color(menuRoller, col(C_TEXT), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(menuRoller, col(0x202024), LV_PART_SELECTED);
    lv_obj_set_style_radius(menuRoller, 12, LV_PART_SELECTED);

    lv_obj_t *ok = lv_btn_create(menuObj);
    lv_obj_set_size(ok, 110, 44);
    lv_obj_align(ok, LV_ALIGN_CENTER, 0, 118);
    lv_obj_set_style_bg_color(ok, col(C_PRINT), 0);
    lv_obj_set_style_radius(ok, 22, 0);
    lv_obj_t *okl = lv_label_create(ok);
    lv_label_set_text(okl, "Select");
    lv_obj_set_style_text_color(okl, lv_color_black(), 0);
    lv_obj_center(okl);
    lv_obj_add_event_cb(ok, [](lv_event_t *) { menuExecute(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *x = lv_btn_create(menuObj);
    lv_obj_set_size(x, 44, 44);
    lv_obj_align(x, LV_ALIGN_CENTER, 0, -146);
    lv_obj_set_style_bg_color(x, col(0x2A2A2E), 0);
    lv_obj_set_style_radius(x, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *xl = lv_label_create(x);
    lv_label_set_text(xl, LV_SYMBOL_CLOSE);
    lv_obj_center(xl);
    lv_obj_add_event_cb(x, [](lv_event_t *) { menuClose(); }, LV_EVENT_CLICKED, nullptr);
}

// ---- value adjuster (knob turns, touch drags the arc)

static lv_obj_t *adjObj = nullptr;
static lv_obj_t *adjArc = nullptr;
static lv_obj_t *adjVal = nullptr;
static std::function<void(int)> adjChange;
static std::function<String(int)> adjFormat;

static void adjClose()
{
    if (adjObj)
        lv_obj_del_async(adjObj);
    adjObj = nullptr;
    adjArc = nullptr;
    settingsSave();
}

static void adjApply(int v)
{
    lv_arc_set_value(adjArc, v);
    setText(adjVal, adjFormat(v));
    adjChange(v);
}

static void adjOpen(const char *title, int minV, int maxV, int value, std::function<String(int)> fmt,
                    std::function<void(int)> change)
{
    adjFormat = std::move(fmt);
    adjChange = std::move(change);
    adjObj = mkOverlay(LV_OPA_COVER);
    mkLabel(adjObj, &lv_font_montserrat_20, C_DIM, -60, title);
    adjArc = mkRing(adjObj, 340, 22, C_PRINT);
    lv_arc_set_range(adjArc, minV, maxV);
    lv_arc_set_value(adjArc, value);
    lv_obj_add_flag(adjArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(adjArc, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(adjArc, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(adjArc, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(adjArc, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(
        adjArc, [](lv_event_t *) { adjApply(lv_arc_get_value(adjArc)); }, LV_EVENT_VALUE_CHANGED, nullptr);
    adjVal = mkLabel(adjObj, &lv_font_montserrat_48, C_TEXT, 0, adjFormat(value).c_str());
    mkLabel(adjObj, &lv_font_montserrat_14, C_FAINT, 60, "turn knob  -  press to save");
    lv_obj_t *hit = mkLabel(adjObj, &lv_font_montserrat_16, C_PRINT, 110, LV_SYMBOL_OK " Done");
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hit, [](lv_event_t *) { adjClose(); }, LV_EVENT_CLICKED, nullptr);
}

// ---- alert

static lv_obj_t *alertObj = nullptr;
static lv_obj_t *alertRing = nullptr;
static int alertPrinter = -1;

void uiDismissAlert()
{
    if (!alertObj)
        return;
    lv_anim_del(alertRing, nullptr);
    lv_obj_del_async(alertObj);
    alertObj = nullptr;
    buzzerStop();
    int idx = pageFor(PageKind::Status, alertPrinter);
    if (idx >= 0)
        showPage(idx, 0);
}

bool uiAlertActive()
{
    return alertObj != nullptr;
}

void uiShowAlert(int printer, AlertKind kind, const String &title, const String &detail)
{
    if (alertObj)
    {
        lv_anim_del(alertRing, nullptr);
        lv_obj_del_async(alertObj);
    }
    menuClose();
    if (adjObj)
        adjClose();
    alertPrinter = printer;

    uint32_t c = kind == AlertKind::Finished ? C_DONE : kind == AlertKind::Paused ? C_PAUSE : C_FAIL;
    const char *icon = kind == AlertKind::Finished ? LV_SYMBOL_OK : kind == AlertKind::Paused ? LV_SYMBOL_PAUSE
                                                                                               : LV_SYMBOL_WARNING;
    alertObj = mkOverlay(LV_OPA_COVER);
    alertRing = lv_arc_create(alertObj);
    lv_obj_set_size(alertRing, 382, 382);
    lv_obj_center(alertRing);
    lv_arc_set_bg_angles(alertRing, 0, 360);
    lv_arc_set_value(alertRing, 0);
    lv_obj_remove_style(alertRing, nullptr, LV_PART_KNOB);
    lv_obj_remove_style(alertRing, nullptr, LV_PART_INDICATOR);
    lv_obj_clear_flag(alertRing, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(alertRing, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_color(alertRing, col(c), LV_PART_MAIN);

    // Breathing ring so the alert is visible from across the room
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, alertRing);
    lv_anim_set_values(&a, LV_OPA_20, LV_OPA_COVER);
    lv_anim_set_time(&a, kind == AlertKind::Error ? 450 : 1100);
    lv_anim_set_playback_time(&a, kind == AlertKind::Error ? 450 : 1100);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, [](void *o, int32_t v) { lv_obj_set_style_arc_opa((lv_obj_t *)o, v, LV_PART_MAIN); });
    lv_anim_start(&a);

    mkLabel(alertObj, &lv_font_montserrat_48, c, -82, icon);
    lv_obj_t *t = mkLabel(alertObj, &lv_font_montserrat_28, C_TEXT, -18, title.c_str());
    lv_obj_set_width(t, 300);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_t *d = mkLabel(alertObj, &lv_font_montserrat_16, C_DIM, 44, detail.c_str());
    lv_obj_set_width(d, 270);
    lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
    mkLabel(alertObj, &lv_font_montserrat_14, C_FAINT, 118, "press knob or tap");
    lv_obj_add_event_cb(
        alertObj,
        [](lv_event_t *) {
            if (touchAllowed())
                uiDismissAlert();
        },
        LV_EVENT_CLICKED, nullptr);
}

// ---- toast

static lv_obj_t *toastObj = nullptr;

void uiToast(const String &text, uint32_t rgb)
{
    if (toastObj)
        lv_obj_del(toastObj);
    toastObj = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(toastObj);
    lv_obj_set_style_bg_color(toastObj, col(0x1E1E22), 0);
    lv_obj_set_style_bg_opa(toastObj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(toastObj, 18, 0);
    lv_obj_set_style_border_color(toastObj, col(rgb), 0);
    lv_obj_set_style_border_width(toastObj, 2, 0);
    lv_obj_set_style_pad_hor(toastObj, 16, 0);
    lv_obj_set_style_pad_ver(toastObj, 8, 0);
    lv_obj_set_size(toastObj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(toastObj, LV_ALIGN_CENTER, 0, 150);
    lv_obj_clear_flag(toastObj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = lv_label_create(toastObj);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, col(C_TEXT), 0);
    lv_label_set_text(l, text.c_str());
    lv_timer_t *t = lv_timer_create(
        [](lv_timer_t *) {
            if (toastObj)
                lv_obj_del(toastObj);
            toastObj = nullptr;
        },
        3500, nullptr);
    lv_timer_set_repeat_count(t, 1);
}

// ============================================================ menus

static void openSpeedMenu(int printer, int currentLevel)
{
    std::vector<MenuItem> items;
    static const char *names[] = {"Silent", "Standard", "Sport", "Ludicrous"};
    for (int i = 0; i < 4; i++)
        items.push_back({names[i], [printer, i] {
                             bambuCommand(printer, PrinterCmd::Speed, i + 1);
                             uiToast(String("Speed: ") + names[i]);
                         }});
    menuOpen("Print speed", std::move(items), constrain(currentLevel - 1, 0, 3));
}

static void openPrinterMenu(int printer)
{
    PrinterState s = bambuGet(printer);
    std::vector<MenuItem> items;
    if (s.link == Link::Online)
    {
        if (s.state == GState::Running || s.state == GState::Prepare)
            items.push_back({LV_SYMBOL_PAUSE " Pause", [printer] {
                                 bambuCommand(printer, PrinterCmd::Pause);
                                 uiToast("Pause sent", C_PAUSE);
                             }});
        if (s.state == GState::Pause)
            items.push_back({LV_SYMBOL_PLAY " Resume", [printer] {
                                 bambuCommand(printer, PrinterCmd::Resume);
                                 uiToast("Resume sent");
                             }});
        if (gstateBusy(s.state))
        {
            int lvl = s.speedLevel;
            items.push_back({LV_SYMBOL_SETTINGS " Speed", [printer, lvl] { openSpeedMenu(printer, lvl); }});
            items.push_back({LV_SYMBOL_STOP " Stop print...", [printer] {
                                 menuOpen("Stop the print?",
                                          {{"Keep printing", nullptr},
                                           {LV_SYMBOL_STOP " Stop", [printer] {
                                                bambuCommand(printer, PrinterCmd::Stop);
                                                uiToast("Stop sent", C_FAIL);
                                            }}});
                             }});
        }
        bool lightOn = s.chamberLight;
        items.push_back({lightOn ? LV_SYMBOL_EYE_CLOSE " Light off" : LV_SYMBOL_EYE_OPEN " Light on", [printer, lightOn] {
                             bambuCommand(printer, lightOn ? PrinterCmd::LightOff : PrinterCmd::LightOn);
                         }});
        items.push_back({LV_SYMBOL_REFRESH " Refresh", [printer] {
                             bambuCommand(printer, PrinterCmd::PushAll);
                             uiToast("Refreshing...");
                         }});
    }
    items.push_back({LV_SYMBOL_SETTINGS " Device...", [] { openSystemMenu(); }});
    items.push_back({LV_SYMBOL_CLOSE " Close", nullptr});
    menuOpen(bambuName(printer), std::move(items));
}

static void openOverviewMenu()
{
    std::vector<MenuItem> items;
    for (int i = 0; i < MAX_PRINTERS; i++)
    {
        int idx = pageFor(PageKind::Status, i);
        if (idx >= 0)
            items.push_back({LV_SYMBOL_RIGHT " " + bambuName(i), [idx] { showPage(idx, 1); }});
    }
    items.push_back({LV_SYMBOL_SETTINGS " Device...", [] { openSystemMenu(); }});
    items.push_back({LV_SYMBOL_CLOSE " Close", nullptr});
    menuOpen("Printers", std::move(items));
}

static void openSystemMenu()
{
    std::vector<MenuItem> items;
    items.push_back({LV_SYMBOL_IMAGE " Brightness", [] {
                         adjOpen("Brightness", 5, 255, settings.brightness,
                                 [](int v) { return String(v * 100 / 255) + "%"; },
                                 [](int v) {
                                     settings.brightness = v;
                                     displaySetBrightness(v);
                                 });
                     }});
    items.push_back({LV_SYMBOL_VOLUME_MAX " Volume", [] {
                         adjOpen("Volume", 0, 3, settings.volume,
                                 [](int v) {
                                     static const char *n[] = {"Mute", "Low", "Medium", "High"};
                                     return String(n[v]);
                                 },
                                 [](int v) {
                                     settings.volume = v;
                                     buzzerSetVolume(v);
                                     buzzerPlay(Sound::Click, true);
                                 });
                     }});
    items.push_back({settings.knobClicks ? LV_SYMBOL_MUTE " Knob clicks off" : LV_SYMBOL_AUDIO " Knob clicks on", [] {
                         settings.knobClicks = !settings.knobClicks;
                         settingsSave();
                     }});
    items.push_back({LV_SYMBOL_BELL " Test alert", [] {
                         buzzerPlay(Sound::Finished, true);
                         uiShowAlert(0, AlertKind::Finished, "Test alert", "This is how a finished print looks");
                     }});
    items.push_back({LV_SYMBOL_WIFI " Wi-Fi setup", [] {
                         netStartSetupAp();
                         uiShowSetup();
                     }});
    items.push_back({LV_SYMBOL_POWER " Restart", [] { ESP.restart(); }});
    items.push_back({LV_SYMBOL_CLOSE " Close", nullptr});
    menuOpen("Device", std::move(items));
}

// ============================================================ public

void uiInit()
{
    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *th = lv_theme_default_init(disp, col(C_PRINT), col(C_PAUSE), true, &lv_font_montserrat_16);
    lv_disp_set_theme(disp, th);
    lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_TRANSP, 0);
    buildPages();
}

void uiRebuild()
{
    menuClose();
    buildPages();
}

void uiLoop()
{
    uint32_t now = millis();
    if (now - lastRefreshMs < 300)
        return;
    lastRefreshMs = now;
    if (!pages.empty())
        refreshPage(pages[current]);
    updateDots();
}

void uiOnKnob(int steps)
{
    if (steps == 0)
        return;
    if (settings.knobClicks)
        buzzerPlay(Sound::Tick);

    if (alertObj)
        return;
    if (adjObj)
    {
        int minV = lv_arc_get_min_value(adjArc), maxV = lv_arc_get_max_value(adjArc);
        int stepSize = max(1, (maxV - minV) / 40);
        adjApply(constrain(lv_arc_get_value(adjArc) + steps * stepSize, minV, maxV));
        return;
    }
    if (menuRoller)
    {
        int n = menuItems.size();
        int sel = constrain((int)lv_roller_get_selected(menuRoller) + steps, 0, n - 1);
        lv_roller_set_selected(menuRoller, sel, LV_ANIM_ON);
        return;
    }
    int n = pages.size();
    if (n < 2)
        return;
    int next = ((current + steps) % n + n) % n;
    showPage(next, steps > 0 ? 1 : -1);
}

void uiOnKnobEvent(KnobEvent e)
{
    if (e == KnobEvent::None)
        return;
    if (alertObj)
    {
        uiDismissAlert();
        return;
    }
    if (adjObj)
    {
        buzzerPlay(Sound::Click);
        adjClose();
        return;
    }
    if (menuObj)
    {
        if (e == KnobEvent::Click)
            menuExecute();
        else
            menuClose();
        return;
    }

    const Page &p = pages[current];
    switch (e)
    {
    case KnobEvent::Click: // press the screen once: menu for what's on screen
        buzzerPlay(Sound::Click);
        if (p.printer >= 0)
            openPrinterMenu(p.printer);
        else if (p.kind == PageKind::Overview)
            openOverviewMenu();
        else
            openSystemMenu();
        break;
    case KnobEvent::DoubleClick: // same page, other printer
    {
        buzzerPlay(Sound::Click);
        int idx = p.printer >= 0 ? otherPrinterPage(1) : pageFor(PageKind::Status, 0);
        if (idx < 0)
            idx = pageFor(PageKind::Status, 1);
        if (idx >= 0)
            showPage(idx, 2);
        break;
    }
    case KnobEvent::LongPress: // hold: home
        buzzerPlay(Sound::Click);
        showPage(0, -1);
        break;
    default:
        break;
    }
}

void uiSetPixelShift(int dx, int dy)
{
    shiftX = dx;
    shiftY = dy;
    for (Page &p : pages)
    {
        lv_obj_set_style_translate_x(p.root, dx, 0);
        lv_obj_set_style_translate_y(p.root, dy, 0);
    }
    if (dots)
    {
        lv_obj_set_style_translate_x(dots, dx, 0);
        lv_obj_set_style_translate_y(dots, dy, 0);
    }
}

void uiShowSetup()
{
    int idx = -1;
    for (size_t i = 0; i < pages.size(); i++)
        if (pages[i].kind == PageKind::Setup || pages[i].kind == PageKind::System)
        {
            idx = i;
            if (pages[i].kind == PageKind::Setup)
                break;
        }
    if (idx >= 0)
        showPage(idx, 0);
}

void uiBlockTouchUntil(uint32_t ms)
{
    touchBlockUntil(ms);
}

// ============================================================ OTA

static lv_obj_t *otaObj = nullptr, *otaRing = nullptr, *otaLbl = nullptr;

void uiOtaProgress(int pct)
{
    if (pct == -1)
    {
        if (otaObj)
            lv_obj_del(otaObj);
        otaObj = nullptr;
        uiToast("Update failed", C_FAIL);
        lv_refr_now(nullptr);
        return;
    }
    if (!otaObj)
    {
        menuClose();
        otaObj = mkOverlay(LV_OPA_COVER);
        otaRing = mkRing(otaObj, 340, 20, C_PREP);
        lv_arc_set_bg_angles(otaRing, 0, 360);
        lv_arc_set_rotation(otaRing, 270);
        mkLabel(otaObj, &lv_font_montserrat_20, C_DIM, -50, LV_SYMBOL_DOWNLOAD " Updating");
        otaLbl = mkLabel(otaObj, &lv_font_montserrat_48, C_TEXT, 10, "0%");
        mkLabel(otaObj, &lv_font_montserrat_14, C_FAINT, 70, "don't unplug");
        displayForceBrightness(max<uint8_t>(settings.brightness, 120));
    }
    static uint32_t lastDraw = 0;
    if (pct == -2)
    {
        // unknown size (web upload): animate the ring, throttle redraws
        if (millis() - lastDraw < 250)
            return;
        lv_arc_set_value(otaRing, (lv_arc_get_value(otaRing) + 7) % 100);
        setText(otaLbl, "...");
    }
    else
    {
        lv_arc_set_value(otaRing, pct);
        setText(otaLbl, String(pct) + "%");
    }
    lastDraw = millis();
    lv_refr_now(nullptr);
}
