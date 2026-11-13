// Over-the-air updates from the project's GitHub Pages site.
//   firmware/ota.json  {"version":"0.1.0","build":57,"url":".../gridmix-app.bin","size":1536558,"sha256":"..."}
// The board checks once a day (and when asked from the setup page); installing is the user's choice.
// The download is checked against the SHA-256 before the board switches to it, and the old
// version stays in the other app slot until the new one is fully written.
#pragma once
#include <stdint.h>

enum OtaState : uint8_t { OTA_IDLE = 0, OTA_CHECKING, OTA_UP_TO_DATE, OTA_AVAILABLE, OTA_DOWNLOADING, OTA_DONE, OTA_FAILED };

struct OtaStatus {
  OtaState state = OTA_IDLE;
  char latest[24] = "";     // "0.1.0 (build 57)"
  uint32_t latest_build = 0;
  int percent = 0;
  char message[96] = "";
  int64_t checked_utc = 0;
};

void ota_begin();                // starts the background task
void ota_request_check();
bool ota_request_install();      // false if there's nothing to install
void ota_status(OtaStatus &out);
bool ota_busy();                 // downloading: the data fetcher pauses to leave memory for it
