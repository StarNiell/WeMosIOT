# WeMosIOT
## IOT eXperience – device firmware

Firmware for the devices of the **IOT eXperience** home automation ecosystem. Devices join the WiFi network and talk to the server **only through MQTT**. Each device serves its own web page for configuration.

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

Pins are defined in `src/defaults.h` and can be overridden per environment from `platformio.ini` (e.g. `build_flags = -DPIN_OUTPUT=12`).

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

Functions are still selected **at compile time** in `src/defaults.h` (overridable with `build_flags`); they will move to the web configuration in a later step.

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
- no WiFi network has been configured yet.

The device creates the WiFi network `WD1R2-REMOTE` (password `12345678`) and does not connect to MQTT.

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

The web server is **always active**:

| Situation | Address |
|---|---|
| Normal operation | `http://<device IP>/` or `http://iotx-<device-uid>.local/` (mDNS) |
| Access point | `http://192.168.4.1/` |

Every page requires a login:

| Field | Default |
|---|---|
| User | `admin` |
| Password | `iotxadmin` |

The password is stored in the configuration (`web-pwd`) and will be editable from the new configuration page in the next step. After saving, reboot the device to apply the new settings.

The configuration is stored on LittleFS in `/store/config.json` (never served by the web server):

| Key | Description | Default |
|---|---|---|
| `wifi-ssid`, `wifi-pwd` | WiFi network | – |
| `mqtt-server`, `mqtt-port` | MQTT broker | –, `1883` |
| `mqtt-username`, `mqtt-pwd` | broker credentials (optional) | – |
| `device-uid` | device identifier | `999` |
| `topic-sub` | commands topic | `iotx/in` |
| `topic-pub` | root of the status topics | `iotx/out` |
| `web-pwd` | password of the web pages | `iotxadmin` |

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
| `pio run -e d1_mini` | build |
| `pio run -e d1_mini -t upload` | flash the firmware |
| `pio run -e d1_mini -t uploadfs` | flash the LittleFS image built from `data/` |
| `pio device monitor` | serial monitor (115200 baud) |

The LittleFS image **replaces the stored configuration** with the template. On devices that are already configured, flash the firmware only.

The `d1_mini` environment uses the same flash layout as the Arduino IDE default (FS 2 MB). The configuration already stored on existing devices is therefore preserved.

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
  web.h/.cpp            web server
data/                   LittleFS content (configuration page, template config)
```

## Roadmap

| Step | Content | Status |
|---|---|---|
| 0 | Minimal fixes (compile error, invalid `config.json`, credentials removed) | done |
| 1 | PlatformIO, modular structure, non-blocking loop, web server always on, automatic access point, mDNS, IP in the payload | **done** |
| 2 | JSON API and new configuration page (password editable, automatic reboot) | |
| 3 | WiFi network selection from a scan | |
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

The MQTT format stays compatible with IotxApp; it will be revised together with the IOTeXperience server.
