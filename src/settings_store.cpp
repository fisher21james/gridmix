#include "settings_store.h"
#include <Preferences.h>

using gridcore::Settings;

static const char *NS = "gridmix";
static const char *KEY = "settings";

bool settings_load(Settings &s) {
  Preferences p;
  if (!p.begin(NS, true)) return false;
  String json = p.getString(KEY, "");
  p.end();
  if (!json.length()) return false;
  char err[64];
  return gridcore::settings_from_json(json.c_str(), json.length(), s, err, sizeof err);
}

bool settings_save(const Settings &s) {
  static char buf[3072];
  const size_t n = gridcore::settings_to_json(s, false, buf, sizeof buf);
  if (!n) return false;
  Preferences p;
  if (!p.begin(NS, false)) return false;
  const bool ok = p.putString(KEY, buf) == n;
  p.end();
  return ok;
}
