// New Zealand: the Electricity Authority's real-time dispatch API (free key, header Ocp-Apim-Subscription-Key).
// Every 5 minutes it lists each grid connection point ("MAN2201 MAN0") with the megawatts it's dispatched to
// generate. Which fuel each point is comes from the Authority's own plant list (Generation_MD dataset,
// October 2026), kept in nz_grid.cpp. Kept free of Arduino code so it can be tested on a PC.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "grid_snapshot.h"

namespace gridcore {

static const char NZ_RTD_URL[] = "https://emi.azure-api.net/real-time-dispatch/";
static const char NZ_KEY_HEADER[] = "Ocp-Apim-Subscription-Key";

// island: "NI", "SI", or "" for the whole country
bool parse_nz(const char *json, size_t len, const char *island, GridSnapshot &out);

// The fuel of a grid point + unit ("HLY2201 HLY1"), and whether it's in the South Island.
// Returns false for a point not in the table (counted as Other).
bool nz_point(const char *code, Source &fuel, bool &south);

}  // namespace gridcore
