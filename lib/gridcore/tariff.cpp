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

// ---------------------------------------------------------------- tariff screen helpers

static const char *DASH = "\xE2\x80\x93";   // en dash

int tariff_day_segments(const Tariff &t, int weekday, DaySegment *out, int max) {
  int n = 0, cur = -2, start = 0;
  for (int m = 0; m <= DAY_MIN; m++) {
    const int idx = m < DAY_MIN ? tariff_period_at(t, weekday, m) : -3;
    if (idx == cur) continue;
    if (cur != -2 && n < max) out[n++] = DaySegment{(uint16_t)start, (uint16_t)m, (int8_t)cur};
    cur = idx;
    start = m;
  }
  return n;
}

void format_clock(int minute_of_day, char *out, size_t cap) {
  const int md = ((minute_of_day % DAY_MIN) + DAY_MIN) % DAY_MIN;
  const int h = md / 60, m = md % 60, h12 = h % 12 ? h % 12 : 12;
  if (m) snprintf(out, cap, "%d:%02d%s", h12, m, h < 12 ? "am" : "pm");
  else snprintf(out, cap, "%d%s", h12, h < 12 ? "am" : "pm");
}

// "6–10am" when both ends share am/pm, else "3pm–1am"
static void format_span(int start, int end, char *out, size_t cap) {
  char a[12], b[12];
  format_clock(start, a, sizeof a);
  format_clock(end, b, sizeof b);
  // Drop the first "am"/"pm" only when both ends are on the same side of noon: "6–10am", "1–5pm",
  // but "7am–12am" (ends at midnight) and "3pm–1am" (past midnight) keep both
  const int s0 = start % DAY_MIN, e0 = end % DAY_MIN;
  const bool morning = s0 < 720 && e0 > s0 && e0 <= 720;
  const bool afternoon = s0 >= 720 && e0 > s0;
  const size_t la = strlen(a), lb = strlen(b);
  if ((morning || afternoon) && la > 2 && lb > 2 && !strcmp(a + la - 2, b + lb - 2)) a[la - 2] = 0;
  snprintf(out, cap, "%s%s%s", a, DASH, b);
}

static bool same_rate(const Tariff &t, int i, int j) {
  return i >= 0 && j >= 0 && t.periods[i].cents == t.periods[j].cents && !strcmp(t.periods[i].name, t.periods[j].name);
}

int tariff_cards(const Tariff &t, int weekday, RateCard *out, int max) {
  DaySegment seg[48];
  const int ns = tariff_day_segments(t, weekday, seg, 48);
  int n = 0;
  for (int k = 0; k < ns && n < max; k++) {
    if (seg[k].index < 0) continue;
    bool seen = false;
    for (int c = 0; c < n; c++) if (same_rate(t, out[c].index, seg[k].index)) seen = true;
    if (seen) continue;
    RateCard &card = out[n++];
    card.index = seg[k].index;
    card.hours[0] = 0;
    // A run that ends at midnight and one that starts at midnight with the same rate are one span ("3pm–1am")
    const bool wraps = ns > 1 && same_rate(t, seg[0].index, seg[ns - 1].index) && same_rate(t, seg[0].index, card.index);
    if (ns == 1) { snprintf(card.hours, sizeof card.hours, "All day"); continue; }
    for (int j = 0; j < ns; j++) {
      if (!same_rate(t, seg[j].index, card.index)) continue;
      int start = seg[j].start, end = seg[j].end;
      if (wraps && j == 0) continue;                  // printed with the last run
      if (wraps && j == ns - 1) end = seg[0].end;     // ... which runs on past midnight
      char span[24];
      format_span(start, end, span, sizeof span);
      const size_t len = strlen(card.hours);
      snprintf(card.hours + len, sizeof card.hours - len, "%s%s", len ? ", " : "", span);
    }
  }
  // cheapest first
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && t.periods[out[j].index].cents < t.periods[out[j - 1].index].cents; j--) {
      RateCard tmp = out[j]; out[j] = out[j - 1]; out[j - 1] = tmp;
    }
  return n;
}

bool tariff_cheapest_window(const Tariff &t, int weekday, int minute, char *out, size_t cap) {
  if (t.count == 0) return false;
  float lo = 1e9f, hi = -1e9f;
  for (int i = 0; i < t.count; i++) { lo = t.periods[i].cents < lo ? t.periods[i].cents : lo; hi = t.periods[i].cents > hi ? t.periods[i].cents : hi; }
  if (hi - lo < 0.01f) return false;   // flat rate
  const int now = weekday * DAY_MIN + minute;
  auto cheap_at = [&](int abs_min) {
    const int m = ((abs_min % WEEK_MIN) + WEEK_MIN) % WEEK_MIN;
    const int idx = tariff_period_at(t, m / DAY_MIN, m % DAY_MIN);
    return idx >= 0 && t.periods[idx].cents <= lo + 0.005f;
  };
  int start = -1;
  for (int step = 0; step < 2 * DAY_MIN; step++) if (cheap_at(now + step)) { start = now + step; break; }
  if (start < 0) return false;
  int end = start;
  while (end - start < DAY_MIN && cheap_at(end)) end++;
  char a[16], b[16];
  format_clock(end % DAY_MIN, b, sizeof b);
  if (start == now) { snprintf(out, cap, "Now %s %s", DASH, b); return true; }
  format_clock(start % DAY_MIN, a, sizeof a);
  const bool tomorrow = start / DAY_MIN != now / DAY_MIN;
  snprintf(out, cap, "%s%s %s %s", tomorrow ? "Tomorrow " : "", a, DASH, b);
  return true;
}

}  // namespace gridcore
