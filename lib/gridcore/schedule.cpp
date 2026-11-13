#include "schedule.h"
#include <stdio.h>
#include <string.h>
#include "grid_snapshot.h"

namespace gridcore {

static const char *OE_BASE = "https://api.openelectricity.org.au/v4";
static const int64_t MIN = 60, HOUR = 3600, DAY = 86400;

Feed feed_for(const char *country, const char *region, bool country_view) {
  Feed f;
  if (country && country[0] == 'U') {
    f.source = SRC_EIA;
    f.has_market = false;
    snprintf(f.region, sizeof f.region, "%s", country_view || !region || !*region ? "US48" : region);
    f.network[0] = 0;
    return f;
  }
  const bool wa = region && !strcmp(region, "WEM");
  if (country_view) {          // the NEM covers every state except WA and the NT
    snprintf(f.network, sizeof f.network, "NEM");
    f.region[0] = 0;
  } else if (wa) {
    snprintf(f.network, sizeof f.network, "WEM");
    f.region[0] = 0;
    f.has_market = false;      // WA reports rooftop solar directly; no AEMO forecast needed
  } else {
    snprintf(f.network, sizeof f.network, "NEM");
    snprintf(f.region, sizeof f.region, "%s", region && *region ? region : "SA1");
  }
  return f;
}

void network_time(int64_t utc, const char *network, char *out, size_t cap) {
  int64_t t = utc + (network && !strcmp(network, "WEM") ? 8 : 10) * HOUR;
  int64_t days = t >= 0 ? t / DAY : (t - DAY + 1) / DAY;
  int64_t secs = t - days * DAY;
  // civil_from_days (Howard Hinnant)
  int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t y = (int64_t)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  if (m <= 2) y++;
  snprintf(out, cap, "%04d-%02u-%02uT%02d:%02d:%02d", (int)y, m, d, (int)(secs / HOUR), (int)(secs % HOUR / MIN),
           (int)(secs % MIN));
}

static size_t finish(int n, size_t cap) { return n > 0 && (size_t)n < cap ? (size_t)n : 0; }

size_t oe_power_url(const Feed &f, int64_t now_utc, char *out, size_t cap) {
  // A few 5-minute points, so rooftop and late rows can be filled in. WA's data can arrive later, so look further back.
  char start[24];
  const bool wa = !strcmp(f.network, "WEM");
  network_time(now_utc - (wa ? 120 : 40) * MIN, f.network, start, sizeof start);
  char region[32] = "";
  if (f.region[0]) snprintf(region, sizeof region, "&network_region=%s", f.region);
  return finish(snprintf(out, cap, "%s/data/network/%s?metrics=power&interval=5m%s&secondary_grouping=fueltech&date_start=%s",
                         OE_BASE, f.network, region, start), cap);
}

size_t oe_market_url(const Feed &f, int64_t now_utc, char *out, size_t cap) {
  // Covers the next half hour too, so the rooftop forecast still matches the mix until the next market check
  char start[24], end[24];
  network_time(now_utc - 40 * MIN, f.network, start, sizeof start);
  network_time(now_utc + 40 * MIN, f.network, end, sizeof end);
  char region[32] = "";
  if (f.region[0]) snprintf(region, sizeof region, "&network_region=%s", f.region);
  return finish(snprintf(out, cap,
                         "%s/market/network/%s?metrics=solar_rooftop_forecast&interval=5m%s&date_start=%s&date_end=%s",
                         OE_BASE, f.network, region, start, end), cap);
}

size_t oe_history_url(const Feed &f, int64_t now_utc, char *out, size_t cap) {
  char start[24];
  network_time((now_utc / HOUR - 25) * HOUR, f.network, start, sizeof start);
  char region[32] = "";
  if (f.region[0]) snprintf(region, sizeof region, "&network_region=%s", f.region);
  return finish(snprintf(out, cap, "%s/data/network/%s?metrics=power&interval=1h%s&secondary_grouping=fueltech&date_start=%s",
                         OE_BASE, f.network, region, start), cap);
}

size_t eia_url(const Feed &f, char *out, size_t cap) {
  // Newest rows first; 30 rows covers every fuel type for the latest hour. Brackets are percent-encoded.
  return finish(snprintf(out, cap,
                         "https://api.eia.gov/v2/electricity/rto/fuel-type-data/data/?frequency=hourly&data%%5B0%%5D=value"
                         "&facets%%5Brespondent%%5D%%5B%%5D=%s&sort%%5B0%%5D%%5Bcolumn%%5D=period"
                         "&sort%%5B0%%5D%%5Bdirection%%5D=desc&length=30&api_key=",
                         f.region), cap);
}

// ---------------------------------------------------------------- timing

void Scheduler::reset(DataSource src, int64_t now) {
  src_ = src;
  next_[FETCH_MIX] = now;
  next_[FETCH_MARKET] = src == SRC_OPEN_ELECTRICITY ? now : INT64_MAX;
  fails_[0] = fails_[1] = 0;
  key_rejected_ = out_of_credits_ = false;
}

bool Scheduler::due(FetchKind k, int64_t now) const { return now >= next_[k]; }

int64_t Scheduler::next_regular(FetchKind k, int64_t now) const {
  if (src_ == SRC_EIA) return (now / (15 * MIN) + 1) * 15 * MIN + 2 * MIN;
  // Open Electricity: just after each 10 (or 30) minutes, when the newest 5-minute point has been published.
  // Slow down when the day's credits are nearly gone.
  int64_t step = k == FETCH_MIX ? 10 * MIN : 30 * MIN;
  if (credits_left_ >= 0 && credits_left_ < 40) step = k == FETCH_MIX ? 30 * MIN : 60 * MIN;
  return (now / step + 1) * step + 90;
}

void Scheduler::done(FetchKind k, int64_t now, int http) {
  if (http == 200) {
    fails_[k] = 0;
    key_rejected_ = out_of_credits_ = false;
    next_[k] = next_regular(k, now);
    return;
  }
  fails_[k]++;
  if (http == 401 || http == 403) {
    // Wrong key: retrying won't help. Check again in a few hours in case the service was at fault;
    // saving new settings resets this straight away.
    key_rejected_ = true;
    next_[k] = now + 6 * HOUR;
    return;
  }
  if (http == 429) {
    // Out of requests. Open Electricity resets at midnight UTC.
    out_of_credits_ = true;
    next_[k] = src_ == SRC_OPEN_ELECTRICITY ? (now / DAY + 1) * DAY + 5 * MIN : now + 30 * MIN;
    return;
  }
  // Network trouble or server error: 1, 2, 5, 10, then 20 minutes, never later than the regular check
  static const int64_t BACKOFF[] = {1 * MIN, 2 * MIN, 5 * MIN, 10 * MIN, 20 * MIN};
  const int i = fails_[k] - 1 < 4 ? fails_[k] - 1 : 4;
  const int64_t retry = now + BACKOFF[i];
  const int64_t regular = next_regular(k, now);
  next_[k] = retry < regular ? retry : regular;
}

// ---------------------------------------------------------------- freshness

void format_ago(int64_t age_s, char *out, size_t cap) {
  if (age_s < 0) age_s = 0;
  if (age_s < 90) snprintf(out, cap, "Updated just now");
  else if (age_s < HOUR) snprintf(out, cap, "Updated %d min ago", (int)((age_s + 30) / MIN));
  else if (age_s < DAY) snprintf(out, cap, "Updated %d h ago", (int)(age_s / HOUR));
  else {
    const int d = (int)(age_s / DAY);
    snprintf(out, cap, "Updated %d day%s ago", d, d == 1 ? "" : "s");
  }
}

int freshness_level(DataSource src, int64_t age_s) {
  const int64_t ok = src == SRC_EIA ? 24 * HOUR : 25 * MIN;
  const int64_t late = src == SRC_EIA ? 48 * HOUR : 90 * MIN;
  return age_s <= ok ? 0 : age_s <= late ? 1 : 2;
}

}  // namespace gridcore
