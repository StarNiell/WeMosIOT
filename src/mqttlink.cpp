// mqttlink.cpp - connection to the MQTT broker.
//
// The connection is retried every MQTT_RETRY_MS without blocking the
// loop, so the web server keeps answering while the broker is down.
#include "mqttlink.h"

#include <ArduinoJson.h>
#include <PubSubClient.h>

#include "config.h"
#include "defaults.h"
#include "log.h"
#include "net.h"
#include "platform.h"

static WiFiClient   s_net;
static PubSubClient s_client(s_net);
static MqttCommandHandler   s_onCommand   = nullptr;
static MqttConnectedHandler s_onConnected = nullptr;
static uint32_t s_lastAttempt = 0;
static bool     s_attempted   = false;

static const size_t MQTT_BUFFER_SIZE  = 512;
static const size_t MAX_COMMAND_SIZE  = 256;

static void onMessage(char *topic, byte *payload, unsigned int length) {
  (void)topic;
  if (length == 0 || length >= MAX_COMMAND_SIZE) {
    LOG("MQTT: message ignored (%u bytes)", length);
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, (const char *)payload, length);
  if (err) {
    LOG("MQTT: invalid JSON command (%s)", err.c_str());
    return;
  }

  const char *deviceId = doc["device_id"] | "";
  long value = doc["value"] | -1L;
  if (deviceId[0] == '\0') return;

  LOG("MQTT: command device_id='%s' value=%ld", deviceId, value);
  // The handler only queues the command: publishing from inside the
  // PubSubClient callback is not safe.
  if (s_onCommand) s_onCommand(deviceId, value);
}

void mqttBegin(MqttCommandHandler onCommand, MqttConnectedHandler onConnected) {
  s_onCommand   = onCommand;
  s_onConnected = onConnected;
  s_client.setBufferSize(MQTT_BUFFER_SIZE);
  s_client.setSocketTimeout(5);
  s_client.setServer(config.mqtt_server, config.mqtt_port);
  s_client.setCallback(onMessage);
}

static void tryConnect() {
  s_lastAttempt = millis();
  s_attempted   = true;

  const char *user = config.mqtt_username[0] ? config.mqtt_username : nullptr;
  const char *pwd  = config.mqtt_username[0] ? config.mqtt_pwd      : nullptr;

  // The client id is the device uid, as in the previous firmware.
  if (s_client.connect(config.device_uid, user, pwd)) {
    LOG("MQTT: connected to %s:%u as '%s'", config.mqtt_server, config.mqtt_port, config.device_uid);
    s_client.subscribe(config.topic_sub);
    LOG("MQTT: subscribed to '%s'", config.topic_sub);
    if (s_onConnected) s_onConnected();
  } else {
    LOG("MQTT: connection to %s:%u failed (state %d)", config.mqtt_server, config.mqtt_port, s_client.state());
  }
}

void mqttLoop() {
  if (config.mqtt_server[0] == '\0' || !netWifiConnected()) return;

  if (!s_client.connected()) {
    if (!s_attempted || millis() - s_lastAttempt >= MQTT_RETRY_MS) tryConnect();
    return;
  }
  s_client.loop();
}

bool mqttConnected() { return s_client.connected(); }

bool mqttPublish(const char *topic, const char *payload, bool retained) {
  if (!s_client.connected()) return false;
  bool ok = s_client.publish(topic, payload, retained);
  LOG("MQTT: %s %s %s", ok ? "published" : "PUBLISH FAILED", topic, payload);
  return ok;
}
