#include "display.h"
#include "board_pins.h"
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <lvgl.h>

static Arduino_DataBus *bus = nullptr;
static Arduino_GFX *gfx = nullptr;
static PanelType panel = PanelType::Unknown;
static bool rot180 = false;

static uint8_t briCurrent = 0;
static uint8_t briTarget = 0;
static bool panelAwake = true;
static uint32_t lastFadeMs = 0;

static volatile bool touchDown = false;
static uint32_t touchActivityMs = 0;
static uint32_t touchBlockedUntil = 0;
static lv_indev_t *touchIndev = nullptr;

// ---------------------------------------------------------------- touch

static bool i2cProbe(uint8_t addr)
{
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

static void touchReset()
{
    pinMode(PIN_TP_RST, OUTPUT);
    digitalWrite(PIN_TP_RST, LOW);
    delay(10);
    digitalWrite(PIN_TP_RST, HIGH);
    delay(60);
}

static void cst816Init()
{
    // Disable auto-sleep (0xFE) so the chip keeps answering on I2C.
    Wire.beginTransmission(TP_ADDR_CST816);
    Wire.write(0xFE);
    Wire.write(0x01);
    Wire.endTransmission();
}

static bool cst816Read(int16_t &x, int16_t &y)
{
    Wire.beginTransmission(TP_ADDR_CST816);
    Wire.write(0x02);
    if (Wire.endTransmission(false) != 0)
        return false;
    if (Wire.requestFrom((uint8_t)TP_ADDR_CST816, (uint8_t)5) != 5)
        return false;
    uint8_t n = Wire.read();
    uint8_t xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
    if ((n & 0x0F) == 0)
        return false;
    x = ((xh & 0x0F) << 8) | xl;
    y = ((yh & 0x0F) << 8) | yl;
    return true;
}

static bool chsc5816Read(int16_t &x, int16_t &y)
{
    // 32-bit register address, big-endian: point report at 0x2000002C
    static const uint8_t reg[4] = {0x20, 0x00, 0x00, 0x2C};
    Wire.beginTransmission(TP_ADDR_CHSC5816);
    Wire.write(reg, 4);
    if (Wire.endTransmission(false) != 0)
        return false;
    if (Wire.requestFrom((uint8_t)TP_ADDR_CHSC5816, (uint8_t)8) != 8)
        return false;
    uint8_t d[8];
    for (auto &b : d)
        b = Wire.read();
    // d[0]=status d[1]=fingers d[2]=x_l8 d[3]=y_l8 d[4]=z d[5]=x_h4|y_h4<<4 d[6]=id|event<<4
    if ((d[0] == 0xFF && d[1] == 0) || d[1] == 0)
        return false;
    uint8_t event = d[6] >> 4;
    if (event == 0x04) // lift-off
        return false;
    x = d[2] | ((d[5] & 0x0F) << 8);
    y = d[3] | ((d[5] >> 4) << 8);
    return true;
}

static void lvTouchRead(lv_indev_drv_t *, lv_indev_data_t *data)
{
    int16_t x = 0, y = 0;
    bool pressed = false;
    if (panel == PanelType::CO5300_CST816)
        pressed = cst816Read(x, y);
    else if (panel == PanelType::SH8601_CHSC5816)
        pressed = chsc5816Read(x, y);

    touchDown = pressed;
    if (pressed)
        touchActivityMs = millis();
    if (touchBlocked())
    {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    if (pressed)
    {
        if (rot180)
        {
            x = LCD_WIDTH - 1 - x;
            y = LCD_HEIGHT - 1 - y;
        }
        data->point.x = constrain(x, 0, LCD_WIDTH - 1);
        data->point.y = constrain(y, 0, LCD_HEIGHT - 1);
        data->state = LV_INDEV_STATE_PR;
    }
    else
    {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ---------------------------------------------------------------- panel

static void writeBrightnessRaw(uint8_t v)
{
    // Both SH8601 and CO5300 use DCS 0x51 (write display brightness)
    bus->beginWrite();
    bus->writeC8D8(0x51, v);
    bus->endWrite();
}

static void lvFlush(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px)
{
    uint32_t w = a->x2 - a->x1 + 1;
    uint32_t h = a->y2 - a->y1 + 1;
    if (panelAwake)
        gfx->draw16bitRGBBitmap(a->x1, a->y1, (uint16_t *)&px->full, w, h);
    lv_disp_flush_ready(drv);
}

// Both controllers require even start coordinates and even width/height.
static void lvRounder(lv_disp_drv_t *, lv_area_t *a)
{
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
}

void displayInit(bool rotate180)
{
    rot180 = rotate180;

    pinMode(PIN_LCD_EN, OUTPUT);
    digitalWrite(PIN_LCD_EN, HIGH);
    delay(20);

    // Identify the panel revision from its touch controller.
    Wire.begin(PIN_TP_SDA, PIN_TP_SCL, 400000);
    touchReset();
    if (i2cProbe(TP_ADDR_CST816))
        panel = PanelType::CO5300_CST816;
    else if (i2cProbe(TP_ADDR_CHSC5816))
        panel = PanelType::SH8601_CHSC5816;
    else
    {
        // Retry once: CST816 can be slow to come out of reset
        delay(150);
        if (i2cProbe(TP_ADDR_CST816))
            panel = PanelType::CO5300_CST816;
        else if (i2cProbe(TP_ADDR_CHSC5816))
            panel = PanelType::SH8601_CHSC5816;
    }
    Serial.printf("[display] panel: %s\n", displayPanelName());
    if (panel == PanelType::CO5300_CST816)
        cst816Init();

    bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_SDIO0, PIN_LCD_SDIO1,
                                PIN_LCD_SDIO2, PIN_LCD_SDIO3);
    if (panel == PanelType::SH8601_CHSC5816)
        gfx = new Arduino_SH8601(bus, PIN_LCD_RST, 0, false, LCD_WIDTH, LCD_HEIGHT);
    else // CO5300 is the current production panel, also the fallback
        gfx = new Arduino_CO5300(bus, PIN_LCD_RST, 0, false, LCD_WIDTH, LCD_HEIGHT, 0, 0, 0, 0);

    gfx->begin(40000000);
    writeBrightnessRaw(0);
    if (rot180)
        gfx->setRotation(2);
    gfx->fillScreen(0x0000);

    // LVGL
    lv_init();
    static lv_disp_draw_buf_t drawBuf;
    const size_t lines = 98; // 4 strips per frame
    const size_t px = LCD_WIDTH * lines;
    auto *b1 = (lv_color_t *)heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    auto *b2 = (lv_color_t *)heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_disp_draw_buf_init(&drawBuf, b1, b2, px);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = LCD_WIDTH;
    dispDrv.ver_res = LCD_HEIGHT;
    dispDrv.flush_cb = lvFlush;
    dispDrv.rounder_cb = lvRounder;
    dispDrv.draw_buf = &drawBuf;
    lv_disp_drv_register(&dispDrv);

    static lv_indev_drv_t touchDrv;
    lv_indev_drv_init(&touchDrv);
    touchDrv.type = LV_INDEV_TYPE_POINTER;
    touchDrv.read_cb = lvTouchRead;
    touchIndev = lv_indev_drv_register(&touchDrv);
}

void displaySetBrightness(uint8_t level)
{
    briTarget = level;
}

void displayForceBrightness(uint8_t level)
{
    if (level > 0 && !panelAwake)
    {
        gfx->displayOn();
        panelAwake = true;
        lv_obj_invalidate(lv_scr_act());
    }
    briTarget = briCurrent = level;
    writeBrightnessRaw(level);
}

uint8_t displayGetBrightness()
{
    return briTarget;
}

bool displayIsOn()
{
    return panelAwake && briTarget > 0;
}

void displayLoop()
{
    uint32_t now = millis();
    if (briCurrent == briTarget || now - lastFadeMs < 8)
        return;
    lastFadeMs = now;

    if (briTarget > 0 && !panelAwake)
    {
        gfx->displayOn();
        panelAwake = true;
        lv_obj_invalidate(lv_scr_act()); // panel RAM may be stale after sleep
    }

    int diff = (int)briTarget - (int)briCurrent;
    int step = max(1, abs(diff) / 6);
    briCurrent += diff > 0 ? min(step, diff) : -min(step, -diff);
    writeBrightnessRaw(briCurrent);

    if (briCurrent == 0 && briTarget == 0 && panelAwake)
    {
        gfx->displayOff();
        panelAwake = false;
    }
}

PanelType displayPanelType()
{
    return panel;
}

const char *displayPanelName()
{
    switch (panel)
    {
    case PanelType::SH8601_CHSC5816:
        return "SH8601 + CHSC5816";
    case PanelType::CO5300_CST816:
        return "CO5300 + CST816";
    default:
        return "unknown (assuming CO5300)";
    }
}

bool touchIsPressed()
{
    return touchDown;
}

uint32_t touchLastActivityMs()
{
    return touchActivityMs;
}

void touchBlockUntil(uint32_t ms)
{
    // Cancel a press LVGL is tracking so the coming release isn't treated as a tap
    if (ms && !touchBlocked() && touchIndev)
        lv_indev_wait_release(touchIndev);
    touchBlockedUntil = ms;
}

bool touchBlocked()
{
    return touchBlockedUntil && (int32_t)(millis() - touchBlockedUntil) < 0;
}
