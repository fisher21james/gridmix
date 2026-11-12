// The display's screens, drawn with LVGL. Shared by the firmware and the desktop/browser simulator,
// so what the simulator shows is exactly what the board runs.
#pragma once
#include <stddef.h>
#include "lvgl.h"
#include "grid_snapshot.h"
#include "tariff.h"

namespace gridui {

enum Sky : uint8_t { SKY_DAY = 0, SKY_DUSK, SKY_NIGHT };
enum Tone : uint8_t { TONE_GOOD = 0, TONE_OK, TONE_BAD };

struct ViewOption {
  char title[32] = "";   // "South Australia"
  char sub[40] = "";     // "My region · state grid"
  char freq[24] = "";    // "Every 10 min"
  char ago[24] = "";     // "Updated 2 min ago"
  bool fresh = true;
};

// Everything the screens show. The app fills this in; the screens never fetch or calculate anything.
struct Model {
  bool has_data = false;
  gridcore::GridSnapshot snap;
  char region[32] = "";        // header, e.g. "SA · Adelaide"
  char clock[12] = "";         // "1:20 pm"
  Sky sky = SKY_DAY;

  bool has_rate = false;
  char rate[12] = "";          // "23.8¢"
  char period[16] = "";        // "Shoulder"
  gridcore::PriceBand band = gridcore::BAND_MID;
  bool flat_rate = false;

  char advice[64] = "";        // status pill in the scene
  Tone tone = TONE_OK;

  bool view_country = false;
  ViewOption local, country;
};

struct Callbacks {
  void (*view_changed)(bool country) = nullptr;   // picked in the region picker
  void (*scene_tapped)() = nullptr;               // tap on the animated scene
};

void create(const Callbacks &cb);
void update(const Model &m);

// ---- helpers for building a Model (no LVGL needed)
Sky sky_for_hour(int hour);
void format_rate(float cents, char *out, size_t cap);   // 23.78 -> "23.8¢"
// Plain-English advice for the status pill. now_min = local minutes after midnight.
void advise(const Model &m, const gridcore::RateNow *rate, const gridcore::Tariff *tariff, int now_min,
            char *out, size_t cap, Tone &tone);

// Builds a Model from a snapshot, a tariff (or nullptr) and local time. weekday: 0 = Sunday.
Model model_build(const gridcore::GridSnapshot &snap, bool has_data, const char *region, int weekday, int minute,
                  const gridcore::Tariff *tariff);
void model_views(Model &m, const char *local, const char *local_sub, const char *country, const char *country_sub,
                 const char *freq, const char *ago, bool fresh);

// Sample scenes (shown before the display has a data key, and used by the simulator)
int demo_count();
Model demo_model(int i);
gridcore::Tariff demo_tariff();

}  // namespace gridui
