#include "health.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <string.h>

// Survives a software restart (not a power cut): why we restarted ourselves
#define RESTART_MAGIC 0x52535452u   // "RSTR"
RTC_NOINIT_ATTR static uint32_t restart_magic;
RTC_NOINIT_ATTR static char restart_why[40];

static char last_restart[48] = "Power on";
static volatile uint32_t fetch_alive_ms = 0;
static uint32_t wifi_lost_ms = 0;     // when Wi-Fi dropped (0 = connected or never set up)
static int wifi_step = 0;
static uint32_t wifi_reconnects = 0;
static bool was_connected = false;
static uint32_t low_mem_since = 0;
static uint32_t heap_min = UINT32_MAX;

static void restart(const char *why) {
  Serial.printf("[health] restarting: %s\n", why);
  restart_magic = RESTART_MAGIC;
  strncpy(restart_why, why, sizeof restart_why - 1);
  restart_why[sizeof restart_why - 1] = 0;
  delay(200);
  ESP.restart();
}

static bool has_saved_wifi() {
  wifi_config_t conf;
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) return false;
  return conf.sta.ssid[0] != 0;
}

void health_begin() {
  // Why did we start?
  const esp_reset_reason_t r = esp_reset_reason();
  if (r == ESP_RST_SW && restart_magic == RESTART_MAGIC) snprintf(last_restart, sizeof last_restart, "%s", restart_why);
  else if (r == ESP_RST_TASK_WDT || r == ESP_RST_INT_WDT || r == ESP_RST_WDT) snprintf(last_restart, sizeof last_restart, "Watchdog (the screen code froze)");
  else if (r == ESP_RST_PANIC) snprintf(last_restart, sizeof last_restart, "Crash");
  else if (r == ESP_RST_BROWNOUT) snprintf(last_restart, sizeof last_restart, "Power dipped (check the USB supply)");
  else if (r == ESP_RST_SW) snprintf(last_restart, sizeof last_restart, "Restarted (settings or update)");
  else snprintf(last_restart, sizeof last_restart, "Power on");
  restart_magic = 0;
  Serial.printf("[health] started after: %s\n", last_restart);

  // Watchdog: 60 s, longer than the slowest web request the loop can make (30 s timeout + connect)
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = 60000;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;
  esp_task_wdt_reconfigure(&cfg);
  enableLoopWDT();   // the Arduino loop feeds it after every pass

  WiFi.setAutoReconnect(true);
  fetch_alive_ms = millis();
}

void health_fetch_alive() { fetch_alive_ms = millis(); }

void health_network_up() {
  MDNS.end();
  if (MDNS.begin("gridmix")) MDNS.addService("http", "tcp", 80);
}

void health_loop() {
  const uint32_t now = millis();

  // ---- Wi-Fi: reconnect in steps
  const bool up = WiFi.status() == WL_CONNECTED;
  if (up) {
    if (!was_connected && wifi_lost_ms) {   // back after a drop
      wifi_reconnects++;
      health_network_up();
      Serial.printf("[health] Wi-Fi back after %lu s\n", (unsigned long)((now - wifi_lost_ms) / 1000));
    }
    wifi_lost_ms = 0;
    wifi_step = 0;
  } else if (was_connected || wifi_lost_ms) {
    if (!wifi_lost_ms) { wifi_lost_ms = now; wifi_step = 0; Serial.println("[health] Wi-Fi lost"); }
    const uint32_t lost = now - wifi_lost_ms;
    if (wifi_step == 0 && lost > 30000) { WiFi.reconnect(); wifi_step = 1; }
    else if (wifi_step == 1 && lost > 3 * 60000UL) { WiFi.disconnect(); WiFi.begin(); wifi_step = 2; }
    else if (wifi_step == 2 && lost > 20 * 60000UL && has_saved_wifi()) restart("Wi-Fi lost for 20 min");
  }
  was_connected = up;

  // ---- The fetch task should check in at least every second, or every ~80 s while it waits on a slow request
  if (now - fetch_alive_ms > 3 * 60000UL) restart("Data fetcher stopped");

  // ---- Memory: HTTPS needs a good block of internal RAM. Low for 5 minutes straight: start fresh.
  static uint32_t last_check = 0;
  if (now - last_check > 10000) {
    last_check = now;
    const uint32_t freeb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (freeb < heap_min) heap_min = freeb;
    if (largest < 20000) {
      if (!low_mem_since) low_mem_since = now;
      if (now - low_mem_since > 5 * 60000UL) restart("Memory ran low");
    } else {
      low_mem_since = 0;
    }
  }
}

void health_info(HealthInfo &out) {
  out.uptime_s = millis() / 1000;
  out.last_restart = last_restart;
  out.heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  out.heap_min = heap_min == UINT32_MAX ? out.heap_free : heap_min;
  out.heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  out.psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  out.wifi_reconnects = wifi_reconnects;
}
