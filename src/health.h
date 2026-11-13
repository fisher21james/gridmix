// Keeps the display running for months without anyone touching it:
//   - reconnects Wi-Fi after a router restart, in steps (reconnect, then restart Wi-Fi, then restart the board)
//   - a 60-second watchdog restarts the board if the screen code ever freezes
//   - restarts the board if the data fetcher stops checking in, or memory runs low and stays low
// Each self-restart records why, shown on the setup page's status.
#pragma once
#include <stdint.h>

void health_begin();          // early in setup()
void health_loop();           // every pass of loop()
void health_fetch_alive();    // called by the fetch task each time round its loop
void health_network_up();     // Wi-Fi (re)connected: restart mDNS so gridmix.local works again

struct HealthInfo {
  uint32_t uptime_s;
  const char *last_restart;   // why the board last started ("Power on", "Watchdog", "Wi-Fi lost for 20 min", ...)
  uint32_t heap_free, heap_min, heap_largest;   // internal RAM, bytes
  uint32_t psram_free;
  uint32_t wifi_reconnects;
};
void health_info(HealthInfo &out);
