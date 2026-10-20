// defaults.h - compile-time defaults of the firmware.
//
// Every value guarded by #ifndef can be overridden from platformio.ini
// (build_flags = -DNAME=value) without touching the sources.
#pragma once

// ---------------------------------------------------------------------------
// Firmware identity
// ---------------------------------------------------------------------------
#define FW_NAME     "IOT eXperience"
#define FW_VERSION  "1.3.1"

// ---------------------------------------------------------------------------
// Features
// (still chosen at compile time; they will move to the web configuration)
// ---------------------------------------------------------------------------
#ifndef CFG_SWITCH_ENABLED
#define CFG_SWITCH_ENABLED    1   // on/off output driven by MQTT
#endif
#ifndef CFG_BUTTON_ENABLED
#define CFG_BUTTON_ENABLED    1   // pulse on the output driven by MQTT
#endif
#ifndef CFG_SENSOR_ENABLED
#define CFG_SENSOR_ENABLED    0   // DHT temperature / humidity
#endif
#ifndef CFG_FAKE_SENSOR
#define CFG_FAKE_SENSOR       0   // random sensor values, for testing
#endif
#ifndef CFG_CONTACT_ENABLED
#define CFG_CONTACT_ENABLED   0   // dry contact input (door, window...)
#endif
#ifndef CFG_INFRARED_ENABLED
#define CFG_INFRARED_ENABLED  0   // placeholder, not implemented yet
#endif

// ---------------------------------------------------------------------------
// Pins (GPIO numbers)
// ---------------------------------------------------------------------------
#if defined(ESP8266)
  // WeMos D1 / D1 mini
  #ifndef PIN_OUTPUT
  #define PIN_OUTPUT       4    // D2  relay / output
  #endif
  #ifndef PIN_CONTACT
  #define PIN_CONTACT      0    // D3  contact input (boot pin: must not be LOW at power on)
  #endif
  #ifndef PIN_SENSOR
  #define PIN_SENSOR       2    // D4  DHT data (boot pin)
  #endif
  #ifndef PIN_MANUAL
  #define PIN_MANUAL       14   // D5  local push button (not used yet)
  #endif
  #ifndef PIN_LED_RED
  #define PIN_LED_RED      16   // D0  output state / configuration mode
  #endif
  #ifndef PIN_LED_GREEN
  #define PIN_LED_GREEN    5    // D1  network state
  #endif
  #ifndef PIN_MODE
  #define PIN_MODE         15   // D8  configuration mode request (external pull-down)
  #endif
  #ifndef PIN_MODE_ACTIVE
  #define PIN_MODE_ACTIVE  HIGH // D8 to 3.3V requests configuration mode
  #endif
#elif defined(ESP32)
  // Generic ESP32 C series profile (C3 / C6 / C5).
  // Uses only GPIOs that are neither strapping, flash nor USB pins on these
  // chips. PROVISIONAL: pins will be configurable from the web page.
  #ifndef PIN_OUTPUT
  #define PIN_OUTPUT       6
  #endif
  #ifndef PIN_CONTACT
  #define PIN_CONTACT      7
  #endif
  #ifndef PIN_SENSOR
  #define PIN_SENSOR       3
  #endif
  #ifndef PIN_MANUAL
  #define PIN_MANUAL       10
  #endif
  #ifndef PIN_LED_RED
  #define PIN_LED_RED      0
  #endif
  #ifndef PIN_LED_GREEN
  #define PIN_LED_GREEN    1
  #endif
  #ifndef PIN_MODE
  #define PIN_MODE         9    // BOOT button on most boards (press after power on)
  #endif
  #ifndef PIN_MODE_ACTIVE
  #define PIN_MODE_ACTIVE  LOW
  #endif
#else
  #error "Unsupported platform: only ESP8266 and ESP32 are supported"
#endif

// ---------------------------------------------------------------------------
// Configuration defaults (used when /store/config.json lacks a value)
// ---------------------------------------------------------------------------
#ifndef DEFAULT_MQTT_SERVER
#define DEFAULT_MQTT_SERVER  ""
#endif
#ifndef DEFAULT_MQTT_PORT
#define DEFAULT_MQTT_PORT    1883
#endif
#ifndef DEFAULT_DEVICE_UID
#define DEFAULT_DEVICE_UID   "999"
#endif
#ifndef DEFAULT_TOPIC_SUB
#define DEFAULT_TOPIC_SUB    "iotx/in"
#endif
#ifndef DEFAULT_TOPIC_PUB
#define DEFAULT_TOPIC_PUB    "iotx/out"
#endif
#ifndef WEB_USER
#define WEB_USER             "admin"      // user name of the web pages
#endif
#ifndef DEFAULT_WEB_PWD
#define DEFAULT_WEB_PWD      "iotxadmin"  // default password of the web pages
#endif

// ---------------------------------------------------------------------------
// Access point used for configuration
// ---------------------------------------------------------------------------
#ifndef AP_SSID
#define AP_SSID              "WD1R2-REMOTE"
#endif
#ifndef AP_PWD
#define AP_PWD               "12345678"   // at least 8 characters
#endif

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
#define BOOT_WINDOW_MS          2000UL  // window to request configuration mode
#define WIFI_AP_FALLBACK_S      60UL    // without WiFi for this long -> open the AP
#define WIFI_RETRY_MS           30000UL // WiFi retry period while the fallback AP is open
#define AP_CLOSE_AFTER_MS       60000UL // close the fallback AP after WiFi is back for this long
#define MQTT_RETRY_MS           5000UL  // pause between MQTT connection attempts
#define OFFLINE_RESTART_S       600UL   // offline (WiFi or MQTT) for this long -> restart (0 = never)
#define STATUS_INTERVAL_S       60UL    // default period of the status messages
#define PULSE_MS                1000UL  // button / infrared pulse length
#define NTP_SERVER_1            "pool.ntp.org"
#define NTP_SERVER_2            "time.google.com"
