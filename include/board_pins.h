#pragma once
// LilyGO T-Encoder-Pro V1.0 pin map (verified against [SCH][T-Encoder-Pro_V1.0].pdf)

// AMOLED, QSPI (SH8601 or CO5300 depending on panel revision)
#define PIN_LCD_SDIO0 11
#define PIN_LCD_SDIO1 13
#define PIN_LCD_SDIO2 7
#define PIN_LCD_SDIO3 14
#define PIN_LCD_SCLK 12
#define PIN_LCD_CS 10
#define PIN_LCD_RST 4
#define PIN_LCD_EN 3 // VCI_EN: panel power rail
#define LCD_WIDTH 390
#define LCD_HEIGHT 390

// Touch controller (CHSC5816 @0x2E on SH8601 panels, CST816 @0x15 on CO5300 panels)
#define PIN_TP_SDA 5
#define PIN_TP_SCL 6
#define PIN_TP_INT 9
#define PIN_TP_RST 8
#define TP_ADDR_CST816 0x15
#define TP_ADDR_CHSC5816 0x2E

// Rotary encoder; the knob push switch shares GPIO0 (BOOT strap, active low)
#define PIN_KNOB_A 1
#define PIN_KNOB_B 2
#define PIN_KNOB_KEY 0

// Passive speaker driven through an S8050 transistor
#define PIN_BUZZER 17

// Qwiic / STEMMA QT connector (CN1), separate bus from touch, 10k pull-ups on board
#define PIN_QWIIC_SDA 16
#define PIN_QWIIC_SCL 15

// GPIO18 is routed to the panel FPC "LED" pad but is not populated (NC) on V1.0.
