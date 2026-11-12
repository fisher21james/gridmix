// Tests for the data translators, run on a normal computer (and on every push via GitHub Actions):
//   pio test -e native
// The fixtures are real API responses captured on 11-12 Nov 2026, trimmed to a few points.

#include <unity.h>
#include <string.h>
#include "grid_snapshot.h"
#include "parsers.h"
#include "fixtures.h"

using namespace gridcore;

void setUp() {}
void tearDown() {}

// ---------- time helpers

void test_iso_time_utc_hour() {
  TEST_ASSERT_EQUAL_INT64(1794373200, parse_iso_time("2026-11-11T05"));
}

void test_iso_time_with_offset() {
  TEST_ASSERT_EQUAL_INT64(1794460800, parse_iso_time("2026-11-12T15:20:00+10:00"));
}

void test_iso_time_half_hour_offset() {
  // Adelaide daylight time is +10:30; 15:50 there is the same moment as 15:20 in NEM time (+10:00)
  TEST_ASSERT_EQUAL_INT64(1794460800, parse_iso_time("2026-11-12T15:50:00+10:30"));
}

void test_iso_time_rejects_garbage() {
  TEST_ASSERT_EQUAL_INT64(-1, parse_iso_time("yesterday"));
}

// ---------- EIA (Texas, real response)

void test_eia_parses() {
  GridSnapshot s;
  TEST_ASSERT_TRUE(parse_eia(EIA_ERCOT, strlen(EIA_ERCOT), s));
  TEST_ASSERT_TRUE(s.valid);
  TEST_ASSERT_EQUAL_INT64(1794373200, s.data_time);
}

void test_eia_mix() {
  GridSnapshot s;
  parse_eia(EIA_ERCOT, strlen(EIA_ERCOT), s);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 49568.0f, s.total());
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 61.11f, s.share_pct(GAS));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 15.59f, s.share_pct(COAL));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 15.26f, s.share_pct(WIND));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 7.78f, s.share_pct(NUCLEAR));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, s.share_pct(SOLAR));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 15.52f, s.renewable_pct());
}

void test_eia_negative_battery_is_charging() {
  GridSnapshot s;
  parse_eia(EIA_ERCOT, strlen(EIA_ERCOT), s);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 938.0f, s.battery_charging);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, s.mw[BATTERY]);
  TEST_ASSERT_TRUE(s.battery_is_charging());
}

void test_eia_bad_json_fails_cleanly() {
  GridSnapshot s;
  const char *bad = "{\"error\":\"API_KEY_INVALID\"}";
  TEST_ASSERT_FALSE(parse_eia(bad, strlen(bad), s));
  TEST_ASSERT_FALSE(s.valid);
}

// ---------- Open Electricity (South Australia, real response, fueltech groups)

void test_oe_parses() {
  GridSnapshot s;
  TEST_ASSERT_TRUE(parse_openelectricity(OE_SA1_GROUPS, strlen(OE_SA1_GROUPS), s));
  TEST_ASSERT_EQUAL_INT64(1794460800, s.data_time);   // 2026-11-12 15:20 +10:00
}

void test_oe_mix() {
  GridSnapshot s;
  parse_openelectricity(OE_SA1_GROUPS, strlen(OE_SA1_GROUPS), s);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 936.0f, s.total());
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 37.22f, s.share_pct(GAS));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 56.23f, s.renewable_pct());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, s.share_pct(COAL));
}

void test_oe_battery_both_ways() {
  GridSnapshot s;
  parse_openelectricity(OE_SA1_GROUPS, strlen(OE_SA1_GROUPS), s);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 61.36f, s.mw[BATTERY]);          // discharging counts in the mix
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 119.71f, s.battery_charging);   // charging does not
  TEST_ASSERT_TRUE(s.battery_is_charging());                      // more going in than out
}

// ---------- Open Electricity rooftop solar that hasn't arrived yet (fueltech level)

static const char OE_ROOFTOP_LATE[] = R"({"success":true,"data":[{"metric":"power","results":[
  {"columns":{"fueltech":"solar_utility"},"data":[["2026-11-12T15:15:00+10:00",120],["2026-11-12T15:20:00+10:00",100]]},
  {"columns":{"fueltech":"solar_rooftop"},"data":[["2026-11-12T15:15:00+10:00",500],["2026-11-12T15:20:00+10:00",null]]},
  {"columns":{"fueltech":"wind"},"data":[["2026-11-12T15:15:00+10:00",380],["2026-11-12T15:20:00+10:00",400]]}
]}]})";

void test_oe_rooftop_carried_forward() {
  GridSnapshot s;
  TEST_ASSERT_TRUE(parse_openelectricity(OE_ROOFTOP_LATE, strlen(OE_ROOFTOP_LATE), s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 600.0f, s.mw[SOLAR]);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000.0f, s.total());
  TEST_ASSERT_TRUE(s.estimated);
}

void test_oe_failure_response() {
  GridSnapshot s;
  const char *bad = "{\"success\":false,\"error\":\"Unauthorized\"}";
  TEST_ASSERT_FALSE(parse_openelectricity(bad, strlen(bad), s));
}

// ---------- Open Electricity at fueltech level + market data (real, 12 Nov 2026 15:35 NEM time)

void test_oe_fueltech_has_no_rooftop() {
  GridSnapshot s;
  TEST_ASSERT_TRUE(parse_openelectricity(OE_SA1_FUELTECH, strlen(OE_SA1_FUELTECH), s));
  TEST_ASSERT_EQUAL_INT64(1794461700, s.data_time);
  TEST_ASSERT_FALSE(s.has_rooftop);                         // 5-minute data has no rooftop series
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 392.13f, s.mw[GAS]);      // ccgt + ocgt + steam + recip folded together
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 1013.64f, s.total());
}

void test_oe_market_price_and_forecast() {
  MarketInfo m;
  TEST_ASSERT_TRUE(parse_openelectricity_market(OE_SA1_MARKET, strlen(OE_SA1_MARKET), 1794461700, m));
  TEST_ASSERT_TRUE(m.has_price);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.0f, m.price);
  TEST_ASSERT_EQUAL_INT64(1794462000, m.price_time);        // 15:40, newest price interval
  TEST_ASSERT_TRUE(m.has_rooftop_forecast);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1045.81f, m.rooftop_forecast);   // forecast for 15:35, not a future slot
}

void test_oe_merge_adds_rooftop_solar() {
  GridSnapshot s;
  MarketInfo m;
  parse_openelectricity(OE_SA1_FUELTECH, strlen(OE_SA1_FUELTECH), s);
  parse_openelectricity_market(OE_SA1_MARKET, strlen(OE_SA1_MARKET), s.data_time, m);
  merge_market(s, m);
  TEST_ASSERT_TRUE(s.has_rooftop);
  TEST_ASSERT_TRUE(s.estimated);
  TEST_ASSERT_TRUE(s.has_price);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 2059.46f, s.total());
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 78.43f, s.renewable_pct());   // 56% without rooftop solar
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 56.03f, s.share_pct(SOLAR));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 19.04f, s.share_pct(GAS));
}

void test_oe_merge_keeps_actual_rooftop() {
  // If the generation data already had rooftop solar, the forecast must not be added on top
  GridSnapshot s;
  parse_openelectricity(OE_ROOFTOP_LATE, strlen(OE_ROOFTOP_LATE), s);
  MarketInfo m;
  m.has_rooftop_forecast = true;
  m.rooftop_forecast = 999;
  merge_market(s, m);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 600.0f, s.mw[SOLAR]);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_iso_time_utc_hour);
  RUN_TEST(test_iso_time_with_offset);
  RUN_TEST(test_iso_time_half_hour_offset);
  RUN_TEST(test_iso_time_rejects_garbage);
  RUN_TEST(test_eia_parses);
  RUN_TEST(test_eia_mix);
  RUN_TEST(test_eia_negative_battery_is_charging);
  RUN_TEST(test_eia_bad_json_fails_cleanly);
  RUN_TEST(test_oe_parses);
  RUN_TEST(test_oe_mix);
  RUN_TEST(test_oe_battery_both_ways);
  RUN_TEST(test_oe_rooftop_carried_forward);
  RUN_TEST(test_oe_failure_response);
  RUN_TEST(test_oe_fueltech_has_no_rooftop);
  RUN_TEST(test_oe_market_price_and_forecast);
  RUN_TEST(test_oe_merge_adds_rooftop_solar);
  RUN_TEST(test_oe_merge_keeps_actual_rooftop);
  return UNITY_END();
}
