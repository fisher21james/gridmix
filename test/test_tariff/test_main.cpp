// Tariff engine tests, using a real South Australian time-of-use plan (Nov 2026 bill):
//   Peak      6am-10am and 3pm-1am  53.99 c/kWh
//   Shoulder  10am-3pm              23.78 c/kWh  (the cheapest on this plan)
//   Off peak  1am-6am               34.51 c/kWh
//   Supply 116.18 c/day, feed-in 1 c/kWh

#include <unity.h>
#include <math.h>
#include "tariff.h"

using namespace gridcore;

static Tariff plan;
static const int THU = 4;

static int hm(int h, int m) { return h * 60 + m; }

void setUp() {
  plan = Tariff();
  plan.add("Peak", hm(6, 0), hm(10, 0), 53.99f);
  plan.add("Peak", hm(15, 0), hm(1, 0), 53.99f);    // crosses midnight
  plan.add("Shoulder", hm(10, 0), hm(15, 0), 23.78f);
  plan.add("Off peak", hm(1, 0), hm(6, 0), 34.51f);
  plan.supply_cents_per_day = 116.18f;
  plan.feed_in_cents = 1.0f;
}
void tearDown() {}

void test_plan_is_valid() {
  char err[48];
  TEST_ASSERT_TRUE_MESSAGE(tariff_validate(plan, err, sizeof err), err);
}

void test_midday_is_cheapest() {
  RateNow r;
  TEST_ASSERT_TRUE(tariff_rate_now(plan, THU, hm(13, 20), r));
  TEST_ASSERT_EQUAL_STRING("Shoulder", plan.periods[r.index].name);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 23.78f, r.cents);
  TEST_ASSERT_EQUAL(BAND_LOW, r.band);                 // green, even though it's called "Shoulder"
  TEST_ASSERT_EQUAL(100, r.minutes_to_change);         // peak at 3pm
  TEST_ASSERT_EQUAL_STRING("Peak", plan.periods[r.next_index].name);
}

void test_evening_peak_crosses_midnight() {
  RateNow r;
  tariff_rate_now(plan, THU, hm(21, 40), r);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 53.99f, r.cents);
  TEST_ASSERT_EQUAL(BAND_HIGH, r.band);
  TEST_ASSERT_EQUAL(200, r.minutes_to_change);         // until 1:00am
  TEST_ASSERT_EQUAL_STRING("Off peak", plan.periods[r.next_index].name);

  tariff_rate_now(plan, THU, hm(0, 30), r);            // just after midnight, still peak
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 53.99f, r.cents);
}

void test_off_peak_is_middle_band() {
  RateNow r;
  tariff_rate_now(plan, THU, hm(3, 0), r);
  TEST_ASSERT_EQUAL_STRING("Off peak", plan.periods[r.index].name);
  TEST_ASSERT_EQUAL(BAND_MID, r.band);                 // dearer than shoulder on this plan
  TEST_ASSERT_EQUAL(180, r.minutes_to_change);         // peak at 6am
}

void test_boundaries() {
  TEST_ASSERT_EQUAL_STRING("Peak", plan.periods[tariff_period_at(plan, THU, hm(6, 0))].name);
  TEST_ASSERT_EQUAL_STRING("Off peak", plan.periods[tariff_period_at(plan, THU, hm(5, 59))].name);
  TEST_ASSERT_EQUAL_STRING("Shoulder", plan.periods[tariff_period_at(plan, THU, hm(10, 0))].name);
  TEST_ASSERT_EQUAL_STRING("Peak", plan.periods[tariff_period_at(plan, THU, hm(15, 0))].name);
  TEST_ASSERT_EQUAL_STRING("Off peak", plan.periods[tariff_period_at(plan, THU, hm(1, 0))].name);
}

void test_rates_match_the_real_bill() {
  // 273.824 kWh peak, 139.955 off peak, 6.201 shoulder, 30 days supply -> $232.46 on the bill
  const float cents = 273.824f * plan.periods[0].cents + 139.955f * plan.periods[3].cents +
                      6.201f * plan.periods[2].cents + 30 * plan.supply_cents_per_day;
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 23246.0f, roundf(cents));
}

void test_gap_is_caught() {
  Tariff t;
  t.add("Day", hm(7, 0), hm(22, 0), 30);
  t.add("Night", hm(22, 0), hm(6, 0), 20);   // 6am-7am missing
  char err[48];
  TEST_ASSERT_FALSE(tariff_validate(t, err, sizeof err));
  TEST_ASSERT_EQUAL_STRING("Gap at Sun 06:00", err);
}

void test_overlap_is_caught() {
  Tariff t;
  t.add("Day", hm(7, 0), hm(22, 0), 30);
  t.add("Night", hm(21, 0), hm(7, 0), 20);
  char err[48];
  TEST_ASSERT_FALSE(tariff_validate(t, err, sizeof err));
  TEST_ASSERT_EQUAL_STRING("Overlap at Sun 21:00", err);
}

void test_flat_rate() {
  Tariff t;
  t.add("Flat", 0, 0, 32.5f);   // start == end: all day
  RateNow r;
  TEST_ASSERT_TRUE(tariff_rate_now(t, THU, hm(12, 0), r));
  TEST_ASSERT_EQUAL(BAND_MID, r.band);
  TEST_ASSERT_EQUAL(-1, r.minutes_to_change);   // never changes
  char err[48];
  TEST_ASSERT_TRUE(tariff_validate(t, err, sizeof err));
}

void test_weekend_rates() {
  Tariff t;
  const uint8_t weekdays = 0x3E, weekend = 0x41;   // Mon-Fri, Sat+Sun
  t.add("Peak", hm(7, 0), hm(21, 0), 50, weekdays);
  t.add("Off peak", hm(21, 0), hm(7, 0), 25, weekdays);
  t.add("Weekend", 0, 0, 30, weekend);
  char err[48];
  TEST_ASSERT_TRUE_MESSAGE(tariff_validate(t, err, sizeof err), err);
  RateNow r;
  tariff_rate_now(t, 5 /*Fri*/, hm(20, 0), r);
  TEST_ASSERT_EQUAL(60, r.minutes_to_change);
  tariff_rate_now(t, 6 /*Sat*/, hm(12, 0), r);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 30, r.cents);
}

void test_parse_hhmm() {
  TEST_ASSERT_EQUAL(900, parse_hhmm("15:00"));
  TEST_ASSERT_EQUAL(1440, parse_hhmm("24:00"));
  TEST_ASSERT_EQUAL(-1, parse_hhmm("25:00"));
  TEST_ASSERT_EQUAL(-1, parse_hhmm("3pm"));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_plan_is_valid);
  RUN_TEST(test_midday_is_cheapest);
  RUN_TEST(test_evening_peak_crosses_midnight);
  RUN_TEST(test_off_peak_is_middle_band);
  RUN_TEST(test_boundaries);
  RUN_TEST(test_rates_match_the_real_bill);
  RUN_TEST(test_gap_is_caught);
  RUN_TEST(test_overlap_is_caught);
  RUN_TEST(test_flat_rate);
  RUN_TEST(test_weekend_rates);
  RUN_TEST(test_parse_hhmm);
  return UNITY_END();
}
