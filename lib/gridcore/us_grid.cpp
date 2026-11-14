#include "us_grid.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace gridcore {

static const char *CODES[US_GRID_COUNT] = {"CAL", "TEX", "MIDW", "CENT", "NY", "NE", "NW"};

UsGrid us_grid(const char *region) {
  for (int i = 0; i < US_GRID_COUNT; i++)
    if (region && !strcmp(region, CODES[i])) return (UsGrid)i;
  return US_GRID_COUNT;
}

// ---------------------------------------------------------------- time

static int64_t civil_to_utc(int y, int mo, int d, int h, int mi, int s) {
  return days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}
// Day of the month of the n-th Sunday (n >= 1) of a month
static int nth_sunday(int y, int mo, int n) {
  const int64_t first = days_from_civil(y, mo, 1);
  const int wd = (int)(((first % 7) + 7 + 4) % 7);   // 1970-01-01 was a Thursday (4); 0 = Sunday
  return 1 + (7 - wd) % 7 + 7 * (n - 1);
}
// Is local standard time (y-mo-d h:00, in standard time) inside US daylight saving?
static bool us_dst_std(int y, int mo, int d, int h) {
  if (mo < 3 || mo > 11) return false;
  if (mo > 3 && mo < 11) return true;
  if (mo == 3) { const int s = nth_sunday(y, 3, 2); return d > s || (d == s && h >= 2); }
  const int e = nth_sunday(y, 11, 1);
  return d < e || (d == e && h < 1);   // ends at 2am daylight time = 1am standard time
}
int64_t us_local_to_utc(int y, int mo, int d, int h, int mi, int std_offset_h) {
  const bool dst = us_dst_std(y, mo, d, h);   // close enough for the hour around the change
  return civil_to_utc(y, mo, d, h, mi, 0) - (std_offset_h + (dst ? 1 : 0)) * 3600;
}
// Local date of a UTC moment in a US zone
static void us_local_date(int64_t utc, int std_offset_h, int &y, int &mo, int &d, int &minute_of_day) {
  for (int pass = 0; pass < 2; pass++) {
    int64_t t = utc + std_offset_h * 3600 + (pass ? 3600 : 0);
    int64_t days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
    // civil_from_days (Howard Hinnant)
    const int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    mo = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yoe + era * 400 + (mo <= 2));
    minute_of_day = (int)((t - days * 86400) / 60);
    if (pass == 0 && !us_dst_std(y, mo, d, minute_of_day / 60)) return;
  }
}

// ---------------------------------------------------------------- requests

size_t us_url(const char *region, int64_t now_utc, char *out, size_t cap) {
  int n = -1;
  switch (us_grid(region)) {
    case US_CAL: n = snprintf(out, cap, "https://www.caiso.com/outlook/current/fuelsource.csv"); break;
    case US_TEX: n = snprintf(out, cap, "https://www.ercot.com/api/1/services/read/dashboards/fuel-mix.json"); break;
    case US_MIDW: n = snprintf(out, cap, "https://public-api.misoenergy.org/api/FuelMix"); break;
    case US_CENT:
      n = snprintf(out, cap, "https://portal.spp.org/file-browser-api/download/generation-mix-historical?path=/SPP/GenMix2Hour_SPP.csv");
      break;
    case US_NY: {
      int y, mo, d, md;
      us_local_date(now_utc, -5, y, mo, d, md);
      n = snprintf(out, cap, "https://mis.nyiso.com/public/csv/rtfuelmix/%04d%02d%02drtfuelmix.csv", y, mo, d);
      break;
    }
    case US_NE: n = snprintf(out, cap, "https://www.iso-ne.com/ws/wsclient"); break;
    case US_NW: n = snprintf(out, cap, "https://transmission.bpa.gov/business/operations/wind/baltwg.txt"); break;
    default: break;
  }
  return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

const char *us_post_body(const char *region) {
  return us_grid(region) == US_NE ? "_nstmp_requestType=fuelmix&_nstmp_fuelType=all" : nullptr;
}

size_t us_history_url(const char *region, int64_t, char *out, size_t cap) {
  if (us_grid(region) != US_MIDW) return 0;
  const int n = snprintf(out, cap, "https://public-api.misoenergy.org/api/FuelMix/Today");
  return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

// ---------------------------------------------------------------- helpers

static void finish(GridSnapshot &s, int64_t t) {
  for (float &v : s.mw) if (v < 0) v = 0;
  s.data_time = t;
  s.has_rooftop = false;
  s.valid = t > 0 && s.total() > 0;
}
static void add_battery(GridSnapshot &s, float net) {   // positive = discharging into the grid
  if (net >= 0) s.mw[BATTERY] += net;
  else s.battery_charging += -net;
}

// Splits one CSV / tab line in place. Returns the field count.
static int split(char *line, char sep, char **f, int max) {
  int n = 0;
  char *p = line;
  while (n < max) {
    f[n++] = p;
    char *q = strchr(p, sep);
    if (!q) break;
    *q = 0;
    p = q + 1;
  }
  for (int i = 0; i < n; i++) {   // trim spaces and \r
    while (*f[i] == ' ') f[i]++;
    size_t l = strlen(f[i]);
    while (l && (f[i][l - 1] == '\r' || f[i][l - 1] == ' ')) f[i][--l] = 0;
  }
  return n;
}

// Walks the lines of a text body. Copies each into `buf`, so the body is never changed.
struct Lines {
  const char *p, *end;
  char buf[512];
  Lines(const char *b, size_t len) : p(b), end(b + len) {}
  char *next() {
    while (p < end) {
      const char *nl = (const char *)memchr(p, '\n', end - p);
      const char *e = nl ? nl : end;
      size_t l = e - p;
      if (l >= sizeof buf) l = sizeof buf - 1;
      memcpy(buf, p, l);
      buf[l] = 0;
      p = nl ? nl + 1 : end;
      if (l && !(l == 1 && buf[0] == '\r')) return buf;
    }
    return nullptr;
  }
};

// Keeps the newest `max` half-hour points
struct HalfHours {
  HistoryPoint *out;
  int max, n = 0;
  HalfHours(HistoryPoint *o, int m) : out(o), max(m) {}
  void add(const GridSnapshot &s) {
    if (!s.valid || s.data_time % 1800) return;
    if (n && out[n - 1].time >= s.data_time) return;   // oldest first, no repeats
    if (n == max) { memmove(out, out + 1, sizeof(HistoryPoint) * (max - 1)); n--; }
    out[n++] = HistoryPoint{s.data_time, s.renewable_pct()};
  }
};

// ---------------------------------------------------------------- California (CAISO)
// Time,Solar,Wind,Geothermal,Biomass,Biogas,Small hydro,Coal,Nuclear,Natural Gas,Large Hydro,Batteries,Imports,Other
// 16:20,903,6091,696,166,158,107,3,2204,9168,1653,2607,1323,0       (start of the 5 minutes, Pacific time, today)

template <typename F>
static void caiso_rows(const char *body, size_t len, int64_t now_utc, F &&each) {
  int y, mo, d, md;
  us_local_date(now_utc, -8, y, mo, d, md);
  Lines lines(body, len);
  char *f[24];
  int col[16];
  for (int &c : col) c = -1;
  static const char *NAMES[] = {"Solar", "Wind", "Geothermal", "Biomass", "Biogas", "Small hydro", "Coal", "Nuclear",
                                "Natural Gas", "Large Hydro", "Batteries", "Imports", "Other"};
  const int NN = sizeof NAMES / sizeof NAMES[0];
  bool header = false;
  while (char *l = lines.next()) {
    const int n = split(l, ',', f, 24);
    if (!header) {
      if (strcmp(f[0], "Time")) continue;
      for (int i = 1; i < n; i++)
        for (int k = 0; k < NN; k++) if (!strcmp(f[i], NAMES[k])) col[k] = i;
      header = true;
      continue;
    }
    int hh, mm;
    if (sscanf(f[0], "%d:%d", &hh, &mm) != 2) continue;
    bool blank = true;
    for (int i = 1; i < n; i++) if (*f[i]) blank = false;
    if (blank) continue;
    // A row later than now belongs to yesterday (just after midnight, before the file rolls over)
    int64_t t = us_local_to_utc(y, mo, d, hh, mm, -8);
    if (t > now_utc + 1800) t -= 86400;
    auto v = [&](int k) { return col[k] >= 0 && col[k] < n ? (float)atof(f[col[k]]) : 0.0f; };
    GridSnapshot s;
    s.mw[SOLAR] = v(0);
    s.mw[WIND] = v(1);
    s.mw[GEOTHERMAL] = v(2);
    s.mw[BIOMASS] = v(3) + v(4);
    s.mw[HYDRO] = v(5) + v(9);
    s.mw[COAL] = v(6);
    s.mw[NUCLEAR] = v(7);
    s.mw[GAS] = v(8);
    add_battery(s, v(10));
    s.mw[IMPORTS] = v(11);   // from other states and Mexico
    s.mw[OTHER] = v(12);
    finish(s, t);
    each(s);
  }
}

// ---------------------------------------------------------------- Texas (ERCOT)
// {"lastUpdated":...,"data":{"2026-11-13":{"2026-11-13 18:20:00-0600":{"Coal and Lignite":{"gen":7240.8},...},...}}}
// About 200 KB, so it's read one five-minute object at a time instead of all at once.

template <typename F>
static void ercot_rows(const char *body, size_t len, F &&each) {
  const char *p = body, *end = body + len;
  static const char KEY[] = "\":{\"";
  while (p < end) {
    const char *k = strstr(p, KEY);
    if (!k || k >= end) break;
    // the key before ':{"' must look like "2026-11-13 18:20:00-0600"
    const char *q = k - 1;
    while (q > body && *q != '"') q--;
    const size_t klen = k - q - 1;
    if (klen < 19 || klen > 30 || q[11] != ' ' || q[5] != '-') { p = k + 3; continue; }
    char ts[32];
    memcpy(ts, q + 1, klen);
    ts[klen] = 0;
    // the object runs to its matching brace
    const char *o = k + 2, *e = o;
    int depth = 0;
    for (; e < end; e++) {
      if (*e == '{') depth++;
      else if (*e == '}' && --depth == 0) { e++; break; }
    }
    if (depth) break;
    JsonDocument doc;
    if (!deserializeJson(doc, o, e - o)) {
      auto g = [&](const char *name) { return doc[name]["gen"] | 0.0f; };
      GridSnapshot s;
      s.mw[COAL] = g("Coal and Lignite");
      s.mw[HYDRO] = g("Hydro");
      s.mw[NUCLEAR] = g("Nuclear");
      s.mw[OTHER] = g("Other");
      s.mw[SOLAR] = g("Solar");
      s.mw[WIND] = g("Wind");
      s.mw[GAS] = g("Natural Gas");
      if (doc["Power Storage Discharging"].isNull()) add_battery(s, g("Power Storage"));
      else {
        s.mw[BATTERY] = g("Power Storage Discharging");
        s.battery_charging = -g("Power Storage Charging");
      }
      finish(s, parse_iso_time(ts));
      each(s);
    }
    p = e;
  }
}

// ---------------------------------------------------------------- Midwest (MISO)
// {"RefId":...,"Fuel":{"Type":[{"INTERVALEST":"2026-11-13 7:20:00 PM","CATEGORY":"Coal","ACT":"24004",...},...]}}
// MISO uses Eastern Standard Time all year. Hydro and biomass are inside "Other".

static int64_t miso_time(const char *s) {
  int y, mo, d, h, mi, se;
  char ap[3] = "";
  if (sscanf(s, "%d-%d-%d %d:%d:%d %2s", &y, &mo, &d, &h, &mi, &se, ap) < 6) return -1;
  if (ap[0] == 'P' && h < 12) h += 12;
  if (ap[0] == 'A' && h == 12) h = 0;
  return civil_to_utc(y, mo, d, h, mi, se) + 5 * 3600;
}
static void miso_add(GridSnapshot &s, const char *cat, float v) {
  if (!strcmp(cat, "Coal")) s.mw[COAL] += v;
  else if (!strcmp(cat, "Natural Gas")) s.mw[GAS] += v;
  else if (!strcmp(cat, "Nuclear")) s.mw[NUCLEAR] += v;
  else if (!strcmp(cat, "Wind")) s.mw[WIND] += v;
  else if (!strcmp(cat, "Solar")) s.mw[SOLAR] += v;
  else if (!strcmp(cat, "Battery Storage")) add_battery(s, v);
  else if (!strcmp(cat, "Imports")) s.mw[IMPORTS] += v > 0 ? v : 0;
  else s.mw[OTHER] += v;
}

template <typename F>
static void miso_rows(const char *body, size_t len, F &&each) {
  JsonDocument filter;
  filter["Fuel"]["Type"][0]["INTERVALEST"] = true;
  filter["Fuel"]["Type"][0]["CATEGORY"] = true;
  filter["Fuel"]["Type"][0]["ACT"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, len, DeserializationOption::Filter(filter))) return;
  GridSnapshot s;
  int64_t cur = -1;
  for (JsonObjectConst r : doc["Fuel"]["Type"].as<JsonArrayConst>()) {
    const int64_t t = miso_time(r["INTERVALEST"] | "");
    if (t != cur) {
      if (cur > 0) { finish(s, cur); each(s); }
      s = GridSnapshot();
      cur = t;
    }
    miso_add(s, r["CATEGORY"] | "", (float)atof(r["ACT"] | "0"));
  }
  if (cur > 0) { finish(s, cur); each(s); }
}

// ---------------------------------------------------------------- Central (SPP)
// GMT MKT Interval,BAA,Coal Market,Coal Self,...,Energy Storage Market,Energy Storage Self,Other Market,Other Self,Load
// Each fuel is split into "Market" and "Self" (self-scheduled); they're added together.

template <typename F>
static void spp_rows(const char *body, size_t len, F &&each) {
  Lines lines(body, len);
  char *f[40];
  Source map[40];
  bool storage[40] = {false}, used[40] = {false};
  bool header = false;
  while (char *l = lines.next()) {
    const int n = split(l, ',', f, 40);
    if (!header) {
      if (strncmp(f[0], "GMT", 3)) continue;
      for (int i = 2; i < n; i++) {
        used[i] = true;
        const char *c = f[i];
        if (!strncmp(c, "Coal", 4)) map[i] = COAL;
        else if (!strncmp(c, "Hydro", 5)) map[i] = HYDRO;
        else if (!strncmp(c, "Natural Gas", 11)) map[i] = GAS;
        else if (!strncmp(c, "Nuclear", 7)) map[i] = NUCLEAR;
        else if (!strncmp(c, "Solar", 5)) map[i] = SOLAR;
        else if (!strncmp(c, "Wind", 4)) map[i] = WIND;
        else if (!strncmp(c, "Waste Disposal", 14)) map[i] = BIOMASS;   // landfill gas and waste-to-energy
        else if (!strncmp(c, "Energy Storage", 14)) { map[i] = BATTERY; storage[i] = true; }
        else if (!strcmp(c, "Load") || strstr(c, "Load")) used[i] = false;
        else map[i] = OTHER;                                            // oil, waste heat, other
      }
      header = true;
      continue;
    }
    GridSnapshot s;
    float batt = 0;
    for (int i = 2; i < n; i++) {
      if (!used[i]) continue;
      const float v = (float)atof(f[i]);
      if (storage[i]) batt += v;
      else s.mw[map[i]] += v;
    }
    add_battery(s, batt);
    finish(s, parse_iso_time(f[0]));
    each(s);
  }
}

// ---------------------------------------------------------------- New York (NYISO)
// Time Stamp,Time Zone,Fuel Category,Gen MW
// 11/13/2026 19:10:00,EST,Dual Fuel,6039.0           (one row per fuel; 7 rows per 5 minutes)
// "Dual Fuel" plants burn gas or oil (nearly always gas). "Other Renewables" is mostly biomass and landfill gas.

template <typename F>
static void nyiso_rows(const char *body, size_t len, F &&each) {
  Lines lines(body, len);
  char *f[8];
  GridSnapshot s;
  int64_t cur = -1;
  int fuels = 0;
  while (char *l = lines.next()) {
    if (split(l, ',', f, 8) < 4) continue;
    int mo, d, y, h, mi, se;
    if (sscanf(f[0], "%d/%d/%d %d:%d:%d", &mo, &d, &y, &h, &mi, &se) != 6) continue;
    const int off = !strcmp(f[1], "EDT") ? 4 : 5;
    const int64_t t = civil_to_utc(y, mo, d, h, mi, se) + off * 3600;
    if (t != cur) {
      if (cur > 0 && fuels >= 5) { finish(s, cur); each(s); }
      s = GridSnapshot();
      cur = t;
      fuels = 0;
    }
    const float v = (float)atof(f[3]);
    const char *c = f[2];
    fuels++;
    if (!strcmp(c, "Dual Fuel") || !strcmp(c, "Natural Gas")) s.mw[GAS] += v;
    else if (!strcmp(c, "Nuclear")) s.mw[NUCLEAR] += v;
    else if (!strcmp(c, "Wind")) s.mw[WIND] += v;
    else if (!strcmp(c, "Hydro")) s.mw[HYDRO] += v;
    else if (!strcmp(c, "Other Renewables")) s.mw[BIOMASS] += v;
    else s.mw[OTHER] += v;   // Other Fossil Fuels
  }
  if (cur > 0 && fuels >= 5) { finish(s, cur); each(s); }
}

// ---------------------------------------------------------------- New England (ISO-NE)
// [{"data":{"GenFuelMixes":{"GenFuelMix":[{"GenMw":947,"FuelCategory":"Hydro","BeginDate":"2026-11-13T19:22:24.000-05:00"},...]}}}]

static bool isone_parse(const char *body, size_t len, GridSnapshot &out) {
  JsonDocument doc;
  if (deserializeJson(doc, body, len)) return false;
  GridSnapshot s;
  int64_t t = -1;
  for (JsonObjectConst r : doc[0]["data"]["GenFuelMixes"]["GenFuelMix"].as<JsonArrayConst>()) {
    const char *c = r["FuelCategory"] | "";
    const float v = r["GenMw"] | 0.0f;
    const int64_t bt = parse_iso_time(r["BeginDate"] | "");
    if (bt > t) t = bt;
    if (!strcmp(c, "Batteries")) add_battery(s, v);
    else if (!strcmp(c, "Hydro")) s.mw[HYDRO] += v;
    else if (!strcmp(c, "Natural Gas")) s.mw[GAS] += v;
    else if (!strcmp(c, "Nuclear")) s.mw[NUCLEAR] += v;
    else if (!strcmp(c, "Coal")) s.mw[COAL] += v;
    else if (!strcmp(c, "Solar")) s.mw[SOLAR] += v;
    else if (!strcmp(c, "Wind")) s.mw[WIND] += v;
    else if (!strcmp(c, "Wood") || !strcmp(c, "Refuse") || !strcmp(c, "Landfill Gas")) s.mw[BIOMASS] += v;
    else if (!strcmp(c, "Net Imports")) s.mw[IMPORTS] += v > 0 ? v : 0;
    else s.mw[OTHER] += v;   // oil, other
  }
  finish(s, t);
  out = s;
  return s.valid;
}

// ---------------------------------------------------------------- Northwest (BPA)
// Date/Time\tLoad\tVER\tHydro\tFossil/Biomass\tNuclear     (Pacific time; future rows are blank)
// 11/13/2026 16:25\t7669\t1589\t7457\t1035\t1145
// VER is wind (plus a little solar). Fossil/Biomass is mostly gas plants, so it's shown as gas.

template <typename F>
static void bpa_rows(const char *body, size_t len, F &&each) {
  Lines lines(body, len);
  char *f[10];
  int c_ver = -1, c_hydro = -1, c_thermal = -1, c_nuc = -1;
  bool header = false;
  while (char *l = lines.next()) {
    const int n = split(l, '\t', f, 10);
    if (!header) {
      if (strncmp(f[0], "Date/Time", 9)) continue;
      for (int i = 1; i < n; i++) {
        if (!strcmp(f[i], "VER") || !strncmp(f[i], "Wind", 4)) c_ver = i;
        else if (!strcmp(f[i], "Hydro")) c_hydro = i;
        else if (!strncmp(f[i], "Fossil", 6) || !strncmp(f[i], "Thermal", 7)) c_thermal = i;
        else if (!strcmp(f[i], "Nuclear")) c_nuc = i;
      }
      header = true;
      continue;
    }
    if (n < 3 || !*f[1] || c_hydro < 0) continue;
    int mo, d, y, h, mi;
    if (sscanf(f[0], "%d/%d/%d %d:%d", &mo, &d, &y, &h, &mi) != 5) continue;
    auto v = [&](int c) { return c > 0 && c < n ? (float)atof(f[c]) : 0.0f; };
    GridSnapshot s;
    s.mw[WIND] = v(c_ver);
    s.mw[HYDRO] = v(c_hydro);
    s.mw[GAS] = v(c_thermal);
    s.mw[NUCLEAR] = v(c_nuc);
    finish(s, us_local_to_utc(y, mo, d, h, mi, -8));
    each(s);
  }
}

// ---------------------------------------------------------------- dispatch

template <typename F>
static bool rows(UsGrid g, const char *body, size_t len, int64_t now_utc, F &&each) {
  switch (g) {
    case US_CAL: caiso_rows(body, len, now_utc, each); return true;
    case US_TEX: ercot_rows(body, len, each); return true;
    case US_MIDW: miso_rows(body, len, each); return true;
    case US_CENT: spp_rows(body, len, each); return true;
    case US_NY: nyiso_rows(body, len, each); return true;
    case US_NW: bpa_rows(body, len, each); return true;
    case US_NE: {
      GridSnapshot s;
      if (isone_parse(body, len, s)) each(s);
      return true;
    }
    default: return false;
  }
}

bool parse_us(const char *region, const char *body, size_t len, int64_t now_utc, GridSnapshot &out) {
  out = GridSnapshot();
  bool ok = false;
  rows(us_grid(region), body, len, now_utc, [&](const GridSnapshot &s) {
    if (s.valid && (!ok || s.data_time >= out.data_time)) { out = s; ok = true; }
  });
  return ok;
}

int parse_us_history(const char *region, const char *body, size_t len, int64_t now_utc, HistoryPoint *out, int max) {
  HalfHours h(out, max);
  rows(us_grid(region), body, len, now_utc, [&](const GridSnapshot &s) {
    if (s.data_time > now_utc - 25 * 3600) h.add(s);
  });
  return h.n;
}

}  // namespace gridcore
