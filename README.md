# WeMosIOT
## IOT eXperience – device firmware

Firmware for the devices of the **IOT eXperience** home automation ecosystem. Devices join the WiFi network and talk to the server **only through MQTT**. Each device serves its own web page for configuration.

These devices were built together with **[IoteXperience](https://github.com/StarNiell/IoteXperience)**, the self-hosted home automation system they work with: web app, API server and MQTT broker. Its app shows and controls the switches, buttons, sensors and contacts driven by this firmware.

> **Project status**: the firmware is being refactored in steps (see [Roadmap](#roadmap)). This document describes the **current** firmware and is updated at every step.

---

## Supported hardware

| Board | PlatformIO env | Status |
|---|---|---|
| WeMos D1 / D1 mini (ESP8266, 4 MB flash) | `d1_mini` | in service |
| ESP32-C3 (generic C series profile) | `esp32c3` | experimental, builds, not tested on hardware |
| ESP32-C6 (generic C series profile) | `esp32c6` | experimental, builds, not tested on hardware |

The source tree is the same for every board; the differences between the ESP8266 and ESP32 cores are isolated in `src/platform.h`. Each chip family needs its own binary.

### Pins

Pins are defined in `src/defaults.h` and can be overridden per environment from `platformio.ini` (see [Build options](#build-options)).

| Function | D1 mini | GPIO | ESP32-C (provisional) | Notes |
|---|---|---|---|---|
| Output (relay) | D2 | 4 | 6 | used by *switch* and *button* |
| Contact input | D3 | 0 | 7 | pull-up; D3 is a boot pin on ESP8266 |
| DHT sensor | D4 | 2 | 3 | DHT11/DHT22 auto-detected; D4 is a boot pin on ESP8266 |
| Local push button | D5 | 14 | 10 | reserved, not used yet |
| Red LED | D0 | 16 | 0 | output state / configuration mode |
| Green LED | D1 | 5 | 1 | network state |
| Configuration mode | D8 | 15 | 9 (BOOT button) | D8: active HIGH (3.3 V); ESP32-C: active LOW |

## Functions

Functions are still selected **at compile time** (see [Build options](#build-options)); they will move to the web configuration in a later step. The web page shows which functions are active.

| Option | MQTT type | Default | Description |
|---|---|---|---|
| `CFG_SWITCH_ENABLED` | `switch` | on | on/off output |
| `CFG_BUTTON_ENABLED` | `button` | on | 1 s pulse on the output |
| `CFG_SENSOR_ENABLED` | `sensor` | off | temperature (°C) and humidity (%RH) from a DHT |
| `CFG_CONTACT_ENABLED` | `contact` | off | dry contact state (door, window...) |
| `CFG_INFRARED_ENABLED` | `infrar` | off | placeholder, not implemented |
| `CFG_FAKE_SENSOR` | – | off | random sensor values, for testing |

## Operating modes

### Normal operation
1. At power on both LEDs stay on for 2 seconds (configuration window).
2. The device joins the configured WiFi network and MQTT broker. Connections are retried in the background: the device and its web page stay responsive while the broker is unreachable.
3. If the WiFi is unreachable for 60 s, the configuration access point is opened **as well**; it closes again by itself once the WiFi is back.
4. If the device stays offline (WiFi or MQTT) for 10 minutes and nobody is connected to its access point, it restarts.

### Configuration mode (access point only)
Entered when:
- the configuration pin is activated during the 2 s window at power on (D1 mini: connect D8 to 3.3 V; ESP32-C: press BOOT), or
- **Restart in access point mode** is pressed on the web page, or
- no WiFi network has been configured yet.

The device creates the WiFi network `WD1R2-REMOTE` (password `12345678`) and does not connect to MQTT. Its station interface stays on, so that the configuration page can list the networks in range.

### LEDs

| LED | State | Meaning |
|---|---|---|
| Red | on/off | output state |
| Red | steady on | configuration mode |
| Green | on | connected to the MQTT broker |
| Green | slow blink | WiFi connected, MQTT not connected |
| Green | fast blink | access point active |
| Green | off | offline |

## Web configuration

The configuration page is **always available**:

| Situation | Address |
|---|---|
| Normal operation | `http://<device IP>/` or `http://iotx-<device-uid>.local/` (mDNS) |
| Access point | `http://192.168.4.1/` |

Type `http://` explicitly: browsers that try `https://` first will not reach the device.

Every page requires a login. The user is `admin`; the default password is `iotxadmin`, which can be changed from the page itself (section *Page password*).

The page shows the state of the device (with the two LEDs as they are on the board) and lets you change:

- the Wi-Fi network, chosen from the networks in range or typed by hand (hidden networks);
- the MQTT broker;
- device ID and topics;
- the password of the page.

**Save and restart** validates the values, saves them and restarts the device. Password fields left empty keep the stored password, with one exception: when you choose a different Wi-Fi network and leave its password empty, the network is treated as open.

The list of networks is filled automatically when the device has no working Wi-Fi (first configuration, access point); otherwise press **Find networks**. Each network shows its signal, whether it is secured, and which one the device is connected to.

The page also offers three device actions:

| Action | Effect |
|---|---|
| **Restart** | normal restart; in configuration mode the button reads *Restart in normal mode* |
| **Restart in access point mode** | the same as activating the configuration pin at power on: nothing is erased, and the device stays in configuration mode until you save or press *Restart in normal mode* |
| **Reset to factory settings** | erases the configuration; the device restarts as an access point |

The page (`web/index.html`) is compressed and **embedded in the firmware** by `tools/embed_web.py`, which PlatformIO runs before every build. Flashing the firmware updates the page; the configuration on LittleFS is not touched.

### JSON API

All endpoints require the same login as the page.

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/status` | device state: firmware, uptime, free memory, Wi-Fi, access point, MQTT, active functions, output |
| `GET` | `/api/config` | configuration; passwords are never returned, `<name>-set` tells whether one is stored |
| `POST` | `/api/config` | JSON body with the keys to change; validated, saved, then the device restarts |
| `POST` | `/api/restart` | restart the device; `?mode=ap` restarts it in configuration (access point) mode |
| `POST` | `/api/reset` | erase the configuration and restart |
| `GET` | `/api/scan` | Wi-Fi networks in range, strongest first; `?refresh=1` starts a new scan. While `"scanning": true`, ask again after a second or two |

Errors return `{"ok": false, "error": "<message>", "field": "<key>"}` with HTTP status 400 or 500.

### Stored configuration

The configuration is stored on LittleFS in `/store/config.json` (never served by the web server):

| Key | Description | Default |
|---|---|---|
| `config-version` | format version (current: 2) | – |
| `wifi-ssid`, `wifi-pwd` | WiFi network | – |
| `mqtt-server`, `mqtt-port` | MQTT broker (empty server = no MQTT) | –, `1883` |
| `mqtt-username`, `mqtt-pwd` | broker credentials (optional) | – |
| `device-uid` | device identifier: 1–15 letters, digits, `-`, `_` | `999` |
| `topic-sub` | commands topic | `iotx/in` |
| `topic-pub` | root of the status topics | `iotx/out` |
| `web-pwd` | password of the web page | `iotxadmin` |

Files written by firmware 1.0 (no `config-version`) are read as they are and converted at the next save. The files of the old page (`index.html`, `index.static`, `css/`, `js/`) are removed from LittleFS at start-up.

`data/store/config.json` in this repository is only a template: **never commit real credentials**.

## MQTT protocol

### Messages published by the device
Topic: `<topic-pub>/<type>-<device-uid>` (e.g. `iotx/out/switch-999`), **retained**.

```json
{
  "device_id": "switch-999",
  "timestamp": "2026-10-17T10:00:00Z",
  "type": "switch",
  "value": 1,
  "unit": "state",
  "value2": 0,
  "unit2": "",
  "subscription": "iotx/in",
  "status": "ok",
  "ip": "192.168.1.50"
}
```

| Field | Notes |
|---|---|
| `timestamp` | UTC time from NTP, ISO 8601. It is `0000-00-00T00:00:00Z` until the clock is synchronised. |
| `subscription` | topic on which the device receives its commands |
| `ip` | IP address of the device, to reach its web page |
| `value`, `value2` | for `sensor`: temperature (`°C`) and humidity (`%RH`) |

The state of every enabled function is published when the device connects to the broker, and then periodically (default every 60 s).

### Commands received by the device
Topic: `<topic-sub>` (e.g. `iotx/in`). `device_id` must match `<type>-<device-uid>` exactly.

| Command | Payload |
|---|---|
| Switch on / off | `{"device_id": "switch-999", "value": 1}` / `"value": 0` |
| Button pulse | `{"device_id": "button-999"}` |
| Immediate sensor reading | `{"device_id": "sensor-999"}` |
| Reading + new status interval (s) | `{"device_id": "sensor-999", "value": 300}` |

## Build and upload (VS Code + PlatformIO)

1. Install VS Code and the PlatformIO IDE extension, then open this folder.
2. Libraries are installed automatically:
   - ArduinoJson 7
   - PubSubClient 2.8
   - DHT sensor library for ESPx
3. Build and flash:

| Command | Action |
|---|---|
| `pio run -e d1_mini` | build (also embeds the web page) |
| `pio run -e d1_mini -t upload` | flash the firmware: this is all an update needs |
| `pio run -e d1_mini -t uploadfs` | flash the LittleFS image built from `data/` |
| `pio device monitor` | serial monitor (115200 baud) |

`uploadfs` **replaces the stored configuration** with the template in `data/`. Use it only on a new device, never for updates.

The `d1_mini` environment uses the same flash layout as the Arduino IDE default (FS 2 MB). The configuration already stored on existing devices is therefore preserved.

## Build options

Every option below is defined in `src/defaults.h` and can be overridden per environment in `platformio.ini`:

```ini
build_flags =
    -DCFG_SENSOR_ENABLED=1
    -DDEFAULT_WEB_PWD=\"mypassword\"
```

Write each flag without spaces around `=`; text values need escaped quotes (`\"...\"`).

### Functions (`1` on, `0` off)

| Flag | Default | Description |
|---|---|---|
| `CFG_SWITCH_ENABLED` | 1 | on/off output |
| `CFG_BUTTON_ENABLED` | 1 | 1 s pulse on the output |
| `CFG_SENSOR_ENABLED` | 0 | DHT temperature / humidity |
| `CFG_FAKE_SENSOR` | 0 | random sensor values, for testing |
| `CFG_CONTACT_ENABLED` | 0 | contact input |
| `CFG_INFRARED_ENABLED` | 0 | placeholder, not implemented |

### Pins (GPIO numbers)

| Flag | D1 mini | ESP32-C |
|---|---|---|
| `PIN_OUTPUT` | 4 (D2) | 6 |
| `PIN_CONTACT` | 0 (D3) | 7 |
| `PIN_SENSOR` | 2 (D4) | 3 |
| `PIN_MANUAL` | 14 (D5) | 10 |
| `PIN_LED_RED` | 16 (D0) | 0 |
| `PIN_LED_GREEN` | 5 (D1) | 1 |
| `PIN_MODE` | 15 (D8) | 9 |
| `PIN_MODE_ACTIVE` | `HIGH` | `LOW` |

On the WeMos D1 in Arduino UNO format the silkscreen differs from the D1 mini: GPIO4 is marked D4/D14 and GPIO16 is marked D2.

### Defaults of the configuration

Used only when `/store/config.json` lacks the value:

| Flag | Default |
|---|---|
| `DEFAULT_MQTT_SERVER` | `""` |
| `DEFAULT_MQTT_PORT` | `1883` |
| `DEFAULT_DEVICE_UID` | `"999"` |
| `DEFAULT_TOPIC_SUB` | `"iotx/in"` |
| `DEFAULT_TOPIC_PUB` | `"iotx/out"` |
| `WEB_USER` | `"admin"` |
| `DEFAULT_WEB_PWD` | `"iotxadmin"` |
| `AP_SSID` | `"WD1R2-REMOTE"` |
| `AP_PWD` | `"12345678"` (at least 8 characters) |

Timings (`WIFI_AP_FALLBACK_S`, `STATUS_INTERVAL_S`, `OFFLINE_RESTART_S`...) are set directly in `src/defaults.h`.

## Repository layout

```
platformio.ini          build environments
src/
  main.cpp              start-up, main loop, LEDs, restart guard
  defaults.h            compile-time defaults: functions, pins, timings
  platform.h            ESP8266 / ESP32 abstraction
  config.h/.cpp         configuration load/save
  net.h/.cpp            WiFi, access point, mDNS, time sync
  mqttlink.h/.cpp       MQTT connection and commands
  devices.h/.cpp        device functions and status messages
  web.h/.cpp            web server and JSON API
  web_assets.h          generated from web/index.html (not in git)
web/index.html          configuration page (embedded in the firmware)
tools/embed_web.py      compresses the page into src/web_assets.h
data/store/config.json  configuration template for new devices (uploadfs)
```

## Roadmap

| Step | Content | Status |
|---|---|---|
| 0 | Minimal fixes (compile error, invalid `config.json`, credentials removed) | done |
| 1 | PlatformIO, modular structure, non-blocking loop, web server always on, automatic access point, mDNS, IP in the payload | done |
| 2 | JSON API and new configuration page (password editable, automatic restart) | done |
| 3 | WiFi network selection from a scan | **done** |
| 4 | Functions and pins configurable from the web page ("Advanced" section) | |
| 5 | Smart features (see below) | |
| 6 | Remaining fixes and clean-up | |

Smart features planned for step 5:
- captive portal
- status panel
- firmware update from the browser (OTA)
- unique access point name
- test buttons
- local push button
- state restore after a power loss
- online/offline status (MQTT Last Will) on `iotx/status/<uid>`

The MQTT format stays compatible with [IotxApp](https://github.com/StarNiell/IoteXperience/tree/main/IotxApp); it will be revised together with the [IoteXperience](https://github.com/StarNiell/IoteXperience) server.
