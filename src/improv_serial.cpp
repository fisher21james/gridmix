#include "improv_serial.h"
#include <WiFi.h>
#include <vector>

namespace improv {

// Packet types
static const uint8_t TYPE_STATE = 0x01;
static const uint8_t TYPE_ERROR = 0x02;
static const uint8_t TYPE_RPC = 0x03;
static const uint8_t TYPE_RPC_RESULT = 0x04;

// States
static const uint8_t STATE_AUTHORIZED = 0x02;
static const uint8_t STATE_PROVISIONING = 0x03;
static const uint8_t STATE_PROVISIONED = 0x04;

// Errors
static const uint8_t ERR_NONE = 0x00;
static const uint8_t ERR_INVALID_RPC = 0x01;
static const uint8_t ERR_UNKNOWN_RPC = 0x02;
static const uint8_t ERR_UNABLE_TO_CONNECT = 0x03;

// RPC commands
static const uint8_t CMD_WIFI_SETTINGS = 0x01;
static const uint8_t CMD_GET_STATE = 0x02;
static const uint8_t CMD_GET_INFO = 0x03;
static const uint8_t CMD_SCAN = 0x04;

static String g_fw, g_ver, g_name, g_url;
static std::vector<uint8_t> g_buf;

static void send_packet(uint8_t type, const std::vector<uint8_t> &data) {
  std::vector<uint8_t> p = {'I', 'M', 'P', 'R', 'O', 'V', 0x01, type, (uint8_t)data.size()};
  p.insert(p.end(), data.begin(), data.end());
  uint8_t sum = 0;
  for (uint8_t b : p) sum += b;
  p.push_back(sum);
  p.push_back('\n');
  Serial.write(p.data(), p.size());
  Serial.flush();
}

static void send_state(uint8_t s) { send_packet(TYPE_STATE, {s}); }
static void send_error(uint8_t e) { send_packet(TYPE_ERROR, {e}); }

static void send_rpc_result(uint8_t cmd, const std::vector<String> &strings) {
  std::vector<uint8_t> body;
  for (const String &s : strings) {
    body.push_back((uint8_t)s.length());
    for (size_t i = 0; i < s.length(); i++) body.push_back((uint8_t)s[i]);
  }
  std::vector<uint8_t> data = {cmd, (uint8_t)body.size()};
  data.insert(data.end(), body.begin(), body.end());
  send_packet(TYPE_RPC_RESULT, data);
}

static String device_url() {
  String u = g_url;
  u.replace("{IP}", WiFi.localIP().toString());
  return u;
}

static bool try_connect(const String &ssid, const String &pass) {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);  // remember for next boot
  WiFi.begin(ssid.c_str(), pass.c_str());
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) delay(100);
  return WiFi.status() == WL_CONNECTED;
}

static void handle_rpc(const uint8_t *data, uint8_t len) {
  if (len < 2) { send_error(ERR_INVALID_RPC); return; }
  const uint8_t cmd = data[0];
  const uint8_t dlen = data[1];
  const uint8_t *d = data + 2;
  if (dlen + 2 > len) { send_error(ERR_INVALID_RPC); return; }

  switch (cmd) {
    case CMD_GET_STATE:
      if (WiFi.status() == WL_CONNECTED) {
        send_state(STATE_PROVISIONED);
        send_rpc_result(CMD_GET_STATE, {device_url()});
      } else {
        send_state(STATE_AUTHORIZED);
      }
      break;

    case CMD_GET_INFO:
      send_rpc_result(CMD_GET_INFO, {g_fw, g_ver, "ESP32-S3", g_name});
      break;

    case CMD_SCAN: {
      int n = WiFi.scanNetworks();
      for (int i = 0; i < n; i++) {
        send_rpc_result(CMD_SCAN, {WiFi.SSID(i), String(WiFi.RSSI(i)),
                                   WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "NO" : "YES"});
      }
      WiFi.scanDelete();
      send_rpc_result(CMD_SCAN, {});  // empty result = end of list
      break;
    }

    case CMD_WIFI_SETTINGS: {
      if (dlen < 1) { send_error(ERR_INVALID_RPC); return; }
      const uint8_t ssid_len = d[0];
      if (1 + ssid_len + 1 > dlen) { send_error(ERR_INVALID_RPC); return; }
      String ssid((const char *)(d + 1), ssid_len);
      const uint8_t pass_len = d[1 + ssid_len];
      if (1 + ssid_len + 1 + pass_len > dlen) { send_error(ERR_INVALID_RPC); return; }
      String pass((const char *)(d + 2 + ssid_len), pass_len);

      send_state(STATE_PROVISIONING);
      if (try_connect(ssid, pass)) {
        send_error(ERR_NONE);
        send_state(STATE_PROVISIONED);
        send_rpc_result(CMD_WIFI_SETTINGS, {device_url()});
      } else {
        send_error(ERR_UNABLE_TO_CONNECT);
        send_state(STATE_AUTHORIZED);
      }
      break;
    }

    default:
      send_error(ERR_UNKNOWN_RPC);
  }
}

void begin(const char *firmware_name, const char *version, const char *device_name, const char *url) {
  g_fw = firmware_name;
  g_ver = version;
  g_name = device_name;
  g_url = url;
  g_buf.reserve(300);
}

void loop() {
  while (Serial.available()) {
    const uint8_t b = (uint8_t)Serial.read();
    const size_t pos = g_buf.size();

    // Header must spell IMPROV, then version 1
    static const char HDR[] = "IMPROV";
    if (pos < 6) {
      if (b != (uint8_t)HDR[pos]) { g_buf.clear(); if (b == 'I') g_buf.push_back(b); continue; }
      g_buf.push_back(b);
      continue;
    }
    g_buf.push_back(b);
    if (pos == 6 && b != 0x01) { g_buf.clear(); continue; }
    if (g_buf.size() < 9) continue;

    const uint8_t type = g_buf[7];
    const uint8_t len = g_buf[8];
    if (g_buf.size() < (size_t)(9 + len + 1)) continue;

    uint8_t sum = 0;
    for (size_t i = 0; i < (size_t)(9 + len); i++) sum += g_buf[i];
    const bool ok = (sum == g_buf[9 + len]);

    if (ok && type == TYPE_RPC) handle_rpc(&g_buf[9], len);
    g_buf.clear();
  }
}

}  // namespace improv
