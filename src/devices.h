// devices.h - the device functions (switch, button, sensor, contact,
// infrared) and the MQTT status messages.
#pragma once
#include <Arduino.h>

void devicesBegin();          // configure the pins
void devicesLoop();           // commands, pulses, contact, periodic status
void devicesOnMqttConnected();// send the status of every enabled function

// Queue a command received from MQTT (executed in devicesLoop()).
void devicesQueueCommand(const char *deviceId, long value);

bool devicesOutputState();
