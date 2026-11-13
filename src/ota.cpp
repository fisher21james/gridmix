#include "ota.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>
#include "net.h"
#include "generated/ca_roots.h"

#ifndef FW_BUILD
#define FW_BUILD 0
#endif

static const char *MANIFEST = "https://fisher21james.github.io/gridmix/firmware/ota.json";

static SemaphoreHandle_t lock;
static OtaStatus st;
static volatile bool want_check = false, want_install = false;
static char url[160];
static uint32_t size = 0;
static char sha[65];

static void set_state(OtaState s, const char *msg, int pct = -1) {
  xSemaphoreTake(lock, portMAX_DELAY);
  st.state = s;
  if (msg) snprintf(st.message, sizeof st.message, "%s", msg);
  if (pct >= 0) st.percent = pct;
  xSemaphoreGive(lock);
  if (msg) Serial.printf("[ota] %s\n", msg);
}

static void check() {
  set_state(OTA_CHECKING, "Checking for updates");
  String body;
  const int code = https_get(MANIFEST, nullptr, body);
  JsonDocument doc;
  if (code != 200 || deserializeJson(doc, body)) {
    set_state(OTA_FAILED, code < 0 ? "Couldn't reach the update site" : "The update site gave an unexpected answer");
    return;
  }
  const uint32_t build = doc["build"] | 0;
  snprintf(url, sizeof url, "%s", doc["url"] | "");
  size = doc["size"] | 0;
  snprintf(sha, sizeof sha, "%s", doc["sha256"] | "");
  xSemaphoreTake(lock, portMAX_DELAY);
  snprintf(st.latest, sizeof st.latest, "%s (build %lu)", doc["version"] | "?", (unsigned long)build);
  st.latest_build = build;
  st.checked_utc = time(nullptr);
  xSemaphoreGive(lock);
  if (build > FW_BUILD && url[0] && size > 0 && strlen(sha) == 64) set_state(OTA_AVAILABLE, "Update available");
  else set_state(OTA_UP_TO_DATE, "Up to date");
}

static bool hex_equal(const uint8_t *digest, const char *hex) {
  static const char *H = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    if (tolower(hex[i * 2]) != H[digest[i] >> 4] || tolower(hex[i * 2 + 1]) != H[digest[i] & 15]) return false;
  }
  return true;
}

static void install() {
  set_state(OTA_DOWNLOADING, "Downloading the update", 0);
  WiFiClientSecure client;
  client.setCACert(CA_ROOTS_PEM);
  HTTPClient http;
  http.setTimeout(30000);
  if (!http.begin(client, url)) { set_state(OTA_FAILED, "Couldn't start the download"); return; }
  const int code = http.GET();
  if (code != 200) { http.end(); set_state(OTA_FAILED, "The update file couldn't be downloaded"); return; }
  const int len = http.getSize();
  if (len > 0 && (uint32_t)len != size) { http.end(); set_state(OTA_FAILED, "The update file is the wrong size"); return; }
  if (!Update.begin(size)) { http.end(); set_state(OTA_FAILED, "Not enough room for the update"); return; }

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  WiFiClient *stream = http.getStreamPtr();
  static uint8_t buf[4096];
  uint32_t done = 0, last_data = millis();
  while (done < size) {
    const size_t avail = stream->available();
    if (!avail) {
      if (!http.connected() || millis() - last_data > 20000) break;
      delay(5);
      continue;
    }
    const int n = stream->readBytes(buf, avail < sizeof buf ? avail : sizeof buf);
    if (n <= 0) continue;
    last_data = millis();
    mbedtls_sha256_update(&ctx, buf, n);
    if (Update.write(buf, n) != (size_t)n) break;
    done += n;
    set_state(OTA_DOWNLOADING, nullptr, (int)((uint64_t)done * 100 / size));
  }
  http.end();
  uint8_t digest[32];
  mbedtls_sha256_finish(&ctx, digest);
  mbedtls_sha256_free(&ctx);

  if (done != size) { Update.abort(); set_state(OTA_FAILED, "The download stopped part way. Nothing was changed."); return; }
  if (!hex_equal(digest, sha)) { Update.abort(); set_state(OTA_FAILED, "The download didn't match its checksum. Nothing was changed."); return; }
  if (!Update.end(true)) { set_state(OTA_FAILED, "The update couldn't be finished. Nothing was changed."); return; }
  set_state(OTA_DONE, "Updated. Restarting", 100);
  delay(1500);
  ESP.restart();
}

static void task(void *) {
  uint32_t next_auto = millis() + 2 * 60000UL;   // first check two minutes after start-up
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (WiFi.status() != WL_CONNECTED) continue;
    if (want_install) {
      want_install = false;
      install();
      continue;
    }
    if (want_check || (int32_t)(millis() - next_auto) >= 0) {
      want_check = false;
      next_auto = millis() + 24 * 3600000UL;
      OtaState s;
      xSemaphoreTake(lock, portMAX_DELAY);
      s = st.state;
      xSemaphoreGive(lock);
      if (s != OTA_DOWNLOADING && s != OTA_DONE) check();
    }
  }
}

void ota_begin() {
  lock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(task, "ota", 12288, nullptr, 1, nullptr, 0);
}

void ota_request_check() { want_check = true; }

bool ota_request_install() {
  xSemaphoreTake(lock, portMAX_DELAY);
  const bool ok = st.state == OTA_AVAILABLE || (st.state == OTA_FAILED && st.latest_build > FW_BUILD);
  xSemaphoreGive(lock);
  if (ok) want_install = true;
  return ok;
}

void ota_status(OtaStatus &out) {
  xSemaphoreTake(lock, portMAX_DELAY);
  out = st;
  xSemaphoreGive(lock);
}

bool ota_busy() {
  xSemaphoreTake(lock, portMAX_DELAY);
  const bool b = st.state == OTA_DOWNLOADING || st.state == OTA_DONE;
  xSemaphoreGive(lock);
  return b;
}
