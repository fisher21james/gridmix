// HTTPS requests with certificate checking (keys never go out over an unverified connection).
#pragma once
#include <Arduino.h>

// GET url. bearer = token for an "Authorization: Bearer" header, or nullptr.
// Returns the HTTP status (200 = OK), or a negative number if the connection failed.
int https_get(const char *url, const char *bearer, String &body);
// Same, but POSTs `form` (application/x-www-form-urlencoded) when it isn't nullptr
int https_request(const char *url, const char *bearer, const char *form, String &body);

// Percent-encodes a value for use in a URL query string.
String url_encode(const char *s);
