# ESP8266 Smart Home Automation & AC Phase Dimmer Controller

A production-ready, feature-rich firmware designed for the **ESP8266** (NodeMCU / Wemos D1 Mini) to control a dual-relay module and an AC phase-controlled fan dimmer. It combines local manual wall switches, an embedded responsive Web GUI control dashboard, secure MQTT cloud integration, instantaneous dual-channel state echoes, and a full-duplex **Serial Command Line Interface (CLI)** over hardware TX/RX without sacrificing any usable GPIO pins. 

Included in this repository is a companion **Python Tkinter Desktop Application** (`pc-controller-serial_and_mqtt.py`) supporting direct USB serial control as well as secure MQTTS cloud telemetry synchronization.

---

## 🌟 Features & Detailed Description

### 1. First-Boot Auto Provisioning & EEPROM Reset
- **Description:** Detects unconfigured devices out of the box and forces setup mode.
- **Mechanism:** On the very first boot (or if EEPROM magic byte `0x5A` at address 10 is uninitialized), the ESP8266 clears its configuration sector, boots directly into **AP Mode** (`ESP8266-Setup-AP`), and logs AP connection credentials and IP address (`192.168.4.1`) to the hardware Serial Terminal. Upon saving parameters via the web configuration page, the `0x5A` byte is written and the device reboots cleanly into standard Station Mode.

### 2. Manual Long-Press AP Recovery Mode
- **Description:** Allows manual override to force AP setup mode after boot without reflashing.
- **Mechanism:** Holding the physical **Fan On/Off Switch (`D7`)** for more than 1 second triggers an instant software fallback to Access Point mode. 
- *Hardware Note:* GPIO 0 (`D3`) retains its hardware flashloader behavior during power-on boot while serving exclusively as Zero-Cross Detection after startup.

### 3. Instant Multi-Channel State Echo (`broadcastState`)
- **Description:** Ensures real-time UI synchronization across local switches, web clients, serial monitors, and MQTT subscribers.
- **Mechanism:** Any state change (relay toggle, fan dimmer step, global command) triggers `broadcastState()`, instantly publishing JSON status updates to the MQTT state topic (`home/esp8266/state`) and printing diagnostic output directly to the Serial console.

### 4. AC Phase Angle Dimming (Fan Speed Control)
- **Description:** Provides precise fan speed regulation across 10 distinct stages (`0` to `9`).
- **Mechanism:** Synchronizes directly with an optoisolated Zero-Cross Detector connected to `D3` (GPIO 0). Upon every zero-crossing interrupt, it arms hardware Timer 1 to trigger a Triac connected to `D8` (GPIO 15) with microsecond-accurate phase delays ranging from ~8.5ms (Stage 0: Min speed/brightness) down to ~0.8ms (Stage 9: Max speed/full ON).

### 5. Dual Relay Management
- **Description:** Independently controls two active-LOW relay channels (`RelayPin1` on `D1` / GPIO 5 and `RelayPin2` on `D2` / GPIO 4) designed for high-voltage lighting or heavy appliance switching.
- **Mechanism:** Integrates failsafe boot states and stores persistent relay configurations directly in non-volatile memory so devices return to their exact previous state after a power interruption.

### 6. Physical Wall Switch & Push-Button Integration
- **Description:** Supports 5 tactile and toggle inputs managed via the robust **AceButton** library with built-in software debouncing and event mapping:
  * **Wall Switches 1 & 2 (`D5` / `D6`):** Standard latching toggle behavior for physical load switching.
  * **Fan On/Off Switch (`D7`):** Momentary/toggle button for fan triac state; long-press (>1s) forces AP mode.
  * **Dimmer Up / Down Buttons (`D0` / `D4`):** Short presses step speed incrementally up or down; long presses instantly jump to minimum speed (`0`) or maximum speed (`9`).

### 7. Embedded Web GUI Dashboard & Fast Reboot Redirection
- **Description:** Serves a responsive, single-page application directly from flash memory via `ESP8266WebServer`.
- **Mechanism:** Displays interactive sliders, relay controls, state polling, and terminal logs. When submitting configuration updates (`/save-settings`), the web server returns a sleek, minimal status page (`Settings Saved! Rebooting...`) and uses client-side JavaScript to redirect straight back to the device's local IP address upon reboot.

### 8. Wi-Fi Auto-Reconnection & AP Portal Fallback
- **Description:** Implements a resilient background connection manager.
- **Mechanism:** Retries dropped Wi-Fi connections every 10 seconds. If Wi-Fi remains disconnected for a continuous 10-minute window, it halts station mode and spins up **Access Point Mode** (`ESP8266-Setup-AP` with password `12345678`) hosting the `/config` portal.

### 9. Secure Cloud MQTT Integration (`MQTTS`)
- **Description:** Establishes encrypted TLS communication (`WiFiClientSecure` with insecure certificate validation enabled) with cloud brokers (such as EMQX Cloud).
- **Mechanism:** Subscribes to custom command topics (`home/esp8266/command`) and publishes JSON telemetry packages (`home/esp8266/state`) periodically (every 10s) and immediately upon command execution.

### 10. Text User Interface / Serial CLI & Terminal Echo
- **Description:** Responsive serial command interpreter operating at `115200` baud over `TX` (GPIO 1) and `RX` (GPIO 3).
- **Mechanism:** Outputs explicit status messages (including IP address assignment upon connection) and accepts raw commands (`POWER1 ON`, `DIMMER 5`, `STATUS`).

---

## 💻 Python Companion Desktop Controller

Included with this repository is **`pc-controller-serial_and_mqtt.py`**, a native Python Tkinter GUI desktop app for monitoring and controlling the ESP8266 hardware via direct USB serial or MQTTS cloud telemetry.

### Key GUI Features:
- **Dual-Mode Connectivity:** Seamlessly switches between local COM Port USB Serial communication and encrypted EMQX MQTT Cloud connection.
- **Real-Time Synchronized Interface:** Dual relay toggle cards and interactive 0–9 dimmer slider auto-update in real time whenever physical wall switches or web interfaces alter device state.
- **Config Persistence:** Saves serial baud rate, MQTT broker endpoints, credentials, and topics locally in `esp8266_config.json`.
- **Live Terminal Monitor:** Displays formatted raw log outputs, command responses, and JSON state updates inside an embedded GUI scroll view.

#### Running the Python Controller:
```bash
pip install pyserial paho-mqtt
python pc-controller-serial_and_mqtt.py
```

---

## 🔌 Pin Mapping (`D0` to `D8` Layout & Serial CLI)

The firmware utilizes all 9 usable digital GPIO pins while keeping the hardware UART completely free for interactive text commands and debugging logs.

| Function | ESP8266 Pin (Silkscreen) | Underlying GPIO | Description |
| :--- | :--- | :--- | :--- |
| **Dimmer Up Button** | `D0` | GPIO 16 | Push Button (Step Up / Long-hold Max) |
| **Relay 1 Output** | `D1` | GPIO 5 | Active-LOW Relay 1 Driver |
| **Relay 2 Output** | `D2` | GPIO 4 | Active-LOW Relay 2 Driver |
| **Zero-Cross Detect** | `D3` | GPIO 0 | ZCD Optocoupler Input (Internal Pull-up / Boot loader pin) |
| **Dimmer Down Button** | `D4` | GPIO 2 | Push Button (Step Down / Long-hold Min) |
| **Wall Switch 1** | `D5` | GPIO 14 | Latching Toggle Switch for Relay 1 |
| **Wall Switch 2** | `D6` | GPIO 12 | Latching Toggle Switch for Relay 2 |
| **Fan On/Off Switch** | `D7` | GPIO 13 | Momentary Toggle Switch / Long-hold (>1s) AP Mode |
| **Triac Gate Output** | `D8` | GPIO 15 | Optoisolator / Triac Gate Trigger Pulse |
| **Serial CLI / Debug TX** | `TX` | GPIO 1 | Hardware UART Transmission (`115200` baud) |
| **Serial CLI / Debug RX** | `RX` | GPIO 3 | Hardware UART Reception (`115200` baud) |

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

On initial unconfigured boot, the firmware falls back to pre-defined default credentials if none are saved in flash memory:

```cpp
const char* DEFAULT_SSID = "ash_jio_2G";
const char* DEFAULT_PASSWORD = "ThundersnO0w@,,#";
const char* DEFAULT_MQTT_SERVER = "i11b0026.ala.eu-central-1.emqxsl.com";
const int   DEFAULT_MQTT_PORT = 8883;
const char* DEFAULT_MQTT_USER = "admin";
const char* DEFAULT_MQTT_PASS = "admin1234S";
const char* DEFAULT_MQTT_TOPIC_CMD = "home/esp8266/command";
const char* DEFAULT_MQTT_TOPIC_STATE = "home/esp8266/state";
```
