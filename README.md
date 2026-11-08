# ESP8266 Smart Home Automation & AC Phase Dimmer Controller

A production-ready, feature-rich firmware designed for the **ESP8266** (NodeMCU / Wemos D1 Mini) to control a dual-relay module and an AC phase-controlled fan dimmer. It combines local manual wall switches, an embedded responsive Web GUI control dashboard, secure MQTT cloud integration, and a full-duplex **Serial Command Line Interface (CLI)** over hardware TX/RX without sacrificing any usable GPIO pins.

---

## 🌟 Features & Detailed Description

### 1. AC Phase Angle Dimming (Fan Speed Control)
* **Description:** Provides precise fan speed regulation across 10 distinct stages (`0` to `9`). 
* **Mechanism:** Synchronizes directly with an optoisolated Zero-Cross Detector connected to `D3` (GPIO 0). Upon every zero-crossing interrupt, it arms hardware Timer 1 to trigger a Triac connected to `D8` (GPIO 15) with microsecond-accurate phase delays ranging from ~8.5ms (Stage 0: Min speed/brightness) down to ~0.8ms (Stage 9: Max speed/full ON).

### 2. Dual Relay Management
* **Description:** Independently controls two active-LOW relay channels (`RelayPin1` on `D1` / GPIO 5 and `RelayPin2` on `D2` / GPIO 4) designed for high-voltage lighting or heavy appliance switching.
* **Mechanism:** Integrates failsafe boot states and stores persistent relay configurations directly in non-volatile memory so devices return to their exact previous state after a power interruption.

### 3. Physical Wall Switch & Push-Button Integration
* **Description:** Supports 5 tactile and toggle inputs managed via the robust **AceButton** library with built-in software debouncing and event mapping:
  * **Wall Switches 1 & 2 (`D5` / `D6`):** Standard latching toggle behavior for physical load switching.
  * **Fan On/Off Switch (`D7`):** Momentary/toggle button to toggle the fan triac state.
  * **Dimmer Up / Down Buttons (`D0` / `D4`):** Short presses step speed incrementally up or down; long presses (held $\ge$ 1.2 seconds) instantly jump to minimum speed (`0`) or maximum speed (`9`).

### 4. Embedded Web GUI Dashboard
* **Description:** Serves a responsive, lightweight single-page application directly from the ESP8266's flash memory via an asynchronous web server (`ESP8266WebServer`).
* **Features:** Includes real-time asynchronous state synchronization (polling every 3 seconds), interactive slider controls for dimming levels, click-to-toggle relay buttons, global commands (`ALL_ON`, `ALL_OFF`), and a live browser-side console capturing device feedback logs.

### 5. Wi-Fi Auto-Reconnection & AP Portal Fallback
* **Description:** Implements a resilient, non-blocking background connection manager. 
* **Mechanism:** If the router connection drops, the firmware retries connection loops every 10 seconds. If Wi-Fi remains disconnected for a continuous 10-minute window, it automatically halts station mode and spins up an independent **Access Point Mode** (`ESP8266-Setup-AP` with password `12345678`) hosting a dedicated `/config` portal to let you update Wi-Fi and MQTT credentials on the fly without reflashing.

### 6. Secure Cloud MQTT Integration (`MQTTS`)
* **Description:** Establishes encrypted TLS communication (`WiFiClientSecure` with insecure certificate validation enabled) with cloud MQTT brokers (such as EMQX Cloud).
* **Mechanism:** Subscribes to custom command topics (`home/esp8266/command`) and periodically publishes comprehensive JSON telemetry status packages (`home/esp8266/state`) every 10 seconds or upon explicit command execution.

### 7. Non-Volatile EEPROM State Storage
* **Description:** Persists user settings, Wi-Fi strings, MQTT endpoints, individual relay states, and fan dimmer values across system reboots and power outages using the ESP8266 `EEPROM` library and JSON formatting buffers.

### 8. Text User Interface / Serial CLI
* **Description:** Fully responsive text-based serial command interpreter operating at `115200` baud via the hardware `TX` (GPIO 1) and `RX` (GPIO 3) pins.
* **Mechanism:** Allows engineers and maintainers to stream live diagnostic logs (`logMessage`) and type raw control commands (`POWER1 ON`, `DIMMER 5`, `STATUS`) directly from a connected USB terminal or UART bridge without sacrificing any of the `D0–D8` digital control pins.

---

## 🔌 Pin Mapping (`D0` to `D8` Layout & Serial CLI)

The firmware utilizes all 9 usable digital GPIO pins while keeping the hardware UART completely free for interactive text commands and debugging logs.

| Function | ESP8266 Pin (Silkscreen) | Underlying GPIO | Description |
| :--- | :--- | :--- | :--- |
| **Dimmer Up Button** | `D0` | GPIO 16 | Momentary Push Button (Step Up / Long-hold Max) |
| **Relay 1 Output** | `D1` | GPIO 5 | Active-LOW Relay 1 Driver |
| **Relay 2 Output** | `D2` | GPIO 4 | Active-LOW Relay 2 Driver |
| **Zero-Cross Detect** | `D3` | GPIO 0 | ZCD Optocoupler Input (with internal pull-up) |
| **Dimmer Down Button** | `D4` | GPIO 2 | Momentary Push Button (Step Down / Long-hold Min) |
| **Wall Switch 1** | `D5` | GPIO 14 | Latching Toggle Switch for Relay 1 |
| **Wall Switch 2** | `D6` | GPIO 12 | Latching Toggle Switch for Relay 2 |
| **Fan On/Off Switch** | `D7` | GPIO 13 | Momentary/Toggle Switch for Fan Triac State |
| **Triac Gate Output** | `D8` | GPIO 15 | Optoisolator / Triac Gate Trigger Pulse |
| **Serial CLI / Debug TX** | `TX` | GPIO 1 | Hardware UART Transmission (`115200` baud) |
| **Serial CLI / Debug RX** | `RX` | GPIO 3 | Hardware UART Reception for Text Commands (`115200` baud) |

---

## 📦 Required Arduino Libraries

Ensure the following libraries are installed via the Arduino IDE Library Manager:
- **`ArduinoJson`** (v7+) — For parsing and serializing web and MQTT JSON payloads.
- **`PubSubClient`** — For lightweight MQTT broker communication.
- **`arduino-timer`** — For non-blocking periodic background tasks.
- **`AceButton`** — For robust multi-event button handling (clicks and long-presses).
- **`EEPROM`** — Built-in ESP8266 core library for state and configuration persistence.

---

## ⚙️ Configuration & Credentials

On initial boot, the firmware falls back to pre-defined default credentials if none are saved in flash memory:

```cpp
const char* DEFAULT_SSID = "ash_jio_2G";
const char* DEFAULT_PASSWORD = "ThundersnO0w@,,#";
const char* DEFAULT_MQTT_SERVER = "i11b0026.ala.eu-central-1.emqxsl.com";
const int   DEFAULT_MQTT_PORT = 8883;
const char* DEFAULT_MQTT_USER = "admin";
const char* DEFAULT_MQTT_PASS = "admin1234S";
