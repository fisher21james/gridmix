#include "parsers.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdlib.h>

namespace gridcore {

static bool starts_with(const char *s, const char *prefix) {
  return s && strncmp(s, prefix, strlen(prefix)) == 0;
}

static float as_float(JsonVariantConst v, bool &is_null) {
  is_null = v.isNull();
  if (is_null) return 0;
  if (v.is<const char *>()) return (float)atof(v.as<const char *>());  // EIA sends numbers as strings
  return v.as<float>();
}

// ---------------------------------------------------------------- EIA

// What a value of this EIA fuel code means for the snapshot.
// Returns false if the row should be ignored.
static void add_eia(GridSnapshot &s, const char *code, float v) {
  if (!code) return;
  // Storage: battery, pumped storage, other/unknown storage. Negative = charging.
  const bool battery = !strcmp(code, "BAT") || !strcmp(code, "OES") || !strcmp(code, "UES");
  if (battery || !strcmp(code, "PS")) {
    if (v < 0) s.battery_charging += -v;
    else s.mw[battery ? BATTERY : HYDRO] += v;
    return;
  }
  if (v < 0) return;  // any other negative (station load) is not generation
  if (!strcmp(code, "SUN") || !strcmp(code, "SNB")) s.mw[SOLAR] += v;   // SNB = solar with a battery on site
  else if (!strcmp(code, "WND") || !strcmp(code, "WNB")) s.mw[WIND] += v;   // WNB = wind with a battery on site
  else if (!strcmp(code, "WAT")) s.mw[HYDRO] += v;
  else if (!strcmp(code, "NUC")) s.mw[NUCLEAR] += v;
  else if (!strcmp(code, "NG")) s.mw[GAS] += v;
  else if (!strcmp(code, "COL")) s.mw[COAL] += v;
  else s.mw[OTHER] += v;  // OIL, OTH, GEO, UNK ...
}

bool parse_eia(const char *json, size_t len, GridSnapshot &out) {
  out = GridSnapshot();
  JsonDocument filter;
  filter["response"]["data"][0]["period"] = true;
  filter["response"]["data"][0]["fueltype"] = true;
  filter["response"]["data"][0]["value"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
  JsonArrayConst rows = doc["response"]["data"];
  if (rows.isNull() || rows.size() == 0) return false;

  // Newest period (ISO strings of the same format sort correctly as text)
  const char *newest = nullptr;
  for (JsonObjectConst r : rows) {
    const char *p = r["period"];
    if (p && (!newest || strcmp(p, newest) > 0)) newest = p;
  }
  if (!newest) return false;

  for (JsonObjectConst r : rows) {
    const char *p = r["period"];
    if (!p || strcmp(p, newest) != 0) continue;
    bool is_null;
    const float v = as_float(r["value"], is_null);
    if (!is_null) add_eia(out, r["fueltype"], v);
  }
  out.data_time = parse_iso_time(newest);   // EIA periods are UTC
  out.valid = out.total() > 0;
  return out.valid;
}

// ---------------------------------------------------------------- Open Electricity

enum OeKind { OE_IGNORE, OE_SOURCE, OE_CHARGING };

static OeKind classify_oe(const char *key, Source &src) {
  if (!key) return OE_IGNORE;
  if (!strcmp(key, "battery_charging")) return OE_CHARGING;
  if (!strcmp(key, "battery_discharging")) { src = BATTERY; return OE_SOURCE; }
  if (!strcmp(key, "battery") || starts_with(key, "pumps")) return OE_IGNORE;  // net battery would double count; pumps are load
  if (starts_with(key, "coal")) src = COAL;
  else if (starts_with(key, "gas")) src = GAS;
  else if (starts_with(key, "solar")) src = SOLAR;
  else if (starts_with(key, "wind")) src = WIND;
  else if (starts_with(key, "hydro")) src = HYDRO;
  else src = OTHER;  // distillate, bioenergy_*, ...
  return OE_SOURCE;
}

bool parse_openelectricity(const char *json, size_t len, GridSnapshot &out) {
  out = GridSnapshot();
  JsonDocument filter;
  filter["success"] = true;
  filter["data"][0]["metric"] = true;
  filter["data"][0]["results"][0]["columns"] = true;
  filter["data"][0]["results"][0]["data"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
  if (!(doc["success"] | false)) return false;

  JsonArrayConst series_sets = doc["data"];
  if (series_sets.isNull()) return false;

  // Pass 1: newest timestamp across all power series
  const char *newest = nullptr;
  for (JsonObjectConst set : series_sets) {
    if (strcmp(set["metric"] | "", "power") != 0) continue;
    for (JsonObjectConst res : set["results"].as<JsonArrayConst>()) {
      JsonArrayConst pts = res["data"];
      if (pts.size() == 0) continue;
      const char *t = pts[pts.size() - 1][0];
      if (t && (!newest || strcmp(t, newest) > 0)) newest = t;
    }
  }
  if (!newest) return false;

  // Pass 2: each series' value at the newest timestamp
  for (JsonObjectConst set : series_sets) {
    if (strcmp(set["metric"] | "", "power") != 0) continue;
    for (JsonObjectConst res : set["results"].as<JsonArrayConst>()) {
      JsonObjectConst cols = res["columns"];
      const char *key = cols["fueltech"].as<const char *>();
      if (!key) key = cols["fueltech_group"].as<const char *>();
      Source src = OTHER;
      const OeKind kind = classify_oe(key, src);
      if (kind == OE_IGNORE) continue;

      JsonArrayConst pts = res["data"];
      if (pts.size() == 0) continue;

      // Value at the newest timestamp, if this series has it
      bool have = false, is_null = true;
      float v = 0;
      JsonArrayConst lastPt = pts[pts.size() - 1];
      if (strcmp(lastPt[0] | "", newest) == 0) {
        v = as_float(lastPt[1], is_null);
        have = !is_null;
      }
      // Rooftop solar lags behind: carry its last known value forward
      if (!have && key && !strcmp(key, "solar_rooftop")) {
        for (int i = (int)pts.size() - 1; i >= 0; i--) {
          v = as_float(pts[i][1], is_null);
          if (!is_null) { have = true; out.estimated = true; break; }
        }
      }
      if (!have) continue;   // anything else missing counts as zero
      if (key && !strcmp(key, "solar_rooftop")) out.has_rooftop = true;

      if (kind == OE_CHARGING) out.battery_charging += v > 0 ? v : -v;
      else if (v > 0) out.mw[src] += v;
    }
  }
  out.data_time = parse_iso_time(newest);
  out.valid = out.total() > 0;
  return out.valid;
}

// ---------------------------------------------------------------- Open Electricity market data

bool parse_openelectricity_market(const char *json, size_t len, int64_t at_time, MarketInfo &out) {
  out = MarketInfo();
  JsonDocument filter;
  filter["success"] = true;
  filter["data"][0]["metric"] = true;
  filter["data"][0]["results"][0]["data"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
  if (!(doc["success"] | false)) return false;

  for (JsonObjectConst set : doc["data"].as<JsonArrayConst>()) {
    const char *metric = set["metric"] | "";
    JsonArrayConst results = set["results"];
    if (results.size() == 0) continue;
    JsonArrayConst pts = results[0]["data"];

    if (!strcmp(metric, "price")) {
      // Newest non-null price
      for (int i = (int)pts.size() - 1; i >= 0; i--) {
        bool is_null;
        const float v = as_float(pts[i][1], is_null);
        if (!is_null) {
          out.has_price = true;
          out.price = v;
          out.price_time = parse_iso_time(pts[i][0] | "");
          break;
        }
      }
    } else if (!strcmp(metric, "solar_rooftop_forecast")) {
      // Forecast value for the interval at (or just before) at_time
      for (JsonArrayConst pt : pts) {
        const int64_t t = parse_iso_time(pt[0] | "");
        if (t < 0 || t > at_time) continue;
        bool is_null;
        const float v = as_float(pt[1], is_null);
        if (!is_null) { out.has_rooftop_forecast = true; out.rooftop_forecast = v; }
      }
    }
  }
  return out.has_price || out.has_rooftop_forecast;
}

void merge_market(GridSnapshot &snap, const MarketInfo &m) {
  if (m.has_price) {
    snap.has_price = true;
    snap.price = m.price;
    snap.price_time = m.price_time;
  }
  if (!snap.has_rooftop && m.has_rooftop_forecast && m.rooftop_forecast > 0) {
    snap.mw[SOLAR] += m.rooftop_forecast;
    snap.has_rooftop = true;
    snap.estimated = true;
  }
}

}  // namespace gridcore
