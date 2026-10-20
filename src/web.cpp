// web.cpp - web server: configuration page and JSON API.
//
//   GET  /             configuration page (embedded in the firmware)
//   GET  /api/status   device state
//   GET  /api/config   configuration (passwords excluded)
//   POST /api/config   validate + save the configuration, then restart
//   POST /api/restart  restart the device (?mode=ap: in access point mode)
//   POST /api/reset    erase the configuration, then restart
//   GET  /api/scan     WiFi networks in range (?refresh=1 starts a new scan)
//
// Every request requires HTTP basic authentication
// (user WEB_USER, password config.web_pwd).
#include "web.h"

#include <ArduinoJson.h>

#include "config.h"
#include "defaults.h"
#include "devices.h"
#include "log.h"
#include "mqttlink.h"
#include "net.h"
#include "platform.h"
#include "web_assets.h"

static WebServerClass s_server(80);
static uint32_t s_restartAt = 0;          // millis() of a scheduled restart, 0 = none

static const uint32_t RESTART_DELAY_MS = 1500;   // time to deliver the response

static bool authenticate() {
  if (s_server.authenticate(WEB_USER, config.web_pwd)) return true;
  s_server.requestAuthentication(BASIC_AUTH, FW_NAME);
  return false;
}

static void scheduleRestart() {
  s_restartAt = millis() + RESTART_DELAY_MS;
  if (s_restartAt == 0) s_restartAt = 1;
}

static void sendJson(int code, const JsonDocument &doc) {
  String body;
  serializeJson(doc, body);
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(code, "application/json", body);
}

static void sendResult(int code, const char *error = nullptr, const String &field = String()) {
  JsonDocument doc;
  doc["ok"] = (error == nullptr);
  if (error) doc["error"] = error;
  if (field.length()) doc["field"] = field;
  sendJson(code, doc);
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------
static void handleIndex() {
  if (!authenticate()) return;
  s_server.sendHeader("Content-Encoding", "gzip");
  s_server.sendHeader("Cache-Control", "no-cache");
  s_server.send_P(200, "text/html", (PGM_P)INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
}

static void handleStatus() {
  if (!authenticate()) return;
  JsonDocument doc;
  char chip[9];
  snprintf(chip, sizeof(chip), "%06X", (unsigned)platform::chipId());

  doc["name"]     = FW_NAME;
  doc["firmware"] = FW_VERSION;
  doc["platform"] = PLATFORM_NAME;
  doc["chip"]     = chip;
  doc["mac"]      = WiFi.macAddress();
  doc["uptime"]   = millis() / 1000;
  doc["heap"]     = platform::freeHeap();
  doc["uid"]      = config.device_uid;
  doc["hostname"] = netHostname();
  doc["mode"]     = netMode() == NetMode::ConfigAP ? "config" : "station";
  doc["ip"]       = netIp();
  doc["output"]   = devicesOutputState();

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"]      = configHasWifi() ? config.wifi_ssid : "";
  wifi["connected"] = netWifiConnected();
  if (netWifiConnected()) wifi["rssi"] = WiFi.RSSI();

  JsonObject ap = doc["ap"].to<JsonObject>();
  ap["ssid"]    = AP_SSID;
  ap["active"]  = netApActive();
  ap["clients"] = netApClients();

  JsonObject mqtt = doc["mqtt"].to<JsonObject>();
  mqtt["server"]    = config.mqtt_server;
  mqtt["port"]      = config.mqtt_port;
  mqtt["connected"] = mqttConnected();

  JsonObject fn = doc["functions"].to<JsonObject>();
  fn["switch"]  = (bool)CFG_SWITCH_ENABLED;
  fn["button"]  = (bool)CFG_BUTTON_ENABLED;
  fn["sensor"]  = (bool)CFG_SENSOR_ENABLED;
  fn["contact"] = (bool)CFG_CONTACT_ENABLED;

  sendJson(200, doc);
}

static void handleGetConfig() {
  if (!authenticate()) return;
  JsonDocument doc;
  configToJson(doc.to<JsonObject>());
  sendJson(200, doc);
}

static void handlePostConfig() {
  if (!authenticate()) return;

  JsonDocument in;
  DeserializationError err = deserializeJson(in, s_server.arg("plain"));
  if (err || !in.is<JsonObject>()) {
    sendResult(400, "The request is not valid JSON.");
    return;
  }

  String field, error;
  if (!configFromJson(in.as<JsonObjectConst>(), field, error)) {
    sendResult(400, error.c_str(), field);
    return;
  }
  if (!configSave()) {
    sendResult(500, "The configuration could not be written to the flash memory.");
    return;
  }
  sendResult(200);
  LOG("Configuration changed from the web page: restarting");
  scheduleRestart();
}

static void handleRestart() {
  if (!authenticate()) return;
  const bool apMode = s_server.arg("mode") == "ap";
  if (apMode && !configRequestApBoot()) {
    sendResult(500, "The request could not be written to the flash memory.");
    return;
  }
  sendResult(200);
  LOG("Restart%s requested from the web page", apMode ? " in access point mode" : "");
  scheduleRestart();
}

static void handleReset() {
  if (!authenticate()) return;
  if (!configErase()) {
    sendResult(500, "The configuration could not be erased.");
    return;
  }
  sendResult(200);
  LOG("Factory reset requested from the web page");
  scheduleRestart();
}

// Results older than this are refreshed automatically.
static const uint32_t SCAN_MAX_AGE_MS = 60000;

static void handleScan() {
  if (!authenticate()) return;

  const bool refresh = s_server.arg("refresh") == "1";
  if (!netScanRunning() && (refresh || netScanAge() > SCAN_MAX_AGE_MS)) netScanStart();

  JsonDocument doc;
  doc["scanning"] = netScanRunning();
  doc["failed"]   = netScanFailed();
  uint32_t age = netScanAge();
  if (age != UINT32_MAX) doc["age"] = age / 1000;
  JsonArray list = doc["networks"].to<JsonArray>();
  for (uint8_t i = 0; i < netScanCount(); i++) {
    const NetScanResult &r = netScanResult(i);
    JsonObject n = list.add<JsonObject>();
    n["ssid"]    = r.ssid;
    n["rssi"]    = r.rssi;
    n["channel"] = r.channel;
    n["secure"]  = r.secure;
  }
  sendJson(200, doc);
}

static void handleFavicon() {
  s_server.send(204, "image/x-icon", "");
}

static void handleNotFound() {
  if (s_server.uri().startsWith("/api/")) {
    sendResult(404, "Unknown API.");
    return;
  }
  // Any other address leads to the configuration page.
  s_server.sendHeader("Location", "/");
  s_server.send(302, "text/plain", "");
}

// ---------------------------------------------------------------------------
void webBegin() {
  s_server.on("/",            HTTP_GET,  handleIndex);
  s_server.on("/index.html",  HTTP_GET,  handleIndex);
  s_server.on("/api/status",  HTTP_GET,  handleStatus);
  s_server.on("/api/config",  HTTP_GET,  handleGetConfig);
  s_server.on("/api/config",  HTTP_POST, handlePostConfig);
  s_server.on("/api/restart", HTTP_POST, handleRestart);
  s_server.on("/api/reset",   HTTP_POST, handleReset);
  s_server.on("/api/scan",    HTTP_GET,  handleScan);
  s_server.on("/favicon.ico", HTTP_GET,  handleFavicon);
  s_server.onNotFound(handleNotFound);
  s_server.begin();
  LOG("Web server started (user '%s')", WEB_USER);
}

void webLoop() {
  s_server.handleClient();
  if (s_restartAt && (int32_t)(millis() - s_restartAt) >= 0) {
    LOG("Restarting...");
    delay(100);
    ESP.restart();
  }
}
