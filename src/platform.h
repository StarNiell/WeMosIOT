// platform.h - thin abstraction over the differences between the
// ESP8266 and ESP32 Arduino cores. Everything else in the firmware is
// written against these names only.
#pragma once

#include <Arduino.h>
#include <LittleFS.h>

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <ESP8266mDNS.h>
  using WebServerClass = ESP8266WebServer;
  #define PLATFORM_NAME "ESP8266"
#elif defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  #include <ESPmDNS.h>
  using WebServerClass = WebServer;
  #define PLATFORM_NAME "ESP32"
#endif

namespace platform {

// Mount the LittleFS filesystem.
inline bool fsBegin() {
#if defined(ESP8266)
  return LittleFS.begin();
#else
  return LittleFS.begin(true);   // format on first use
#endif
}

// Unique chip identifier (lower 24 bits of the MAC on ESP32).
inline uint32_t chipId() {
#if defined(ESP8266)
  return ESP.getChipId();
#else
  return (uint32_t)(ESP.getEfuseMac() >> 24);
#endif
}

// Input mode for the configuration-mode pin.
inline uint8_t modePinMode(uint8_t activeLevel) {
#if defined(ESP8266)
  (void)activeLevel;
  return INPUT;                  // D8 has an external pull-down on the D1 mini
#else
  return activeLevel == HIGH ? INPUT_PULLDOWN : INPUT_PULLUP;
#endif
}

// Set the network host name (call before WiFi.begin()).
inline void setHostname(const char *name) {
#if defined(ESP8266)
  WiFi.hostname(name);
#else
  WiFi.setHostname(name);
#endif
}

// Periodic mDNS housekeeping (needed on ESP8266 only).
inline void mdnsUpdate() {
#if defined(ESP8266)
  MDNS.update();
#endif
}

inline uint32_t freeHeap() { return ESP.getFreeHeap(); }

}  // namespace platform
