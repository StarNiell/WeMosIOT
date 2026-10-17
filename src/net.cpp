// net.cpp - WiFi management.
//
// Station mode never blocks: the connection is handled by the WiFi stack
// and checked in netLoop(). If the network is not reachable for
// WIFI_AP_FALLBACK_S seconds, the configuration access point is opened
// as well (AP+STA), so the device is always reachable from a browser.
#include "net.h"

#include <time.h>

#include "config.h"
#include "defaults.h"
#include "log.h"
#include "platform.h"

static NetMode  s_mode           = NetMode::Station;
static bool     s_apActive       = false;
static bool     s_connected      = false;
static bool     s_staRetrying    = false;
static bool     s_mdnsStarted    = false;
static uint32_t s_lostSince      = 0;   // millis() when the WiFi was lost
static uint32_t s_connectedSince = 0;   // millis() when the WiFi came back
static uint32_t s_lastBegin      = 0;   // millis() of the last WiFi.begin()
static char     s_hostname[32]   = "";

// "iotx-<uid>", lower case, letters/digits/hyphens only.
static void buildHostname() {
  snprintf(s_hostname, sizeof(s_hostname), "iotx-%s", config.device_uid);
  for (char *p = s_hostname; *p; p++) {
    *p = tolower((unsigned char)*p);
    if (!isalnum((unsigned char)*p) && *p != '-') *p = '-';
  }
}

static void startAP(bool keepStation) {
  WiFi.mode(keepStation ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PWD);
  s_apActive = true;
  LOG("Access point '%s' started, IP %s", AP_SSID, WiFi.softAPIP().toString().c_str());
}

static void stopAP() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  s_apActive = false;
  LOG("Access point closed");
}

static void beginStation() {
  WiFi.begin(config.wifi_ssid, config.wifi_pwd);
  s_lastBegin   = millis();
  s_staRetrying = true;
}

void netBegin(bool configAP) {
  buildHostname();
  WiFi.persistent(false);          // never write WiFi credentials to flash

  if (configAP || !configHasWifi()) {
    s_mode = NetMode::ConfigAP;
    LOG("%s", configAP ? "Configuration mode requested" : "No WiFi configured: configuration mode");
    startAP(false);
    return;
  }

  s_mode = NetMode::Station;
  WiFi.mode(WIFI_STA);
  platform::setHostname(s_hostname);
  WiFi.setAutoReconnect(true);
  LOG("Connecting to WiFi '%s'...", config.wifi_ssid);
  beginStation();
  s_lostSince = millis();
}

static void onConnected() {
  LOG("WiFi connected, IP %s, RSSI %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  if (!s_mdnsStarted) {
    if (MDNS.begin(s_hostname)) {
      MDNS.addService("http", "tcp", 80);
      s_mdnsStarted = true;
      LOG("mDNS: http://%s.local", s_hostname);
    }
  }
  // Time sync (UTC), used for the MQTT timestamps. Non blocking.
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);
}

void netLoop() {
  if (s_mode != NetMode::Station) return;

  const uint32_t now = millis();
  const bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected != s_connected) {
    s_connected = connected;
    if (connected) {
      s_connectedSince = now;
      s_staRetrying    = false;
      onConnected();
    } else {
      s_lostSince = now;
      LOG("WiFi connection lost");
    }
  }

  if (s_mdnsStarted) platform::mdnsUpdate();

  if (connected) {
    // WiFi is back: close the fallback AP once nobody is using it.
    if (s_apActive && now - s_connectedSince > AP_CLOSE_AFTER_MS && WiFi.softAPgetStationNum() == 0) {
      stopAP();
    }
    return;
  }

  // Not connected: open the fallback AP after WIFI_AP_FALLBACK_S.
  if (!s_apActive && now - s_lostSince > WIFI_AP_FALLBACK_S * 1000UL) {
    LOG("No WiFi for %lu s: opening the configuration access point", WIFI_AP_FALLBACK_S);
    startAP(true);
  }

  if (s_apActive) {
    // While the station is searching for its network the radio hops between
    // channels and the AP becomes unusable: pause the retries while a
    // client is connected to the AP, retry periodically otherwise.
    if (WiFi.softAPgetStationNum() > 0) {
      if (s_staRetrying) {
        WiFi.disconnect(false);
        s_staRetrying = false;
        LOG("Client on the access point: WiFi retries paused");
      }
    } else if (!s_staRetrying || now - s_lastBegin > WIFI_RETRY_MS) {
      beginStation();
    }
  }
}

NetMode netMode()          { return s_mode; }
bool    netWifiConnected() { return s_mode == NetMode::Station && s_connected; }
bool    netApActive()      { return s_apActive; }
uint8_t netApClients()     { return s_apActive ? WiFi.softAPgetStationNum() : 0; }

String netIp() {
  if (netWifiConnected()) return WiFi.localIP().toString();
  if (s_apActive)         return WiFi.softAPIP().toString();
  return String();
}

const char *netHostname() { return s_hostname; }
