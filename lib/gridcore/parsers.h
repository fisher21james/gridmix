// Translators: raw API responses -> GridSnapshot.
#pragma once
#include <stddef.h>
#include "grid_snapshot.h"

namespace gridcore {

// U.S. EIA v2 /electricity/rto/fuel-type-data/data/ (hourly, one balancing authority).
// Uses the newest "period" in the response and sums its rows.
bool parse_eia(const char *json, size_t len, GridSnapshot &out);

// Open Electricity v4 /data/network/{NEM|WEM}?metrics=power, grouped by fueltech or fueltech_group.
// Uses the newest timestamp in the response. Rooftop solar arrives later than the rest,
// so if it's missing for the newest timestamp its last known value is carried forward
// and the snapshot is marked estimated.
bool parse_openelectricity(const char *json, size_t len, GridSnapshot &out);

// The same endpoint over a longer span (interval=1h, last 24 h): renewable share at each timestamp,
// oldest first, for the detail screen's chart. Returns how many points were written.
struct HistoryPoint { int64_t time; float renewable_pct; };
int parse_openelectricity_history(const char *json, size_t len, HistoryPoint *out, int max);

// Open Electricity v4 /market/network/{NEM}?metrics=solar_rooftop_forecast (AEMO's rooftop solar estimate).
// Any other metrics in the answer (such as price) are ignored.
struct MarketInfo {
  bool has_rooftop_forecast = false;
  float rooftop_forecast = 0;      // MW, AEMO forecast for the interval at at_time
};
// at_time: the generation snapshot's data_time, so the forecast matches the same 5 minutes.
bool parse_openelectricity_market(const char *json, size_t len, int64_t at_time, MarketInfo &out);

// Adds rooftop solar from the forecast if the generation data had none
// (at 5-minute detail Open Electricity has no rooftop series, so this is the normal case).
void merge_market(GridSnapshot &snap, const MarketInfo &m);

}  // namespace gridcore
