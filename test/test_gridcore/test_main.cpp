// Tests for the data translators, run on a normal computer (and on every push via GitHub Actions):
//   pio test -e native
// The fixtures are real API responses captured on 11-12 Nov 2026, trimmed to a few points.

#include <unity.h>
#include <string.h>
#include "grid_snapshot.h"
#include "parsers.h"
#include "fixtures.h"
#include "i18n.h"
#include "tariff.h"

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

// Lower-48 answers carry newer codes (live check, 13 Nov 2026)
void test_eia_us48_codes() {
  const char *j = "{\"response\":{\"data\":["
    "{\"period\":\"2026-11-12T04\",\"fueltype\":\"WND\",\"value\":\"100\"},"
    "{\"period\":\"2026-11-12T04\",\"fueltype\":\"WNB\",\"value\":\"20\"},"
    "{\"period\":\"2026-11-12T04\",\"fueltype\":\"SNB\",\"value\":\"30\"},"
    "{\"period\":\"2026-11-12T04\",\"fueltype\":\"OES\",\"value\":\"10\"},"
    "{\"period\":\"2026-11-12T04\",\"fueltype\":\"UES\",\"value\":\"-11\"},"
    "{\"period\":\"2026-11-12T04\",\"fueltype\":\"GEO\",\"value\":\"40\"}]}}";
  GridSnapshot s;
  TEST_ASSERT_TRUE(parse_eia(j, strlen(j), s));
  TEST_ASSERT_EQUAL_FLOAT(120, s.mw[WIND]);
  TEST_ASSERT_EQUAL_FLOAT(30, s.mw[SOLAR]);
  TEST_ASSERT_EQUAL_FLOAT(10, s.mw[BATTERY]);
  TEST_ASSERT_EQUAL_FLOAT(11, s.battery_charging);
  TEST_ASSERT_EQUAL_FLOAT(40, s.mw[OTHER]);
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

void test_oe_market_forecast() {
  MarketInfo m;   // the captured answer also has a price series, which is ignored
  TEST_ASSERT_TRUE(parse_openelectricity_market(OE_SA1_MARKET, strlen(OE_SA1_MARKET), 1794461700, m));
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

// WA (WEM), live 13 Nov 2026 8:05am Perth: its 5-minute data includes rooftop solar,
// and the net "battery" series must not be counted on top of charging/discharging.
void test_oe_wem() {
  GridSnapshot s;
  TEST_ASSERT_TRUE(parse_openelectricity(OE_WEM_FUELTECH, strlen(OE_WEM_FUELTECH), s));
  TEST_ASSERT_EQUAL_INT64(days_from_civil(2026, 11, 13) * 86400 + 5 * 60, s.data_time);   // 08:05+08:00 = 00:05 UTC
  TEST_ASSERT_TRUE(s.has_rooftop);
  TEST_ASSERT_FALSE(s.estimated);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 1080.9f, s.mw[SOLAR]);     // 836.9 rooftop + 244.0 utility
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, s.mw[BATTERY]);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 488.7f, s.battery_charging);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 48.7f, s.renewable_pct());   // includes WA's bioenergy (46 MW)
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 46.0f, s.mw[BIOMASS]);
}

// History: the 5-minute capture has 14 timestamps; each point's share must match what the live
// translator gets for that moment (newest point checked; rooftop isn't in this capture).
void test_oe_history() {
  HistoryPoint pts[64];
  const int n = parse_openelectricity_history(OE_SA1_FUELTECH, strlen(OE_SA1_FUELTECH), pts, 64);
  TEST_ASSERT_EQUAL(14, n);
  TEST_ASSERT_TRUE(pts[0].time < pts[13].time);
  GridSnapshot s;
  parse_openelectricity(OE_SA1_FUELTECH, strlen(OE_SA1_FUELTECH), s);
  TEST_ASSERT_EQUAL_INT64(s.data_time, pts[13].time);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, s.renewable_pct(), pts[13].renewable_pct);
  TEST_ASSERT_EQUAL(0, parse_openelectricity_history("{\"success\":false}", 17, pts, 64));
}

// Real hourly answer for SA (13 Nov 2026 10am): 25 hours, rooftop solar stops at 8am,
// so the last two hours are left out rather than shown too low.
void test_oe_history_real() {
  HistoryPoint pts[64];
  const int n = parse_openelectricity_history(OE_SA1_HISTORY, strlen(OE_SA1_HISTORY), pts, 64);
  TEST_ASSERT_EQUAL(23, n);
  TEST_ASSERT_EQUAL_INT64(parse_iso_time("2026-11-12T10:00:00+10:00"), pts[0].time);
  TEST_ASSERT_EQUAL_INT64(parse_iso_time("2026-11-13T08:00:00+10:00"), pts[n - 1].time);
  for (int i = 0; i < n; i++) {
    TEST_ASSERT_TRUE(pts[i].renewable_pct >= 0 && pts[i].renewable_pct <= 100);
    if (i) TEST_ASSERT_EQUAL_INT64(3600, pts[i].time - pts[i - 1].time);
  }
}

// ---------- Great Britain (Carbon Intensity API), London 13 Nov 2026 04:00-04:30 UTC

void test_ci_now() {
  GridSnapshot s;
  TEST_ASSERT_TRUE(parse_carbon_intensity(CI_LONDON_NOW, strlen(CI_LONDON_NOW), s));
  TEST_ASSERT_TRUE(s.percent_only);
  TEST_ASSERT_EQUAL_INT64(parse_iso_time("2026-11-13T04:30Z"), s.data_time);   // end of the half hour
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 37.1f, s.mw[WIND]);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 32.6f, s.mw[IMPORTS]);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.6f, s.mw[BIOMASS]);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 13.6f, s.mw[NUCLEAR]);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 41.9f, s.renewable_pct());   // wind 37.1 + hydro 0.2 + biomass 4.6; imports not counted
  TEST_ASSERT_FALSE(parse_carbon_intensity("{\"data\":[]}", 11, s));
}

void test_rte_now() {
  GridSnapshot s;
  TEST_ASSERT_TRUE(parse_rte(RTE_FRANCE_NOW, strlen(RTE_FRANCE_NOW), s));
  TEST_ASSERT_FALSE(s.percent_only);
  TEST_ASSERT_EQUAL_INT64(parse_iso_time("2026-11-13T05:15Z"), s.data_time);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 44436, s.mw[NUCLEAR]);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 4158, s.mw[HYDRO]);    // 4206 less 48 MW of pumped storage
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 84, s.mw[OTHER]);      // oil 36 + pumped storage 48
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 969, s.mw[BIOMASS]);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0, s.mw[IMPORTS]);     // exporting 4578 MW
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 12, s.battery_charging);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 58689, s.total());
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 21.7f, s.renewable_pct());   // wind + hydro + bioenergy; nuclear isn't renewable
  // Some figures arrive as strings, and importing shows up as imports
  const char *j = "{\"results\":[{\"date_heure\":\"2026-11-13T05:00:00+00:00\",\"nucleaire\":100,\"hydraulique\":\"50\",\"ech_physiques\":25}]}";
  TEST_ASSERT_TRUE(parse_rte(j, strlen(j), s));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50, s.mw[HYDRO]);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25, s.mw[IMPORTS]);
  TEST_ASSERT_FALSE(parse_rte("{\"results\":[{\"date_heure\":\"2026-11-13T05:30:00+00:00\",\"nucleaire\":null}]}", 64, s));
}

void test_rte_history() {
  HistoryPoint p[64];
  const int n = parse_rte_history(RTE_FRANCE_HISTORY, strlen(RTE_FRANCE_HISTORY), p, 64);
  TEST_ASSERT_EQUAL(47, n);
  TEST_ASSERT_EQUAL_INT64(parse_iso_time("2026-11-12T06:00Z"), p[0].time);
  TEST_ASSERT_EQUAL_INT64(parse_iso_time("2026-11-13T05:00Z"), p[46].time);
  for (int i = 1; i < n; i++) TEST_ASSERT_TRUE(p[i].time > p[i - 1].time);
}

void test_french_text() {
  set_lang(LANG_FR);
  TEST_ASSERT_EQUAL_STRING("Surtout renouvelable", T("Mostly renewable"));
  TEST_ASSERT_EQUAL_STRING("Nucl\xC3\xA9" "aire", source_name(NUCLEAR));
  TEST_ASSERT_EQUAL_STRING("no translation", T("no translation"));
  char b[24];
  format_decimal(20.68f, 1, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("20,7", b);
  format_thousands(58689, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("58 689", b);
  format_header_clock(16 * 60 + 5, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("16:05", b);
  format_clock(22 * 60, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("22h", b);
  set_lang(LANG_EN);
  TEST_ASSERT_EQUAL_STRING("Nuclear", source_name(NUCLEAR));
  format_header_clock(16 * 60 + 5, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("4:05 pm", b);
  format_thousands(58689, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("58,689", b);
}

void test_ci_history() {
  HistoryPoint p[64];
  const int n = parse_carbon_intensity_history(CI_LONDON_HISTORY, strlen(CI_LONDON_HISTORY), p, 64);
  TEST_ASSERT_EQUAL(6, n);
  TEST_ASSERT_EQUAL_INT64(parse_iso_time("2026-11-12T04:30Z"), p[0].time);
  TEST_ASSERT_EQUAL_INT64(parse_iso_time("2026-11-13T04:00Z"), p[5].time);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 60.0f, p[1].renewable_pct);   // midday: solar 18.6 + wind 34.9 + biomass 6.3 + hydro 0.1
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
  RUN_TEST(test_eia_us48_codes);
  RUN_TEST(test_oe_wem);
  RUN_TEST(test_oe_history);
  RUN_TEST(test_oe_history_real);
  RUN_TEST(test_ci_now);
  RUN_TEST(test_ci_history);
  RUN_TEST(test_rte_now);
  RUN_TEST(test_rte_history);
  RUN_TEST(test_french_text);
  RUN_TEST(test_eia_bad_json_fails_cleanly);
  RUN_TEST(test_oe_parses);
  RUN_TEST(test_oe_mix);
  RUN_TEST(test_oe_battery_both_ways);
  RUN_TEST(test_oe_rooftop_carried_forward);
  RUN_TEST(test_oe_failure_response);
  RUN_TEST(test_oe_fueltech_has_no_rooftop);
  RUN_TEST(test_oe_market_forecast);
  RUN_TEST(test_oe_merge_adds_rooftop_solar);
  RUN_TEST(test_oe_merge_keeps_actual_rooftop);
  return UNITY_END();
}
