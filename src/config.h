// config.h - device configuration stored on LittleFS (/store/config.json).
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Version of the stored configuration format.
//   1  firmware <= 1.0: no "config-version" key
//   2  firmware 1.1+:   adds "config-version" and "web-pwd"
#define CONFIG_VERSION 2

struct Config {
  char     wifi_ssid[33];
  char     wifi_pwd[65];
  char     mqtt_server[64];
  uint16_t mqtt_port;
  char     mqtt_username[64];
  char     mqtt_pwd[64];
  char     device_uid[16];
  char     topic_sub[64];
  char     topic_pub[64];
  char     web_pwd[33];
};

extern Config config;

// Load the configuration; missing values fall back to defaults.h.
void configLoad();

// Save the configuration.
bool configSave();

// Delete the stored configuration (factory reset).
bool configErase();

// True when a WiFi network has been configured.
bool configHasWifi();

// Write the configuration to a JSON object for the web page.
// Passwords are never included: "<name>-set" tells whether one is stored.
void configToJson(JsonObject out);

// Validate the values in `in` and apply them to the configuration.
// Missing keys, and empty passwords, leave the current value unchanged
// (except the WiFi password when the network changes: see config.cpp).
// On error returns false and fills `field` and `error`; nothing is changed.
bool configFromJson(JsonObjectConst in, String &field, String &error);

// One-shot request to start in configuration (access point) mode at the
// next boot, as if PIN_MODE had been activated. Nothing is erased.
bool configRequestApBoot();
// True if a request is pending; the request is consumed.
bool configTakeApBootRequest();

// Remove the files of the old page-on-LittleFS configuration (firmware 1.0).
void configRemoveLegacyFiles();
