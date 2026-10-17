// config.h - device configuration stored on LittleFS (/store/config.json).
#pragma once
#include <Arduino.h>

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

// Save the configuration and regenerate the configuration page.
bool configSave();

// True when a WiFi network has been configured.
bool configHasWifi();
