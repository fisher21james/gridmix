// Everything the user sets on the phone setup page, plus conversion to and from JSON.
// Two JSON forms:
//   public  - sent to the setup page. API keys are never included, only "set" and the last 4 characters.
//   storage - saved in the board's flash. Includes the keys.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "tariff.h"

namespace gridcore {

struct Settings {
  char country[3] = "AU";
  char region[6] = "SA1";
  char tz[48] = "ACST-9:30ACDT,M10.1.0,M4.1.0/3";   // POSIX TZ string, applied with setenv("TZ")
  bool view_country = false;                         // start-up view: whole country instead of region

  char key_oe[72] = "";
  char key_eia[72] = "";

  bool tariff_flat = false;
  float flat_cents = 0;
  Tariff tou;                 // time-of-use periods, supply and feed-in live here
  bool warn_before_rise = true;

  uint8_t brightness = 80;    // percent
  bool night_dim = true;
  bool local_lang = false;    // show the screen in the country's own language (French in France) instead of English

  bool has_tariff() const { return tariff_flat ? flat_cents > 0 : tou.count > 0; }
  Tariff active_tariff() const;   // what the rate engine should use (flat becomes one all-day period)
  // Great Britain's Carbon Intensity API and France's RTE data need no key
  bool needs_key() const { return country[0] == 'A' || country[0] == 'U'; }
  const char *key_for_country() const { return country[0] == 'U' ? key_eia : needs_key() ? key_oe : ""; }
  bool ready() const { return !needs_key() || key_for_country()[0]; }   // enough set up to fetch live data
};

// Words and units that depend on the country
struct CountryInfo {
  const char *name;            // "Australia"
  const char *whole_header;    // header in the whole-country view: "Australia (NEM)"
  const char *whole_sub;       // picker: "East coast grid (NEM)"
  const char *local_sub;       // picker: "state grid"
  const char *freq;            // picker: "Every 10 min"
  bool pence;                  // rates in p/£ instead of ¢/$
  char currency;               // '$' (dollars and cents), 'L' (pounds and pence) or 'E' (euros and cents)
  const char *lang;            // the country's own language for the screen: "en", "fr"
  bool imports;                // grid buys power from abroad (draw the transmission line); never in Australia
};
const CountryInfo &country_info(const char *country);

bool region_is_valid(const char *country, const char *region);
const char *region_label(const char *country, const char *region);   // "South Australia", "Texas", ...

// public=true leaves the keys out. Returns bytes written (0 if out was too small).
size_t settings_to_json(const Settings &s, bool public_view, char *out, size_t cap);

// Reads JSON from the setup page (public form) or from storage. Keys are only changed when the
// JSON contains keys.<oe|eia>.value, so saving other settings never wipes a stored key.
// On failure, s is untouched and err says why in plain English.
bool settings_from_json(const char *json, size_t len, Settings &s, char *err, size_t err_len);

}  // namespace gridcore
