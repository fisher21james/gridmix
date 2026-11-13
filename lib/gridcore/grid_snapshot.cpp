#include "grid_snapshot.h"
#include <string.h>

namespace gridcore {

const char *source_name(Source s) {
  static const char *names[SOURCE_COUNT] = {"Solar", "Wind", "Hydro", "Battery", "Nuclear", "Gas", "Coal", "Other"};
  return s < SOURCE_COUNT ? names[s] : "?";
}

bool source_is_renewable(Source s) { return s == SOLAR || s == WIND || s == HYDRO; }

float GridSnapshot::total() const {
  float t = 0;
  for (int i = 0; i < SOURCE_COUNT; i++) if (mw[i] > 0) t += mw[i];
  return t;
}

float GridSnapshot::share_pct(Source s) const {
  const float t = total();
  return (t > 0 && mw[s] > 0) ? mw[s] * 100.0f / t : 0.0f;
}

float GridSnapshot::renewable_pct() const {
  const float t = total();
  if (t <= 0) return 0;
  float r = 0;
  for (int i = 0; i < SOURCE_COUNT; i++)
    if (source_is_renewable((Source)i) && mw[i] > 0) r += mw[i];
  return r * 100.0f / t;
}


// Howard Hinnant's algorithm: days since 1970-01-01 for a proleptic Gregorian date.
int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

static bool read_int(const char *&p, int digits, int &out) {
  out = 0;
  for (int i = 0; i < digits; i++) {
    if (p[i] < '0' || p[i] > '9') return false;
    out = out * 10 + (p[i] - '0');
  }
  p += digits;
  return true;
}

int64_t parse_iso_time(const char *s) {
  if (!s) return -1;
  const char *p = s;
  int y, mo, d, h = 0, mi = 0, se = 0;
  if (!read_int(p, 4, y) || *p++ != '-' || !read_int(p, 2, mo) || *p++ != '-' || !read_int(p, 2, d)) return -1;
  if (*p == 'T' || *p == ' ') {
    p++;
    if (!read_int(p, 2, h)) return -1;
    if (*p == ':') { p++; if (!read_int(p, 2, mi)) return -1; }
    if (*p == ':') { p++; if (!read_int(p, 2, se)) return -1; }
    if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
  }
  int64_t t = days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
  if (*p == '+' || *p == '-') {
    const int sign = (*p == '+') ? 1 : -1;
    p++;
    int oh, om = 0;
    if (!read_int(p, 2, oh)) return -1;
    if (*p == ':') p++;
    read_int(p, 2, om);
    t -= sign * (oh * 3600 + om * 60);   // local = UTC + offset, so UTC = local - offset
  }
  return t;
}

}  // namespace gridcore
