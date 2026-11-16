#include "nz_grid.h"
#include <ArduinoJson.h>
#include <string.h>

namespace gridcore {

// Grid point (first 3 letters of the substation) -> fuel, from the Electricity Authority's plant list
// (Generation_MD, October 2026). Where one point has plants of different fuels, the unit code decides.
struct Point { const char *site; Source fuel; bool south; };
static const Point POINTS[] = {
  // hydro
  {"ARA", HYDRO, false}, {"ARG", HYDRO, true}, {"ARI", HYDRO, false}, {"ATI", HYDRO, false}, {"AVI", HYDRO, true},
  {"BEN", HYDRO, true}, {"BWK", HYDRO, true}, {"CML", HYDRO, true}, {"COL", HYDRO, true}, {"CYD", HYDRO, true},
  {"HWB", HYDRO, true}, {"KPO", HYDRO, false}, {"MAN", HYDRO, true}, {"MAT", HYDRO, false}, {"MHO", HYDRO, false},
  {"MTI", HYDRO, false}, {"NSY", HYDRO, true}, {"OHA", HYDRO, true}, {"OHB", HYDRO, true}, {"OHC", HYDRO, true},
  {"OHG", HYDRO, false}, {"OHK", HYDRO, false}, {"ROT", HYDRO, false}, {"ROX", HYDRO, true}, {"RPO", HYDRO, false},
  {"STK", HYDRO, true}, {"KUM", HYDRO, true}, {"TGA", HYDRO, false}, {"TKA", HYDRO, true}, {"TKB", HYDRO, true}, {"TKU", HYDRO, false},
  {"TUI", HYDRO, false}, {"WPA", HYDRO, false}, {"WPG", HYDRO, true}, {"WTK", HYDRO, true}, {"WKM", HYDRO, false},
  // geothermal
  {"KAW", GEOTHERMAL, false}, {"NAP", GEOTHERMAL, false}, {"OKI", GEOTHERMAL, false}, {"PPI", GEOTHERMAL, false},
  {"TAB", GEOTHERMAL, false}, {"THI", GEOTHERMAL, false}, {"WRK", GEOTHERMAL, false}, {"KOE", GEOTHERMAL, false},
  // wind
  {"BPE", WIND, false}, {"GOR", WIND, true}, {"HRP", WIND, false}, {"KIW", WIND, true}, {"LTN", WIND, false},
  {"MPE", WIND, false}, {"NMA", WIND, true}, {"TWC", WIND, false}, {"TWH", WIND, false}, {"WDV", WIND, false},
  {"WIL", WIND, false}, {"WVY", WIND, false}, {"WWD", WIND, false},
  // solar
  {"ASB", SOLAR, true}, {"DVK", SOLAR, false}, {"EDG", SOLAR, false}, {"ISL", SOLAR, true}, {"KPU", SOLAR, false},
  {"MTO", SOLAR, false}, {"TMK", SOLAR, true}, {"WAI", SOLAR, false}, {"WHU", SOLAR, false},
  // gas, coal, diesel, wood, batteries
  {"HLY", GAS, false}, {"HWA", GAS, false}, {"JRD", GAS, false}, {"KPA", GAS, false}, {"MKE", GAS, false},
  {"SFD", GAS, false}, {"WHI", OTHER, false}, {"GLN", OTHER, false}, {"KIN", BIOMASS, false}, {"BRB", BATTERY, false},
};

// Points shared by plants of different fuels: unit code (after the space) -> fuel
struct Unit { const char *code; Source fuel; };
static const Unit UNITS[] = {
  {"HLY1", COAL},        // Huntly units 1-4 (Rankine, coal; can also burn gas). HLY6 and the others are gas.
  {"TOH0", BATTERY},     // Rotohiko battery at Huntly (HLY0331)
  {"MOK0", GEOTHERMAL},  // Mokai geothermal, at the same point as Whakamaru hydro (WKM2201)
  {"NGB0", GEOTHERMAL},  // Ngawha geothermal; the other units at Kaikohe (KOE1101) are solar farms
  {"MAH0", WIND},        // Mahinerangi wind, at Halfway Bush with Waipori hydro (HWB0331)
  {"LAU0", SOLAR},       // Lauriston solar; Highbank (hydro) shares ASB0661
  {"HBK0", HYDRO},
};

bool nz_point(const char *code, Source &fuel, bool &south) {
  if (!code || strlen(code) < 3) return false;
  const Point *pt = nullptr;
  for (const Point &p : POINTS)
    if (!strncmp(code, p.site, 3)) { pt = &p; break; }
  if (!pt) { fuel = OTHER; south = false; return false; }
  fuel = pt->fuel;
  south = pt->south;
  const char *unit = strchr(code, ' ');
  if (unit) {
    unit++;
    bool listed = false;
    for (const Unit &u : UNITS)
      if (!strcmp(unit, u.code)) { fuel = u.fuel; listed = true; break; }
    // Kaikohe: every unit other than Ngawha is solar
    if (!listed && !strncmp(code, "KOE", 3)) fuel = SOLAR;
  }
  return true;
}

bool parse_nz(const char *json, size_t len, const char *island, GridSnapshot &out) {
  out = GridSnapshot();
  JsonDocument filter;
  filter[0]["PointOfConnectionCode"] = true;
  filter[0]["RunDateTime"] = true;
  filter[0]["SPDGenerationMegawatt"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
  const bool want_south = island && !strcmp(island, "SI"), want_north = island && !strcmp(island, "NI");
  int64_t run = 0;
  for (JsonObjectConst r : doc.as<JsonArrayConst>()) {
    const float mw = r["SPDGenerationMegawatt"] | 0.0f;
    // RunDateTime is UTC (no zone given), e.g. "2026-11-16T06:24:00"
    char t[24];
    snprintf(t, sizeof t, "%.19sZ", r["RunDateTime"] | "");
    const int64_t rt = parse_iso_time(t);
    if (rt > run) run = rt;
    if (mw <= 0) continue;
    Source fuel;
    bool south;
    nz_point(r["PointOfConnectionCode"] | "", fuel, south);
    if ((want_south && !south) || (want_north && south)) continue;
    out.mw[fuel] += mw;
  }
  // The run just before each 5 minutes dispatches that 5 minutes: show it as the start of the interval
  out.data_time = run > 0 ? (run + 299) / 300 * 300 : 0;
  out.has_rooftop = false;
  out.valid = out.data_time > 0 && out.total() > 0;
  return out.valid;
}

}  // namespace gridcore
