#include "fetcher.h"
#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>
#include "app_state.h"
#include "net.h"
#include "parsers.h"
#include "health.h"
#include "ota.h"

using namespace gridcore;

static SemaphoreHandle_t lock;
static LiveData live;                 // shared, guarded by lock

// The request settings, copied in by fetcher_reset() so the task never reads g_settings while
// the setup page is changing it.
struct Job {
  Feed feed;
  char key[72] = "";
  uint32_t generation = 0;
};
static Job job;                       // guarded by lock

static void log_url(const char *what, const char *url) {
  // Never print the EIA URL whole: the key is on the end of it
  const char *k = strstr(url, "api_key=");
  Serial.printf("[fetch] %s %.*s\n", what, k ? (int)(k - url) : (int)strlen(url), url);
}

static void task(void *) {
  Scheduler sched;
  uint32_t seen_generation = UINT32_MAX;
  int history_tries = 0;              // the 24 h history is fetched once per region/view, up to 3 tries
  int64_t history_next = 0;
  String market_body;                 // last price/rooftop answer, matched to each new mix
  char url[400];

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    health_fetch_alive();
    const int64_t now = time(nullptr);
    if (WiFi.status() != WL_CONNECTED || now < 1700000000) continue;   // no Wi-Fi or clock not set yet
    if (ota_busy()) continue;                                             // leave the memory to the update

    Job j;
    xSemaphoreTake(lock, portMAX_DELAY);
    j = job;
    xSemaphoreGive(lock);
    if (!j.key[0]) continue;
    if (j.generation != seen_generation) {
      seen_generation = j.generation;
      sched.reset(j.feed.source, now);
      market_body = "";
      history_tries = 0;
      history_next = 0;
    }

    const bool oe = j.feed.source == SRC_OPEN_ELECTRICITY;
    if (oe && j.feed.has_market && sched.due(FETCH_MARKET, now)) {
      if (oe_market_url(j.feed, now, url, sizeof url)) {
        log_url("market", url);
        String body;
        const int code = https_get(url, j.key, body);
        Serial.printf("[fetch] market -> %d (%u bytes)\n", code, body.length());
        sched.done(FETCH_MARKET, now, code);
        if (code == 200) market_body = body;
      }
      continue;   // one request per pass
    }
    // Once the first mix is in, fill the 24-hour chart with one hourly request
    if (oe && history_tries >= 0 && history_tries < 3 && now >= history_next && sched.next(FETCH_MIX) > now) {
      if (oe_history_url(j.feed, now, url, sizeof url)) {
        log_url("history", url);
        String body;
        const int code = https_get(url, j.key, body);
        static HistoryPoint pts[32];
        const int n = code == 200 ? parse_openelectricity_history(body.c_str(), body.length(), pts, 32) : 0;
        Serial.printf("[fetch] history -> %d, %d points\n", code, n);
        if (n > 0) {
          history_tries = -1;   // done for this region/view
          xSemaphoreTake(lock, portMAX_DELAY);
          if (j.generation == job.generation) {
            for (int i = 0; i < n; i++) live.history[i] = pts[i];
            live.history_count = n;
            live.history_version++;
          }
          xSemaphoreGive(lock);
        } else {
          history_tries++;
          history_next = now + 10 * 60;
        }
      }
      continue;
    }
    if (!sched.due(FETCH_MIX, now)) continue;

    String body;
    int code = -1;
    if (oe) {
      if (!oe_power_url(j.feed, now, url, sizeof url)) continue;
      log_url("mix", url);
      code = https_get(url, j.key, body);
    } else {
      if (!eia_url(j.feed, url, sizeof url)) continue;
      log_url("mix", url);
      String full = String(url) + url_encode(j.key);
      code = https_get(full.c_str(), nullptr, body);
    }
    Serial.printf("[fetch] mix -> %d (%u bytes)\n", code, body.length());

    GridSnapshot snap;
    bool ok = false;
    if (code == 200) {
      ok = oe ? parse_openelectricity(body.c_str(), body.length(), snap) : parse_eia(body.c_str(), body.length(), snap);
      if (ok && oe && market_body.length()) {
        MarketInfo mk;
        if (parse_openelectricity_market(market_body.c_str(), market_body.length(), snap.data_time, mk)) merge_market(snap, mk);
      }
      if (!ok) Serial.println("[fetch] couldn't read the answer");
    }
    sched.done(FETCH_MIX, now, ok ? 200 : (code == 200 ? 500 : code));

    xSemaphoreTake(lock, portMAX_DELAY);
    if (j.generation == job.generation) {   // settings didn't change while we were waiting
      if (ok) {
        live.has_data = true;
        live.snap = snap;
        live.from_cache = false;
        live.version++;
      }
      live.source = j.feed.source;
      live.key_rejected = sched.key_rejected();
      live.out_of_credits = sched.out_of_credits();
      live.failures = sched.failures(FETCH_MIX);
      if (ok) g_status.last_ok_utc = now;
    }
    xSemaphoreGive(lock);
  }
}

void fetcher_begin() {
  lock = xSemaphoreCreateMutex();
  fetcher_reset(g_settings.view_country);
  // TLS needs a deep stack. Core 0, so drawing on core 1 carries on during requests.
  xTaskCreatePinnedToCore(task, "fetch", 16384, nullptr, 1, nullptr, 0);
}

void fetcher_reset(bool country_view) {
  xSemaphoreTake(lock, portMAX_DELAY);
  const uint32_t gen = job.generation + 1;
  job = Job();
  job.generation = gen;
  job.feed = feed_for(g_settings.country, g_settings.region, country_view);
  snprintf(job.key, sizeof job.key, "%s", g_settings.key_for_country());
  const uint32_t version = live.version + 1, hversion = live.history_version;
  live = LiveData();
  live.version = version;
  live.history_version = hversion;   // keep counting up, so the screen never mistakes new history for old
  live.source = job.feed.source;
  xSemaphoreGive(lock);
}

void fetcher_seed(const GridSnapshot &snap) {
  xSemaphoreTake(lock, portMAX_DELAY);
  if (!live.has_data) {
    live.has_data = true;
    live.snap = snap;
    live.from_cache = true;
    live.version++;
  }
  xSemaphoreGive(lock);
}

void fetcher_get(LiveData &out) {
  xSemaphoreTake(lock, portMAX_DELAY);
  out = live;
  xSemaphoreGive(lock);
}
