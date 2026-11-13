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

static bool use_pence = false;
void set_pence(bool pence) { use_pence = pence; }
const char *minor_unit() { return use_pence ? "p" : "\xC2\xA2"; }

void format_rate(float cents, char *out, size_t cap) {
  if (cents >= 100) snprintf(out, cap, use_pence ? "\xC2\xA3%.2f" : "$%.2f", cents / 100.0f);
  else snprintf(out, cap, "%.1f%s", cents, minor_unit());
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
  // How green the grid is right now drives everything; your own tariff adds timing tips
  const float renew = m.snap.renewable_pct();
  const bool clean = renew >= 70, low = renew < 30;
  // Only call it coal/gas when coal and gas really are most of the mix. Britain can be low on
  // renewables because of imports or nuclear, with little gas and no coal at all.
  const float coal = m.snap.share_pct(COAL), gas = m.snap.share_pct(GAS);
  const bool fossil = coal + gas >= 50;
  const char *fuel = coal >= 1 && gas >= 1 ? "coal & gas" : coal >= 1 ? "coal" : "gas";

  if (rate && tariff && rate->index >= 0) {
    if (rate->minutes_to_change > 0 && rate->minutes_to_change <= 30 && rate->next_index >= 0 &&
        tariff->periods[rate->next_index].cents > rate->cents) {
      char when[12];
      clock_text((now_min + rate->minutes_to_change) % 1440, when, sizeof when);
      snprintf(out, cap, "Price goes up at %s%sstart big loads now", when, DOT);
      tone = TONE_GOOD;
      return;
    }
    if (rate->band == BAND_HIGH) {
      if (fossil) snprintf(out, cap, "Peak rate & mostly %s%shold off if you can", fuel, DOT);
      else if (low) snprintf(out, cap, "Peak rate & few renewables%shold off if you can", DOT);
      else snprintf(out, cap, "Peak rate%swait for cheaper times if you can", DOT);
      tone = TONE_BAD;
      return;
    }
    if (rate->band == BAND_LOW) {
      if (clean) snprintf(out, cap, "Cheapest rate & clean power%srun big loads", DOT);
      else snprintf(out, cap, "Cheapest rate%sgood time for big loads", DOT);
      tone = TONE_GOOD;
      return;
    }
  }
  if (clean) { snprintf(out, cap, "Mostly renewable right now"); tone = TONE_GOOD; return; }
  if (fossil) { snprintf(out, cap, "Mostly %s right now", fuel); tone = TONE_BAD; return; }
  if (low) { snprintf(out, cap, "Not many renewables right now"); tone = TONE_BAD; return; }
  if (coal + gas >= 15) snprintf(out, cap, "Mix of renewables & fossil fuels");
  else snprintf(out, cap, "Mix of renewables & other sources");
  tone = TONE_OK;
}

}  // namespace gridui
