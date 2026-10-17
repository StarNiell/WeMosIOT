// net.h - WiFi management: station mode, configuration access point,
// automatic fallback access point, host name and mDNS, time sync.
#pragma once
#include <Arduino.h>

enum class NetMode : uint8_t {
  Station,    // normal operation: WiFi + MQTT (+ fallback AP when needed)
  ConfigAP    // configuration only: access point, no MQTT
};

// Start the network. When configAP is true, or no WiFi is configured,
// the device starts in configuration (access point) mode.
void netBegin(bool configAP);

// Call from loop(): handles reconnection and the fallback access point.
void netLoop();

NetMode     netMode();
bool        netWifiConnected();
bool        netApActive();
uint8_t     netApClients();
String      netIp();         // station IP, or access point IP
const char *netHostname();   // e.g. "iotx-999"
