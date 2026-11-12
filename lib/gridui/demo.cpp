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
  return t;
}

void model_views(Model &m, const char *local, const char *local_sub, const char *country, const char *country_sub,
                 const char *freq, const char *ago, bool fresh) {
  snprintf(m.local.title, sizeof m.local.title, "%s", local);
  snprintf(m.local.sub, sizeof m.local.sub, "%s", local_sub);
  snprintf(m.country.title, sizeof m.country.title, "%s", country);
  snprintf(m.country.sub, sizeof m.country.sub, "%s", country_sub);
  ViewOption *opts[2] = {&m.local, &m.country};
  for (ViewOption *o : opts) {
    snprintf(o->freq, sizeof o->freq, "%s", freq);
    snprintf(o->ago, sizeof o->ago, "%s", ago);
    o->fresh = fresh;
  }
}

Model model_build(const GridSnapshot &snap, bool has_data, const char *region, int weekday, int minute, const Tariff *tariff) {
  Model m;
  m.has_data = has_data;
  m.snap = snap;
  snprintf(m.region, sizeof m.region, "%s", region);
  const int h = (minute / 60) % 24, mi = minute % 60, h12 = h % 12 ? h % 12 : 12;
  snprintf(m.clock, sizeof m.clock, "%d:%02d %s", h12, mi, h < 12 ? "am" : "pm");
  m.sky = sky_for_hour(h);
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

static GridSnapshot mix(const float *mw, float price, float charging) {
  GridSnapshot s;
  for (int i = 0; i < SOURCE_COUNT; i++) s.mw[i] = mw[i];
  s.battery_charging = charging;
  if (!isnan(price)) { s.has_price = true; s.price = price; }
  s.valid = true;
  return s;
}

int demo_count() { return 4; }

Model demo_model(int i) {
  static const char *DOT = "\xC2\xB7";
  static const Tariff plan = demo_tariff();
  static Tariff flat;
  if (flat.count == 0) flat.add("Flat", 0, 0, 14.2f);
  char region[32], sub[40];
  snprintf(sub, sizeof sub, "My region %s state grid", DOT);
  //                       solar wind hydro batt  nuc  gas  coal other
  static const float A[8] = {58,   31,  0,    4,    0,   7,   0,   0};
  static const float B[8] = {0,    64,  0,    14,   0,   22,  0,   0};
  static const float C[8] = {4,    6,   8,    5,    0,   9,   68,  0};
  static const float D[8] = {20,   26,  0,    0,    8,   38,  8,   0};
  Model m;
  switch (i % 4) {
    case 0:
      snprintf(region, sizeof region, "SA %s Adelaide", DOT);
      m = model_build(mix(A, -12, 9), true, region, 4, 13 * 60 + 20, &plan);
      model_views(m, "South Australia", sub, "Australia", "Whole country (NEM + WA)", "Every 10 min", "Sample data", true);
      break;
    case 1:
      snprintf(region, sizeof region, "SA %s Adelaide", DOT);
      m = model_build(mix(B, 88, 0), true, region, 4, 21 * 60 + 40, &plan);
      model_views(m, "South Australia", sub, "Australia", "Whole country (NEM + WA)", "Every 10 min", "Sample data", true);
      break;
    case 2:
      snprintf(region, sizeof region, "NSW %s Sydney", DOT);
      m = model_build(mix(C, 312, 0), true, region, 4, 18 * 60 + 15, &plan);
      model_views(m, "New South Wales", sub, "Australia", "Whole country (NEM + WA)", "Every 10 min", "Sample data", true);
      break;
    default:
      m = model_build(mix(D, NAN, 0), true, "Texas", 4, 15 * 60, &flat);
      snprintf(sub, sizeof sub, "My region %s EIA", DOT);
      model_views(m, "Texas", sub, "United States", "Lower 48 states (EIA)", "Hourly", "Sample data", false);
      break;
  }
  return m;
}

}  // namespace gridui
