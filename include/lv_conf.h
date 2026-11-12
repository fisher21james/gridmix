// LVGL 9 configuration, shared by the firmware and the simulator.
// Anything not set here uses LVGL's default. Unused widgets are switched off to save flash.
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#define LV_USE_FLOAT 1               // smooth turbine blades and other rotating lines

#define LV_USE_STDLIB_MALLOC   LV_STDLIB_CLIB   // on the board, big blocks land in PSRAM
#define LV_USE_STDLIB_STRING   LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF  LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 16

#ifndef GRIDMIX_SIM
// FPS / CPU overlay in the corner while we're testing on the real board. Turn off for release.
#define LV_USE_SYSMON        1
#define LV_USE_PERF_MONITOR  1
#endif

#define LV_USE_LOG 0

// Fonts: our own (lib/gridui/fonts), no built-in Montserrat
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(gm_inter_12)
#define LV_FONT_DEFAULT &gm_inter_12

// Widgets we use: obj, label, line, image. Everything else off.
#define LV_USE_ANIMIMG    0
#define LV_USE_ARC        0
#define LV_USE_ARCLABEL   0
#define LV_USE_BAR        0
#define LV_USE_BUTTON     0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR   0
#define LV_USE_CANVAS     0
#define LV_USE_CHART      0
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD   0
#define LV_USE_LED        0
#define LV_USE_LIST       0
#define LV_USE_MENU       0
#define LV_USE_MSGBOX     0
#define LV_USE_ROLLER     0
#define LV_USE_SCALE      0
#define LV_USE_SLIDER     0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0
#define LV_USE_SWITCH     0
#define LV_USE_TEXTAREA   0
#define LV_USE_TABLE      0
#define LV_USE_TABVIEW    0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0
#define LV_USE_GRID       0
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE  0
#define LV_USE_THEME_MONO    0
#define LV_USE_XML        0

#endif // LV_CONF_H
#endif
