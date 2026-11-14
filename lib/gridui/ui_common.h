// Shared drawing helpers and palette for the screens (internal to lib/gridui).
#pragma once
#include <math.h>
#include <stdio.h>
#include "gridui.h"
#include "fonts/gm_fonts.h"

extern "C" void lv_image_cache_drop(const void *src);   // public in LVGL 9.6 but not in lvgl.h

namespace gridui {

using namespace gridcore;

// ---------------------------------------------------------------- palette

static inline lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

struct SourceStyle { uint32_t color, text; };
static const SourceStyle SRC_STYLE[SOURCE_COUNT] = {
  {0xF5B800, 0x2E2300},   // solar
  {0x4FB3D9, 0x03202C},   // wind
  {0x2E6FD8, 0xFFFFFF},   // hydro
  {0x9B7BEA, 0x160B33},   // battery
  {0x3BB273, 0x03200E},   // nuclear
  {0xF07A3A, 0x2B1100},   // gas
  {0x8C7B6E, 0x1A120C},   // coal
  {0x6E7783, 0xFFFFFF},   // other
  {0x8DB255, 0x162204},   // biomass
  {0xE07BA0, 0x2A0A15},   // imports
  {0x1AA59A, 0x02201D},   // geothermal (teal, like the hot pools)
};

static const uint32_t BG = 0x0E1116, MUTED = 0xA9B1BC, TEXT = 0xE6EAF0;

// ---------------------------------------------------------------- small builders

static inline lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, lv_opa_t opa = LV_OPA_COVER) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_scrollable(o, false);
  lv_obj_set_clickable(o, false);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, hex(color), 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
  return o;
}

static inline lv_obj_t *circle(lv_obj_t *parent, float cx, float cy, float r, uint32_t color) {
  const int d = (int)lroundf(r * 2);
  lv_obj_t *o = box(parent, (int)lroundf(cx - r), (int)lroundf(cy - r), d, d, color);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  return o;
}

static inline lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text = "") {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, hex(color), 0);
  lv_label_set_text(l, text);
  return l;
}

// A fixed label in English that follows the screen language (see set_language)
void remember_translation(lv_obj_t *l, const char *en);
static inline lv_obj_t *label_tr(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *en) {
  lv_obj_t *l = label(parent, font, color, gridcore::T(en));
  remember_translation(l, en);
  return l;
}

static inline lv_obj_t *row(lv_obj_t *parent, int gap) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_scrollable(o, false);
  lv_obj_set_clickable(o, false);
  lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(o, gap, 0);
  return o;
}

static inline lv_obj_t *pill(lv_obj_t *parent, const lv_font_t *font) {
  lv_obj_t *l = label(parent, font, 0xFFFFFF);
  lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(l, 8, 0);
  lv_obj_set_style_pad_hor(l, 6, 0);
  lv_obj_set_style_pad_ver(l, 1, 0);
  return l;
}


static inline lv_obj_t *clickable(lv_obj_t *o, lv_event_cb_t cb, void *user = nullptr) {
  lv_obj_set_clickable(o, true);
  lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, user);
  return o;
}

static inline int text_width(const char *s, const lv_font_t *font) {   // ASCII only
  int w = 0;
  for (; *s; s++) w += lv_font_get_glyph_width(font, (uint32_t)(uint8_t)*s, (uint32_t)(uint8_t)s[1]);
  return w;
}

static inline void band_colors(PriceBand b, bool flat, uint32_t &bg, uint32_t &fg) {
  if (flat) { bg = 0x2A303A; fg = 0xE6EAF0; return; }
  if (b == BAND_LOW) { bg = 0x3BB273; fg = 0x04210F; }
  else if (b == BAND_HIGH) { bg = 0xD2453A; fg = 0xFFFFFF; }
  else { bg = 0xE39B3A; fg = 0x2B1700; }
}

static inline uint32_t freshness_color(Freshness f) { return f == FRESH_OK ? 0x3BB273 : f == FRESH_LATE ? 0xE39B3A : 0xE0634F; }

// The detail and tariff screens (screens.cpp)
void screens_build(lv_obj_t *scr);
void screens_update(const Model &m);
void show_detail();
void show_tariff();
bool screens_open();

}  // namespace gridui
