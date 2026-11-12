// The one format every data source is translated into.
// Screens only ever read a GridSnapshot, so they never care which country or API the data came from.
#pragma once
#include <stdint.h>

namespace gridcore {

enum Source : uint8_t {
  SOLAR = 0,
  WIND,
  HYDRO,
  BATTERY,   // battery discharging into the grid
  NUCLEAR,
  GAS,
  COAL,
  OTHER,     // oil/distillate, biomass, anything unmapped
  SOURCE_COUNT
};

const char *source_name(Source s);
bool source_is_renewable(Source s);

struct GridSnapshot {
  bool valid = false;
  float mw[SOURCE_COUNT] = {0};   // generation per source, MW (or MWh for hourly sources; only shares matter)
  float battery_charging = 0;     // power going INTO batteries; not part of the mix
  int64_t data_time = 0;          // UTC seconds of the data point shown
  bool estimated = false;         // true if part of the mix was filled in (e.g. rooftop solar from AEMO's forecast)
  bool has_rooftop = false;       // true once rooftop solar is included (actual, carried forward or forecast)

  bool has_price = false;         // wholesale price ($/MWh), when the source provides one
  float price = 0;
  int64_t price_time = 0;

  float total() const;
  float share_pct(Source s) const;   // 0..100
  float renewable_pct() const;       // solar + wind + hydro, 0..100
  bool battery_is_charging() const { return battery_charging > mw[BATTERY]; }
};

// Days/seconds helpers (no timezone database needed).
int64_t days_from_civil(int y, unsigned m, unsigned d);
// Parses "YYYY-MM-DDTHH", "YYYY-MM-DDTHH:MM", or "...:SS", with optional "+HH:MM"/"-HH:MM"/"Z" offset. Returns UTC seconds, or -1.
int64_t parse_iso_time(const char *s);

}  // namespace gridcore
