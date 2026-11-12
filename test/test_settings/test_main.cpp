// Settings <-> JSON tests. The JSON below is exactly what the phone setup page sends.
#include <unity.h>
#include <string.h>
#include "settings.h"

using namespace gridcore;

static const char FROM_PAGE[] = R"({"country":"AU","region":"SA1","tz":"ACST-9:30ACDT,M10.1.0,M4.1.0/3","view":"local",
"keys":{"oe":{"set":false,"hint":"","value":"oe_TESTKEY1234abcd"},"eia":{"set":false,"hint":""}},
"tariff":{"type":"tou","flat":null,"periods":[
 {"name":"Peak","from":"06:00","to":"10:00","rate":53.99,"days":"all"},
 {"name":"Shoulder","from":"10:00","to":"15:00","rate":23.78,"days":"all"},
 {"name":"Peak","from":"15:00","to":"01:00","rate":53.99,"days":"all"},
 {"name":"Off peak","from":"01:00","to":"06:00","rate":34.51,"days":"all"}],
 "supply":116.18,"feed_in":1,"warn":true},
"display":{"brightness":80,"night_dim":true}})";

static char err[64];
static char buf[2048];

void setUp() { err[0] = 0; }
void tearDown() {}

void test_reads_page_json() {
  Settings s;
  TEST_ASSERT_TRUE_MESSAGE(settings_from_json(FROM_PAGE, strlen(FROM_PAGE), s, err, sizeof err), err);
  TEST_ASSERT_EQUAL_STRING("SA1", s.region);
  TEST_ASSERT_EQUAL_STRING("oe_TESTKEY1234abcd", s.key_oe);
  TEST_ASSERT_EQUAL(4, s.tou.count);
  TEST_ASSERT_EQUAL(15 * 60, s.tou.periods[2].start_min);
  TEST_ASSERT_EQUAL(60, s.tou.periods[2].end_min);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 116.18f, s.tou.supply_cents_per_day);
  TEST_ASSERT_TRUE(s.has_tariff());
}

void test_public_json_hides_keys() {
  Settings s;
  settings_from_json(FROM_PAGE, strlen(FROM_PAGE), s, err, sizeof err);
  TEST_ASSERT_TRUE(settings_to_json(s, true, buf, sizeof buf) > 0);
  TEST_ASSERT_NULL(strstr(buf, "TESTKEY"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"hint\":\"abcd\""));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"set\":true"));
}

void test_storage_round_trip() {
  Settings a, b;
  settings_from_json(FROM_PAGE, strlen(FROM_PAGE), a, err, sizeof err);
  a.brightness = 55;
  a.view_country = true;
  const size_t n = settings_to_json(a, false, buf, sizeof buf);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE_MESSAGE(settings_from_json(buf, n, b, err, sizeof err), err);
  TEST_ASSERT_EQUAL_STRING(a.key_oe, b.key_oe);
  TEST_ASSERT_EQUAL(55, b.brightness);
  TEST_ASSERT_TRUE(b.view_country);
  TEST_ASSERT_EQUAL(a.tou.count, b.tou.count);
  TEST_ASSERT_EQUAL_STRING("Off peak", b.tou.periods[3].name);
}

void test_saving_without_key_keeps_stored_key() {
  Settings s;
  settings_from_json(FROM_PAGE, strlen(FROM_PAGE), s, err, sizeof err);
  const char *later = R"({"country":"AU","region":"VIC1","tz":"AEST-10AEDT,M10.1.0,M4.1.0/3","keys":{"oe":{"set":true,"hint":"abcd"}}})";
  TEST_ASSERT_TRUE_MESSAGE(settings_from_json(later, strlen(later), s, err, sizeof err), err);
  TEST_ASSERT_EQUAL_STRING("VIC1", s.region);
  TEST_ASSERT_EQUAL_STRING("oe_TESTKEY1234abcd", s.key_oe);
}

void test_bad_region_changes_nothing() {
  Settings s;
  settings_from_json(FROM_PAGE, strlen(FROM_PAGE), s, err, sizeof err);
  const char *bad = R"({"country":"AU","region":"TEX","keys":{"oe":{"value":"oe_OTHER"}}})";
  TEST_ASSERT_FALSE(settings_from_json(bad, strlen(bad), s, err, sizeof err));
  TEST_ASSERT_EQUAL_STRING("That region isn't in the country chosen.", err);
  TEST_ASSERT_EQUAL_STRING("SA1", s.region);
  TEST_ASSERT_EQUAL_STRING("oe_TESTKEY1234abcd", s.key_oe);
}

void test_plan_with_gap_is_refused() {
  Settings s;
  const char *gap = R"({"country":"AU","region":"SA1","tariff":{"type":"tou","periods":[
    {"name":"Day","from":"07:00","to":"22:00","rate":30,"days":"all"},
    {"name":"Night","from":"22:00","to":"06:00","rate":20,"days":"all"}]}})";
  TEST_ASSERT_FALSE(settings_from_json(gap, strlen(gap), s, err, sizeof err));
  TEST_ASSERT_EQUAL_STRING("Power plan: Gap at Sun 06:00.", err);
}

void test_flat_plan() {
  Settings s;
  const char *flat = R"({"country":"US","region":"TEX","tz":"CST6CDT,M3.2.0,M11.1.0","tariff":{"type":"flat","flat":14.2,"periods":[],"supply":45}})";
  TEST_ASSERT_TRUE_MESSAGE(settings_from_json(flat, strlen(flat), s, err, sizeof err), err);
  TEST_ASSERT_TRUE(s.tariff_flat);
  Tariff t = s.active_tariff();
  RateNow r;
  TEST_ASSERT_TRUE(tariff_rate_now(t, 3, 600, r));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 14.2f, r.cents);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 45.0f, t.supply_cents_per_day);
  TEST_ASSERT_EQUAL_STRING("Texas", region_label("US", "TEX"));
}

void test_key_with_spaces_refused() {
  Settings s;
  const char *bad = R"({"country":"AU","region":"SA1","keys":{"oe":{"value":"oe_abc def"}}})";
  TEST_ASSERT_FALSE(settings_from_json(bad, strlen(bad), s, err, sizeof err));
}

void test_brightness_clamped() {
  Settings s;
  const char *j = R"({"country":"AU","region":"SA1","display":{"brightness":3}})";
  settings_from_json(j, strlen(j), s, err, sizeof err);
  TEST_ASSERT_EQUAL(10, s.brightness);
}

void test_garbage_refused() {
  Settings s;
  TEST_ASSERT_FALSE(settings_from_json("{nope", 5, s, err, sizeof err));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_reads_page_json);
  RUN_TEST(test_public_json_hides_keys);
  RUN_TEST(test_storage_round_trip);
  RUN_TEST(test_saving_without_key_keeps_stored_key);
  RUN_TEST(test_bad_region_changes_nothing);
  RUN_TEST(test_plan_with_gap_is_refused);
  RUN_TEST(test_flat_plan);
  RUN_TEST(test_key_with_spaces_refused);
  RUN_TEST(test_brightness_clamped);
  RUN_TEST(test_garbage_refused);
  return UNITY_END();
}
