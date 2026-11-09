#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <arduino-timer.h>
#include <AceButton.h>

using namespace ace_button;

// --- Default Fallback Credentials ---
const char* DEFAULT_SSID = "ash_jio_2G";
const char* DEFAULT_PASSWORD = "ThundersnO0w@,,#";

const char* DEFAULT_MQTT_SERVER = "i11b0026.ala.eu-central-1.emqxsl.com";
const int   DEFAULT_MQTT_PORT = 8883;
const char* DEFAULT_MQTT_USER = "admin";
const char* DEFAULT_MQTT_PASS = "admin1234S";
const char* DEFAULT_MQTT_TOPIC_CMD = "home/esp8266/command";
const char* DEFAULT_MQTT_TOPIC_STATE = "home/esp8266/state";

// --- Runtime Configuration Variables ---
String stored_ssid;
String stored_password;
String mqtt_server;
int mqtt_port;
String mqtt_user;
String mqtt_pass;
String mqtt_topic_cmd;
String mqtt_topic_state;

// --- Controller State ---
bool relayStates[2] = {false, false};
uint8_t dimm_value = 0; // 0 to 9
volatile bool triacState = false;

// 10 stages (0 to 9) mapped to microsecond delays from Zero Cross
const uint16_t spd[10] = {
    8500, // Stage 0: Min Brightness / Lowest Speed (~8.5ms delay)
    7700, // Stage 1
    6900, // Stage 2
    6100, // Stage 3
    5300, // Stage 4
    4500, // Stage 5
    3700, // Stage 6
    2900, // Stage 7
    2100, // Stage 8
    800   // Stage 9: Max Brightness / Full ON (~0.8ms delay)
};

auto timer = timer_create_default();

// --- Pin Definitions using D0 to D8 board labels ---
#define DimmerUpPin    D0  // GPIO 16
#define RelayPin1      D1  // GPIO 5  - Relay Out 1
#define RelayPin2      D2  // GPIO 4  - Relay Out 2
#define ZeroCrossPin   D3  // GPIO 0  - Zero Cross Detect
#define DimmerDownPin  D4  // GPIO 2
#define SwitchPin1     D5  // GPIO 14 - Wall Switch for Relay 1
#define SwitchPin2     D6  // GPIO 12 - Wall Switch for Relay 2
#define FanSwitchPin   D7  // GPIO 13 - Fan On/Off Switch
#define TriacPin       D8  // GPIO 15 - Triac Gate Output

// --- EEPROM Configuration Storage Layout (512 bytes) ---
#define EEPROM_RELAY1      0
#define EEPROM_RELAY2      1
#define EEPROM_DIMMER      7
#define EEPROM_TRIAC_STATE 8
#define EEPROM_FIRST_BOOT  10 // 1 byte flag (0xFF = unconfigured first boot, 0x5A = configured)
#define EEPROM_CFG_START   16 

#define DIMMER_MIN 0
#define DIMMER_MAX 9

// --- AceButton Instances ---
AceButton buttonRelay1(SwitchPin1, HIGH);
AceButton buttonRelay2(SwitchPin2, HIGH);
AceButton buttonFanOnOff(FanSwitchPin, HIGH);
AceButton buttonDimDown(DimmerDownPin, HIGH);
AceButton buttonDimUp(DimmerUpPin, HIGH);

// --- Web Server & MQTT ---
ESP8266WebServer server(80);
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

// --- Serial Buffering State ---
String serialBuffer = "";
unsigned long lastCharTime = 0;
const unsigned long SERIAL_TIMEOUT = 50;
String consoleLogBuffer = "";

// --- Wi-Fi Reconnection & AP Fallback State ---
unsigned long lastWiFiCheckTime = 0;
const unsigned long WIFI_RETRY_INTERVAL = 10000; 
unsigned long wifiDisconnectedStartTime = 0;        
const unsigned long WIFI_AP_FALLBACK_TIMEOUT = 600000; // 10 minutes
bool apModeActive = false;

// Forward declarations
void loadConfig();
void saveConfig(String ssid, String pass, String srv, int prt, String usr, String pwd, String tCmd, String tState);
void handleCommand(String cmd);
void reconnectMQTT();
void callbackMQTT(char* topic, byte* payload, unsigned int length);
String getStateJson(String logMsg = "");
void logMessage(String msg);
void broadcastState();
void checkWiFiConnection();
void enterAPMode();
void setRelayState(int relay, bool turnOn);
void relayToggle(int relay);
void setDimmerLevel(uint8_t val);
void triacOnOff();
void dimm_Up();
void dimm_Dn();
void eepromState();
bool sendPeriodicStatus(void*);
void handleButtonEvent(AceButton* button, uint8_t eventType, uint8_t buttonState);

// --- Phase Control: Hardware Timer 1 Callback (Fires Triac Pulse) ---
void IRAM_ATTR onTimer1Fire()
{
    digitalWrite(TriacPin, HIGH);
    os_delay_us(30); 
    digitalWrite(TriacPin, LOW);
    timer1_disable();
}

// --- Zero-Cross Interrupt (Arms Hardware Timer 1 instantly) ---
void IRAM_ATTR handleZeroCross()
{
  if (triacState && dimm_value > 0)
  {
    timer1_write(spd[dimm_value] * 5);
    timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);
  }
}

// --- Main Dashboard HTML Template ---
const char MAIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP8266 Home Automation Web Controller</title>
    <style>
        body { font-family: Arial, sans-serif; background-color: #f4f6f8; margin: 0; padding: 20px; display: flex; justify-content: center; }
        .container { width: 100%; max-width: 650px; background: #ffffff; border-radius: 8px; padding: 20px; box-shadow: 0 4px 10px rgba(0,0,0,0.1); position: relative; }
        .config-link { position: absolute; top: 20px; right: 20px; text-decoration: none; background-color: #6c757d; color: white; padding: 8px 12px; font-size: 13px; border-radius: 4px; font-weight: bold; }
        .config-link:hover { background-color: #5a6268; }
        h2 { text-align: center; margin-top: 0; color: #333; }
        .card { border: 1px solid #ddd; border-radius: 6px; padding: 15px; margin-bottom: 15px; background: #fafafa; }
        .card-title { font-weight: bold; margin-bottom: 10px; color: #555; font-size: 14px; text-transform: uppercase; }
        .row { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }
        button { padding: 10px 15px; font-size: 14px; border-radius: 4px; border: none; cursor: pointer; background-color: #007bff; color: white; transition: 0.2s; }
        button:hover { background-color: #0056b3; }
        .relay-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; }
        .relay-btn { height: 70px; font-weight: bold; background-color: #e0e0e0; color: #000; border-radius: 6px; }
        .relay-btn.on { background-color: #4CAF50; color: #fff; }
        #console { width: 100%; height: 150px; background-color: #1e1e1e; color: #00ff00; font-family: monospace; font-size: 12px; padding: 10px; box-sizing: border-box; overflow-y: scroll; border-radius: 4px; white-space: pre-wrap; }
        .flex-fill { flex: 1; }
        #connectionStatus { font-weight: bold; padding: 5px 10px; border-radius: 4px; display: inline-block; background: #e8f5e9; color: #2e7d32; }
    </style>
</head>
<body>
<div class="container">
    <a href="/config" class="config-link">⚙️ Config</a>
    <h2>ESP8266 Home Automation</h2>
    <div class="card">
        <div class="card-title">ESP8266 Status</div>
        <div id="connectionStatus">Status: Connected</div>
    </div>
    <div class="card">
        <div class="card-title">Relay Controls</div>
        <div class="relay-grid">
            <button id="relay-1" class="relay-btn" onclick="toggleRelay(1)">Relay 1<br>OFF</button>
            <button id="relay-2" class="relay-btn" onclick="toggleRelay(2)">Relay 2<br>OFF</button>
        </div>
    </div>
    <div class="card">
        <div class="card-title">Fan Dimmer Control (0-9)</div>
        <div class="row">
            <button onclick="sendAction('DIMMER 0')">MIN (0)</button>
            <button onclick="sendAction('DIMM_DN')">Step -</button>
            <input type="range" id="dimmerSlider" class="flex-fill" min="0" max="9" value="0" onchange="onSliderChange(this.value)">
            <button onclick="sendAction('DIMM_UP')">Step +</button>
            <button onclick="sendAction('DIMMER 9')">MAX (9)</button>
        </div>
    </div>
    <div class="card">
        <div class="card-title">Global Commands & Status</div>
        <div class="row">
            <button class="flex-fill" onclick="sendAction('ALL_ON')">ALL ON</button>
            <button class="flex-fill" onclick="sendAction('ALL_OFF')">ALL OFF</button>
            <button class="flex-fill" onclick="sendAction('STATUS')">Get Status</button>
        </div>
    </div>
    <div class="card">
        <div class="card-title">Web Console / Feedback</div>
        <div id="console"></div>
    </div>
</div>
<script>
    let relayStates = [false, false];
    function toggleRelay(relayNum) { sendAction(`POWER${relayNum} ${relayStates[relayNum - 1] ? 'OFF' : 'ON'}`); }
    function onSliderChange(val) { sendAction(`DIMMER ${val}`); }
    function sendAction(cmd) {
        fetch('/cmd?action=' + encodeURIComponent(cmd)).then(res => res.json()).then(data => updateUIState(data)).catch(err => {});
    }
    function pollState() {
        fetch('/status-json').then(res => res.json()).then(data => updateUIState(data)).catch(err => {});
    }
    function updateUIState(data) {
        if (data.relays) {
            data.relays.forEach((state, i) => {
                relayStates[i] = state;
                const btn = document.getElementById(`relay-${i + 1}`);
                if (state) {
                    btn.innerText = `Relay ${i + 1}\nON`;
                    btn.classList.add('on');
                } else {
                    btn.innerText = `Relay ${i + 1}\nOFF`;
                    btn.classList.remove('on');
                }
            });
        }
        if (data.dimmer !== undefined) document.getElementById('dimmerSlider').value = data.dimmer;
        if (data.log) {
            const consoleEl = document.getElementById('console');
            consoleEl.innerHTML += data.log + '\n';
            consoleEl.scrollTop = consoleEl.scrollHeight;
        }
    }
    window.onload = function() { pollState(); setInterval(pollState, 3000); };
</script>
</body>
</html>
)rawliteral";

// --- Dedicated Wi-Fi & MQTT Configuration Page HTML ---
const char CONFIG_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Device Configuration</title>
    <style>
        body { font-family: Arial, sans-serif; background-color: #f4f6f8; margin: 0; padding: 20px; display: flex; justify-content: center; }
        .container { width: 100%; max-width: 500px; background: #ffffff; border-radius: 8px; padding: 20px; box-shadow: 0 4px 10px rgba(0,0,0,0.1); }
        h2 { text-align: center; margin-top: 0; color: #333; }
        .back-link { display: inline-block; margin-bottom: 15px; text-decoration: none; color: #007bff; font-weight: bold; font-size: 14px; }
        label { font-weight: bold; font-size: 13px; color: #555; display: block; margin-top: 10px; }
        input[type="text"], input[type="password"], input[type="number"] { padding: 8px; border: 1px solid #ccc; border-radius: 4px; width: 100%; box-sizing: border-box; margin-top: 5px; margin-bottom: 5px; }
        button { width: 100%; padding: 12px; font-size: 15px; border-radius: 4px; border: none; cursor: pointer; background-color: #28a745; color: white; font-weight: bold; margin-top: 15px; }
    </style>
</head>
<body>
<div class="container">
    <a href="/" class="back-link">&larr; Back to Dashboard</a>
    <h2>Device Configuration</h2>
    <form action="/save-settings" method="POST">
        <h3>Wi-Fi Settings</h3>
        <label>Wi-Fi SSID:</label><input type="text" name="wifi_ssid" id="cfg_ssid">
        <label>Wi-Fi Password:</label><input type="password" name="wifi_pass" id="cfg_pass_wifi">
        <hr style="border:0; border-top:1px solid #eee; margin:20px 0;">
        <h3>MQTT Settings</h3>
        <label>MQTT Server:</label><input type="text" name="mqtt_server" id="cfg_server">
        <label>MQTT Port:</label><input type="number" name="mqtt_port" id="cfg_port">
        <label>MQTT Username:</label><input type="text" name="mqtt_user" id="cfg_user">
        <label>MQTT Password:</label><input type="password" name="mqtt_pass" id="cfg_pass">
        <label>Command Topic:</label><input type="text" name="mqtt_topic_cmd" id="cfg_tcmd">
        <label>State Topic:</label><input type="text" name="mqtt_topic_state" id="cfg_tstate">
        <button type="submit">Save & Reboot</button>
    </form>
</div>
<script>
    fetch('/get-config').then(res => res.json()).then(data => {
        document.getElementById('cfg_ssid').value = data.ssid || '';
        document.getElementById('cfg_pass_wifi').value = data.pass_wifi || '';
        document.getElementById('cfg_server').value = data.server;
        document.getElementById('cfg_port').value = data.port;
        document.getElementById('cfg_user').value = data.user;
        document.getElementById('cfg_pass').value = data.pass;
        document.getElementById('cfg_tcmd').value = data.tcmd;
        document.getElementById('cfg_tstate').value = data.tstate;
    });
</script>
</body>
</html>
)rawliteral";

// --- EEPROM Config Storage Helpers ---
void loadConfig() {
    char buffer[256];
    for (int i = 0; i < 256; i++) buffer[i] = EEPROM.read(EEPROM_CFG_START + i);
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, buffer);
    if (error) {
        stored_ssid = DEFAULT_SSID;
        stored_password = DEFAULT_PASSWORD;
        mqtt_server = DEFAULT_MQTT_SERVER;
        mqtt_port = DEFAULT_MQTT_PORT;
        mqtt_user = DEFAULT_MQTT_USER;
        mqtt_pass = DEFAULT_MQTT_PASS;
        mqtt_topic_cmd = DEFAULT_MQTT_TOPIC_CMD;
        mqtt_topic_state = DEFAULT_MQTT_TOPIC_STATE;
    } else {
        stored_ssid = doc["ssid"] | DEFAULT_SSID;
        stored_password = doc["pass"] | DEFAULT_PASSWORD;
        mqtt_server = doc["srv"] | DEFAULT_MQTT_SERVER;
        mqtt_port = doc["prt"] | DEFAULT_MQTT_PORT;
        mqtt_user = doc["usr"] | DEFAULT_MQTT_USER;
        mqtt_pass = doc["pwd"] | DEFAULT_MQTT_PASS;
        mqtt_topic_cmd = doc["tCmd"] | DEFAULT_MQTT_TOPIC_CMD;
        mqtt_topic_state = doc["tState"] | DEFAULT_MQTT_TOPIC_STATE;
    }
}

void saveConfig(String ssid, String pass, String srv, int prt, String usr, String pwd, String tCmd, String tState) {
    JsonDocument doc;
    doc["ssid"] = ssid;
    doc["pass"] = pass;
    doc["srv"] = srv;
    doc["prt"] = prt;
    doc["usr"] = usr;
    doc["pwd"] = pwd;
    doc["tCmd"] = tCmd;
    doc["tState"] = tState;

    char buffer[256];
    serializeJson(doc, buffer);
    for (int i = 0; i < 256; i++) {
        EEPROM.write(EEPROM_CFG_START + i, buffer[i]);
    }
    
    // Mark configuration as explicitly saved (Exits first-boot state)
    EEPROM.write(EEPROM_FIRST_BOOT, 0x5A);
    EEPROM.commit();
}

void logMessage(String msg) {
    Serial.println(msg);
    if (consoleLogBuffer.length() > 2000) consoleLogBuffer = consoleLogBuffer.substring(consoleLogBuffer.length() - 1500);
    consoleLogBuffer += msg + "\n";
}

// --- Instant State Broadcast (Serial & MQTT Echo) ---
void broadcastState() {
    JsonDocument doc;
    doc["POWER1"] = relayStates[0] ? "ON" : "OFF";
    doc["POWER2"] = relayStates[1] ? "ON" : "OFF";
    doc["Dimmer"] = dimm_value;
    String out;
    serializeJson(doc, out);

    // Echo to Serial Terminal Log
    logMessage(">> STATE BROADCAST: " + out);

    // Echo to MQTT Broker
    if (mqttClient.connected()) {
        mqttClient.publish(mqtt_topic_state.c_str(), out.c_str());
    }
}

// --- Dimmer & Triac Control ---
void setDimmerLevel(uint8_t val) {
    if (val > DIMMER_MAX) val = DIMMER_MAX;
    dimm_value = val;
    triacState = (dimm_value > 0);
    EEPROM.write(EEPROM_DIMMER, dimm_value);
    EEPROM.write(EEPROM_TRIAC_STATE, triacState ? 1 : 0);
    EEPROM.commit();
    logMessage(">> Dimmer set to: " + String(dimm_value));
    broadcastState(); // Instant echo across serial & MQTT
}

void triacOn() {
    uint8_t saved = EEPROM.read(EEPROM_DIMMER);
    setDimmerLevel((saved > 0 && saved <= DIMMER_MAX) ? saved : DIMMER_MAX);
}

void triacOff() {
    setDimmerLevel(DIMMER_MIN);
}

void triacOnOff() {
    if (triacState) triacOff(); else triacOn();
}

void dimm_Up() {
    if (!triacState) { setDimmerLevel(1); return; }
    if (dimm_value < DIMMER_MAX) setDimmerLevel(dimm_value + 1);
}

void dimm_Dn() {
    if (!triacState) return;
    if (dimm_value > DIMMER_MIN) setDimmerLevel(dimm_value - 1);
}

// --- Relay Control ---
void setRelayState(int relay, bool turnOn) {
    int pin = (relay == 1) ? RelayPin1 : RelayPin2;
    int addr = (relay == 1) ? EEPROM_RELAY1 : EEPROM_RELAY2;
    digitalWrite(pin, turnOn ? LOW : HIGH);
    EEPROM.write(addr, turnOn ? HIGH : LOW);
    EEPROM.commit();
    relayStates[relay - 1] = turnOn;
    logMessage(">> Relay " + String(relay) + " -> " + (turnOn ? "ON" : "OFF"));
    broadcastState(); // Instant echo across serial & MQTT
}

void relayToggle(int relay) {
    int pin = (relay == 1) ? RelayPin1 : RelayPin2;
    bool currentState = (digitalRead(pin) == LOW);
    setRelayState(relay, !currentState);
}

void eepromState() {
    uint8_t r1 = EEPROM.read(EEPROM_RELAY1);
    uint8_t r2 = EEPROM.read(EEPROM_RELAY2);
    digitalWrite(RelayPin1, (r1 == HIGH) ? LOW : HIGH);
    digitalWrite(RelayPin2, (r2 == HIGH) ? LOW : HIGH);
    relayStates[0] = (r1 == HIGH);
    relayStates[1] = (r2 == HIGH);

    uint8_t storedDimmer = EEPROM.read(EEPROM_DIMMER);
    dimm_value = (storedDimmer <= DIMMER_MAX) ? storedDimmer : DIMMER_MAX;
    triacState = (EEPROM.read(EEPROM_TRIAC_STATE) == 1 || dimm_value > 0);
}

// Forward Declaration for AP Mode trigger
void enterAPMode();

// --- AceButton Event Handler ---
void handleButtonEvent(AceButton* button, uint8_t eventType, uint8_t buttonState) {
    uint8_t pin = button->getPin();

    if (eventType == AceButton::kEventPressed) {
        if (pin == SwitchPin1) {
            relayToggle(1);
        } else if (pin == SwitchPin2) {
            relayToggle(2);
        } else if (pin == FanSwitchPin) {
            triacOnOff();
        } else if (pin == DimmerDownPin) {
            dimm_Dn();
        } else if (pin == DimmerUpPin) {
            dimm_Up();
        }
    } 
    else if (eventType == AceButton::kEventLongPressed) {
        if (pin == DimmerDownPin) {
            setDimmerLevel(DIMMER_MIN);
            logMessage(">> Long Press: Dimmer set to MIN");
        } else if (pin == DimmerUpPin) {
            setDimmerLevel(DIMMER_MAX);
            logMessage(">> Long Press: Dimmer set to MAX");
        } else if (pin == FanSwitchPin) {
            logMessage(">> Long Press detected on Fan Switch: Forcing AP Setup Mode...");
            enterAPMode();
        }
    }
}

// --- AP Mode Setup & Server Portal ---
void enterAPMode() {
    apModeActive = true;
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    
    const char* apSsid = "ESP8266-Setup-AP";
    const char* apPass = "12345678";
    
    WiFi.softAP(apSsid, apPass);
    
    logMessage(">> Entered AP Setup Mode!");
    logMessage(">> AP SSID:     " + String(apSsid));
    logMessage(">> AP Password: " + String(apPass));
    logMessage(">> AP IP Addr:  " + WiFi.softAPIP().toString());

    server.stop();

    server.on("/", [](){ server.send_P(200, "text/html", CONFIG_PAGE); });
    server.on("/get-config", [](){
        JsonDocument doc;
        doc["ssid"] = stored_ssid; doc["pass_wifi"] = stored_password;
        doc["server"] = mqtt_server; doc["port"] = mqtt_port;
        doc["user"] = mqtt_user; doc["pass"] = mqtt_pass;
        doc["tcmd"] = mqtt_topic_cmd; doc["tstate"] = mqtt_topic_state;
        String resp; serializeJson(doc, resp);
        server.send(200, "application/json", resp);
    });
    server.on("/save-settings", HTTP_POST, [](){
        saveConfig(
            server.hasArg("wifi_ssid") ? server.arg("wifi_ssid") : DEFAULT_SSID,
            server.hasArg("wifi_pass") ? server.arg("wifi_pass") : DEFAULT_PASSWORD,
            server.hasArg("mqtt_server") ? server.arg("mqtt_server") : DEFAULT_MQTT_SERVER,
            server.hasArg("mqtt_port") ? server.arg("mqtt_port").toInt() : DEFAULT_MQTT_PORT,
            server.hasArg("mqtt_user") ? server.arg("mqtt_user") : DEFAULT_MQTT_USER,
            server.hasArg("mqtt_pass") ? server.arg("mqtt_pass") : DEFAULT_MQTT_PASS,
            server.hasArg("mqtt_topic_cmd") ? server.arg("mqtt_topic_cmd") : DEFAULT_MQTT_TOPIC_CMD,
            server.hasArg("mqtt_topic_state") ? server.arg("mqtt_topic_state") : DEFAULT_MQTT_TOPIC_STATE
        );

        String deviceIp = WiFi.localIP().toString();
        if (apModeActive || deviceIp == "0.0.0.0") {
            deviceIp = "192.168.4.1";
        }

        String html = "<script>setTimeout(function(){ window.location.href = 'http://" + deviceIp + "/'; }, 3500);</script>";
        html += "<h3>Settings Saved! Rebooting...</h3>";

        server.send(200, "text/html", html);
        delay(1000);
        ESP.restart();
    });
    server.begin();
}

void checkWiFiConnection() {
    if (!apModeActive) {
        if (WiFi.status() != WL_CONNECTED) {
            if (wifiDisconnectedStartTime == 0) wifiDisconnectedStartTime = millis();
            else if (millis() - wifiDisconnectedStartTime >= WIFI_AP_FALLBACK_TIMEOUT) {
                enterAPMode();
                return;
            }
            if (millis() - lastWiFiCheckTime >= WIFI_RETRY_INTERVAL) {
                lastWiFiCheckTime = millis();
                WiFi.disconnect();
                WiFi.begin(stored_ssid.c_str(), stored_password.c_str());
            }
        } else {
            wifiDisconnectedStartTime = 0;
        }
    }
}

void reconnectMQTT() {
    if (!apModeActive && !mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
        static unsigned long lastRetry = 0;
        if (millis() - lastRetry > 15000) {
            lastRetry = millis();
            String clientId = "ESP8266-" + String(ESP.getChipId());
            if (mqttClient.connect(clientId.c_str(), mqtt_user.c_str(), mqtt_pass.c_str())) {
                mqttClient.subscribe(mqtt_topic_cmd.c_str());
                logMessage(">> MQTT Connected & Subscribed!");
                broadcastState(); // Broadcast state on fresh connection
            } else {
                logMessage(">> MQTT Connection failed, will retry...");
            }
        }
    }
}

void callbackMQTT(char* topic, byte* payload, unsigned int length) {
    String msg;
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    msg.trim();
    handleCommand(msg);
}

bool sendPeriodicStatus(void*) {
    broadcastState();
    return true;
}

void handleCommand(String cmd) {
    cmd.trim();
    if (cmd == "POWER1 ON") setRelayState(1, true);
    else if (cmd == "POWER1 OFF") setRelayState(1, false);
    else if (cmd == "POWER2 ON") setRelayState(2, true);
    else if (cmd == "POWER2 OFF") setRelayState(2, false);
    else if (cmd.startsWith("DIMMER ")) setDimmerLevel(cmd.substring(7).toInt());
    else if (cmd == "DIMM_UP") dimm_Up();
    else if (cmd == "DIMM_DN") dimm_Dn();
    else if (cmd == "ALL_ON") { setRelayState(1, true); setRelayState(2, true); setDimmerLevel(DIMMER_MAX); }
    else if (cmd == "ALL_OFF") { setRelayState(1, false); setRelayState(2, false); setDimmerLevel(DIMMER_MIN); }
    else if (cmd == "STATUS") {
        logMessage(">> [STATUS REQUEST] Refreshing States...");
        if (!mqttClient.connected()) {
            reconnectMQTT();
        }
        broadcastState();
        logMessage("-> Relay 1: " + String(relayStates[0] ? "ON" : "OFF"));
        logMessage("-> Relay 2: " + String(relayStates[1] ? "ON" : "OFF"));
        logMessage("-> Fan Dimmer Level: " + String(dimm_value) + " (Triac: " + String(triacState ? "ACTIVE" : "OFF") + ")");
        logMessage("-> MQTT Status: " + String(mqttClient.connected() ? "CONNECTED" : "DISCONNECTED"));
    }
}

String getStateJson(String logMsg) {
    if (logMsg.length() > 0) logMessage(logMsg);
    JsonDocument doc;
    JsonArray relays = doc["relays"].to<JsonArray>();
    relays.add(relayStates[0]);
    relays.add(relayStates[1]);
    doc["dimmer"] = dimm_value;
    doc["log"] = consoleLogBuffer;
    consoleLogBuffer = "";
    String output;
    serializeJson(doc, output);
    return output;
}

void setup() {
    Serial.begin(115200);
    EEPROM.begin(512);
    delay(1000);

    // Initialize pin modes early for safety
    pinMode(RelayPin1, OUTPUT);
    pinMode(RelayPin2, OUTPUT);
    pinMode(TriacPin, OUTPUT);
    digitalWrite(RelayPin1, HIGH);
    digitalWrite(RelayPin2, HIGH);
    digitalWrite(TriacPin, LOW);

    pinMode(ZeroCrossPin, INPUT_PULLUP);
    pinMode(SwitchPin1, INPUT_PULLUP);
    pinMode(SwitchPin2, INPUT_PULLUP);
    pinMode(FanSwitchPin, INPUT_PULLUP);
    pinMode(DimmerDownPin, INPUT_PULLUP);
    pinMode(DimmerUpPin, INPUT_PULLUP);

    ButtonConfig* btnConfig = ButtonConfig::getSystemButtonConfig();
    btnConfig->setFeature(ButtonConfig::kFeatureClick);
    btnConfig->setFeature(ButtonConfig::kFeatureLongPress);
    btnConfig->setLongPressDelay(1000);

    buttonRelay1.init(SwitchPin1);
    buttonRelay2.init(SwitchPin2);
    buttonFanOnOff.init(FanSwitchPin);
    buttonDimDown.init(DimmerDownPin);
    buttonDimUp.init(DimmerUpPin);

    btnConfig->setEventHandler(handleButtonEvent);

    // Check if first boot / unconfigured EEPROM
    uint8_t firstBootFlag = EEPROM.read(EEPROM_FIRST_BOOT);
    if (firstBootFlag != 0x5A) {
        logMessage(">> First boot detected: Clearing EEPROM and entering AP mode...");
        for (int i = 0; i < 256; i++) {
            EEPROM.write(EEPROM_CFG_START + i, 0);
        }
        EEPROM.commit();
        
        loadConfig();
        enterAPMode();
        return; // Skip normal station startup
    }

    loadConfig();

    timer1_isr_init();
    timer1_attachInterrupt(onTimer1Fire);

    attachInterrupt(digitalPinToInterrupt(ZeroCrossPin), handleZeroCross, RISING);

    eepromState();

    WiFi.begin(stored_ssid.c_str(), stored_password.c_str());
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
        delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
        logMessage(">> WiFi Connected Successfully!");
        logMessage(">> Local IP Address: " + WiFi.localIP().toString());
    } else {
        logMessage(">> WiFi Connection Failed. Entering background retry mode...");
    }

    espClient.setInsecure();
    mqttClient.setServer(mqtt_server.c_str(), mqtt_port);
    mqttClient.setCallback(callbackMQTT);
    mqttClient.setBufferSize(512);

    server.on("/", [](){ server.send_P(200, "text/html", MAIN_PAGE); });
    server.on("/config", [](){ server.send_P(200, "text/html", CONFIG_PAGE); });
    server.on("/get-config", [](){
        JsonDocument doc;
        doc["ssid"] = stored_ssid; doc["pass_wifi"] = stored_password;
        doc["server"] = mqtt_server; doc["port"] = mqtt_port;
        doc["user"] = mqtt_user; doc["pass"] = mqtt_pass;
        doc["tcmd"] = mqtt_topic_cmd; doc["tstate"] = mqtt_topic_state;
        String resp; serializeJson(doc, resp);
        server.send(200, "application/json", resp);
    });
    server.on("/cmd", [](){
        if (server.hasArg("action")) {
            handleCommand(server.arg("action"));
            server.send(200, "application/json", getStateJson());
        } else {
            server.send(400, "application/json", "{\"error\":\"Missing action\"}");
        }
    });
    server.on("/status-json", [](){ server.send(200, "application/json", getStateJson()); });
    server.on("/save-settings", HTTP_POST, [](){
        saveConfig(
            server.hasArg("wifi_ssid") ? server.arg("wifi_ssid") : DEFAULT_SSID,
            server.hasArg("wifi_pass") ? server.arg("wifi_pass") : DEFAULT_PASSWORD,
            server.hasArg("mqtt_server") ? server.arg("mqtt_server") : DEFAULT_MQTT_SERVER,
            server.hasArg("mqtt_port") ? server.arg("mqtt_port").toInt() : DEFAULT_MQTT_PORT,
            server.hasArg("mqtt_user") ? server.arg("mqtt_user") : DEFAULT_MQTT_USER,
            server.hasArg("mqtt_pass") ? server.arg("mqtt_pass") : DEFAULT_MQTT_PASS,
            server.hasArg("mqtt_topic_cmd") ? server.arg("mqtt_topic_cmd") : DEFAULT_MQTT_TOPIC_CMD,
            server.hasArg("mqtt_topic_state") ? server.arg("mqtt_topic_state") : DEFAULT_MQTT_TOPIC_STATE
        );

        String deviceIp = WiFi.localIP().toString();
        if (apModeActive || deviceIp == "0.0.0.0") {
            deviceIp = "192.168.4.1";
        }

        String html = "<script>setTimeout(function(){ window.location.href = 'http://" + deviceIp + "/'; }, 3500);</script>";
        html += "<h3>Settings Saved! Rebooting...</h3>";

        server.send(200, "text/html", html);
        delay(1000);
        ESP.restart();
    });
    server.begin();

    timer.every(10000, sendPeriodicStatus);
}

void loop() {
    if (apModeActive) {
        server.handleClient();
        buttonRelay1.check();
        buttonRelay2.check();
        buttonFanOnOff.check();
        buttonDimDown.check();
        buttonDimUp.check();
        return;
    }

    checkWiFiConnection();

    if (!mqttClient.connected()) {
        reconnectMQTT();
    } else {
        mqttClient.loop();
    }
    
    server.handleClient();

    buttonRelay1.check();
    buttonRelay2.check();
    buttonFanOnOff.check();
    buttonDimDown.check();
    buttonDimUp.check();

    timer.tick();

    while (Serial.available()) {
        char c = Serial.read();
        lastCharTime = millis();
        if (serialBuffer.length() < 512) serialBuffer += c;
        else serialBuffer = "";
    }
    if (serialBuffer.length() > 0 && (millis() - lastCharTime >= SERIAL_TIMEOUT)) {
        serialBuffer.trim();
        handleCommand(serialBuffer);
        serialBuffer = "";
    }
}
