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

}  // namespace gridcore
