// LVGL 8.3 configuration for the T-Encoder-Pro (390x390 AMOLED, PSRAM).
// Anything not set here falls back to lv_conf_internal.h defaults.
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_COLOR_SCREEN_TRANSP 0

// Large heap for widgets, placed in PSRAM via the stdlib allocator
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE "lv_psram_alloc.h"
#define LV_MEM_CUSTOM_ALLOC lv_psram_malloc
#define LV_MEM_CUSTOM_FREE lv_psram_free
#define LV_MEM_CUSTOM_REALLOC lv_psram_realloc

#define LV_DISP_DEF_REFR_PERIOD 20
#define LV_INDEV_DEF_READ_PERIOD 20

#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DPI_DEF 200

#define LV_DRAW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_CIRCLE_CACHE_SIZE 8
#define LV_IMG_CACHE_DEF_SIZE 0
#define LV_GRADIENT_MAX_STOPS 2
#define LV_DISP_ROT_MAX_BUF (10 * 1024)

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#define LV_SPRINTF_CUSTOM 0
#define LV_SPRINTF_USE_FLOAT 0
#define LV_USE_USER_DATA 1

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_USE_LABEL 1
#define LV_LABEL_TEXT_SELECTION 0
#define LV_LABEL_LONG_TXT_HINT 1
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BTN 1
#define LV_USE_ROLLER 1
#define LV_ROLLER_INF_PAGES 7
#define LV_USE_LINE 1
#define LV_USE_IMG 1
#define LV_USE_SLIDER 1
#define LV_USE_TABLE 0
#define LV_USE_TEXTAREA 0
#define LV_USE_CHART 0
#define LV_USE_CALENDAR 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 1
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_SPAN 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN 0
#define LV_USE_LED 0

#define LV_USE_QRCODE 1

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 0
#define LV_THEME_DEFAULT_TRANSITION_TIME 80

#define LV_USE_FLEX 1
#define LV_USE_GRID 0

#endif // LV_CONF_H
