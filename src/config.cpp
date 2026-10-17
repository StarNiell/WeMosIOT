// config.cpp - load / save / validation of the device configuration.
#include "config.h"

#include <LittleFS.h>

#include "defaults.h"
#include "log.h"

Config config;

static const char *CONFIG_FILE = "/store/config.json";
static const char *CONFIG_TMP  = "/store/config.tmp";

// Copy a JSON string value (or a default) into a fixed size buffer.
static void copyValue(char *dst, size_t size, JsonVariantConst src, const char *def) {
  strlcpy(dst, src | def, size);
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

  // Version 1 files have the same keys, minus "web-pwd": the defaults below
  // complete them, and the next save writes them as version 2.
  int version = doc["config-version"] | 1;

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
  LOG("Config v%d: wifi='%s' mqtt=%s:%u user='%s' uid='%s' sub='%s' pub='%s'",
      version, config.wifi_ssid, config.mqtt_server, config.mqtt_port, config.mqtt_username,
      config.device_uid, config.topic_sub, config.topic_pub);
}

bool configSave() {
  JsonDocument doc;
  doc["config-version"] = CONFIG_VERSION;
  doc["wifi-ssid"]      = config.wifi_ssid;
  doc["wifi-pwd"]       = config.wifi_pwd;
  doc["mqtt-server"]    = config.mqtt_server;
  doc["mqtt-port"]      = config.mqtt_port;
  doc["mqtt-username"]  = config.mqtt_username;
  doc["mqtt-pwd"]       = config.mqtt_pwd;
  doc["device-uid"]     = config.device_uid;
  doc["topic-sub"]      = config.topic_sub;
  doc["topic-pub"]      = config.topic_pub;
  doc["web-pwd"]        = config.web_pwd;

  if (!LittleFS.exists("/store")) LittleFS.mkdir("/store");

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
  LOG("Configuration saved");
  return true;
}

bool configErase() {
  bool ok = !LittleFS.exists(CONFIG_FILE) || LittleFS.remove(CONFIG_FILE);
  LOG("%s", ok ? "Configuration erased" : "Unable to erase the configuration");
  return ok;
}

bool configHasWifi() {
  // Template values such as "<YOUR WIFI SSID>" count as "not configured".
  return config.wifi_ssid[0] != '\0' && config.wifi_ssid[0] != '<';
}

void configToJson(JsonObject out) {
  out["config-version"] = CONFIG_VERSION;
  out["wifi-ssid"]      = configHasWifi() ? config.wifi_ssid : "";
  out["wifi-pwd-set"]   = config.wifi_pwd[0] != '\0';
  out["mqtt-server"]    = config.mqtt_server;
  out["mqtt-port"]      = config.mqtt_port;
  out["mqtt-username"]  = config.mqtt_username;
  out["mqtt-pwd-set"]   = config.mqtt_pwd[0] != '\0';
  out["device-uid"]     = config.device_uid;
  out["topic-sub"]      = config.topic_sub;
  out["topic-pub"]      = config.topic_pub;
  out["web-pwd-set"]    = true;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

// MQTT topic usable for publish/subscribe: no wildcards, no spaces,
// no leading or trailing '/'.
static bool validTopic(const char *t) {
  size_t n = strlen(t);
  if (n == 0 || n > 63 || t[0] == '/' || t[n - 1] == '/') return false;
  for (const char *p = t; *p; p++) {
    if (*p == '+' || *p == '#' || isspace((unsigned char)*p)) return false;
  }
  return true;
}

static bool validUid(const char *u) {
  size_t n = strlen(u);
  if (n == 0 || n > 15) return false;
  for (const char *p = u; *p; p++) {
    if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') return false;
  }
  return true;
}

// Read an optional string field; returns false if present but too long.
static bool readString(JsonObjectConst in, const char *key, char *dst, size_t size,
                       String &field, String &error) {
  JsonVariantConst v = in[key];
  if (v.isNull()) return true;
  if (!v.is<const char *>()) {
    field = key; error = "must be text";
    return false;
  }
  const char *s = v.as<const char *>();
  if (strlen(s) >= size) {
    field = key; error = "is too long (max " + String(size - 1) + " characters)";
    return false;
  }
  strlcpy(dst, s, size);
  return true;
}

bool configFromJson(JsonObjectConst in, String &field, String &error) {
  Config c = config;   // work on a copy: nothing changes unless all is valid
  char pwd[65];

  if (!readString(in, "wifi-ssid", c.wifi_ssid, sizeof(c.wifi_ssid), field, error)) return false;
  if (c.wifi_ssid[0] == '\0') {
    field = "wifi-ssid"; error = "Enter the name of the Wi-Fi network.";
    return false;
  }

  pwd[0] = '\0';
  if (!readString(in, "wifi-pwd", pwd, sizeof(pwd), field, error)) return false;
  if (pwd[0]) {
    if (strlen(pwd) < 8) {
      field = "wifi-pwd"; error = "A Wi-Fi password has at least 8 characters.";
      return false;
    }
    strlcpy(c.wifi_pwd, pwd, sizeof(c.wifi_pwd));
  }

  if (!readString(in, "mqtt-server", c.mqtt_server, sizeof(c.mqtt_server), field, error)) return false;
  if (!in["mqtt-port"].isNull()) {
    long port = in["mqtt-port"] | 0L;
    if (port < 1 || port > 65535) {
      field = "mqtt-port"; error = "Enter a port between 1 and 65535.";
      return false;
    }
    c.mqtt_port = (uint16_t)port;
  }
  if (!readString(in, "mqtt-username", c.mqtt_username, sizeof(c.mqtt_username), field, error)) return false;

  pwd[0] = '\0';
  if (!readString(in, "mqtt-pwd", pwd, sizeof(c.mqtt_pwd), field, error)) return false;
  if (pwd[0]) strlcpy(c.mqtt_pwd, pwd, sizeof(c.mqtt_pwd));
  if (c.mqtt_username[0] == '\0') c.mqtt_pwd[0] = '\0';   // no user, no password

  if (!readString(in, "device-uid", c.device_uid, sizeof(c.device_uid), field, error)) return false;
  if (!validUid(c.device_uid)) {
    field = "device-uid"; error = "Use 1 to 15 letters, digits, - or _.";
    return false;
  }
  if (!readString(in, "topic-sub", c.topic_sub, sizeof(c.topic_sub), field, error)) return false;
  if (!validTopic(c.topic_sub)) {
    field = "topic-sub"; error = "Enter a topic without spaces, + or #, not starting or ending with /.";
    return false;
  }
  if (!readString(in, "topic-pub", c.topic_pub, sizeof(c.topic_pub), field, error)) return false;
  if (!validTopic(c.topic_pub)) {
    field = "topic-pub"; error = "Enter a topic without spaces, + or #, not starting or ending with /.";
    return false;
  }

  pwd[0] = '\0';
  if (!readString(in, "web-pwd", pwd, sizeof(c.web_pwd), field, error)) return false;
  if (pwd[0]) {
    if (strlen(pwd) < 6) {
      field = "web-pwd"; error = "Use at least 6 characters.";
      return false;
    }
    strlcpy(c.web_pwd, pwd, sizeof(c.web_pwd));
  }

  config = c;
  return true;
}

void configRemoveLegacyFiles() {
  static const char *const legacy[] = {
    "/index.html", "/index.static", "/css/bootstrap.min.css", "/css/custom-style.css",
    "/js/index.js", "/js/jquery-3.5.1.min.js",
  };
  for (const char *path : legacy) {
    if (LittleFS.exists(path)) {
      LittleFS.remove(path);
      LOG("Removed old file %s", path);
    }
  }
}
