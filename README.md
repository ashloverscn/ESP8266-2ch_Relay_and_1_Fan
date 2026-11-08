# ESP8266 Smart Home Automation & AC Phase Dimmer Controller

A production-ready, feature-rich firmware designed for the **ESP8266** (NodeMCU / Wemos D1 Mini) to control a dual-relay module and an AC phase-controlled fan dimmer. It combines local manual wall switches, an embedded responsive Web GUI control dashboard, secure MQTT cloud integration, and a dedicated serial command interface without wasting any usable GPIO pins.

---

## 🌟 Key Features

- **AC Phase Angle Dimming (Fan Speed Control):** Synchronizes via a Zero-Cross Detect interrupt (`D3`) to drive a Triac (`D8`) across 10 distinct speed stages (`0` to `9`).
- **Dual Relay Control:** Independent management of 2 active-LOW relay channels (`D1` and `D2`) for lighting or appliance switching.
- **Physical Wall Switch Integration:** Supports 5 tactile/toggle inputs with software debouncing:
  - **Wall Switches (`D5`, `D6`):** Latching toggle behavior for physical load switching.
  - **Fan On/Off Switch (`D7`):** Momentary/toggle button for the fan triac state.
  - **Dimmer Up / Down Buttons (`D0`, `D4`):** Short presses speed incrementally; long presses (hold $\ge$ 1.2s) instantly jump to minimum (`0`) or maximum (`9`) speed.
- **Embedded Web GUI Dashboard:** Responsive local web controller featuring real-time state synchronization, interactive slider controls, quick toggles, and a live serial/event console.
- **Wi-Fi Portal & AP Fallback:** Non-blocking auto-reconnection loop. If Wi-Fi fails to connect for 10 minutes, it automatically drops into **Access Point Mode** (`ESP8266-Setup-AP`) hosting an embedded settings configuration page (`/config`).
- **Secure MQTT Client (`MQTTS`):** Encrypted communication via TLS (`WiFiClientSecure` with `setInsecure()`) to securely sync states and commands with cloud brokers (e.g., EMQX Cloud).
- **Non-Volatile State & Config Storage:** Persists relay states, dimmer levels, and system parameters across reboots using `EEPROM` and the `Preferences` library.
- **USB/Serial Command Line Interface:** Fully responsive serial monitor interface operating at `115200` baud.

---

## 🔌 Pin Mapping (`D0` to `D8` Layout)

The firmware utilizes all 9 usable GPIO pins on the ESP8266 while leaving the hardware `RX` pin free for debugging.

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
| **Serial Monitor** | `RX` | GPIO 3 | Hardware Serial RX (`115200` baud) |

---

## 📦 Required Arduino Libraries

Ensure the following libraries are installed via the Arduino IDE Library Manager:
- **`ArduinoJson`** (v7+) — For parsing and serializing web and MQTT JSON payloads.
- **`PubSubClient`** — For lightweight MQTT broker communication.
- **`arduino-timer`** — For non-blocking periodic background tasks.
- **`Preferences`** — For flash-backed key-value configuration storage.
- **`EEPROM`** — Built-in ESP8266 core library for state persistence.

---

## ⚙️ Configuration & Credentials

On initial boot, the firmware falls back to pre-defined default credentials if none are saved in flash memory:

```cpp
const char* DEFAULT_SSID = "ash_jio_2G";
const char* DEFAULT_PASSWORD = "ThundersnO0w@,,#";
const char* DEFAULT_MQTT_SERVER = "i11b0026.ala.eu-central-1.emqxsl.com";
const int   DEFAULT_MQTT_PORT = 8883;
