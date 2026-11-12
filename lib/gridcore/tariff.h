// Tariff engine: works out the rate right now, which period it is, how cheap or dear that is
// compared with the rest of the plan, and when it next changes.
// Times are LOCAL minutes after midnight (the board converts from UTC using the user's time zone).
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace gridcore {

static const int MAX_TARIFF_PERIODS = 8;
static const uint8_t EVERY_DAY = 0x7F;   // bit 0 = Sunday ... bit 6 = Saturday

struct TariffPeriod {
  char name[16] = "";
  uint16_t start_min = 0;   // inclusive, 0..1439
  uint16_t end_min = 0;     // exclusive; may be earlier than start (crosses midnight); equal = all day
  float cents = 0;          // c/kWh
  uint8_t days = EVERY_DAY;
};

struct Tariff {
  TariffPeriod periods[MAX_TARIFF_PERIODS];
  uint8_t count = 0;
  float supply_cents_per_day = 0;
  float feed_in_cents = 0;

  bool add(const char *name, int start_min, int end_min, float cents, uint8_t days = EVERY_DAY);
};

// Cheapest / middle / dearest, ranked by the plan's own rates (not by period names).
enum PriceBand : uint8_t { BAND_LOW = 0, BAND_MID, BAND_HIGH };

struct RateNow {
  int index = -1;              // period index in the tariff
  float cents = 0;
  PriceBand band = BAND_MID;
  int minutes_to_change = -1;  // until the rate changes (-1 = never, e.g. flat rate)
  int next_index = -1;         // period that starts then
};

// Which period applies at a local weekday (0 = Sunday) and minute of day. -1 if none.
int tariff_period_at(const Tariff &t, int weekday, int minute);

PriceBand tariff_band(const Tariff &t, float cents);

bool tariff_rate_now(const Tariff &t, int weekday, int minute, RateNow &out);

// Every minute of every day covered exactly once? On failure, err gets a short reason
// ("Gap at Sun 01:00", "Overlap at Mon 15:00").
bool tariff_validate(const Tariff &t, char *err, size_t err_len);

// ---- for the tariff screen

// Today's periods as runs of minutes: [start, end) with the period index. Returns how many.
struct DaySegment { uint16_t start, end; int8_t index; };
int tariff_day_segments(const Tariff &t, int weekday, DaySegment *out, int max);

// One card per distinct rate (same name and price), cheapest first, with its hours today
// written like "10am – 3pm" or "6–10am, 3pm–1am".
struct RateCard { int index; char hours[40]; };
int tariff_cards(const Tariff &t, int weekday, RateCard *out, int max);

// "1:20pm", "6am", "12pm"
void format_clock(int minute_of_day, char *out, size_t cap);

// The next stretch of the cheapest rate, from now on: "Now – 3pm" or "10am – 3pm" (or "Tomorrow 10am – 3pm").
// False for a flat rate or no plan.
bool tariff_cheapest_window(const Tariff &t, int weekday, int minute, char *out, size_t cap);

// "HH:MM" -> minutes after midnight; "24:00" allowed as end of day. -1 if invalid.
int parse_hhmm(const char *s);

}  // namespace gridcore
