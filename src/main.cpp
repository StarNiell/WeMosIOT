// WeMosIOT - IOT eXperience device firmware
//
// Boards: WeMos D1 / D1 mini (ESP8266); ESP32 C series (experimental).
//
// Start-up:
//   1. both LEDs on for BOOT_WINDOW_MS: activating PIN_MODE in this window
//      starts the configuration access point (the web page can request the
//      same for the next boot: "Restart in access point mode");
//   2. otherwise the device joins the configured WiFi and MQTT broker.
//      If the WiFi is unreachable for WIFI_AP_FALLBACK_S seconds the
//      configuration access point is opened as well.
// The web server is always active.
//
// LEDs:
//   red    output state (steady on in configuration mode)
//   green  on = MQTT connected, slow blink = WiFi only,
//          fast blink = access point active, off = offline
#include <Arduino.h>

#include "config.h"
#include "defaults.h"
#include "devices.h"
#include "log.h"
#include "mqttlink.h"
#include "net.h"
#include "platform.h"
#include "web.h"

static uint32_t s_offlineSince = 0;

// Keep both LEDs on during the boot window and watch PIN_MODE.
static bool configModeRequested() {
  pinMode(PIN_MODE, platform::modePinMode(PIN_MODE_ACTIVE));
  digitalWrite(PIN_LED_GREEN, HIGH);
  digitalWrite(PIN_LED_RED, HIGH);

  bool requested = false;
  const uint32_t start = millis();
  while (millis() - start < BOOT_WINDOW_MS) {
    if (digitalRead(PIN_MODE) == PIN_MODE_ACTIVE) requested = true;
    delay(10);
  }

  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  return requested;
}

static void updateLeds() {
  const uint32_t now = millis();
  bool green;
  if (netMode() == NetMode::ConfigAP) {
    digitalWrite(PIN_LED_RED, HIGH);
    green = (now / 125) % 2;                 // fast blink
  } else if (mqttConnected()) {
    green = true;
  } else if (netApActive()) {
    green = (now / 125) % 2;                 // fast blink
  } else if (netWifiConnected()) {
    green = (now / 500) % 2;                 // slow blink
  } else {
    green = false;
  }
  digitalWrite(PIN_LED_GREEN, green ? HIGH : LOW);
}

// Safety net inherited from the previous firmware: restart the device when
// it stays offline too long, unless someone is using the access point.
static void restartGuard() {
  if (OFFLINE_RESTART_S == 0 || netMode() != NetMode::Station) return;

  const bool online = netWifiConnected() && (config.mqtt_server[0] == '\0' || mqttConnected());
  if (online || netApClients() > 0) {
    s_offlineSince = 0;
    return;
  }
  if (s_offlineSince == 0) s_offlineSince = millis();
  if (millis() - s_offlineSince > OFFLINE_RESTART_S * 1000UL) {
    LOG("Offline for %lu s: restarting", OFFLINE_RESTART_S);
    delay(100);
    ESP.restart();
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);
  LOG("");
  LOG("%s v%s (%s, chip %06X)", FW_NAME, FW_VERSION, PLATFORM_NAME, (unsigned)platform::chipId());

  devicesBegin();
  bool configMode = configModeRequested();

  if (!platform::fsBegin()) LOG("LittleFS mount failed");
  if (configTakeApBootRequest()) {
    LOG("Access point mode requested from the web page");
    configMode = true;
  }
  configRemoveLegacyFiles();
  configLoad();

  netBegin(configMode);
  webBegin();
  if (netMode() == NetMode::Station) {
    mqttBegin(devicesQueueCommand, devicesOnMqttConnected);
  }
}

void loop() {
  netLoop();
  webLoop();
  if (netMode() == NetMode::Station) {
    mqttLoop();
    devicesLoop();
  }
  updateLeds();
  restartGuard();
  delay(1);
}
