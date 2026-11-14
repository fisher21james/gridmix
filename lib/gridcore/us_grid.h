// United States: each grid operator's own live fuel mix (EIA's national data runs hours behind).
// All free and keyless; every one was 4-15 minutes behind when checked (14 Nov 2026).
//   CAL  California (CAISO)       today's 5-minute CSV, Pacific time
//   TEX  Texas (ERCOT)            dashboard JSON, yesterday + today (~200 KB; only reachable from the US)
//   MIDW Midwest (MISO)           public API JSON, latest interval (+ today for the chart)
//   CENT Central (SPP)            2-hour 5-minute CSV, UTC
//   NY   New York (NYISO)         today's 5-minute CSV, Eastern time
//   NE   New England (ISO-NE)     the website's fuel mix feed (a POST), latest reading
//   NW   Northwest (BPA area)     7-day 5-minute text file, Pacific time
// Kept free of Arduino code so it can be tested on a PC.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "grid_snapshot.h"
#include "parsers.h"

namespace gridcore {

enum UsGrid : uint8_t { US_CAL = 0, US_TEX, US_MIDW, US_CENT, US_NY, US_NE, US_NW, US_GRID_COUNT };
// "CAL" -> US_CAL ...; US_GRID_COUNT if it isn't one we support
UsGrid us_grid(const char *region);

// The request for the latest mix. Returns the length written, 0 if it didn't fit or the region is unknown.
size_t us_url(const char *region, int64_t now_utc, char *out, size_t cap);
// Form body to POST instead of a GET (New England), or nullptr
const char *us_post_body(const char *region);
// A separate request for the 24-hour chart (Midwest), or 0: the latest answer already holds the day's points
size_t us_history_url(const char *region, int64_t now_utc, char *out, size_t cap);

bool parse_us(const char *region, const char *body, size_t len, int64_t now_utc, GridSnapshot &out);
// Half-hour points, oldest first, the newest `max` of them
int parse_us_history(const char *region, const char *body, size_t len, int64_t now_utc, HistoryPoint *out, int max);

// US daylight saving (2nd Sunday of March to 1st Sunday of November, at 2am local).
// Local wall-clock time to UTC, for a zone `std_offset_h` hours from UTC in winter (Pacific -8, Eastern -5).
int64_t us_local_to_utc(int y, int mo, int d, int h, int mi, int std_offset_h);

}  // namespace gridcore
