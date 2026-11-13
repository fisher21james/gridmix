// Runs a saved API answer through the display's own translators. Used by the daily API check
// (.github/workflows/api-check.yml): if a data service changes its format, this fails.
//   check_api ci  answer.json         Great Britain (Carbon Intensity), current half hour
//   check_api cih answer.json         Great Britain, 24 h history
//   check_api oe  answer.json         Open Electricity 5-minute mix
//   check_api oeh answer.json         Open Electricity hourly history
#include <stdio.h>
#include <string.h>
#include <string>
#include <fstream>
#include <sstream>
#include <ctime>
#include "parsers.h"
using namespace gridcore;

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: check_api ci|cih|oe|oeh file\n"); return 2; }
  std::ifstream f(argv[2]);
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string j = ss.str();
  const std::string kind = argv[1];
  const long long now = (long long)time(nullptr);
  if (kind == "ci" || kind == "oe") {
    GridSnapshot s;
    const bool ok = kind == "ci" ? parse_carbon_intensity(j.data(), j.size(), s) : parse_openelectricity(j.data(), j.size(), s);
    if (!ok) { printf("FAIL: couldn't read the %s answer\n", kind.c_str()); return 1; }
    const long long age_min = (now - s.data_time) / 60;
    printf("OK: %.1f%% renewable, data %lld min old\n", s.renewable_pct(), age_min);
    if (age_min > 180) { printf("FAIL: data is more than 3 hours old\n"); return 1; }
    return 0;
  }
  HistoryPoint p[64];
  const int n = kind == "cih" ? parse_carbon_intensity_history(j.data(), j.size(), p, 64)
                              : parse_openelectricity_history(j.data(), j.size(), p, 64);
  printf("%s: %d history points\n", n >= 12 ? "OK" : "FAIL", n);
  return n >= 12 ? 0 : 1;
}
