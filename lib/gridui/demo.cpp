// Sample scenes, shown on the board before it has a data key (so it looks alive on day one)
// and used by the simulator.
#include "gridui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace gridui {

using namespace gridcore;

Tariff demo_tariff() {   // a real SA time-of-use plan
  Tariff t;
  t.add("Peak", 6 * 60, 10 * 60, 53.99f);
  t.add("Shoulder", 10 * 60, 15 * 60, 23.78f);
  t.add("Peak", 15 * 60, 60, 53.99f);
  t.add("Off peak", 60, 6 * 60, 34.51f);
  t.supply_cents_per_day = 116.18f;
  t.feed_in_cents = 1.0f;
  return t;
}

void model_views(Model &m, const char *local, const char *local_sub, const char *country, const char *country_sub,
                 const char *freq, const char *ago, Freshness freshness) {
  snprintf(m.local.title, sizeof m.local.title, "%s", local);
  snprintf(m.local.sub, sizeof m.local.sub, "%s", local_sub);
  snprintf(m.country.title, sizeof m.country.title, "%s", country);
  snprintf(m.country.sub, sizeof m.country.sub, "%s", country_sub);
  ViewOption *opts[2] = {&m.local, &m.country};
  for (ViewOption *o : opts) {
    snprintf(o->freq, sizeof o->freq, "%s", freq);
    snprintf(o->ago, sizeof o->ago, "%s", ago);
    o->freshness = freshness;
  }
}

Model model_build(const GridSnapshot &snap, bool has_data, const char *region, int weekday, int minute, const Tariff *tariff) {
  Model m;
  m.has_data = has_data;
  m.snap = snap;
  snprintf(m.region, sizeof m.region, "%s", region);
  const int h = (minute / 60) % 24;
  format_header_clock(minute, m.clock, sizeof m.clock);
  m.sky = sky_for_hour(h);
  m.weekday = weekday;
  m.minute = minute;
  if (tariff && tariff->count > 0) { m.has_tariff = true; m.tariff = *tariff; }
  RateNow r;
  const bool rate_ok = tariff && tariff->count > 0 && tariff_rate_now(*tariff, weekday, minute, r);
  if (rate_ok) {
    m.has_rate = true;
    format_rate(r.cents, m.rate, sizeof m.rate);
    snprintf(m.period, sizeof m.period, "%s", tariff->periods[r.index].name);
    m.band = r.band;
    m.flat_rate = r.minutes_to_change < 0;
  }
  advise(m, rate_ok ? &r : nullptr, tariff, minute, m.advice, sizeof m.advice, m.tone);
  return m;
}

GridSnapshot demo_snapshot(const DemoScene &d) {
  GridSnapshot s;
  for (int i = 0; i < SOURCE_COUNT; i++) s.mw[i] = d.mw[i];
  s.battery_charging = d.charging;
  s.valid = true;
  return s;
}

void demo_history(Model &m) {
  // Wind drifts slowly, solar follows the sun; scaled so the last point is the share now.
  const float now = m.has_data ? m.snap.renewable_pct() : 0;
  float raw[HISTORY_POINTS];
  for (int i = 0; i < HISTORY_POINTS; i++) {
    const float hour = fmodf(m.minute / 60.0f - (HISTORY_POINTS - 1 - i) * 0.5f + 48, 24);
    const float sun = hour > 6.5f && hour < 19 ? sinf((hour - 6.5f) / 12.5f * (float)M_PI) : 0;
    const float wind = 0.5f + 0.25f * sinf(i * 0.21f + m.minute * 0.003f) + 0.1f * sinf(i * 0.7f);
    raw[i] = 20 + 45 * sun + 30 * wind;
  }
  const float k = raw[HISTORY_POINTS - 1] > 1 ? now / raw[HISTORY_POINTS - 1] : 1;
  for (int i = 0; i < HISTORY_POINTS; i++) {
    const float v = raw[i] * k;
    m.history[i] = v < 0 ? 0 : v > 100 ? 100 : v;
  }
}

int demo_count() { return 4; }

DemoScene demo_scene(int i) {
  //                                 solar wind hydro batt nuc gas coal other   charging day minute
  static const DemoScene SCENES[4] = {
    {"SA \xC2\xB7 Adelaide", "South Australia", {58, 31, 0, 4, 0, 7, 0, 0}, 9, 4, 13 * 60 + 20, false},
    {"SA \xC2\xB7 Adelaide", "South Australia", {0, 64, 0, 14, 0, 22, 0, 0}, 0, 4, 21 * 60 + 40, false},
    {"NSW \xC2\xB7 Sydney", "New South Wales", {4, 6, 8, 5, 0, 9, 68, 0}, 0, 4, 18 * 60 + 15, false},
    {"VIC \xC2\xB7 Melbourne", "Victoria", {18, 30, 6, 2, 0, 4, 40, 0}, 0, 4, 8 * 60 + 30, false},
  };
  return SCENES[((i % 4) + 4) % 4];
}

Model demo_model(int i) {
  static const Tariff plan = demo_tariff();
  static Tariff flat;
  if (flat.count == 0) flat.add("Flat", 0, 0, 14.2f);
  const DemoScene d = demo_scene(i);
  Model m = model_build(demo_snapshot(d), true, d.region, d.weekday, d.minute, d.us ? &flat : &plan);
  if (d.us) model_views(m, d.title, "My region \xC2\xB7 EIA", "United States", "Lower 48 states (EIA)", "Hourly", "Sample data", FRESH_OK);
  else model_views(m, d.title, "My region \xC2\xB7 state grid", "Australia", "East coast grid (NEM)", "Every 10 min", "Sample data", FRESH_OK);
  demo_history(m);
  return m;
}

}  // namespace gridui
