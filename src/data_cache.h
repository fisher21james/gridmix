// Keeps the last good mix (and the 24-hour history) in flash, so after a power cut the display
// shows something straight away instead of "Waiting for data" while Wi-Fi reconnects.
#pragma once
#include <stdint.h>
#include "grid_snapshot.h"
#include "gridui.h"

struct CachedData {
  gridcore::GridSnapshot snap;
  float hist[gridui::HISTORY_POINTS];
  int64_t hist_slot[gridui::HISTORY_POINTS];
};

// feed_key says what the data is for ("AU/SA1/0"); a cache for a different region or view is ignored.
bool cache_load(const char *feed_key, CachedData &out);
void cache_save(const char *feed_key, const CachedData &in);
void cache_clear();
