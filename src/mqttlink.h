// mqttlink.h - connection to the MQTT broker (non blocking).
#pragma once
#include <Arduino.h>

// Called for every valid command addressed to the commands topic.
// deviceId: "<type>-<uid>"; value: -1 when missing.
using MqttCommandHandler = void (*)(const char *deviceId, long value);
using MqttConnectedHandler = void (*)();

void mqttBegin(MqttCommandHandler onCommand, MqttConnectedHandler onConnected);
void mqttLoop();
bool mqttConnected();
bool mqttPublish(const char *topic, const char *payload, bool retained = true);
