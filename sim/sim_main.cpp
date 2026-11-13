// Desktop simulator: runs the real screen code (lib/gridui) with LVGL in memory and saves screenshots.
//   sim [out_dir]            -> one PNG per scenario
//   sim out_dir --frames N   -> also N animation frames per scenario (for GIFs)
// Built by sim/build.sh, and on GitHub by the workflow.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "lvgl.h"
#include "gridui.h"
#include "parsers.h"
#include "../test/test_gridcore/fixtures.h"

using namespace gridcore;
using namespace gridui;

static const int W = 480, H = 272;
static uint16_t fb[W * H];
static uint32_t sim_ms = 0;
static int touch_x = -1, touch_y = -1;

static uint32_t tick_cb() { return sim_ms; }
static void flush_cb(lv_display_t *d, const lv_area_t *, uint8_t *) { lv_display_flush_ready(d); }
static void touch_cb(lv_indev_t *, lv_indev_data_t *data) {
  if (touch_x >= 0) { data->point.x = touch_x; data->point.y = touch_y; data->state = LV_INDEV_STATE_PRESSED; }
  else data->state = LV_INDEV_STATE_RELEASED;
}

static void run(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 10) { sim_ms += 10; lv_timer_handler(); }
}
static void tap(int x, int y) {
  touch_x = x; touch_y = y; run(80);
  touch_x = -1; run(120);
}

// ---------------------------------------------------------------- PNG (uncompressed deflate, no libraries)
static uint32_t crc_table[256];
static void crc_init() {
  for (uint32_t n = 0; n < 256; n++) { uint32_t c = n; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; crc_table[n] = c; }
}
static uint32_t crc(const uint8_t *b, size_t n, uint32_t c = 0xFFFFFFFFu) { for (size_t i = 0; i < n; i++) c = crc_table[(c ^ b[i]) & 255] ^ (c >> 8); return c; }
static void be32(std::vector<uint8_t> &v, uint32_t x) { v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x); }
static void chunk(FILE *f, const char *type, const std::vector<uint8_t> &data) {
  std::vector<uint8_t> c;
  be32(c, data.size());
  c.insert(c.end(), type, type + 4);
  c.insert(c.end(), data.begin(), data.end());
  be32(c, crc(c.data() + 4, c.size() - 4) ^ 0xFFFFFFFFu);
  fwrite(c.data(), 1, c.size(), f);
}
static void save_png(const std::string &path) {
  std::vector<uint8_t> raw;
  for (int y = 0; y < H; y++) {
    raw.push_back(0);
    for (int x = 0; x < W; x++) {
      const uint16_t p = fb[y * W + x];
      raw.push_back(((p >> 11) & 31) * 255 / 31);
      raw.push_back(((p >> 5) & 63) * 255 / 63);
      raw.push_back((p & 31) * 255 / 31);
    }
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
  for (size_t i = 0; i < raw.size(); i += 65535) {
    const size_t n = raw.size() - i < 65535 ? raw.size() - i : 65535;
    z.push_back(i + n == raw.size());
    z.push_back(n & 255); z.push_back(n >> 8); z.push_back(~n & 255); z.push_back((~n >> 8) & 255);
    z.insert(z.end(), raw.begin() + i, raw.begin() + i + n);
  }
  be32(z, (b << 16) | a);
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) { perror(path.c_str()); exit(1); }
  const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  be32(ihdr, W); be32(ihdr, H);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  chunk(f, "IHDR", ihdr);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", {});
  fclose(f);
}

// ---------------------------------------------------------------- scenarios
static const char *SA = "SA \xC2\xB7 Adelaide";
static const char *SUB = "My region \xC2\xB7 state grid";

int main(int argc, char **argv) {
  const std::string out = argc > 1 ? argv[1] : "sim-out";
  int frames = 0;
  for (int i = 2; i + 1 < argc; i++) if (!strcmp(argv[i], "--frames")) frames = atoi(argv[i + 1]);
  crc_init();

  lv_init();
  lv_tick_set_cb(tick_cb);
  lv_display_t *disp = lv_display_create(W, H);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_buffers(disp, fb, nullptr, sizeof fb, LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_cb);

  static int scene_taps = 0;
  Callbacks cb;
  cb.scene_tapped = [] { scene_taps++; };
  create(cb);

  const Tariff plan = demo_tariff();
  struct Scenario { std::string name; Model model; int action; };   // action: 0 none, 1 picker, 2 thin bar, 3 detail, 4 tariff
  std::vector<Scenario> list;
  static const char *DEMO_NAMES[4] = {"01-sa-sunny-afternoon", "02-sa-windy-night", "03-nsw-evening-peak", "04-vic-morning"};
  for (int i = 0; i < demo_count(); i++) list.push_back({DEMO_NAMES[i], demo_model(i), 0});

  // 5: real data captured from Open Electricity
  {
    GridSnapshot s;
    MarketInfo mk;
    parse_openelectricity(OE_SA1_FUELTECH, strlen(OE_SA1_FUELTECH), s);
    parse_openelectricity_market(OE_SA1_MARKET, strlen(OE_SA1_MARKET), s.data_time, mk);
    merge_market(s, mk);
    Model m = model_build(s, true, SA, 4, 16 * 60 + 5, &plan);
    model_views(m, "South Australia", SUB, "Australia", "East coast grid (NEM)", "Every 10 min", "Updated 9 min ago", FRESH_OK);
    list.push_back({"05-real-sa-12nov-4pm", m, 0});
  }

  // 7-9: states and interactions
  {
    Model m = model_build(GridSnapshot(), false, SA, 5, 9 * 60 + 2, nullptr);
    list.push_back({"07-waiting-for-data", m, 0});
  }
  list.push_back({"08-region-picker", list[0].model, 1});
  list.push_back({"09-thin-bar", list[2].model, 2});
  {
    // First boot: sample scenes play, and the pill says what to do next (as src/main.cpp does)
    Model m = demo_model(1);
    snprintf(m.advice, sizeof m.advice, "Sample data \xC2\xB7 finish setup at gridmix.local");
    m.tone = TONE_OK;
    list.push_back({"10-first-boot-sample", m, 0});
  }
  {
    Model m = model_build(GridSnapshot(), false, SA, 5, 9 * 60 + 2, &plan);
    snprintf(m.advice, sizeof m.advice, "Data key not accepted \xC2\xB7 check gridmix.local");
    m.tone = TONE_BAD;
    list.push_back({"11-key-problem", m, 0});
  }
  // Detail screen (tap the scene) and tariff screen (tap your rate)
  list.push_back({"12-detail-sa-sunny", list[0].model, 3});
  list.push_back({"13-detail-real-sa", list[4].model, 3});
  list.push_back({"14-tariff-sa-afternoon", list[0].model, 4});
  list.push_back({"15-tariff-sa-evening", list[1].model, 4});
  {
    Tariff flat;
    flat.add("Flat", 0, 0, 31.5f);
    flat.supply_cents_per_day = 98;
    flat.feed_in_cents = 4;
    list.push_back({"16-tariff-flat", model_build(list[0].model.snap, true, SA, 4, 13 * 60 + 20, &flat), 4});
    list.push_back({"17-tariff-no-plan", model_build(list[4].model.snap, true, SA, 4, 16 * 60 + 5, nullptr), 4});
  }
  {
    // Great Britain, real Carbon Intensity answer for London (13 Nov 2026, 4:30am UK time), pence tariff
    set_pence(true);
    GridSnapshot s;
    parse_carbon_intensity(CI_LONDON_NOW, strlen(CI_LONDON_NOW), s);
    Tariff uk;
    uk.add("Night", 0, 7 * 60, 13.5f);
    uk.add("Day", 7 * 60, 0, 27.0f);
    uk.supply_cents_per_day = 53.4f;
    uk.feed_in_cents = 15.0f;
    Model m = model_build(s, true, "London", 5, 4 * 60 + 40, &uk);
    model_views(m, "London", "My region \xC2\xB7 local area", "Great Britain", "England, Scotland & Wales", "Every 30 min",
                "Updated 10 min ago", FRESH_OK);
    HistoryPoint pts[64];
    const int n = parse_carbon_intensity_history(CI_LONDON_HISTORY, strlen(CI_LONDON_HISTORY), pts, 64);
    const int64_t end = s.data_time / 1800;
    for (int i = 0; i < n; i++) {
      const int64_t idx = pts[i].time / 1800 - (end - (HISTORY_POINTS - 1));
      if (idx >= 0 && idx < HISTORY_POINTS) m.history[idx] = pts[i].renewable_pct;
    }
    m.history[HISTORY_POINTS - 1] = s.renewable_pct();
    set_pence(false);
    list.push_back({"19-uk-london-real", m, 0});
    list.push_back({"20-uk-london-detail", m, 3});
    list.push_back({"21-uk-london-tariff", m, 4});
  }
  {
    Model m = list[4].model;       // data that's falling behind: amber dot next to the clock
    m.freshness = FRESH_LATE;
    list.push_back({"18-data-falling-behind", m, 0});
  }

  for (Scenario &sc : list) {
    set_pence(sc.name.find("-uk-") != std::string::npos);
    update(sc.model);
    run(1500);
    if (sc.action == 1) tap(60, 20);
    if (sc.action == 2) tap(240, 224);
    if (sc.action == 3) tap(300, 120);
    if (sc.action == 4) tap(420, 14);
    save_png(out + "/" + sc.name + ".png");
    for (int f = 0; f < frames; f++) {
      run(50);
      char path[512];
      snprintf(path, sizeof path, "%s/%s-f%03d.png", out.c_str(), sc.name.c_str(), f);
      save_png(path);
    }
    if (sc.action == 1) tap(447, 60);   // close the picker
    if (sc.action == 2) tap(240, 217);  // back to the fat bar
    if (sc.action >= 3) tap(30, 20);    // Back
    printf("%s\n", sc.name.c_str());
  }
  tap(240, 120);
  printf("scene taps: %d\n", scene_taps);
  return 0;
}
