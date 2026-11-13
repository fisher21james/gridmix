#include "settings.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

namespace gridcore {

struct RegionInfo { const char *country, *code, *label; };
static const RegionInfo REGIONS[] = {
  {"AU", "SA1", "South Australia"}, {"AU", "NSW1", "New South Wales"}, {"AU", "VIC1", "Victoria"},
  {"AU", "QLD1", "Queensland"}, {"AU", "TAS1", "Tasmania"}, {"AU", "WEM", "Western Australia"},
  {"US", "CAL", "California"}, {"US", "CAR", "Carolinas"}, {"US", "CENT", "Central"}, {"US", "FLA", "Florida"},
  {"US", "MIDA", "Mid-Atlantic"}, {"US", "MIDW", "Midwest"}, {"US", "NE", "New England"}, {"US", "NY", "New York"},
  {"US", "NW", "Northwest"}, {"US", "SE", "Southeast"}, {"US", "SW", "Southwest"}, {"US", "TEN", "Tennessee"},
  {"US", "TEX", "Texas"},
  // Great Britain: Carbon Intensity API region ids (DNO areas, then nations; 18 = all of GB)
  {"GB", "1", "North Scotland"}, {"GB", "2", "South Scotland"}, {"GB", "3", "North West England"},
  {"GB", "4", "North East England"}, {"GB", "5", "Yorkshire"}, {"GB", "6", "North Wales & Merseyside"},
  {"GB", "7", "South Wales"}, {"GB", "8", "West Midlands"}, {"GB", "9", "East Midlands"},
  {"GB", "10", "East England"}, {"GB", "11", "South West England"}, {"GB", "12", "South England"},
  {"GB", "13", "London"}, {"GB", "14", "South East England"}, {"GB", "15", "England"},
  {"GB", "16", "Scotland"}, {"GB", "17", "Wales"}, {"GB", "18", "Great Britain"},
  // France: national figures only (RTE's regional data runs about 45 minutes behind)
  {"FR", "FR", "France"},
};

const CountryInfo &country_info(const char *country) {
  static const CountryInfo AU = {"Australia", "Australia (NEM)", "East coast grid (NEM)", "state grid", "Every 10 min", false, '$', "en", false};
  static const CountryInfo US = {"United States", "United States", "Lower 48 states (EIA)", "EIA", "Hourly", false, '$', "en", true};
  static const CountryInfo GB = {"Great Britain", "Great Britain", "England, Scotland & Wales", "local area", "Every 30 min", true, 'L', "en", true};
  static const CountryInfo FR = {"France", "France", "Whole country", "national grid", "Every 15 min", false, 'E', "fr", true};
  if (country && country[0] == 'G') return GB;
  if (country && country[0] == 'F') return FR;
  if (country && country[0] == 'U') return US;
  return AU;
}

bool region_is_valid(const char *country, const char *region) {
  return region_label(country, region)[0] != 0;
}

const char *region_label(const char *country, const char *region) {
  if (!country || !region) return "";
  for (const RegionInfo &r : REGIONS)
    if (!strcmp(r.country, country) && !strcmp(r.code, region)) return r.label;
  return "";
}

Tariff Settings::active_tariff() const {
  if (!tariff_flat) return tou;
  Tariff t;
  t.add("Flat", 0, 0, flat_cents);
  t.supply_cents_per_day = tou.supply_cents_per_day;
  t.feed_in_cents = tou.feed_in_cents;
  return t;
}

static const char *days_name(uint8_t d) {
  if (d == 0x3E) return "weekdays";
  if (d == 0x41) return "weekends";
  return "all";
}
static bool days_from_name(const char *s, uint8_t &out) {
  if (!s || !strcmp(s, "all")) { out = EVERY_DAY; return true; }
  if (!strcmp(s, "weekdays")) { out = 0x3E; return true; }
  if (!strcmp(s, "weekends")) { out = 0x41; return true; }
  return false;
}
static void fmt_hhmm(unsigned m, char *buf, size_t cap) { snprintf(buf, cap, "%02u:%02u", (m / 60u) % 24u, m % 60u); }

static void key_json(JsonObject o, const char *key, bool public_view) {
  const size_t n = strlen(key);
  o["set"] = n > 0;
  o["hint"] = n >= 4 ? key + n - 4 : "";
  if (!public_view && n) o["value"] = key;
}

size_t settings_to_json(const Settings &s, bool public_view, char *out, size_t cap) {
  JsonDocument doc;
  doc["country"] = s.country;
  doc["region"] = s.region;
  doc["tz"] = s.tz;
  doc["view"] = s.view_country ? "country" : "local";
  key_json(doc["keys"]["oe"].to<JsonObject>(), s.key_oe, public_view);
  key_json(doc["keys"]["eia"].to<JsonObject>(), s.key_eia, public_view);

  JsonObject t = doc["tariff"].to<JsonObject>();
  t["type"] = s.tariff_flat ? "flat" : "tou";
  t["flat"] = s.flat_cents;
  JsonArray ps = t["periods"].to<JsonArray>();
  for (int i = 0; i < s.tou.count; i++) {
    const TariffPeriod &p = s.tou.periods[i];
    char from[8], to[8];
    fmt_hhmm(p.start_min, from, sizeof from);
    fmt_hhmm(p.end_min, to, sizeof to);
    JsonObject o = ps.add<JsonObject>();
    o["name"] = p.name;
    o["from"] = from;
    o["to"] = to;
    o["rate"] = p.cents;
    o["days"] = days_name(p.days);
  }
  t["supply"] = s.tou.supply_cents_per_day;
  t["feed_in"] = s.tou.feed_in_cents;
  t["warn"] = s.warn_before_rise;

  doc["display"]["brightness"] = s.brightness;
  doc["display"]["night_dim"] = s.night_dim;
  doc["display"]["language"] = s.local_lang ? "local" : "en";

  const size_t need = measureJson(doc);
  if (need + 1 > cap) return 0;
  return serializeJson(doc, out, cap);
}

static bool fail(char *err, size_t len, const char *msg) {
  if (err && len) snprintf(err, len, "%s", msg);
  return false;
}

static bool copy_str(const char *src, char *dst, size_t cap) {
  if (!src) return false;
  if (strlen(src) >= cap) return false;
  strcpy(dst, src);
  return true;
}

static bool plausible_key(const char *k) {
  for (const char *p = k; *p; p++)
    if (*p <= ' ' || *p == '"' || *p == '\\' || *p > '~') return false;
  return true;
}

bool settings_from_json(const char *json, size_t len, Settings &out, char *err, size_t err_len) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return fail(err, err_len, "Settings weren't valid JSON.");

  Settings s = out;   // work on a copy so a bad save changes nothing

  if (!copy_str(doc["country"] | s.country, s.country, sizeof s.country) || (strcmp(s.country, "AU") && strcmp(s.country, "US") && strcmp(s.country, "GB") && strcmp(s.country, "FR")))
    return fail(err, err_len, "Unknown country.");
  if (!copy_str(doc["region"] | s.region, s.region, sizeof s.region) || !region_is_valid(s.country, s.region))
    return fail(err, err_len, "That region isn't in the country chosen.");
  if (!copy_str(doc["tz"] | s.tz, s.tz, sizeof s.tz) || !s.tz[0])
    return fail(err, err_len, "Unknown time zone.");
  s.view_country = !strcmp(doc["view"] | "local", "country");

  const char *k = doc["keys"]["oe"]["value"];
  if (k) {
    if (!copy_str(k, s.key_oe, sizeof s.key_oe) || !plausible_key(k)) return fail(err, err_len, "That Open Electricity key doesn't look right.");
  }
  k = doc["keys"]["eia"]["value"];
  if (k) {
    if (!copy_str(k, s.key_eia, sizeof s.key_eia) || !plausible_key(k)) return fail(err, err_len, "That EIA key doesn't look right.");
  }

  JsonObjectConst t = doc["tariff"];
  if (!t.isNull()) {
    s.tariff_flat = !strcmp(t["type"] | "tou", "flat");
    s.flat_cents = t["flat"] | 0.0f;
    s.warn_before_rise = t["warn"] | true;
    Tariff tou;
    tou.supply_cents_per_day = t["supply"] | 0.0f;
    tou.feed_in_cents = t["feed_in"] | 0.0f;
    JsonArrayConst ps = t["periods"];
    if (ps.size() > MAX_TARIFF_PERIODS) return fail(err, err_len, "Too many periods (the most is 8).");
    for (JsonObjectConst p : ps) {
      const int from = parse_hhmm(p["from"] | "");
      const int to = parse_hhmm(p["to"] | "");
      uint8_t days;
      if (from < 0 || from >= 1440 || to < 0) return fail(err, err_len, "A period has a time that isn't HH:MM.");
      if (!days_from_name(p["days"] | "all", days)) return fail(err, err_len, "A period has unknown days.");
      if (p["rate"].isNull()) return fail(err, err_len, "A period is missing its rate.");
      tou.add(p["name"] | "", from, to, p["rate"].as<float>(), days);
    }
    if (!s.tariff_flat && tou.count > 0) {
      char why[40];
      if (!tariff_validate(tou, why, sizeof why)) {
        if (err && err_len) snprintf(err, err_len, "Power plan: %s.", why);
        return false;
      }
    }
    s.tou = tou;
  }

  JsonObjectConst d = doc["display"];
  if (!d.isNull()) {
    int b = d["brightness"] | (int)s.brightness;
    s.brightness = (uint8_t)(b < 10 ? 10 : b > 100 ? 100 : b);
    s.night_dim = d["night_dim"] | s.night_dim;
    const char *lang = d["language"] | (s.local_lang ? "local" : "en");
    s.local_lang = !strcmp(lang, "local");
  }

  out = s;
  if (err && err_len) err[0] = 0;
  return true;
}

}  // namespace gridcore
