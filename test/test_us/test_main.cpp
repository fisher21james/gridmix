// US grid operators: each parser against a real answer saved on 14 Nov 2026 (00:28 UTC).
#include <unity.h>
#include <string.h>
#include "us_grid.h"
#include "schedule.h"
#include "settings.h"
#include "../test_gridcore/fixtures.h"
using namespace gridcore;

static const int64_t NOW = 1794616131;   // 2026-11-14T00:28:51Z
static int64_t T(const char *iso) { return parse_iso_time(iso); }

void setUp() {}
void tearDown() {}

static void check(const char *region, const char *body, const char *when, float total, float renew) {
  GridSnapshot s;
  TEST_ASSERT_TRUE_MESSAGE(parse_us(region, body, strlen(body), NOW, s), region);
  TEST_ASSERT_EQUAL_INT64_MESSAGE(T(when), s.data_time, region);
  TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, total, s.total(), region);
  TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.05f, renew, s.renewable_pct(), region);
}

void test_now() {
  TEST_ASSERT_EQUAL_INT64(T("2026-11-14T00:28:51Z"), NOW);
  check("CAL", US_CAISO, "2026-11-14T00:20Z", 25079, 38.97f);    // 4:20pm Pacific
  check("TEX", US_ERCOT, "2026-11-14T00:25Z", 53609, 20.22f);    // 6:25pm Central
  check("MIDW", US_MISO, "2026-11-14T00:20Z", 79109, 8.85f);     // 7:20pm Eastern Standard
  check("CENT", US_SPP, "2026-11-14T00:25Z", 34627.5f, 44.65f);
  check("NY", US_NYISO, "2026-11-14T00:10Z", 17580, 24.08f);     // 7:10pm Eastern
  check("NE", US_ISONE, "2026-11-14T00:22:24Z", 14641, 18.94f);
  check("NW", US_BPA, "2026-11-14T00:25Z", 11226, 80.58f);       // 4:25pm Pacific
}

void test_details() {
  GridSnapshot s;
  parse_us("CAL", US_CAISO, strlen(US_CAISO), NOW, s);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 696, s.mw[GEOTHERMAL]);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 1760, s.mw[HYDRO]);     // small + large hydro
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 2607, s.mw[BATTERY]);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 1323, s.mw[IMPORTS]);
  parse_us("TEX", US_ERCOT, strlen(US_ERCOT), NOW, s);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 6048, s.mw[BATTERY]);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 7248, s.mw[COAL]);
  parse_us("NE", US_ISONE, strlen(US_ISONE), NOW, s);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 585, s.mw[BIOMASS]);    // wood + refuse + landfill gas
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 959, s.mw[IMPORTS]);
  parse_us("NY", US_NYISO, strlen(US_NYISO), NOW, s);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 10008, s.mw[GAS]);      // dual fuel + natural gas
  TEST_ASSERT_FALSE(parse_us("SE", US_CAISO, strlen(US_CAISO), NOW, s));   // not a grid we support
}

void test_history() {
  HistoryPoint p[48];
  struct { const char *region, *body; int min; } c[] = {
    {"CAL", US_CAISO, 33}, {"TEX", US_ERCOT, 48}, {"NY", US_NYISO, 38}, {"NW", US_BPA, 40}, {"MIDW", US_MISO_TODAY, 12}};
  for (auto &k : c) {
    const int n = parse_us_history(k.region, k.body, strlen(k.body), NOW, p, 48);
    TEST_ASSERT_TRUE_MESSAGE(n >= k.min, k.region);
    for (int i = 1; i < n; i++) TEST_ASSERT_TRUE_MESSAGE(p[i].time > p[i - 1].time, k.region);
    TEST_ASSERT_TRUE_MESSAGE(p[n - 1].time <= NOW && p[0].time >= NOW - 25 * 3600, k.region);
  }
}

void test_requests() {
  char url[256];
  TEST_ASSERT_TRUE(us_url("NY", NOW, url, sizeof url) > 0);
  TEST_ASSERT_EQUAL_STRING("https://mis.nyiso.com/public/csv/rtfuelmix/20261113rtfuelmix.csv", url);   // still the 13th in New York
  TEST_ASSERT_NOT_NULL(us_post_body("NE"));
  TEST_ASSERT_NULL(us_post_body("CAL"));
  TEST_ASSERT_TRUE(us_history_url("MIDW", NOW, url, sizeof url) > 0);
  TEST_ASSERT_EQUAL(0, (int)us_history_url("CAL", NOW, url, sizeof url));
  const Feed f = feed_for("US", "TEX", true);
  TEST_ASSERT_EQUAL(SRC_US, f.source);
  TEST_ASSERT_EQUAL_STRING("TEX", f.region);
  TEST_ASSERT_TRUE(country_info("US").single_view);
  TEST_ASSERT_TRUE(country_info("FR").single_view);
  TEST_ASSERT_FALSE(country_info("GB").single_view);
}

void test_daylight_saving() {
  // 4:20pm Pacific: winter (PST, UTC-8) and summer (PDT, UTC-7)
  TEST_ASSERT_EQUAL_INT64(T("2026-11-14T00:20Z"), us_local_to_utc(2026, 11, 13, 16, 20, -8));
  TEST_ASSERT_EQUAL_INT64(T("2026-07-01T23:20Z"), us_local_to_utc(2026, 7, 1, 16, 20, -8));
  // 2026: daylight saving from Sunday 8 March to Sunday 1 November
  TEST_ASSERT_EQUAL_INT64(T("2026-03-08T15:00Z"), us_local_to_utc(2026, 3, 8, 8, 0, -8));    // after 2am: PDT
  TEST_ASSERT_EQUAL_INT64(T("2026-03-07T16:00Z"), us_local_to_utc(2026, 3, 7, 8, 0, -8));    // day before: PST
  TEST_ASSERT_EQUAL_INT64(T("2026-11-01T16:00Z"), us_local_to_utc(2026, 11, 1, 8, 0, -8));   // after the change: PST
  TEST_ASSERT_EQUAL_INT64(T("2026-10-31T15:00Z"), us_local_to_utc(2026, 10, 31, 8, 0, -8));  // day before: PDT
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_now);
  RUN_TEST(test_details);
  RUN_TEST(test_history);
  RUN_TEST(test_requests);
  RUN_TEST(test_daylight_saving);
  return UNITY_END();
}
