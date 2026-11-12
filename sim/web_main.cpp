// Browser simulator: the real screen code (lib/gridui) compiled to WebAssembly with Emscripten.
// sim/web/index.html drives it: it sets the mix, price, time and plan, and draws the framebuffer on a canvas.
// Built by sim/build_web.sh (and on GitHub by the workflow).
#include <emscripten.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "gridui.h"
#include "parsers.h"
#include "settings.h"
#include "schedule.h"
#include "../test/test_gridcore/fixtures.h"

using namespace gridcore;
using namespace gridui;

static const int W = 480, H = 272;
static uint16_t fb[W * H];
static uint32_t now_ms = 0;
static bool flushed = false;
static int ptr_x = 0, ptr_y = 0;
static bool ptr_down = false;

// ---- everything the screen is built from (the page changes these)
static GridSnapshot snap;
static bool has_data = true;
static int weekday = 4, minute = 13 * 60 + 20;
static Settings settings;          // country, region and power plan, in the setup page's JSON format
static bool has_plan = true;
static bool view_country = false;
static char header[32] = "SA \xC2\xB7 Adelaide";
static char problem[64] = "";
static int data_age_min = 2;
static Model last;
static char out[1024];

static void flush_cb(lv_display_t *d, const lv_area_t *, uint8_t *) {
  flushed = true;
  lv_display_flush_ready(d);
}
static uint32_t tick_cb() { return now_ms; }
static void pointer_cb(lv_indev_t *, lv_indev_data_t *data) {
  data->point.x = ptr_x;
  data->point.y = ptr_y;
  data->state = ptr_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void on_view(bool country) {
  view_country = country;
  EM_ASM({ if (Module.onEvent) Module.onEvent("view", $0); }, country);
}
static void on_scene() {
  EM_ASM({ if (Module.onEvent) Module.onEvent("scene", 0); });
}

static void js_escape(const char *in, char *o, size_t cap) {
  size_t n = 0;
  for (; *in && n + 7 < cap; in++) {
    const unsigned char c = (unsigned char)*in;
    if (c == '"' || c == '\\') { o[n++] = '\\'; o[n++] = c; }
    else if (c < 0x20) n += snprintf(o + n, cap - n, "\\u%04x", c);
    else o[n++] = c;
  }
  o[n] = 0;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE void gm_init() {
  lv_init();
  lv_tick_set_cb(tick_cb);
  lv_display_t *d = lv_display_create(W, H);
  lv_display_set_flush_cb(d, flush_cb);
  lv_display_set_buffers(d, fb, nullptr, sizeof fb, LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_indev_t *in = lv_indev_create();
  lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(in, pointer_cb);
  Callbacks cb;
  cb.view_changed = on_view;
  cb.scene_tapped = on_scene;
  create(cb);
  settings.tou = demo_tariff();
}

// Advances the clock and runs LVGL. Returns 1 if the picture changed.
EMSCRIPTEN_KEEPALIVE int gm_tick(int ms) {
  flushed = false;
  for (int t = 0; t < ms; t += 5) { now_ms += 5; lv_timer_handler(); }
  return flushed;
}
EMSCRIPTEN_KEEPALIVE uint16_t *gm_fb() { return fb; }
EMSCRIPTEN_KEEPALIVE void gm_pointer(int x, int y, int down) { ptr_x = x; ptr_y = y; ptr_down = down; }

// ---- inputs
EMSCRIPTEN_KEEPALIVE void gm_set_mw(int src, float mw) { if (src >= 0 && src < SOURCE_COUNT) snap.mw[src] = mw < 0 ? 0 : mw; snap.valid = true; }
EMSCRIPTEN_KEEPALIVE void gm_set_charging(float mw) { snap.battery_charging = mw < 0 ? 0 : mw; }
EMSCRIPTEN_KEEPALIVE void gm_set_price(int has, float price) { snap.has_price = has; snap.price = price; }
EMSCRIPTEN_KEEPALIVE void gm_set_time(int wd, int min) { weekday = wd; minute = min; }
EMSCRIPTEN_KEEPALIVE void gm_set_has_data(int b) { has_data = b; }
EMSCRIPTEN_KEEPALIVE void gm_set_header(const char *s) { snprintf(header, sizeof header, "%s", s); }
EMSCRIPTEN_KEEPALIVE void gm_set_problem(const char *s) { snprintf(problem, sizeof problem, "%s", s); }
EMSCRIPTEN_KEEPALIVE void gm_set_age(int minutes) { data_age_min = minutes; }
EMSCRIPTEN_KEEPALIVE void gm_set_has_plan(int b) { has_plan = b; }
EMSCRIPTEN_KEEPALIVE void gm_set_view(int country) { view_country = country; }

// Settings JSON as the setup page saves it (gridmix.local/api/settings). Returns "" or the reason it was refused.
EMSCRIPTEN_KEEPALIVE const char *gm_set_settings(const char *json) {
  char err[96];
  if (!settings_from_json(json, strlen(json), settings, err, sizeof err)) { snprintf(out, sizeof out, "%s", err); return out; }
  out[0] = 0;
  return out;
}
EMSCRIPTEN_KEEPALIVE const char *gm_get_settings() {
  if (!settings_to_json(settings, true, out, sizeof out)) out[0] = 0;
  return out;
}

// Paste a real API answer. kind: 0 = Open Electricity generation, 1 = Open Electricity market, 2 = EIA.
EMSCRIPTEN_KEEPALIVE const char *gm_parse(int kind, const char *json) {
  const size_t len = strlen(json);
  if (kind == 1) {
    MarketInfo mk;
    if (!parse_openelectricity_market(json, len, snap.data_time, mk)) { snprintf(out, sizeof out, "Couldn't read that as Open Electricity market data."); return out; }
    merge_market(snap, mk);
    snprintf(out, sizeof out, "Market data added: price %s, rooftop forecast %s.", mk.has_price ? "yes" : "no", mk.has_rooftop_forecast ? "yes" : "no");
    return out;
  }
  GridSnapshot s;
  const bool ok = kind == 0 ? parse_openelectricity(json, len, s) : parse_eia(json, len, s);
  if (!ok) { snprintf(out, sizeof out, "Couldn't read that as %s data.", kind == 0 ? "Open Electricity generation" : "EIA"); return out; }
  snap = s;
  has_data = true;
  snprintf(out, sizeof out, "Read OK: %.0f MW total, %.0f%% renewable%s.", s.total(), s.renewable_pct(), s.estimated ? " (part estimated)" : "");
  return out;
}

// The two real captures the tests use
EMSCRIPTEN_KEEPALIVE void gm_load_real(int which) {
  GridSnapshot s;
  if (which == 0) {
    parse_openelectricity(OE_SA1_FUELTECH, strlen(OE_SA1_FUELTECH), s);
    MarketInfo mk;
    parse_openelectricity_market(OE_SA1_MARKET, strlen(OE_SA1_MARKET), s.data_time, mk);
    merge_market(s, mk);
  } else {
    parse_eia(EIA_ERCOT, strlen(EIA_ERCOT), s);
  }
  snap = s;
  has_data = true;
}

EMSCRIPTEN_KEEPALIVE void gm_load_demo(int i) {
  const DemoScene d = demo_scene(i);
  snap = demo_snapshot(d);
  has_data = true;
  weekday = d.weekday;
  minute = d.minute;
  snprintf(header, sizeof header, "%s", d.region);
}

// Builds the screen from the inputs, as the firmware does. Returns a JSON summary for the page.
EMSCRIPTEN_KEEPALIVE const char *gm_apply() {
  const bool us = settings.country[0] == 'U';
  const Tariff plan = settings.active_tariff();
  Model m = model_build(snap, has_data, header, weekday, minute, has_plan && settings.has_tariff() ? &plan : nullptr);
  m.view_country = view_country;
  char ago[24] = "Waiting for data";
  if (has_data) format_ago((int64_t)data_age_min * 60, ago, sizeof ago);
  const bool fresh = has_data && data_is_fresh(us ? SRC_EIA : SRC_OPEN_ELECTRICITY, (int64_t)data_age_min * 60);
  model_views(m, region_label(settings.country, settings.region), us ? "My region \xC2\xB7 EIA" : "My region \xC2\xB7 state grid",
              us ? "United States" : "Australia", us ? "Lower 48 states (EIA)" : "East coast grid (NEM)",
              us ? "Hourly" : "Every 10 min", ago, fresh);
  if (problem[0] && !has_data) {
    snprintf(m.advice, sizeof m.advice, "%s", problem);
    m.tone = TONE_BAD;
  }
  update(m);
  last = m;

  char adv[160], per[40];
  js_escape(m.advice, adv, sizeof adv);
  js_escape(m.period, per, sizeof per);
  const GridStatus g = has_data ? grid_status(snap) : GRID_UNKNOWN;
  int n = snprintf(out, sizeof out,
                   "{\"renewable\":%.1f,\"total\":%.0f,\"status\":\"%s\",\"has_rate\":%s,\"rate\":\"%s\",\"period\":\"%s\","
                   "\"band\":%d,\"advice\":\"%s\",\"tone\":%d,\"sky\":%d,\"clock\":\"%s\",\"estimated\":%s,\"shares\":[",
                   has_data ? snap.renewable_pct() : 0.0f, snap.total(), g == GRID_UNKNOWN ? "" : grid_status_label(g),
                   m.has_rate ? "true" : "false", m.rate, per, (int)m.band, adv, (int)m.tone, (int)m.sky, m.clock,
                   snap.estimated ? "true" : "false");
  for (int i = 0; i < SOURCE_COUNT && n < (int)sizeof out - 16; i++)
    n += snprintf(out + n, sizeof out - n, "%s%.1f", i ? "," : "", has_data ? snap.share_pct((Source)i) : 0.0f);
  snprintf(out + n, sizeof out - n, "]}");
  return out;
}

// Current inputs, so the page's sliders can follow a pasted or sample scene
EMSCRIPTEN_KEEPALIVE const char *gm_state() {
  char h[64];
  js_escape(header, h, sizeof h);
  int n = snprintf(out, sizeof out, "{\"weekday\":%d,\"minute\":%d,\"has_data\":%s,\"has_price\":%s,\"price\":%.1f,\"charging\":%.1f,\"header\":\"%s\",\"mw\":[",
                   weekday, minute, has_data ? "true" : "false", snap.has_price ? "true" : "false", snap.price, snap.battery_charging, h);
  for (int i = 0; i < SOURCE_COUNT; i++) n += snprintf(out + n, sizeof out - n, "%s%.1f", i ? "," : "", snap.mw[i]);
  snprintf(out + n, sizeof out - n, "]}");
  return out;
}

}  // extern "C"
