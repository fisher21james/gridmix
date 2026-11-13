// Fetches the live mix in a background task so the animation never stalls on a slow request.
#pragma once
#include <stdint.h>
#include "grid_snapshot.h"
#include "schedule.h"
#include "parsers.h"

struct LiveData {
  bool has_data = false;
  gridcore::GridSnapshot snap;
  gridcore::DataSource source = gridcore::SRC_OPEN_ELECTRICITY;
  uint32_t version = 0;          // goes up every time new data arrives
  bool key_rejected = false;
  bool out_of_credits = false;
  int failures = 0;              // failed requests in a row
  bool from_cache = false;       // shown from flash after a restart; nothing fetched yet

  // Last 24 hours, fetched once after start-up or a change of region
  gridcore::HistoryPoint history[48];   // hourly (Australia) or half-hourly (Great Britain)
  int history_count = 0;
  uint32_t history_version = 0;
};

void fetcher_begin();
// Settings or the region/country view changed: forget old data and fetch straight away.
void fetcher_reset(bool country_view);
// Show data saved before a restart until the first fetch replaces it.
void fetcher_seed(const gridcore::GridSnapshot &snap);
// Copies the latest state (thread safe).
void fetcher_get(LiveData &out);
