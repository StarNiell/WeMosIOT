// web.cpp - web server.
//
// Serves the configuration page from LittleFS and receives the
// configuration form. Every request requires HTTP basic authentication
// (user WEB_USER, password config.web_pwd).
#include "web.h"

#include "config.h"
#include "defaults.h"
#include "log.h"
#include "platform.h"

static WebServerClass s_server(80);

static bool authenticate() {
  if (s_server.authenticate(WEB_USER, config.web_pwd)) return true;
  s_server.requestAuthentication(BASIC_AUTH, FW_NAME);
  return false;
}

static const char *contentType(const String &path) {
  if (path.endsWith(".html") || path.endsWith(".htm")) return "text/html";
  if (path.endsWith(".css"))  return "text/css";
  if (path.endsWith(".js"))   return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  if (path.endsWith(".ico"))  return "image/x-icon";
  return "text/plain";
}

static void sendNotFound() {
  s_server.send(404, "text/plain", "Page not found");
}

// Static files from LittleFS ("/" -> "/index.html"). The configuration
// store is never served.
static void handleStatic() {
  if (!authenticate()) return;

  String path = s_server.uri();
  if (path.endsWith("/")) path += "index.html";
  if (path.indexOf("..") >= 0 || path.startsWith("/store")) {
    sendNotFound();
    return;
  }

  String fsPath = path;
  if (LittleFS.exists(path + ".gz")) {
    fsPath += ".gz";
  } else if (!LittleFS.exists(path)) {
    sendNotFound();
    return;
  }

  File file = LittleFS.open(fsPath, "r");
  if (!file) {
    sendNotFound();
    return;
  }
  s_server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  // streamFile() adds "Content-Encoding: gzip" by itself for ".gz" files.
  s_server.streamFile(file, contentType(path));
  file.close();
}

// Copy a form field into a configuration buffer (only when present).
static void formValue(const char *name, char *dst, size_t size) {
  if (s_server.hasArg(name)) strlcpy(dst, s_server.arg(name).c_str(), size);
}

static void handleSaveConfig() {
  if (!authenticate()) return;

  formValue("wifi_ssid",     config.wifi_ssid,     sizeof(config.wifi_ssid));
  formValue("wifi_pwd",      config.wifi_pwd,      sizeof(config.wifi_pwd));
  formValue("mqtt_deviceid", config.device_uid,    sizeof(config.device_uid));
  formValue("mqtt_server",   config.mqtt_server,   sizeof(config.mqtt_server));
  formValue("mqtt_user",     config.mqtt_username, sizeof(config.mqtt_username));
  formValue("mqtt_pwd",      config.mqtt_pwd,      sizeof(config.mqtt_pwd));
  formValue("topic_sub",     config.topic_sub,     sizeof(config.topic_sub));
  formValue("topic_pub",     config.topic_pub,     sizeof(config.topic_pub));
  if (s_server.hasArg("mqtt_port")) {
    long port = s_server.arg("mqtt_port").toInt();
    config.mqtt_port = (port > 0 && port <= 65535) ? (uint16_t)port : DEFAULT_MQTT_PORT;
  }

  if (configSave()) {
    s_server.send(200, "text/plain", "IOT eXperience configuration saved! Please reboot the device.");
  } else {
    s_server.send(500, "text/plain", "Unable to save the IOT eXperience configuration! Please reboot the device and retry.");
  }
}

void webBegin() {
  s_server.on("/saveconfig", HTTP_POST, handleSaveConfig);
  s_server.onNotFound(handleStatic);
  s_server.begin();
  LOG("Web server started (user '%s')", WEB_USER);
}

void webLoop() {
  s_server.handleClient();
}
