#include "net.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "generated/ca_roots.h"

int https_get(const char *url, const char *bearer, String &body) { return https_request(url, bearer, nullptr, body); }

int https_request(const char *url, const char *bearer, const char *form, String &body) {
  body = "";
  WiFiClientSecure client;
  client.setCACert(CA_ROOTS_PEM);

  HTTPClient http;
  http.setTimeout(30000);   // the EIA can take 12+ s for the lower-48 total (measured 13 Nov 2026)
  http.setConnectTimeout(10000);
  http.setUserAgent("GridMixDisplay/" FW_VERSION);
  if (!http.begin(client, url)) return -1;
  if (bearer && *bearer) http.addHeader("Authorization", String("Bearer ") + bearer);

  // Big answers (Texas, ~200 KB) go to PSRAM; follow plain redirects (NYISO's archive moves files around)
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int code;
  if (form) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    code = http.POST((uint8_t *)form, strlen(form));
  } else {
    code = http.GET();
  }
  if (code > 0) body = http.getString();
  http.end();
  return code;
}

String url_encode(const char *s) {
  static const char *hex = "0123456789ABCDEF";
  String out;
  for (; s && *s; s++) {
    const unsigned char c = (unsigned char)*s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}
