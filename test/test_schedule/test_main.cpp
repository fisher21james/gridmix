// Request building and refresh timing.
#include <unity.h>
#include <string.h>
#include "schedule.h"
#include "grid_snapshot.h"

using namespace gridcore;

void setUp() {}
void tearDown() {}

// 2026-11-12 05:35:00 UTC = 4:05 pm in Adelaide (daylight saving) = 3:35 pm NEM time
static int64_t T0() { return days_from_civil(2026, 11, 12) * 86400 + 5 * 3600 + 35 * 60; }

void test_nem_time_is_utc_plus_10() {
  char s[24];
  nem_time(T0(), s, sizeof s);
  TEST_ASSERT_EQUAL_STRING("2026-11-12T15:35:00", s);
  nem_time(days_from_civil(2026, 12, 31) * 86400 + 20 * 3600, s, sizeof s);   // rolls into the new year
  TEST_ASSERT_EQUAL_STRING("2027-01-01T06:00:00", s);
}

void test_feeds() {
  Feed f = feed_for("AU", "SA1", false);
  TEST_ASSERT_EQUAL(SRC_OPEN_ELECTRICITY, f.source);
  TEST_ASSERT_EQUAL_STRING("NEM", f.network);
  TEST_ASSERT_EQUAL_STRING("SA1", f.region);
  TEST_ASSERT_TRUE(f.has_market);

  f = feed_for("AU", "WEM", false);
  TEST_ASSERT_EQUAL_STRING("WEM", f.network);
  TEST_ASSERT_EQUAL_STRING("", f.region);
  TEST_ASSERT_FALSE(f.has_market);

  f = feed_for("AU", "VIC1", true);
  TEST_ASSERT_EQUAL_STRING("", f.region);   // whole NEM

  f = feed_for("US", "TEX", false);
  TEST_ASSERT_EQUAL(SRC_EIA, f.source);
  TEST_ASSERT_EQUAL_STRING("TEX", f.region);
  TEST_ASSERT_EQUAL_STRING("US48", feed_for("US", "TEX", true).region);
}

void test_urls() {
  char u[400];
  Feed f = feed_for("AU", "SA1", false);
  TEST_ASSERT_TRUE(oe_power_url(f, T0(), u, sizeof u) > 0);
  TEST_ASSERT_EQUAL_STRING(
      "https://api.openelectricity.org.au/v4/data/network/NEM?metrics=power&interval=5m&network_region=SA1"
      "&secondary_grouping=fueltech&date_start=2026-11-12T14:55:00", u);
  TEST_ASSERT_TRUE(oe_market_url(f, T0(), u, sizeof u) > 0);
  TEST_ASSERT_NOT_NULL(strstr(u, "/market/network/NEM?metrics=price&metrics=solar_rooftop_forecast"));
  TEST_ASSERT_NOT_NULL(strstr(u, "date_start=2026-11-12T14:55:00&date_end=2026-11-12T16:15:00"));

  f = feed_for("AU", "SA1", true);
  oe_power_url(f, T0(), u, sizeof u);
  TEST_ASSERT_NULL(strstr(u, "network_region"));

  f = feed_for("US", "TEX", false);
  TEST_ASSERT_TRUE(eia_url(f, u, sizeof u) > 0);
  TEST_ASSERT_NOT_NULL(strstr(u, "facets%5Brespondent%5D%5B%5D=TEX&"));
  TEST_ASSERT_EQUAL_STRING("api_key=", u + strlen(u) - 8);
  TEST_ASSERT_EQUAL(0, eia_url(f, u, 40));   // too small: nothing half-written is used
}

void test_regular_timing_stays_in_budget() {
  Scheduler s;
  int64_t t = T0();
  s.reset(SRC_OPEN_ELECTRICITY, t);
  TEST_ASSERT_TRUE(s.due(FETCH_MIX, t));
  TEST_ASSERT_TRUE(s.due(FETCH_MARKET, t));
  // Simulate a whole day, answering every request with 200
  int requests = 0;
  for (int64_t now = t; now < t + 86400; now += 30) {
    for (int k = 0; k < 2; k++)
      if (s.due((FetchKind)k, now)) { s.done((FetchKind)k, now, 200); requests++; }
  }
  TEST_ASSERT_INT_WITHIN(4, 192, requests);
  TEST_ASSERT_TRUE(requests * 31 < 10000);   // fits the monthly allowance too
  // Mix checks land 90 s after each 10 minutes
  TEST_ASSERT_EQUAL(90, s.next(FETCH_MIX) % 600);
}

void test_low_credits_slow_down() {
  Scheduler s;
  s.reset(SRC_OPEN_ELECTRICITY, T0());
  s.set_credits_left(25);
  s.done(FETCH_MIX, T0(), 200);
  TEST_ASSERT_EQUAL(90, s.next(FETCH_MIX) % 1800);
}

void test_backoff_and_errors() {
  Scheduler s;
  const int64_t t = T0();
  s.reset(SRC_OPEN_ELECTRICITY, t);
  s.done(FETCH_MIX, t, -1);
  TEST_ASSERT_EQUAL(t + 60, s.next(FETCH_MIX));
  s.done(FETCH_MIX, t, 500);
  TEST_ASSERT_EQUAL(t + 120, s.next(FETCH_MIX));
  for (int i = 0; i < 5; i++) s.done(FETCH_MIX, t, -1);
  TEST_ASSERT_TRUE(s.next(FETCH_MIX) <= t + 20 * 60);
  s.done(FETCH_MIX, t, 200);
  TEST_ASSERT_EQUAL(0, s.failures(FETCH_MIX));

  s.done(FETCH_MIX, t, 401);
  TEST_ASSERT_TRUE(s.key_rejected());
  TEST_ASSERT_EQUAL(t + 6 * 3600, s.next(FETCH_MIX));

  s.done(FETCH_MARKET, t, 429);   // out of credits: wait for the reset at midnight UTC (10:30 am Adelaide)
  TEST_ASSERT_TRUE(s.out_of_credits());
  TEST_ASSERT_EQUAL(days_from_civil(2026, 11, 13) * 86400 + 300, s.next(FETCH_MARKET));

  s.reset(SRC_OPEN_ELECTRICITY, t + 5);   // new settings clear it all
  TEST_ASSERT_FALSE(s.key_rejected());
  TEST_ASSERT_TRUE(s.due(FETCH_MIX, t + 5));
}

void test_eia_has_no_market_requests() {
  Scheduler s;
  s.reset(SRC_EIA, T0());
  TEST_ASSERT_FALSE(s.due(FETCH_MARKET, T0() + 86400 * 30));
  s.done(FETCH_MIX, T0(), 200);
  TEST_ASSERT_EQUAL(120, s.next(FETCH_MIX) % 900);
}

void test_ago() {
  char s[32];
  format_ago(20, s, sizeof s);         TEST_ASSERT_EQUAL_STRING("Updated just now", s);
  format_ago(9 * 60 + 10, s, sizeof s); TEST_ASSERT_EQUAL_STRING("Updated 9 min ago", s);
  format_ago(3 * 3600 + 5, s, sizeof s); TEST_ASSERT_EQUAL_STRING("Updated 3 h ago", s);
  format_ago(86400 + 7200, s, sizeof s); TEST_ASSERT_EQUAL_STRING("Updated 1 day ago", s);
  format_ago(3 * 86400, s, sizeof s);  TEST_ASSERT_EQUAL_STRING("Updated 3 days ago", s);
  TEST_ASSERT_TRUE(data_is_fresh(SRC_OPEN_ELECTRICITY, 20 * 60));
  TEST_ASSERT_FALSE(data_is_fresh(SRC_OPEN_ELECTRICITY, 40 * 60));
  TEST_ASSERT_TRUE(data_is_fresh(SRC_EIA, 2 * 3600));
  TEST_ASSERT_EQUAL(1, freshness_level(SRC_OPEN_ELECTRICITY, 60 * 60));
  TEST_ASSERT_EQUAL(2, freshness_level(SRC_OPEN_ELECTRICITY, 2 * 3600));
  TEST_ASSERT_EQUAL(0, freshness_level(SRC_EIA, 19 * 3600));   // normal EIA lag (seen 13 Nov 2026)
  TEST_ASSERT_EQUAL(1, freshness_level(SRC_EIA, 30 * 3600));
  TEST_ASSERT_EQUAL(2, freshness_level(SRC_EIA, 3 * 86400));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_nem_time_is_utc_plus_10);
  RUN_TEST(test_feeds);
  RUN_TEST(test_urls);
  RUN_TEST(test_regular_timing_stays_in_budget);
  RUN_TEST(test_low_credits_slow_down);
  RUN_TEST(test_backoff_and_errors);
  RUN_TEST(test_eia_has_no_market_requests);
  RUN_TEST(test_ago);
  return UNITY_END();
}
