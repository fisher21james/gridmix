// Minimal Improv Wi-Fi (serial) implementation.
// Lets the browser flasher (ESP Web Tools) send Wi-Fi details over USB right after flashing.
// Protocol: https://www.improv-wifi.com/serial/
#pragma once
#include <Arduino.h>

namespace improv {

// Call once in setup(). url is shown in the browser after connecting; "{IP}" is replaced with the device IP.
void begin(const char *firmware_name, const char *version, const char *device_name, const char *url);

// Call often from loop().
void loop();

}  // namespace improv
