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
  return UNITY_END();
}
