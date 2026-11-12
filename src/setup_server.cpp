#include "setup_server.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <time.h>
#include "app_state.h"
#include "net.h"
#include "settings_store.h"
#include "generated/setup_html.h"

using gridcore::Settings;

static WebServer server(80);
static bool started = false;

static void send_json(int code, JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}

static void send_message(int code, bool ok, const char *msg) {
  JsonDocument doc;
  doc["ok"] = ok;
  doc["message"] = msg;
  send_json(code, doc);
}

// Only accept JSON posts: a web form on some other site can't set this header,
// so it can't change the display's settings behind your back.
static bool is_json_post() {
  if (server.header("Content-Type").startsWith("application/json")) return true;
  send_message(415, false, "Expected JSON.");
  return false;
}

static void handle_page() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", (const char *)SETUP_HTML_GZ, SETUP_HTML_GZ_LEN);
}

static void handle_get_settings() {
  static char buf[3072];
  const size_t n = gridcore::settings_to_json(g_settings, true, buf, sizeof buf);
  server.sendHeader("Cache-Control", "no-store");
  server.send(n ? 200 : 500, "application/json", n ? buf : "{}");
}

static void handle_post_settings() {
  if (!is_json_post()) return;
  const String &body = server.arg("plain");
  Settings next = g_settings;
  char err[96];
  if (!gridcore::settings_from_json(body.c_str(), body.length(), next, err, sizeof err)) {
    send_message(400, false, err);
    return;
  }
  if (!settings_save(next)) {
    send_message(500, false, "Couldn't save to the display's memory.");
    return;
  }
  g_settings = next;
  send_message(200, true, "Saved");
  on_settings_changed();
}

static void handle_status() {
  JsonDocument doc;
  doc["fw"] = FW_VERSION;
  doc["ssid"] = WiFi.SSID();
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  const time_t now = time(nullptr);
  const bool have = g_status.last_ok_utc > 0 && now > 1700000000;
  doc["ok"] = have && !g_status.problem;
  if (have) doc["data_age_s"] = (long)(now - g_status.last_ok_utc);
  else doc["data_age_s"] = nullptr;
  if (g_status.problem) doc["problem"] = g_status.problem;
  doc["credits_known"] = g_status.credits_left >= 0;
  doc["credits"] = g_status.credits_left;
  send_json(200, doc);
}

// Tries the key with a tiny request. Uses the stored key if none was typed.
static void handle_test_key() {
  if (!is_json_post()) return;
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain"))) { send_message(400, false, "Bad request."); return; }
  const char *provider = in["provider"] | "";
  const char *typed = in["key"] | "";
  const bool oe = !strcmp(provider, "oe");
  const char *key = *typed ? typed : (oe ? g_settings.key_oe : g_settings.key_eia);
  if (!*key) { send_message(200, false, "Paste a key first."); return; }

  String body;
  if (oe) {
    if (strncmp(key, "oe_", 3) != 0) { send_message(200, false, "That doesn't look like an Open Electricity key. They start with oe_."); return; }
    const int code = https_get("https://api.openelectricity.org.au/v4/me", key, body);
    if (code < 0) { send_message(200, false, "Couldn't reach Open Electricity. Check the display's internet connection."); return; }
    if (code == 401 || code == 403) { send_message(200, false, "Open Electricity didn't accept that key. Check it was copied in full."); return; }
    if (code == 429) { send_message(200, false, "The key works, but today's credits are used up. They reset at 10:30am Adelaide time (midnight UTC)."); return; }
    JsonDocument r;
    deserializeJson(r, body);
    if (code != 200 || !(r["success"] | false)) { send_message(200, false, "Open Electricity gave an unexpected answer. Try again in a minute."); return; }
    const int left = r["data"]["credits"]["remaining"] | -1;
    if (left >= 0) g_status.credits_left = left;
    char msg[96];
    if (left >= 0) snprintf(msg, sizeof msg, "Key works. %d credits left today.", left);
    else snprintf(msg, sizeof msg, "Key works.");
    send_message(200, true, msg);
  } else {
    String url = "https://api.eia.gov/v2/electricity/rto/fuel-type-data/data/?frequency=hourly&data[0]=value&facets[respondent][]=US48&length=1&api_key=";
    url += url_encode(key);
    const int code = https_get(url.c_str(), nullptr, body);
    if (code < 0) { send_message(200, false, "Couldn't reach the EIA. Check the display's internet connection."); return; }
    if (code == 200 && body.indexOf("\"response\"") >= 0) { send_message(200, true, "Key works."); return; }
    send_message(200, false, "The EIA didn't accept that key. Check it was copied in full from the email.");
  }
}

void setup_server_begin() {
  if (started) return;
  static const char *headers[] = {"Content-Type"};
  server.collectHeaders(headers, 1);
  server.on("/", HTTP_GET, handle_page);
  server.on("/api/settings", HTTP_GET, handle_get_settings);
  server.on("/api/settings", HTTP_POST, handle_post_settings);
  server.on("/api/status", HTTP_GET, handle_status);
  server.on("/api/test-key", HTTP_POST, handle_test_key);
  server.onNotFound([] { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });
  server.begin();
  if (MDNS.begin("gridmix")) MDNS.addService("http", "tcp", 80);
  started = true;
}

void setup_server_loop() {
  if (started) server.handleClient();
}
