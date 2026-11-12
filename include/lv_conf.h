// LVGL 9 configuration for Grid Mix. Anything not set here uses LVGL's default.
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// Use the normal C allocator; on the S3 with PSRAM enabled, large blocks land in PSRAM
#define LV_USE_STDLIB_MALLOC   LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING   LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF  LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 16   // aim for ~60 fps; the panel decides what we really get

// FPS / CPU overlay in the corner, handy for the bring-up test. Turn off for release.
#define LV_USE_SYSMON        1
#define LV_USE_PERF_MONITOR  1

#define LV_USE_LOG 0

// Fonts used by the screens
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif // LV_CONF_H
#endif
