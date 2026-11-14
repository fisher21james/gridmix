// When to ask the data services for new numbers, and the exact requests to send.
// Kept free of Arduino code so it can be tested on a PC.
//
// Open Electricity (Community plan): about 500 requests a day and 10,000 a month, reset at
// midnight UTC (10:30 am in Adelaide). One refresh of the mix every 10 minutes plus AEMO's rooftop
// solar estimate every 30 minutes is 144 + 48 = 192 a day, about 5,900 a month.
// EIA has no practical daily limit, but its data is hourly, so it is checked every 15 minutes.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace gridcore {

// ---- requests
enum DataSource : uint8_t { SRC_OPEN_ELECTRICITY = 0, SRC_EIA, SRC_CARBON_INTENSITY, SRC_RTE, SRC_EA };   // EA = NZ Electricity Authority

struct Feed {
  DataSource source = SRC_OPEN_ELECTRICITY;
  char network[4] = "NEM";   // Open Electricity network: NEM or WEM
  char region[6] = "SA1";    // Open Electricity network_region ("" = whole network) or EIA respondent
  bool has_market = true;    // AEMO rooftop solar forecast (NEM only)
};

// Which feed to use for a country/region and view. country view = whole NEM / lower 48 states.
Feed feed_for(const char *country, const char *region, bool country_view);

// Open Electricity wants dates in the network's own time, with no time zone and no daylight saving:
// NEM (east coast) is UTC+10, WEM (WA) is UTC+8. "2026-11-12T16:05:00".
void network_time(int64_t utc, const char *network, char *out, size_t cap);
inline void nem_time(int64_t utc, char *out, size_t cap) { network_time(utc, "NEM", out, cap); }

// Return the length written (0 if it didn't fit). The EIA URL ends with "api_key=" so the key can be appended.
size_t oe_power_url(const Feed &f, int64_t now_utc, char *out, size_t cap);
size_t oe_market_url(const Feed &f, int64_t now_utc, char *out, size_t cap);
size_t eia_url(const Feed &f, char *out, size_t cap);
// Hourly mix for the last 25 hours, to fill the 24-hour chart straight after start-up (1 request).
size_t oe_history_url(const Feed &f, int64_t now_utc, char *out, size_t cap);
// Great Britain (Carbon Intensity API, no key): the current half hour, and the 24 hours before now
size_t ci_url(const Feed &f, char *out, size_t cap);
size_t ci_history_url(const Feed &f, int64_t now_utc, char *out, size_t cap);
// France (RTE eCO2mix on ODRÉ, no key): the newest quarter hour, and the 24 hours before now
size_t rte_url(char *out, size_t cap);
size_t rte_history_url(int64_t now_utc, char *out, size_t cap);

// ---- timing
enum FetchKind : uint8_t { FETCH_MIX = 0, FETCH_MARKET };

class Scheduler {
 public:
  void reset(DataSource src, int64_t now);   // new settings or view: fetch straight away
  bool due(FetchKind k, int64_t now) const;
  // http: status code (200 = OK, negative = no connection)
  void done(FetchKind k, int64_t now, int http);

  int64_t next(FetchKind k) const { return next_[k]; }
  int failures(FetchKind k) const { return fails_[k]; }
  bool key_rejected() const { return key_rejected_; }
  bool out_of_credits() const { return out_of_credits_; }
  void set_credits_left(int n) { credits_left_ = n; }

 private:
  int64_t next_regular(FetchKind k, int64_t now) const;
  DataSource src_ = SRC_OPEN_ELECTRICITY;
  int64_t next_[2] = {0, 0};
  int fails_[2] = {0, 0};
  bool key_rejected_ = false;
  bool out_of_credits_ = false;
  int credits_left_ = -1;
};

// ---- freshness
// "Updated just now", "Updated 4 min ago", "Updated 3 h ago", "Updated 1 day ago"
void format_ago(int64_t age_s, char *out, size_t cap);
// How up to date the data on screen is, for the dot next to the clock:
//   0 = up to date, 1 = falling behind (amber), 2 = out of date (red).
// Open Electricity: up to 25 min, up to 90 min, older. Carbon Intensity (GB) and RTE (France): 45 min, 2 h.
// EIA is hourly and normally runs hours to
// about a day behind, so: up to 24 h, up to 48 h, older.
int freshness_level(DataSource src, int64_t age_s);
inline bool data_is_fresh(DataSource src, int64_t age_s) { return freshness_level(src, age_s) == 0; }

}  // namespace gridcore
