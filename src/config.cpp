// config.cpp - load / save of the device configuration.
#include "config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "defaults.h"
#include "log.h"

Config config;

static const char *CONFIG_FILE = "/store/config.json";
static const char *CONFIG_TMP  = "/store/config.tmp";
static const char *PAGE_TEMPLATE = "/index.static";
static const char *PAGE_FILE     = "/index.html";

// Copy a JSON string value (or a default) into a fixed size buffer.
static void copyValue(char *dst, size_t size, JsonVariantConst src, const char *def) {
  strlcpy(dst, src | def, size);
}

// Escape a value so that it can be placed inside an HTML attribute.
static String htmlEscape(const char *value) {
  String out;
  out.reserve(strlen(value) + 8);
  for (const char *p = value; *p; p++) {
    switch (*p) {
      case '&': out += F("&amp;");  break;
      case '"': out += F("&quot;"); break;
      case '<': out += F("&lt;");   break;
      case '>': out += F("&gt;");   break;
      default:  out += *p;
    }
  }
  return out;
}

// Build /index.html from /index.static, filling in the current values.
// (Temporary: replaced by a static page + JSON API in the next step.)
static void generateConfigPage() {
  File in = LittleFS.open(PAGE_TEMPLATE, "r");
  if (!in) {
    LOG("Config page template %s not found", PAGE_TEMPLATE);
    return;
  }
  String page = in.readString();
  in.close();

  page.replace(F("wifi_ssid_value"),     htmlEscape(config.wifi_ssid));
  page.replace(F("wifi_pwd_value"),      htmlEscape(config.wifi_pwd));
  page.replace(F("mqtt_server_value"),   htmlEscape(config.mqtt_server));
  page.replace(F("mqtt_port_value"),     String(config.mqtt_port));
  page.replace(F("mqtt_user_value"),     htmlEscape(config.mqtt_username));
  page.replace(F("mqtt_pwd_value"),      htmlEscape(config.mqtt_pwd));
  page.replace(F("mqtt_deviceid_value"), htmlEscape(config.device_uid));
  page.replace(F("topic_sub_value"),     htmlEscape(config.topic_sub));
  page.replace(F("topic_pub_value"),     htmlEscape(config.topic_pub));

  File out = LittleFS.open(PAGE_FILE, "w");
  if (!out) {
    LOG("Unable to write %s", PAGE_FILE);
    return;
  }
  out.print(page);
  out.close();
  LOG("Config page regenerated");
}

void configLoad() {
  JsonDocument doc;
  File file = LittleFS.open(CONFIG_FILE, "r");
  if (file) {
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
      LOG("Invalid %s (%s): using defaults", CONFIG_FILE, err.c_str());
      doc.clear();
    }
  } else {
    LOG("%s not found: using defaults", CONFIG_FILE);
  }

  copyValue(config.wifi_ssid,     sizeof(config.wifi_ssid),     doc["wifi-ssid"],     "");
  copyValue(config.wifi_pwd,      sizeof(config.wifi_pwd),      doc["wifi-pwd"],      "");
  copyValue(config.mqtt_server,   sizeof(config.mqtt_server),   doc["mqtt-server"],   DEFAULT_MQTT_SERVER);
  config.mqtt_port = doc["mqtt-port"] | DEFAULT_MQTT_PORT;
  copyValue(config.mqtt_username, sizeof(config.mqtt_username), doc["mqtt-username"], "");
  copyValue(config.mqtt_pwd,      sizeof(config.mqtt_pwd),      doc["mqtt-pwd"],      "");
  copyValue(config.device_uid,    sizeof(config.device_uid),    doc["device-uid"],    DEFAULT_DEVICE_UID);
  copyValue(config.topic_sub,     sizeof(config.topic_sub),     doc["topic-sub"],     DEFAULT_TOPIC_SUB);
  copyValue(config.topic_pub,     sizeof(config.topic_pub),     doc["topic-pub"],     DEFAULT_TOPIC_PUB);
  copyValue(config.web_pwd,       sizeof(config.web_pwd),       doc["web-pwd"],       DEFAULT_WEB_PWD);
  if (config.mqtt_port == 0) config.mqtt_port = DEFAULT_MQTT_PORT;

  // Passwords are never written to the log.
  LOG("Config: wifi='%s' mqtt=%s:%u user='%s' uid='%s' sub='%s' pub='%s'",
      config.wifi_ssid, config.mqtt_server, config.mqtt_port, config.mqtt_username,
      config.device_uid, config.topic_sub, config.topic_pub);
}

bool configSave() {
  JsonDocument doc;
  doc["wifi-ssid"]     = config.wifi_ssid;
  doc["wifi-pwd"]      = config.wifi_pwd;
  doc["mqtt-server"]   = config.mqtt_server;
  doc["mqtt-port"]     = config.mqtt_port;
  doc["mqtt-username"] = config.mqtt_username;
  doc["mqtt-pwd"]      = config.mqtt_pwd;
  doc["device-uid"]    = config.device_uid;
  doc["topic-sub"]     = config.topic_sub;
  doc["topic-pub"]     = config.topic_pub;
  doc["web-pwd"]       = config.web_pwd;

  // Write to a temporary file first, so that a power loss while saving
  // never leaves a truncated configuration behind.
  File file = LittleFS.open(CONFIG_TMP, "w");
  if (!file) {
    LOG("Unable to create %s", CONFIG_TMP);
    return false;
  }
  size_t written = serializeJsonPretty(doc, file);
  file.close();
  if (written == 0) {
    LOG("Unable to write %s", CONFIG_TMP);
    LittleFS.remove(CONFIG_TMP);
    return false;
  }
  LittleFS.remove(CONFIG_FILE);
  if (!LittleFS.rename(CONFIG_TMP, CONFIG_FILE)) {
    LOG("Unable to rename %s", CONFIG_TMP);
    return false;
  }

  generateConfigPage();
  LOG("Configuration saved");
  return true;
}

bool configHasWifi() {
  // Template values such as "<YOUR WIFI SSID>" count as "not configured".
  return config.wifi_ssid[0] != '\0' && config.wifi_ssid[0] != '<';
}
