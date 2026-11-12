// Model helpers: pure text and logic, no drawing.
#include "gridui.h"
#include <stdio.h>
#include <string.h>

namespace gridui {

using namespace gridcore;

Sky sky_for_hour(int hour) {
  if (hour >= 7 && hour < 18) return SKY_DAY;
  if (hour == 6 || (hour >= 18 && hour < 20)) return SKY_DUSK;
  return SKY_NIGHT;
}

void format_rate(float cents, char *out, size_t cap) {
  if (cents >= 100) snprintf(out, cap, "$%.2f", cents / 100.0f);
  else snprintf(out, cap, "%.1f\xC2\xA2", cents);   // ¢
}

static void clock_text(int minute_of_day, char *out, size_t cap) {
  const int h = (minute_of_day / 60) % 24, m = minute_of_day % 60;
  const int h12 = h % 12 ? h % 12 : 12;
  if (m) snprintf(out, cap, "%d:%02d%s", h12, m, h < 12 ? "am" : "pm");
  else snprintf(out, cap, "%d%s", h12, h < 12 ? "am" : "pm");
}

void advise(const Model &m, const RateNow *rate, const Tariff *tariff, int now_min, char *out, size_t cap, Tone &tone) {
  static const char *DOT = " \xC2\xB7 ";   // " · "
  if (!m.has_data) { snprintf(out, cap, "Waiting for data\xE2\x80\xA6"); tone = TONE_OK; return; }
  const GridStatus g = grid_status(m.snap);
  const float renew = m.snap.renewable_pct();

  if (g == GRID_STRAIN) { snprintf(out, cap, "Grid under strain%shold off on big loads", DOT); tone = TONE_BAD; return; }
  if (g == GRID_CLEAN_CHEAP) { snprintf(out, cap, "Clean & cheap%srun the dishwasher", DOT); tone = TONE_GOOD; return; }

  if (rate && tariff && rate->index >= 0) {
    if (rate->minutes_to_change > 0 && rate->minutes_to_change <= 30 && rate->next_index >= 0 &&
        tariff->periods[rate->next_index].cents > rate->cents) {
      char when[12];
      clock_text((now_min + rate->minutes_to_change) % 1440, when, sizeof when);
      snprintf(out, cap, "Price goes up at %s%sstart big loads now", when, DOT);
      tone = TONE_GOOD;
      return;
    }
    if (rate->band == BAND_HIGH) { snprintf(out, cap, "Peak rate%swait for cheaper times if you can", DOT); tone = TONE_BAD; return; }
    if (rate->band == BAND_LOW) {
      if (renew >= 50) snprintf(out, cap, "Cheapest rate & clean power%srun big loads", DOT);
      else snprintf(out, cap, "Cheapest rate%sgood time for big loads", DOT);
      tone = TONE_GOOD;
      return;
    }
  }
  if (renew >= 70) { snprintf(out, cap, "Mostly renewable right now"); tone = TONE_GOOD; return; }
  if (g == GRID_BUSY) { snprintf(out, cap, "Grid is busy right now"); tone = TONE_OK; return; }
  snprintf(out, cap, "Normal grid");
  tone = TONE_OK;
}

}  // namespace gridui
