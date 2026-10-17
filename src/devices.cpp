// devices.cpp - device functions and status messages.
//
// MQTT contract (unchanged, compatible with IotxApp):
//   publish  <topic_pub>/<type>-<uid>   retained JSON status
//   receive  <topic_sub>                {"device_id": "<type>-<uid>", "value": n}
#include "devices.h"

#include <DHTesp.h>
#include <time.h>

#include "config.h"
#include "defaults.h"
#include "log.h"
#include "mqttlink.h"
#include "net.h"

static const char *TYPE_SWITCH  = "switch";
static const char *TYPE_BUTTON  = "button";
static const char *TYPE_SENSOR  = "sensor";
static const char *TYPE_CONTACT = "contact";
static const char *TYPE_INFRAR  = "infrar";

static DHTesp   s_dht;
static uint32_t s_statusIntervalS = STATUS_INTERVAL_S;
static uint32_t s_lastStatus      = 0;
static bool     s_lastContactHigh = true;
static uint32_t s_buttonPulseEnd  = 0;   // 0 = no pulse running
static uint32_t s_infrarPulseEnd  = 0;

// ---------------------------------------------------------------------------
// Command queue (filled by the MQTT callback, drained by devicesLoop())
// ---------------------------------------------------------------------------
struct Command {
  char type[12];
  long value;
};
static const uint8_t QUEUE_SIZE = 4;
static Command s_queue[QUEUE_SIZE];
static uint8_t s_queueHead = 0, s_queueCount = 0;

void devicesQueueCommand(const char *deviceId, long value) {
  // device_id must be exactly "<type>-<uid>" for this device.
  const size_t idLen  = strlen(deviceId);
  const size_t uidLen = strlen(config.device_uid);
  if (idLen < uidLen + 2) return;
  const size_t typeLen = idLen - uidLen - 1;
  if (deviceId[typeLen] != '-' || strcmp(deviceId + typeLen + 1, config.device_uid) != 0) return;
  if (typeLen >= sizeof(Command::type)) return;

  if (s_queueCount == QUEUE_SIZE) {
    LOG("Command queue full: command dropped");
    return;
  }
  Command &cmd = s_queue[(s_queueHead + s_queueCount) % QUEUE_SIZE];
  memcpy(cmd.type, deviceId, typeLen);
  cmd.type[typeLen] = '\0';
  cmd.value = value;
  s_queueCount++;
}

// ---------------------------------------------------------------------------
// Status messages
// ---------------------------------------------------------------------------
static void timestamp(char *buf, size_t size) {
  time_t now = time(nullptr);
  if (now < 1700000000) {                       // clock not synchronised yet
    strlcpy(buf, "0000-00-00T00:00:00Z", size);
    return;
  }
  struct tm tmUtc;
  gmtime_r(&now, &tmUtc);
  strftime(buf, size, "%Y-%m-%dT%H:%M:%SZ", &tmUtc);
}

static void publishState(const char *type, float value, const char *unit, int decimals,
                         float value2 = 0.0f, const char *unit2 = "", int decimals2 = 0) {
  char deviceId[32];
  snprintf(deviceId, sizeof(deviceId), "%s-%s", type, config.device_uid);

  char topic[128];
  snprintf(topic, sizeof(topic), "%s/%s", config.topic_pub, deviceId);

  char ts[24], v1[16], v2[16];
  timestamp(ts, sizeof(ts));
  dtostrf(value, 1, decimals, v1);
  dtostrf(value2, 1, decimals2, v2);

  char payload[384];
  snprintf(payload, sizeof(payload),
           "{\"device_id\": \"%s\", \"timestamp\": \"%s\", \"type\": \"%s\", "
           "\"value\": %s, \"unit\": \"%s\", \"value2\": %s, \"unit2\": \"%s\", "
           "\"subscription\": \"%s\", \"status\": \"ok\", \"ip\": \"%s\"}",
           deviceId, ts, type, v1, unit, v2, unit2, config.topic_sub, netIp().c_str());

  mqttPublish(topic, payload, true);
}

// ---------------------------------------------------------------------------
// Output (shared by switch and button)
// ---------------------------------------------------------------------------
static void setOutput(bool on) {
  digitalWrite(PIN_OUTPUT, on ? HIGH : LOW);
  digitalWrite(PIN_LED_RED, on ? HIGH : LOW);
}

bool devicesOutputState() { return digitalRead(PIN_OUTPUT) == HIGH; }

// ---------------------------------------------------------------------------
// Sensor
// ---------------------------------------------------------------------------
static void publishSensor() {
  float t, h;
  if (CFG_FAKE_SENSOR) {
    t = random(0, 600) / 10.0f;
    h = random(0, 1000) / 10.0f;
  } else {
    TempAndHumidity th = s_dht.getTempAndHumidity();
    t = isnan(th.temperature) ? 0.0f : th.temperature;
    h = isnan(th.humidity)    ? 0.0f : th.humidity;
  }
  publishState(TYPE_SENSOR, t, "°C", 1, h, "%RH", 1);
}

// ---------------------------------------------------------------------------
// Contact (pin LOW = closed = 1)
// ---------------------------------------------------------------------------
static void publishContact() {
  s_lastContactHigh = digitalRead(PIN_CONTACT) == HIGH;
  publishState(TYPE_CONTACT, s_lastContactHigh ? 0 : 1, "state", 0);
}

// ---------------------------------------------------------------------------
// Command execution
// ---------------------------------------------------------------------------
static void execute(const Command &cmd) {
  if (CFG_SWITCH_ENABLED && strcmp(cmd.type, TYPE_SWITCH) == 0) {
    if (cmd.value < 0) return;
    setOutput(cmd.value > 0);
    LOG("Switch %s", cmd.value > 0 ? "ON" : "OFF");
    publishState(TYPE_SWITCH, cmd.value > 0 ? 1 : 0, "state", 0);
  } else if (CFG_BUTTON_ENABLED && strcmp(cmd.type, TYPE_BUTTON) == 0) {
    setOutput(true);
    LOG("Button pulse started");
    publishState(TYPE_BUTTON, 1, "state", 0);
    s_buttonPulseEnd = millis() + PULSE_MS;
    if (s_buttonPulseEnd == 0) s_buttonPulseEnd = 1;
  } else if (CFG_SENSOR_ENABLED && strcmp(cmd.type, TYPE_SENSOR) == 0) {
    if (cmd.value > 1) {
      s_statusIntervalS = cmd.value;
      LOG("Status interval set to %lu s", (unsigned long)s_statusIntervalS);
    }
    publishSensor();
  } else if (CFG_INFRARED_ENABLED && strcmp(cmd.type, TYPE_INFRAR) == 0) {
    // Not implemented: only the feedback is sent.
    publishState(TYPE_INFRAR, 1, "state", 0);
    s_infrarPulseEnd = millis() + PULSE_MS;
    if (s_infrarPulseEnd == 0) s_infrarPulseEnd = 1;
  }
}

// ---------------------------------------------------------------------------
// Public functions
// ---------------------------------------------------------------------------
void devicesBegin() {
  pinMode(PIN_OUTPUT, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  setOutput(false);
  if (CFG_CONTACT_ENABLED) {
    pinMode(PIN_CONTACT, INPUT_PULLUP);
    s_lastContactHigh = digitalRead(PIN_CONTACT) == HIGH;
  }
  if (CFG_SENSOR_ENABLED && !CFG_FAKE_SENSOR) {
    s_dht.setup(PIN_SENSOR, DHTesp::AUTO_DETECT);
  }
}

void devicesOnMqttConnected() {
  if (CFG_SWITCH_ENABLED)  publishState(TYPE_SWITCH, devicesOutputState() ? 1 : 0, "state", 0);
  if (CFG_BUTTON_ENABLED)  publishState(TYPE_BUTTON, devicesOutputState() ? 1 : 0, "state", 0);
  if (CFG_SENSOR_ENABLED)  publishSensor();
  if (CFG_CONTACT_ENABLED) publishContact();
  s_lastStatus = millis();
}

void devicesLoop() {
  const uint32_t now = millis();

  // Commands received from MQTT
  while (s_queueCount > 0) {
    Command cmd = s_queue[s_queueHead];
    s_queueHead = (s_queueHead + 1) % QUEUE_SIZE;
    s_queueCount--;
    execute(cmd);
  }

  // End of the button pulse
  if (s_buttonPulseEnd && (int32_t)(now - s_buttonPulseEnd) >= 0) {
    s_buttonPulseEnd = 0;
    setOutput(false);
    LOG("Button pulse ended");
    publishState(TYPE_BUTTON, 0, "state", 0);
  }

  // End of the infrared feedback
  if (s_infrarPulseEnd && (int32_t)(now - s_infrarPulseEnd) >= 0) {
    s_infrarPulseEnd = 0;
    publishState(TYPE_INFRAR, 0, "state", 0);
  }

  // Contact change
  if (CFG_CONTACT_ENABLED) {
    bool high = digitalRead(PIN_CONTACT) == HIGH;
    if (high != s_lastContactHigh) publishContact();
  }

  // Periodic status
  if (mqttConnected() && now - s_lastStatus >= s_statusIntervalS * 1000UL) {
    s_lastStatus = now;
    if (CFG_SENSOR_ENABLED)  publishSensor();
    if (CFG_CONTACT_ENABLED) publishContact();
    if (CFG_SWITCH_ENABLED)  publishState(TYPE_SWITCH, devicesOutputState() ? 1 : 0, "state", 0);
    if (CFG_BUTTON_ENABLED)  publishState(TYPE_BUTTON, devicesOutputState() ? 1 : 0, "state", 0);
  }
}
