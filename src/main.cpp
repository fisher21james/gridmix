// Grid Mix Display firmware
//
//   - 480x272 QSPI display (NV3041A) and GT911 touch, driven by LVGL 9
//   - Screens come from lib/gridui, the same code the simulator runs
//   - Wi-Fi details arrive from the browser installer (Improv); settings from the phone page at gridmix.local
//   - Live data is fetched in the background (src/fetcher.cpp); until a key is set, sample scenes play

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "improv_serial.h"
#include "board_config.h"
#include "app_state.h"
#include "settings_store.h"
#include "setup_server.h"
#include "fetcher.h"
#include "gridui.h"
#include "data_cache.h"
#include "health.h"
#include "ota.h"

#if HAS_GT911_TOUCH
#include <Wire.h>
#include <TAMC_GT911.h>
#endif

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

using namespace gridcore;

// ---------- Display ----------
Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
Arduino_GFX *gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED /* no reset pin */, 0 /* rotation */, true /* IPS */);

#if HAS_GT911_TOUCH
TAMC_GT911 touch(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, LCD_WIDTH, LCD_HEIGHT);
#endif

// ---------- Settings (from the phone setup page) ----------
gridcore::Settings g_settings;
AppStatus g_status;
static bool net_started = false;
static bool view_country = false;     // what's on screen now (starts from the saved setting)
static bool dirty = true;             // rebuild the screen on the next pass
static void history_clear();

static bool is_night() {
  const time_t now = time(nullptr);
  if (now < 1700000000) return false;
  struct tm lt;
  localtime_r(&now, &lt);
  return lt.tm_hour >= 22 || lt.tm_hour < 6;
}

static void apply_brightness() {
  uint32_t pct = g_settings.brightness;
  if (g_settings.night_dim && is_night() && pct > 15) pct = 15;
  ledcWrite(LCD_BL, pct * 255 / 100);
}

static void apply_settings() {
  setenv("TZ", g_settings.tz, 1);
  tzset();
  apply_brightness();
}

void on_settings_changed() {
  apply_settings();
  view_country = g_settings.view_country;
  history_clear();
  fetcher_reset(view_country);
  dirty = true;
}

// ---------- LVGL glue ----------
static uint32_t tick_cb() { return millis(); }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, lv_area_get_width(area), lv_area_get_height(area));
  lv_display_flush_ready(disp);
}

#if HAS_GT911_TOUCH
static void touch_read_cb(lv_indev_t *, lv_indev_data_t *data) {
  touch.read();
  if (touch.isTouched) {
    data->point.x = touch.points[0].x;
    data->point.y = touch.points[0].y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}
#endif

// ---------- Building what the screen shows ----------
static const char *DOT = "\xC2\xB7";

static void header_for(char *out, size_t cap) {
  if (view_country) { snprintf(out, cap, "%s", country_info(g_settings.country).whole_header); return; }
  if (g_settings.country[0] != 'A') { snprintf(out, cap, "%s", region_label(g_settings.country, g_settings.region)); return; }
  static const char *AU[][3] = {{"SA1", "SA", "Adelaide"}, {"NSW1", "NSW", "Sydney"}, {"VIC1", "VIC", "Melbourne"},
                                {"QLD1", "QLD", "Brisbane"}, {"TAS1", "TAS", "Hobart"}, {"WEM", "WA", "Perth"}};
  for (auto &r : AU)
    if (!strcmp(r[0], g_settings.region)) { snprintf(out, cap, "%s %s %s", r[1], DOT, r[2]); return; }
  snprintf(out, cap, "%s", g_settings.region);
}

static void setup_hint(char *out, size_t cap) {
  snprintf(out, cap, "%s", T(WiFi.status() != WL_CONNECTED ? "Sample data \xC2\xB7 connect Wi-Fi with the installer"
                                                            : "Sample data \xC2\xB7 finish setup at gridmix.local"));
}

static int demo_index = 0;
static uint32_t demo_since = 0;

// Renewable share over the last 24 hours, one slot per half hour (for the detail screen's chart).
// Starts empty after power-up and fills in as data arrives.
// Kept with the last data in flash (data_cache), so a restart doesn't lose it.
static CachedData store;   // .hist / .hist_slot: ring of half-hour slots; .snap: last good data
static void history_clear() { for (auto &s : store.hist_slot) s = -1; }
static void history_record(int64_t data_time, float renewable_pct) {
  const int64_t slot = data_time / 1800;
  const int N = gridui::HISTORY_POINTS;
  store.hist[slot % N] = renewable_pct;
  store.hist_slot[slot % N] = slot;
}
// Hourly points from the history request fill the half hours they cover, without replacing
// anything already recorded from live data (which is more detailed)
// Newest first, so with half-hourly points (Britain) each point's second half is already taken by
// the next point and stays as it is; with hourly points (Australia) it fills the gap.
static void history_import(const LiveData &d) {
  const int N = gridui::HISTORY_POINTS;
  for (int i = d.history_count - 1; i >= 0; i--) {
    for (int half = 0; half < 2; half++) {
      const int64_t slot = d.history[i].time / 1800 + half;
      if (store.hist_slot[slot % N] == slot) continue;
      store.hist[slot % N] = d.history[i].renewable_pct;
      store.hist_slot[slot % N] = slot;
    }
  }
}

static void history_to_model(gridui::Model &m, int64_t data_time) {
  const int64_t slot = data_time / 1800;
  const int N = gridui::HISTORY_POINTS;
  for (int i = 0; i < N; i++) {
    const int64_t want = slot - (N - 1) + i;
    m.history[i] = store.hist_slot[want % N] == want ? store.hist[want % N] : NAN;
  }
}

// What the cached data is for; a change of region or view makes the cache stale
static void feed_key(char *out, size_t cap) {
  snprintf(out, cap, "%s/%s/%d", g_settings.country, g_settings.region, view_country ? 1 : 0);
}

static gridui::Model build_model() {
  const CountryInfo &ci = country_info(g_settings.country);
  gridui::set_currency(ci.currency);
  gridui::set_language(g_settings.local_lang ? lang_from_code(ci.lang) : LANG_EN);   // before any text is made

  if (!g_settings.ready()) {   // not set up yet: play the sample scenes so the display isn't blank on day one
    gridui::Model m = gridui::demo_model(demo_index);
    setup_hint(m.advice, sizeof m.advice);
    m.tone = gridui::TONE_OK;
    return m;
  }

  LiveData d;
  fetcher_get(d);
  const time_t now = time(nullptr);
  const bool clock_ok = now > 1700000000;
  struct tm lt = {};
  if (clock_ok) localtime_r(&now, &lt);

  char header[32];
  header_for(header, sizeof header);
  const Tariff tariff = g_settings.active_tariff();
  gridui::Model m = gridui::model_build(d.snap, d.has_data, header, lt.tm_wday, lt.tm_hour * 60 + lt.tm_min,
                                        g_settings.has_tariff() ? &tariff : nullptr);
  if (!clock_ok) m.clock[0] = 0;
  m.view_country = view_country;
  m.show_imports = ci.imports;
  m.show_geothermal = ci.geothermal;
  m.single_view = ci.single_view;
  if (ci.single_view) m.view_country = false;

  char local_sub[56], ago[48];
  snprintf(ago, sizeof ago, "%s", T("Waiting for data"));
  snprintf(local_sub, sizeof local_sub, T("My region \xC2\xB7 %s"), T(ci.local_sub));
  gridui::Freshness fresh = gridui::FRESH_OLD;
  if (d.has_data && clock_ok) {
    const int64_t age = (int64_t)now - d.snap.data_time;
    format_ago(age, ago, sizeof ago);
    fresh = (gridui::Freshness)freshness_level(d.source, age);
  } else if (d.has_data) {
    // Saved before a restart and the clock isn't set yet: we can't say how old it is
    snprintf(ago, sizeof ago, "%s", T("Saved before restart"));
    fresh = gridui::FRESH_LATE;
  }
  if (d.from_cache && fresh == gridui::FRESH_OK) fresh = gridui::FRESH_LATE;   // amber until a fetch confirms it
  m.freshness = fresh;
  if (d.has_data) history_to_model(m, d.snap.data_time);
  gridui::model_views(m, region_label(g_settings.country, g_settings.region), local_sub,
                      ci.name, T(ci.whole_sub), T(ci.freq), ago, fresh);

  // Say plainly why there's nothing to show
  if (!d.has_data) {
    const char *why = nullptr;
    if (WiFi.status() != WL_CONNECTED) why = "No Wi-Fi \xC2\xB7 reconnect with the installer";
    else if (d.key_rejected) why = "Data key not accepted \xC2\xB7 check gridmix.local";
    else if (d.out_of_credits) why = "Out of data requests for today";
    else if (d.failures >= 3) why = "Can't reach the data service \xC2\xB7 retrying";
    if (why) {
      snprintf(m.advice, sizeof m.advice, "%s", T(why));
      m.tone = gridui::TONE_BAD;
    }
  }
  g_status.problem = !d.has_data && d.key_rejected ? "Data key not accepted" : d.out_of_credits ? "Out of requests for today" : nullptr;
  return m;
}

static void on_view_changed(bool country) {
  if (country == view_country) return;
  view_country = country;
  history_clear();
  fetcher_reset(view_country);
  dirty = true;
}


// Rebuild the screen when new data arrives, the minute changes, or a sample scene is due
static void screen_tick(lv_timer_t *) {
  static uint32_t last_version = UINT32_MAX;
  static int last_minute = -1;
  static wl_status_t last_wifi = WL_IDLE_STATUS;

  LiveData d;
  fetcher_get(d);
  const time_t now = time(nullptr);
  const int minute = (int)(now / 60);
  const bool demo = !g_settings.ready();
  if (demo && millis() - demo_since > 8000) {
    demo_index = (demo_index + 1) % gridui::demo_count();
    demo_since = millis();
    dirty = true;
  }
  static uint32_t last_history = 0;
  if (d.history_version != last_history) {
    last_history = d.history_version;
    history_import(d);
    dirty = true;
  }
  if (d.version != last_version && d.has_data && !d.from_cache) {
    // New data: add it to the 24-hour history, and keep a copy in flash (at most every 30 minutes,
    // to spare the flash; the first one after a restart or a change is saved straight away)
    static uint32_t last_save_ms = 0;
    static bool saved_once = false;
    history_record(d.snap.data_time, d.snap.renewable_pct());
    store.snap = d.snap;
    if (!saved_once || millis() - last_save_ms > 30 * 60 * 1000UL) {
      char key[24];
      feed_key(key, sizeof key);
      cache_save(key, store);
      last_save_ms = millis();
      saved_once = true;
    }
  }
  // Update in progress: show its progress over everything
  static int shown_pct = -2;
  OtaStatus os;
  ota_status(os);
  const bool updating = os.state == OTA_DOWNLOADING || os.state == OTA_DONE;
  const int pct = updating ? os.percent : -1;
  if (pct != shown_pct) {
    gridui::show_update(pct, T(os.state == OTA_DONE ? "Done. Restarting\xE2\x80\xA6" : "Downloading the new version\xE2\x80\xA6"));
    shown_pct = pct;
  }

  if (d.version != last_version || minute != last_minute || WiFi.status() != last_wifi) dirty = true;
  if (!dirty) return;
  dirty = false;
  last_version = d.version;
  last_minute = minute;
  last_wifi = WiFi.status();
  apply_brightness();
  gridui::update(build_model());
}

void setup() {
  Serial.begin(115200);
  Serial.printf("\nGrid Mix Display %s\n", FW_VERSION);
  health_begin();

  settings_load(g_settings);
  history_clear();
  view_country = g_settings.view_country;

  ledcAttach(LCD_BL, 5000, 8);   // backlight on PWM so brightness can be set from the phone
  apply_settings();

  if (!gfx->begin()) Serial.println("Display init failed - check pins in board_config.h");
  gfx->fillScreen(0x0000);

#if HAS_GT911_TOUCH
  touch.begin();
  touch.setRotation(ROTATION_NORMAL);   // if taps land mirrored, try ROTATION_INVERTED
#endif

  lv_init();
  lv_tick_set_cb(tick_cb);
  const size_t buf_px = LCD_WIDTH * 40;  // 40 lines per chunk
  uint8_t *buf1 = (uint8_t *)heap_caps_malloc(buf_px * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  uint8_t *buf2 = (uint8_t *)heap_caps_malloc(buf_px * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_buffers(disp, buf1, buf2, buf_px * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);

#if HAS_GT911_TOUCH
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_read_cb);
#endif

  gridui::Callbacks cb;
  cb.view_changed = on_view_changed;
  gridui::create(cb);
  lv_timer_create(screen_tick, 500, nullptr);

  // Wi-Fi: reuse saved details if we have them; the browser installer can send new ones
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  improv::begin("GridMix", FW_VERSION, "Grid Mix Display", "http://{IP}");

  ota_begin();
  fetcher_begin();

  // Show the last data from before the restart (if it's for the same region and view)
  char key[24];
  feed_key(key, sizeof key);
  CachedData saved;
  if (cache_load(key, saved)) {
    store = saved;
    fetcher_seed(saved.snap);
    Serial.println("Showing data saved before the restart");
  }
}

void loop() {
  health_loop();
  improv::loop();
  if (!net_started && WiFi.status() == WL_CONNECTED) {
    configTzTime(g_settings.tz, "pool.ntp.org", "time.google.com");   // clock, in the user's time zone
    setup_server_begin();
    net_started = true;
  }
  setup_server_loop();
  lv_timer_handler();
  delay(2);
}
