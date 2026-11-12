#include "tariff.h"
#include <stdio.h>
#include <string.h>

namespace gridcore {

static const int DAY_MIN = 1440;
static const int WEEK_MIN = DAY_MIN * 7;

bool Tariff::add(const char *name, int start_min, int end_min, float cents, uint8_t days) {
  if (count >= MAX_TARIFF_PERIODS) return false;
  if (start_min < 0 || start_min >= DAY_MIN || end_min < 0 || end_min > DAY_MIN) return false;
  TariffPeriod &p = periods[count++];
  strncpy(p.name, name ? name : "", sizeof(p.name) - 1);
  p.name[sizeof(p.name) - 1] = 0;
  p.start_min = (uint16_t)start_min;
  p.end_min = (uint16_t)(end_min % DAY_MIN);   // 24:00 is the same as 00:00
  p.cents = cents;
  p.days = days;
  return true;
}

static bool covers(const TariffPeriod &p, int weekday, int minute) {
  if (!(p.days & (1 << weekday))) return false;
  if (p.start_min == p.end_min) return true;                                   // all day
  if (p.start_min < p.end_min) return minute >= p.start_min && minute < p.end_min;
  return minute >= p.start_min || minute < p.end_min;                         // crosses midnight
}

int tariff_period_at(const Tariff &t, int weekday, int minute) {
  for (int i = 0; i < t.count; i++)
    if (covers(t.periods[i], weekday, minute)) return i;
  return -1;
}

PriceBand tariff_band(const Tariff &t, float cents) {
  float lo = 1e9f, hi = -1e9f;
  for (int i = 0; i < t.count; i++) {
    if (t.periods[i].cents < lo) lo = t.periods[i].cents;
    if (t.periods[i].cents > hi) hi = t.periods[i].cents;
  }
  if (t.count == 0 || hi - lo < 0.01f) return BAND_MID;   // flat rate: neither cheap nor dear
  if (cents <= lo + 0.005f) return BAND_LOW;
  if (cents >= hi - 0.005f) return BAND_HIGH;
  return BAND_MID;
}

bool tariff_rate_now(const Tariff &t, int weekday, int minute, RateNow &out) {
  out = RateNow();
  const int idx = tariff_period_at(t, weekday, minute);
  if (idx < 0) return false;
  out.index = idx;
  out.cents = t.periods[idx].cents;
  out.band = tariff_band(t, out.cents);

  // Walk forward until the rate changes (a repeated period at the same rate doesn't count)
  int now = weekday * DAY_MIN + minute;
  for (int step = 1; step <= WEEK_MIN; step++) {
    const int m = (now + step) % WEEK_MIN;
    const int j = tariff_period_at(t, m / DAY_MIN, m % DAY_MIN);
    if (j >= 0 && t.periods[j].cents != out.cents) {
      out.minutes_to_change = step;
      out.next_index = j;
      break;
    }
  }
  return true;
}

bool tariff_validate(const Tariff &t, char *err, size_t err_len) {
  static const char *DAYS[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  if (t.count == 0) { if (err) snprintf(err, err_len, "No rates entered"); return false; }
  for (int d = 0; d < 7; d++) {
    for (int m = 0; m < DAY_MIN; m++) {
      int hits = 0;
      for (int i = 0; i < t.count; i++) hits += covers(t.periods[i], d, m) ? 1 : 0;
      if (hits != 1) {
        if (err) snprintf(err, err_len, "%s at %s %02d:%02d", hits == 0 ? "Gap" : "Overlap", DAYS[d], m / 60, m % 60);
        return false;
      }
    }
  }
  if (err && err_len) err[0] = 0;
  return true;
}

int parse_hhmm(const char *s) {
  if (!s) return -1;
  int h = 0, m = 0;
  if (sscanf(s, "%d:%d", &h, &m) != 2) return -1;
  if (h < 0 || h > 24 || m < 0 || m > 59 || (h == 24 && m != 0)) return -1;
  return h * 60 + m;
}

}  // namespace gridcore
