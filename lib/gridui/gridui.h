// The display's screens, drawn with LVGL. Shared by the firmware and the desktop/browser simulator,
// so what the simulator shows is exactly what the board runs.
#pragma once
#include <math.h>
#include <stddef.h>
#include "lvgl.h"
#include "grid_snapshot.h"
#include "tariff.h"

namespace gridui {

enum Sky : uint8_t { SKY_DAY = 0, SKY_DUSK, SKY_NIGHT };
enum Tone : uint8_t { TONE_GOOD = 0, TONE_OK, TONE_BAD };

// How up to date the data is: dot next to the clock and in the region picker
enum Freshness : uint8_t { FRESH_OK = 0, FRESH_LATE, FRESH_OLD };

struct ViewOption {
  char title[32] = "";   // "South Australia"
  char sub[40] = "";     // "My region · state grid"
  char freq[24] = "";    // "Every 10 min"
  char ago[24] = "";     // "Updated 2 min ago"
  Freshness freshness = FRESH_OK;
};

static const int HISTORY_POINTS = 48;   // renewable share, one point per 30 minutes over the last 24 h

// Everything the screens show. The app fills this in; the screens never fetch or calculate anything.
struct Model {
  bool has_data = false;
  gridcore::GridSnapshot snap;
  char region[32] = "";        // header, e.g. "SA · Adelaide"
  char clock[12] = "";         // "1:20 pm"
  Freshness freshness = FRESH_OK;
  Sky sky = SKY_DAY;
  int weekday = 0, minute = 0; // local time the screen was built for (0 = Sunday)

  bool has_rate = false;
  char rate[12] = "";          // "23.8¢"
  char period[16] = "";        // "Shoulder"
  gridcore::PriceBand band = gridcore::BAND_MID;
  bool flat_rate = false;
  bool has_tariff = false;     // for the tariff screen
  gridcore::Tariff tariff;

  char advice[64] = "";        // status pill in the scene
  Tone tone = TONE_OK;

  bool view_country = false;
  ViewOption local, country;

  // Detail screen: renewable %, oldest first, last point = now. NAN = no data for that half hour.
  float history[HISTORY_POINTS];
  Model() { for (float &h : history) h = NAN; }
};

struct Callbacks {
  void (*view_changed)(bool country) = nullptr;   // picked in the region picker
  void (*scene_tapped)() = nullptr;               // tap on the animated scene (the detail screen opens too)
};

void create(const Callbacks &cb);
void update(const Model &m);
// Full-screen "Updating" message over everything (percent < 0 hides it)
void show_update(int percent, const char *message);

// ---- helpers for building a Model (no LVGL needed)
Sky sky_for_hour(int hour);
void format_rate(float cents, char *out, size_t cap);   // 23.78 -> "23.8¢" (or "23.8p" in Britain)
// Britain: rates in pence and pounds instead of cents and dollars
void set_pence(bool pence);
const char *minor_unit();   // "¢" or "p"
// Plain-English advice for the status pill. now_min = local minutes after midnight.
void advise(const Model &m, const gridcore::RateNow *rate, const gridcore::Tariff *tariff, int now_min,
            char *out, size_t cap, Tone &tone);

// Builds a Model from a snapshot, a tariff (or nullptr) and local time. weekday: 0 = Sunday.
Model model_build(const gridcore::GridSnapshot &snap, bool has_data, const char *region, int weekday, int minute,
                  const gridcore::Tariff *tariff);
void model_views(Model &m, const char *local, const char *local_sub, const char *country, const char *country_sub,
                 const char *freq, const char *ago, Freshness freshness);
// A believable 24-hour renewables curve ending at the current share, for the sample scenes
void demo_history(Model &m);

// Sample scenes (shown before the display has a data key, and used by the simulator)
struct DemoScene {
  const char *region, *title;
  float mw[gridcore::SOURCE_COUNT];
  float charging;   // MW into batteries
  int weekday, minute;
  bool us;          // flat US-style rate instead of the SA time-of-use plan
};
int demo_count();
DemoScene demo_scene(int i);
gridcore::GridSnapshot demo_snapshot(const DemoScene &d);
Model demo_model(int i);
gridcore::Tariff demo_tariff();

}  // namespace gridui
