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

// Scan state
static bool          s_scanRunning = false;
static bool          s_scanFailed  = false;
static uint32_t      s_scanDoneAt  = 0;
static bool          s_scanHasData = false;
static NetScanResult s_scan[NET_SCAN_MAX];
static uint8_t       s_scanCount   = 0;

// "iotx-<uid>", lower case, letters/digits/hyphens only.
static void buildHostname() {
  snprintf(s_hostname, sizeof(s_hostname), "iotx-%s", config.device_uid);
  for (char *p = s_hostname; *p; p++) {
    *p = tolower((unsigned char)*p);
    if (!isalnum((unsigned char)*p) && *p != '-') *p = '-';
  }
}

static void startAP() {
  // AP+STA in every case: the station interface is needed to scan networks.
  WiFi.mode(WIFI_AP_STA);
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
    startAP();
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

static void scanLoop();

void netLoop() {
  scanLoop();
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
    startAP();
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

// ---------------------------------------------------------------------------
// WiFi scan
// ---------------------------------------------------------------------------
bool netScanStart() {
  if (s_scanRunning) return false;
  WiFi.scanNetworks(true, false);       // async, hidden networks excluded
  s_scanRunning = true;
  s_scanFailed  = false;
  LOG("WiFi scan started");
  return true;
}

// Collect the results of a completed scan: hidden networks are skipped,
// duplicates (same SSID on several access points) keep the strongest.
static void scanLoop() {
  if (!s_scanRunning) return;
  int n = WiFi.scanComplete();
  if (n == -1) return;                  // still running (WIFI_SCAN_RUNNING)
  s_scanRunning = false;
  if (n < 0) {                          // WIFI_SCAN_FAILED
    s_scanFailed = true;
    LOG("WiFi scan failed");
    WiFi.scanDelete();
    return;
  }

  s_scanCount = 0;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0 || ssid.length() > 32) continue;
    int8_t rssi = WiFi.RSSI(i);
#if defined(ESP8266)
    bool secure = WiFi.encryptionType(i) != ENC_TYPE_NONE;
#else
    bool secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
#endif
    int found = -1;
    for (uint8_t j = 0; j < s_scanCount; j++) {
      if (ssid.equals(s_scan[j].ssid)) { found = j; break; }
    }
    if (found >= 0) {
      if (rssi > s_scan[found].rssi) {
        s_scan[found].rssi = rssi;
        s_scan[found].channel = WiFi.channel(i);
        s_scan[found].secure = secure;
      }
      continue;
    }
    // Keep the strongest NET_SCAN_MAX networks.
    uint8_t slot = s_scanCount;
    if (s_scanCount == NET_SCAN_MAX) {
      uint8_t weakest = 0;
      for (uint8_t j = 1; j < s_scanCount; j++) if (s_scan[j].rssi < s_scan[weakest].rssi) weakest = j;
      if (rssi <= s_scan[weakest].rssi) continue;
      slot = weakest;
    } else {
      s_scanCount++;
    }
    strlcpy(s_scan[slot].ssid, ssid.c_str(), sizeof(s_scan[slot].ssid));
    s_scan[slot].rssi = rssi;
    s_scan[slot].channel = WiFi.channel(i);
    s_scan[slot].secure = secure;
  }
  WiFi.scanDelete();

  // Strongest first (insertion sort, at most NET_SCAN_MAX items).
  for (uint8_t i = 1; i < s_scanCount; i++) {
    NetScanResult item = s_scan[i];
    int j = i - 1;
    while (j >= 0 && s_scan[j].rssi < item.rssi) { s_scan[j + 1] = s_scan[j]; j--; }
    s_scan[j + 1] = item;
  }

  s_scanHasData = true;
  s_scanDoneAt  = millis();
  LOG("WiFi scan: %d access points, %u networks", n, s_scanCount);
}

bool     netScanRunning() { return s_scanRunning; }
bool     netScanFailed()  { return s_scanFailed; }
uint32_t netScanAge()     { return s_scanHasData ? millis() - s_scanDoneAt : UINT32_MAX; }
uint8_t  netScanCount()   { return s_scanCount; }
const NetScanResult &netScanResult(uint8_t i) { return s_scan[i < s_scanCount ? i : 0]; }
