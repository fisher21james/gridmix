// State shared between the data fetcher, the screens and the setup page.
#pragma once
#include <stdint.h>
#include "settings.h"

struct AppStatus {
  int64_t last_ok_utc = 0;        // when data was last fetched successfully (0 = never)
  int credits_left = -1;          // Open Electricity credits left today (-1 = unknown)
  const char *problem = nullptr;  // short plain-English reason data isn't showing, or nullptr
};

extern gridcore::Settings g_settings;
extern AppStatus g_status;

// Called after the setup page saves new settings.
void on_settings_changed();
