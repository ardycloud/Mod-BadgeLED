#include "WebUI.h"
#include <ArduinoJson.h>
#include <ElegantOTA.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include "Settings.h"
#include "Config.h"
#include "Display.h"
#include "BQ25895CONFIG.h"  // Für Akku-Funktionen
#include <Arduino.h>

// Externe Variablen für LED
extern uint8_t gCurrentMode;
extern uint8_t BRIGHTNESS;
extern uint8_t NOISE_LEVEL;
extern uint16_t animationSpeed;
extern uint16_t micFrequency;
extern String apName;
extern String apPassword;
extern Settings settings;
extern DisplayContent displayContent;
extern bool displayAvailable;
extern QueueHandle_t displayQueue;
extern bool isLowBattery; // Externe Variable für niedrigen Batteriestand

// LIGHT_EN Pin für LED-Steuerung
#define LIGHT_EN 35

// Webserver auf Port 80
AsyncWebServer server(80);

// LED-Status - standardmäßig eingeschaltet
bool ledsEnabled = true;

// Globale Variable für Task-Handle
TaskHandle_t webServerTaskHandle = NULL;

// Logs speichern
const int MAX_LOG_ENTRIES = 100;
std::vector<String> espLogs;
std::vector<String> bqLogs;

// Globale Prototypen für WebUI.cpp
void addEspLog(const String& message);
void addBqLog(const String& message);

// HTML Templates
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>LED Badge Controller</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 0;
            padding: 20px;
            background: #1a1a1a;
            color: #fff;
        }
        .container {
            max-width: 600px;
            margin: 0 auto;
        }
        .card {
            background: #2d2d2d;
            border-radius: 10px;
            padding: 20px;
            margin-bottom: 20px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.2);
        }
        h1 {
            color:rgb(0, 191, 255);
            text-align: center;
        }
        .control-group {
            margin-bottom: 15px;
        }
        label {
            display: block;
            margin-bottom: 5px;
            color:rgb(0, 191, 255);
        }
        select, input[type="range"] {
            width: 100%;
            padding: 8px;
            border-radius: 5px;
            background: #404040;
            color: white;
            border: 1px solid rgb(0, 191, 255);
        }
        input[type="range"] {
            -webkit-appearance: none;
            height: 15px;
            background: #404040;
            outline: none;
            opacity: 0.7;
            transition: opacity .2s;
        }
        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 25px;
            height: 25px;
            background: rgb(0, 191, 255);
            cursor: pointer;
            border-radius: 50%;
        }
        .value-display {
            text-align: right;
            color:rgb(0, 191, 255);
            font-size: 0.9em;
        }
        .nav-links {
            display: flex;
            justify-content: space-between;
            margin-bottom: 20px;
        }
        .nav-links a {
            color: #00ff88;
            text-decoration: none;
            padding: 10px 20px;
            border: 1px solid #00ff88;
            border-radius: 5px;
        }
        .battery-info {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 15px;
        }
        .battery-icon {
            width: 50px;
            height: 25px;
            border: 2px solid rgb(0, 191, 255);
            border-radius: 3px;
            position: relative;
            margin-right: 15px;
        }
        .battery-icon:after {
            content: '';
            position: absolute;
            top: 20%;
            right: -5px;
            width: 3px;
            height: 60%;
            background: rgb(0, 191, 255);
            border-radius: 0 2px 2px 0;
        }
        .battery-level {
            height: 100%;
            background: rgb(0, 255, 100);
            transition: width 0.3s;
        }
        .battery-level.low {
            background: rgb(255, 100, 0);
        }
        .battery-level.critical {
            background: rgb(255, 0, 0);
        }
        .battery-status {
            font-size: 1.2em;
            color: #fff;
        }
        .charging-icon {
            color: rgb(255, 215, 0);
            font-size: 1.5em;
            margin-left: 10px;
        }
        .battery-stats {
            display: flex;
            flex-wrap: wrap;
            justify-content: space-between;
        }
        .battery-stat {
            width: 30%;
            margin-bottom: 10px;
            padding: 10px;
            background: #404040;
            border-radius: 5px;
            text-align: center;
        }
        .battery-stat .value {
            font-size: 1.2em;
            font-weight: bold;
            color: rgb(0, 191, 255);
        }
        .battery-stat .label {
            font-size: 0.8em;
            color: #aaa;
        }
        .header-with-status {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 15px;
        }
        .led-control-button {
            background: #ff4444;
            color: white;
            border: none;
            border-radius: 5px;
            padding: 10px 15px;
            cursor: pointer;
            font-weight: bold;
            margin-top: 15px;
            transition: background 0.3s;
            width: 100%;
        }
        .led-control-button:hover {
            background: #cc2222;
        }
        .led-control-button.on {
            background: #44ff44;
        }
        .led-control-button.on:hover {
            background: #22cc22;
        }
        .log-container {
            background: #111;
            color: #0f0;
            font-family: monospace;
            height: 300px;
            overflow-y: auto;
            padding: 10px;
            border-radius: 5px;
            margin-top: 15px;
            font-size: 12px;
        }
        .log-title {
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .log-title button {
            background: #333;
            color: #fff;
            border: none;
            border-radius: 3px;
            padding: 5px 10px;
            cursor: pointer;
            font-size: 0.8em;
        }
        .tab-buttons {
            display: flex;
            margin-bottom: 10px;
        }
        .tab-button {
            background: #333;
            border: 1px solid #444;
            color: #ccc;
            border-radius: 5px 5px 0 0;
            padding: 8px 15px;
            cursor: pointer;
            margin-right: 5px;
        }
        .tab-button.active {
            background: #111;
            color: #0f0;
            border-bottom: none;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="nav-links">
            <a href="/network">Netzwerk</a>
            <a href="/display">Display</a>
            <a href="/update">OTA Update</a>
        </div>

        <h1>LED Badge Controller</h1>

        <div class="card">
            <div class="control-group">
                <label for="mode">Animation Mode</label>
                <select id="mode" onchange="updateValue('mode', this.value)">
                    <option value="1">Color Wipe</option>
                    <option value="2">Twinkle</option>
                    <option value="3">Fire</option>
                    <option value="4">Pulse</option>
                    <option value="5">Wave</option>
                    <option value="6">Sparkle</option>
                    <option value="7">Gradient</option>
                    <option value="8">Dots</option>
                    <option value="9">Comet</option>
                    <option value="10">Bounce</option>
                    <option value="11">Music Reactive</option>
                </select>
            </div>
        </div>

        <div class="card">
            <div class="control-group">
                <label for="brightness">Brightness</label>
                <input type="range" id="brightness" min="5" max="255" value="5" 
                       oninput="updateValue('brightness', this.value)">
                <div class="value-display"><span id="brightnessValue">5</span></div>
            </div>
        </div>

        <div class="card">
            <div class="control-group">
                <label for="speed">Animation Speed</label>
                <input type="range" id="speed" min="1" max="50" value="20" 
                       oninput="updateValue('speed', this.value)">
                <div class="value-display"><span id="speedValue">20</span>ms</div>
            </div>
        </div>

        <div class="card">
            <div class="control-group">
                <label for="sensitivity">Microphone Sensitivity</label>
                <input type="range" id="sensitivity" min="1" max="50" value="10" 
                       oninput="updateValue('sensitivity', this.value)">
                <div class="value-display"><span id="sensitivityValue">10</span></div>
            </div>

            <div class="control-group">
                <label for="frequency">Microphone Frequency Response</label>
                <input type="range" id="frequency" min="10" max="200" value="50" 
                       oninput="updateValue('frequency', this.value)">
                <div class="value-display"><span id="frequencyValue">50</span>ms</div>
            </div>
        </div>

        <div class="card">
            <div class="header-with-status">
                <h2 style="color:rgb(0, 191, 255); margin: 0;">Akku Status</h2>
                <div class="battery-status">
                    <span id="batteryStatus">Entladen</span>
                    <span id="chargingIcon" style="display:none;">&#x26A1;</span>
                </div>
            </div>
            
            <div class="battery-info">
                <div class="battery-icon">
                    <div class="battery-level" id="batteryLevel" style="width:50%"></div>
                </div>
                <div>
                    <span id="batteryPercentage">50%</span>
                </div>
            </div>
            
            <div class="battery-stats">
                <div class="battery-stat">
                    <div class="value" id="batteryVoltage">3.7V</div>
                    <div class="label">Akku-Spannung</div>
                </div>
                <div class="battery-stat">
                    <div class="value" id="batteryCurrent">0 mA</div>
                    <div class="label">Ladestrom</div>
                </div>
                <div class="battery-stat">
                    <div class="value" id="batteryPower">0 mW</div>
                    <div class="label">Leistung</div>
                </div>
                <div class="battery-stat">
                    <div class="value" id="batteryTemperature">--</div>
                    <div class="label">Temperatur</div>
                </div>
                <div class="battery-stat">
                    <div class="value" id="batterySystemVoltage">--</div>
                    <div class="label">System-Spannung</div>
                </div>
                <div class="battery-stat">
                    <div class="value" id="batteryBusVoltage">--</div>
                    <div class="label">USB-Spannung</div>
                </div>
                <div class="battery-stat">
                    <div class="value" id="batteryInputCurrent">--</div>
                    <div class="label">Eingangsstrom</div>
                </div>
                <div class="battery-stat">
                    <div class="value" id="batteryChargeStatus">--</div>
                    <div class="label">Lade-Status</div>
                </div>
                <div class="battery-stat">
                    <div class="value" id="batteryFaultStatus">--</div>
                    <div class="label">Fehler-Status</div>
                </div>
            </div>
            
            <div style="margin-top: 15px; background: #333; padding: 10px; border-radius: 5px;">
                <h3 style="margin-top: 0; color: rgb(0, 191, 255);">Register-Werte</h3>
                <div id="registerValues" style="font-family: monospace; font-size: 12px; color: #ddd;"></div>
            </div>
            
            <div class="control-group">
                <label for="chargeCurrent">Ladestrom</label>
                <select id="chargeCurrent" onchange="setChargeCurrent(this.value)">
                    <option value="500">0.5A</option>
                    <option value="1000">1.0A</option>
                    <option value="1500" selected>1.5A</option>
                    <option value="2000">2.0A</option>
                    <option value="3000">3.0A</option>
                    <option value="4000">4.0A</option>
                </select>
            </div>
            
            <button id="ledControlButton" class="led-control-button" onclick="toggleLEDs()">LEDs ausschalten</button>
        </div>
        
        <div class="card">
            <div class="log-title">
                <h2 style="color:rgb(0, 191, 255); margin: 0;">System Logs</h2>
                <button onclick="clearLogs()">Clear Logs</button>
            </div>
            
            <div class="tab-buttons">
                <button class="tab-button active" onclick="switchTab('esp')">ESP32 Logs</button>
                <button class="tab-button" onclick="switchTab('bq')">BQ25895 Logs</button>
            </div>
            
            <div id="esp-logs" class="log-container"></div>
            <div id="bq-logs" class="log-container" style="display: none;"></div>
        </div>
    </div>

    <script>
        // Globale Variable für den LED-Status
        let ledsEnabled = true;
        let currentLogType = 'esp';
        
        // Puffer für Logs
        let espLogs = [];
        let bqLogs = [];
        const maxLogLines = 100;

        function updateValue(param, value) {
            const xhr = new XMLHttpRequest();
            xhr.open("GET", `/settings?param=${param}&value=${value}`, true);
            xhr.send();
            
            // Update display value
            const displayElement = document.getElementById(param + 'Value');
            if (displayElement) {
                displayElement.textContent = value;
            }
        }

        // LED-Steuerung
        function toggleLEDs() {
            const button = document.getElementById('ledControlButton');
            ledsEnabled = !ledsEnabled;
            
            // Button-Text und -Farbe ändern
            if (ledsEnabled) {
                button.textContent = 'LEDs ausschalten';
                button.classList.remove('on');
            } else {
                button.textContent = 'LEDs einschalten';
                button.classList.add('on');
            }
            
            // API-Anfrage senden
            fetch(`/toggle-leds?enabled=${ledsEnabled ? 1 : 0}`)
            .then(response => response.text())
            .then(result => {
                console.log('LED-Steuerung: ' + result);
            })
            .catch(error => {
                console.error('Fehler bei LED-Steuerung:', error);
            });
        }

        // Initial values
        window.onload = function() {
            fetch('/values')
            .then(response => response.json())
            .then(data => {
                document.getElementById('mode').value = data.mode;
                document.getElementById('brightness').value = data.brightness;
                document.getElementById('brightnessValue').textContent = data.brightness;
                document.getElementById('speed').value = data.speed;
                document.getElementById('speedValue').textContent = data.speed;
                document.getElementById('sensitivity').value = data.sensitivity;
                document.getElementById('sensitivityValue').textContent = data.sensitivity;
                document.getElementById('frequency').value = data.frequency;
                document.getElementById('frequencyValue').textContent = data.frequency;
                
                // LED-Status abrufen und Button aktualisieren
                if (data.ledsEnabled !== undefined) {
                    ledsEnabled = data.ledsEnabled;
                    const button = document.getElementById('ledControlButton');
                    if (!ledsEnabled) {
                        button.textContent = 'LEDs einschalten';
                        button.classList.add('on');
                    }
                }
            });
            
            // Initiale Akku-Daten abrufen
            updateBatteryInfo();
            
            // Akku-Daten alle 1 Sekunde aktualisieren
            setInterval(updateBatteryInfo, 1000);
            
            // Logs regelmäßig abrufen
            setInterval(fetchLogs, 2000);
            fetchLogs(); // Initial logs abrufen
        }
        
        // Akku-Informationen aktualisieren
        function updateBatteryInfo() {
            fetch('/battery-status')
            .then(response => response.json())
            .then(data => {
                // Spannungen
                document.getElementById('batteryVoltage').textContent = data.vbat.toFixed(2) + 'V';
                document.getElementById('batterySystemVoltage').textContent = data.vsys.toFixed(2) + 'V';
                document.getElementById('batteryBusVoltage').textContent = data.vbus.toFixed(2) + 'V';
                
                // Prozentsatz
                document.getElementById('batteryPercentage').textContent = data.percentage + '%';
                
                // Batteriestand-Balken aktualisieren
                const batteryLevel = document.getElementById('batteryLevel');
                batteryLevel.style.width = data.percentage + '%';
                
                // Farbe je nach Ladezustand
                batteryLevel.className = 'battery-level';
                if (data.percentage < 20) {
                    batteryLevel.classList.add('critical');
                } else if (data.percentage < 50) {
                    batteryLevel.classList.add('low');
                }
                
                // Ladezustand
                const chargingIcon = document.getElementById('chargingIcon');
                if (data.isCharging) {
                    chargingIcon.style.display = 'inline';
                    chargingIcon.innerHTML = '&#x26A1;'; // HTML-kodiertes Blitzsymbol
                    document.getElementById('batteryStatus').textContent = 'Wird geladen';
                } else {
                    chargingIcon.style.display = 'none';
                    document.getElementById('batteryStatus').textContent = 'Entladen';
                }
                
                // Ströme
                document.getElementById('batteryCurrent').textContent = data.ichg.toFixed(0) + ' mA';
                document.getElementById('batteryInputCurrent').textContent = data.iin.toFixed(0) + ' mA';
                
                // Leistung in Watt anzeigen
                document.getElementById('batteryPower').textContent = data.power.toFixed(3) + ' W';
                
                // Temperatur und Status anzeigen
                document.getElementById('batteryTemperature').textContent = data.temperature;
                document.getElementById('batteryChargeStatus').textContent = data.chargeStatus;
                document.getElementById('batteryFaultStatus').textContent = 
                    data.faultStatus.length > 0 ? data.faultStatus : "Keine Fehler";
                
                // Register-Werte anzeigen
                if (data.registers) {
                    let regHtml = '';
                    for (const [reg, value] of Object.entries(data.registers)) {
                        regHtml += `<div>${reg}: 0x${value.toString(16).padStart(2, '0')} (${value})</div>`;
                    }
                    document.getElementById('registerValues').innerHTML = regHtml;
                }
            })
            .catch(error => {
                console.error('Fehler beim Abrufen der Akku-Daten:', error);
            });
        }
        
        // Funktion für Ladestrom-Anpassung
        function setChargeCurrent(mA) {
            fetch(`/set-charge-current?current=${mA}`)
            .then(response => response.text())
            .then(result => {
                console.log('Ladestrom geändert: ' + result);
                addLogEntry('esp', 'Ladestrom auf ' + mA + ' mA gesetzt');
            })
            .catch(error => {
                console.error('Fehler bei Ladestrom-Einstellung:', error);
            });
        }
        
        // Log-Tab wechseln
        function switchTab(logType) {
            document.getElementById('esp-logs').style.display = logType === 'esp' ? 'block' : 'none';
            document.getElementById('bq-logs').style.display = logType === 'bq' ? 'block' : 'none';
            
            const buttons = document.querySelectorAll('.tab-button');
            buttons.forEach(btn => {
                btn.classList.remove('active');
            });
            
            const activeButton = Array.from(buttons).find(btn => 
                btn.textContent.toLowerCase().includes(logType));
            if (activeButton) {
                activeButton.classList.add('active');
            }
            
            currentLogType = logType;
        }
        
        // Logs löschen
        function clearLogs() {
            if (currentLogType === 'esp') {
                espLogs = [];
                document.getElementById('esp-logs').innerHTML = '';
            } else {
                bqLogs = [];
                document.getElementById('bq-logs').innerHTML = '';
            }
            
            // Auch auf dem Server löschen
            fetch('/clear-logs?type=' + currentLogType)
            .then(response => response.text())
            .then(result => {
                console.log('Logs gelöscht: ' + result);
            });
        }
        
        // Lokaler Log-Eintrag hinzufügen
        function addLogEntry(type, message) {
            const timestamp = new Date().toLocaleTimeString();
            const logEntry = `[${timestamp}] ${message}`;
            
            if (type === 'esp') {
                espLogs.push(logEntry);
                if (espLogs.length > maxLogLines) {
                    espLogs.shift(); // Ältesten Eintrag entfernen
                }
                updateLogDisplay('esp-logs', espLogs);
            } else {
                bqLogs.push(logEntry);
                if (bqLogs.length > maxLogLines) {
                    bqLogs.shift();
                }
                updateLogDisplay('bq-logs', bqLogs);
            }
        }
        
        // Log-Anzeige aktualisieren
        function updateLogDisplay(containerId, logs) {
            const container = document.getElementById(containerId);
            const wasScrolledToBottom = container.scrollHeight - container.scrollTop === container.clientHeight;
            
            container.innerHTML = logs.join('<br>');
            
            // Nur scrollen wenn vorher am Ende war
            if (wasScrolledToBottom) {
                container.scrollTop = container.scrollHeight;
            }
        }
        
        // Logs vom Server abrufen
        function fetchLogs() {
            fetch('/get-logs')
            .then(response => response.json())
            .then(data => {
                if (data.esp && data.esp.length > 0) {
                    espLogs = data.esp;
                    updateLogDisplay('esp-logs', espLogs);
                }
                
                if (data.bq && data.bq.length > 0) {
                    bqLogs = data.bq;
                    updateLogDisplay('bq-logs', bqLogs);
                }
            })
            .catch(error => {
                console.error('Fehler beim Abrufen der Logs:', error);
            });
        }
    </script>
</body>
</html>
)rawliteral";

const char network_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Netzwerk Konfiguration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 0;
            padding: 20px;
            background: #1a1a1a;
            color: #fff;
        }
        .container {
            max-width: 600px;
            margin: 0 auto;
        }
        .card {
            background: #2d2d2d;
            border-radius: 10px;
            padding: 20px;
            margin-bottom: 20px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.2);
        }
        h1 {
            color: #00ff88;
            text-align: center;
        }
        .control-group {
            margin-bottom: 15px;
        }
        label {
            display: block;
            margin-bottom: 5px;
            color: #00ff88;
        }
        input[type="text"], input[type="password"] {
            width: 100%;
            padding: 8px;
            border-radius: 5px;
            background: #404040;
            color: white;
            border: 1px solid #00ff88;
            box-sizing: border-box;
        }
        button {
            background: #00ff88;
            color: #1a1a1a;
            border: none;
            padding: 10px 20px;
            border-radius: 5px;
            cursor: pointer;
            width: 100%;
            font-weight: bold;
            margin-bottom: 10px;
        }
        button:hover {
            background: #00cc6a;
        }
        .nav-buttons {
            display: flex;
            justify-content: space-between;
            margin-bottom: 20px;
        }
        .nav-buttons a {
            color: #00ff88;
            text-decoration: none;
            padding: 10px 20px;
            border: 1px solid #00ff88;
            border-radius: 5px;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="nav-buttons">
            <a href="/">Zur Startseite</a>
        </div>
        
        <h1>Netzwerk Konfiguration</h1>
        
        <div class="card">
            <div class="control-group">
                <label for="apname">Access Point Name</label>
                <input type="text" id="apname" maxlength="31" value="LED-Badge">
            </div>
        </div>

        <div class="card">
            <div class="control-group">
                <label for="appassword">Access Point Passwort (optional)</label>
                <input type="password" id="appassword" maxlength="63" placeholder="Leer lassen für offenen AP">
            </div>
        </div>

        <div class="card">
            <button onclick="updateNetwork()">Netzwerk aktualisieren</button>
        </div>
    </div>

    <script>
        // Lade aktuelle Werte
        window.onload = function() {
            fetch('/network-values')
            .then(response => response.json())
            .then(data => {
                document.getElementById('apname').value = data.apname || 'LED-Badge';
                document.getElementById('appassword').value = data.appassword || '';
            });
        }

        function updateNetwork() {
            const data = {
                apname: document.getElementById('apname').value,
                appassword: document.getElementById('appassword').value
            };

            fetch('/update-network', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify(data)
            })
            .then(response => response.text())
            .then(result => {
                if(result === 'OK') {
                    alert('Netzwerk wird aktualisiert. Bitte verbinden Sie sich neu mit dem Badge.');
                } else {
                    alert('Fehler beim Aktualisieren!');
                }
            });
        }
    </script>
</body>
</html>
)rawliteral";

const char display_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Display Konfiguration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 0;
            padding: 20px;
            background: #1a1a1a;
            color: #fff;
        }
        .container {
            max-width: 600px;
            margin: 0 auto;
        }
        .card {
            background: #2d2d2d;
            border-radius: 10px;
            padding: 20px;
            margin-bottom: 20px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.2);
        }
        h1 {
            color: #00ff88;
            text-align: center;
        }
        .control-group {
            margin-bottom: 15px;
        }
        label {
            display: block;
            margin-bottom: 5px;
            color: #00ff88;
        }
        input[type="text"], textarea {
            width: 100%;
            padding: 8px;
            border-radius: 5px;
            background: #404040;
            color: white;
            border: 1px solid #00ff88;
            box-sizing: border-box;
        }
        textarea {
            height: 100px;
            resize: vertical;
        }
        button {
            background: #00ff88;
            color: #1a1a1a;
            border: none;
            padding: 10px 20px;
            border-radius: 5px;
            cursor: pointer;
            width: 100%;
            font-weight: bold;
            margin-bottom: 10px;
        }
        button:hover {
            background: #00cc6a;
        }
        .nav-buttons {
            display: flex;
            justify-content: space-between;
            margin-bottom: 20px;
        }
        .nav-buttons a {
            color: #00ff88;
            text-decoration: none;
            padding: 10px 20px;
            border: 1px solid #00ff88;
            border-radius: 5px;
        }
        .test-button {
            background: #ff8800 !important;
        }
        .test-button:hover {
            background: #cc6a00 !important;
        }
        .clear-button {
            background: #ff4444 !important;
        }
        .clear-button:hover {
            background: #cc2222 !important;
        }
        .toggle-switch {
            position: relative;
            display: inline-block;
            width: 60px;
            height: 30px;
        }
        .toggle-switch input {
            opacity: 0;
            width: 0;
            height: 0;
        }
        .toggle-slider {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: #404040;
            border: 1px solid #00ff88;
            transition: .4s;
            border-radius: 30px;
        }
        .toggle-slider:before {
            position: absolute;
            content: "";
            height: 22px;
            width: 22px;
            left: 4px;
            bottom: 3px;
            background-color: #00ff88;
            transition: .4s;
            border-radius: 50%;
        }
        input:checked + .toggle-slider {
            background-color: #205f4d;
        }
        input:checked + .toggle-slider:before {
            transform: translateX(30px);
        }
        .image-preview {
            width: 64px;
            height: 64px;
            border: 2px solid #00ff88;
            margin: 10px 0;
            background-color: #404040;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 12px;
            color: #aaa;
        }
        .image-preview img {
            max-width: 100%;
            max-height: 100%;
            display: none;
        }
        .file-upload {
            position: relative;
            display: inline-block;
            width: 100%;
            margin-top: 10px;
        }
        .file-upload-input {
            position: absolute;
            left: 0;
            top: 0;
            opacity: 0;
            width: 100%;
            height: 100%;
            cursor: pointer;
        }
        .file-upload-button {
            display: inline-block;
            background-color: #005f4d;
            color: white;
            padding: 10px;
            border-radius: 5px;
            border: 1px solid #00ff88;
            width: 100%;
            text-align: center;
        }
        .toggle-container {
            display: flex;
            align-items: center;
            justify-content: space-between;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="nav-buttons">
            <a href="/">Zur Startseite</a>
        </div>
        
        <h1>Display Konfiguration</h1>
        
        <div class="card">
            <div class="control-group">
                <label for="name">Name (wird in Rot angezeigt)</label>
                <input type="text" id="name" maxlength="31" value="ArdyMoon">
            </div>
        </div>

        <div class="card">
            <div class="control-group">
                <label for="description">Beschreibung</label>
                <textarea id="description" maxlength="255"></textarea>
            </div>
        </div>

        <div class="card">
            <div class="control-group">
                <label for="telegram">Telegram Name (ohne @)</label>
                <input type="text" id="telegram" maxlength="31">
            </div>
        </div>

        <div class="card">
            <div class="control-group">
                <label>Bild hochladen (64x64 Pixel)</label>
                <div class="image-preview" id="imagePreview">
                    <img id="previewImg">
                    <span id="noImageText">Kein Bild</span>
                </div>
                <div class="file-upload">
                    <input type="file" class="file-upload-input" id="imageUpload" accept="image/*">
                    <div class="file-upload-button">Bild auswählen</div>
                </div>
                <p style="font-size: 12px; color: #aaa; margin-top: 5px;">
                    Hinweis: Für beste Ergebnisse lade ein 64x64 Pixel großes Schwarz-Weiß-Bild hoch. 
                    Unterstützte Formate: JPG, PNG. Bilder werden auf dem Display nur als Platzhalter angezeigt.
                </p>
            </div>
        </div>

        <div class="card">
            <div class="control-group">
                <div class="toggle-container">
                    <label>Farben invertieren (schwarzer Hintergrund)</label>
                    <label class="toggle-switch">
                        <input type="checkbox" id="invertColors">
                        <span class="toggle-slider"></span>
                    </label>
                </div>
            </div>
        </div>

        <div class="card">
            <button onclick="updateDisplay()">Aktualisieren</button>
            <button class="test-button" onclick="testDisplay()">Display Test</button>
            <button class="clear-button" style="background: #ff4444 !important; margin-top: 10px;" onclick="clearDisplay()">Display leeren</button>
        </div>
    </div>

    <script>
        // Bild-Upload und Vorschau
        const imageUpload = document.getElementById('imageUpload');
        const previewImg = document.getElementById('previewImg');
        const noImageText = document.getElementById('noImageText');
        let imageData = null;

        imageUpload.addEventListener('change', function(e) {
            const file = e.target.files[0];
            if (!file) return;

            // Überprüfe Dateigröße (max. 5MB)
            if (file.size > 5 * 1024 * 1024) {
                alert('Bild zu groß! Maximale Größe ist 5MB.');
                return;
            }

            const reader = new FileReader();
            reader.onload = function(e) {
                previewImg.src = e.target.result;
                previewImg.style.display = 'block';
                noImageText.style.display = 'none';
                imageData = e.target.result;
            };
            reader.readAsDataURL(file);
        });

        // Lade aktuelle Werte
        window.onload = function() {
            fetch('/display-values')
            .then(response => response.json())
            .then(data => {
                document.getElementById('name').value = data.name;
                document.getElementById('description').value = data.description;
                document.getElementById('telegram').value = data.telegram;
                document.getElementById('invertColors').checked = data.invertColors;
                
                // Bild laden, falls vorhanden
                if (data.imagePath) {
                    fetch('/image' + data.imagePath)
                    .then(response => {
                        if (response.ok) {
                            return response.blob();
                        }
                        throw new Error('Bild konnte nicht geladen werden');
                    })
                    .then(blob => {
                        const url = URL.createObjectURL(blob);
                        previewImg.src = url;
                        previewImg.style.display = 'block';
                        noImageText.style.display = 'none';
                    })
                    .catch(error => {
                        console.error('Fehler beim Laden des Bildes:', error);
                    });
                }
            });
        }

        function updateDisplay() {
            // Daten direkt als einfaches Objekt zusammenstellen
            const name = document.getElementById('name').value;
            const description = document.getElementById('description').value;
            const telegram = document.getElementById('telegram').value;
            const invertColors = document.getElementById('invertColors').checked;
            
            // Verwende ein einfaches GET wie beim simple-update Endpunkt, der funktioniert
            const url = `/simple-update?name=${encodeURIComponent(name)}&description=${encodeURIComponent(description)}&telegram=${encodeURIComponent(telegram)}&invert=${invertColors ? '1' : '0'}`;
            
            fetch(url)
            .then(response => response.text())
            .then(result => {
                if(result.includes("Simple Update")) {
                    alert('Display wird aktualisiert, Bitte warten...');
                } else {
                    alert('Fehler beim Aktualisieren!');
                }
            });
        }

        function testDisplay() {
            if (isLowBattery) {
                alert('Display-Update wegen niedriger Batterie nicht möglich');
                return;
            }
            
            fetch('/test-display')
            .then(response => response.text())
            .then(result => {
                if(result === 'OK') {
                    alert('Display-Test wurde gestartet!');
                } else {
                    alert('Fehler beim Display-Test!');
                }
            });
        }

        function clearDisplay() {
            if (isLowBattery) {
                alert('Display-Update wegen niedriger Batterie nicht möglich');
                return;
            }
            
            fetch('/clear-display')
            .then(response => response.text())
            .then(result => {
                if(result === 'OK') {
                    alert('Display wurde geleert!');
                    // Formularfelder leeren
                    document.getElementById('name').value = '';
                    document.getElementById('description').value = '';
                    document.getElementById('telegram').value = '';
                    document.getElementById('invertColors').checked = false;
                    previewImg.style.display = 'none';
                    noImageText.style.display = 'block';
                    imageData = null;
                    imageUpload.value = '';
                } else {
                    alert('Fehler beim leeren des Displays!');
                }
            });
        }
    </script>
</body>
</html>
)rawliteral";

// Webserver-Task zum Starten des Servers auf einem separaten Core
void webServerTask(void *parameter) {
    delay(1000); // Verzögerung zum Stabilisieren des Systems
    
    // Starte den Server
    try {
        server.begin();
        Serial.println("Webserver erfolgreich gestartet!");
        addEspLog("Webserver erfolgreich gestartet!");
    } catch (const std::exception& e) {
        Serial.print("Fehler beim WebServer-Start: ");
        Serial.println(e.what());
        addEspLog("Fehler beim WebServer-Start: " + String(e.what()));
    } catch (...) {
        Serial.println("Unbekannter Fehler beim WebServer-Start");
        addEspLog("Unbekannter Fehler beim WebServer-Start");
    }
    
    // Task bleibt aktiv und bearbeitet WebServer-Events
    for(;;) {
        delay(1000);
    }
}

void initWebServer() {
    try {
        Serial.println("Konfiguriere WebServer...");
        addEspLog("Starte WebServer-Konfiguration");
        
        // Server-Konfiguration
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
        
        // ElegantOTA initialisieren
        Serial.println("Initialisiere ElegantOTA...");
        ElegantOTA.begin(&server, "", "");    // Mit leeren Authentifizierungsparametern aufrufen
        Serial.println("OTA-Server initialisiert");
        addEspLog("OTA-Server initialisiert");
        
        // Route für die Hauptseite
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(200, "text/html", index_html);
        });

        // Route für die Netzwerk-Konfigurationsseite
        server.on("/network", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(200, "text/html", network_html);
        });

        // Route für die Display-Konfigurationsseite
        server.on("/display", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(200, "text/html", display_html);
        });

        // Route für die aktuellen Werte
        server.on("/values", HTTP_GET, [](AsyncWebServerRequest *request){
            StaticJsonDocument<200> doc;
            doc["mode"] = gCurrentMode;
            doc["brightness"] = BRIGHTNESS;
            doc["speed"] = animationSpeed;
            doc["sensitivity"] = NOISE_LEVEL;
            doc["frequency"] = micFrequency;
            doc["ledsEnabled"] = ledsEnabled;

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        });

        // Route für die Netzwerk-Werte
        server.on("/network-values", HTTP_GET, [](AsyncWebServerRequest *request){
            StaticJsonDocument<200> doc;
            doc["apname"] = apName;
            doc["appassword"] = apPassword;

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        });

        // Route für die Display-Werte
        server.on("/display-values", HTTP_GET, [](AsyncWebServerRequest *request){
            StaticJsonDocument<512> doc;
            doc["name"] = displayContent.name;
            doc["description"] = displayContent.description;
            doc["telegram"] = displayContent.telegram;
            doc["imagePath"] = displayContent.imagePath;
            doc["invertColors"] = displayContent.invertColors;

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        });

        // Route für die OTA-Update-Seite
        server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
            String html = "<!DOCTYPE html><html><head><title>OTA Update</title>";
            html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
            html += "<style>body{font-family:Arial;text-align:center;margin:20px;}";
            html += "a{color:#2196F3;text-decoration:none;margin:10px;}";
            html += "a:hover{text-decoration:underline;}</style></head>";
            html += "<body><h1>OTA Update</h1>";
            html += "<p>Bitte öffnen Sie <a href='/update' target='_blank'>hier</a> für das OTA-Update.</p>";
            html += "<p><a href='/'>Zurück zur Hauptseite</a></p></body></html>";
            request->send(200, "text/html", html);
        });

        // Route für Einstellungs-Updates
        server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request){
            if (request->hasParam("param") && request->hasParam("value")) {
                String param = request->getParam("param")->value();
                String value = request->getParam("value")->value();
                
                if (param == "mode") {
                    gCurrentMode = value.toInt();
                } else if (param == "brightness") {
                    BRIGHTNESS = value.toInt();
                    if (BRIGHTNESS < 5) BRIGHTNESS = 5; // Mindesthelligkeit
                    FastLED.setBrightness(BRIGHTNESS);
                } else if (param == "speed") {
                    animationSpeed = value.toInt();
                } else if (param == "sensitivity") {
                    NOISE_LEVEL = value.toInt();
                } else if (param == "frequency") {
                    micFrequency = value.toInt();
                }
                
                saveSettings();
            }
            request->send(200, "text/plain", "OK");
        });

        // Route für Netzwerk-Updates
        server.on("/update-network", HTTP_POST, [](AsyncWebServerRequest *request){
            request->send(200, "text/plain", "OK");
        }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
            if (index + len == total) {
                StaticJsonDocument<200> doc;
                deserializeJson(doc, (char*)data);
                
                String newApName = doc["apname"].as<String>();
                String newApPassword = doc["appassword"].as<String>();
                
                Serial.print("Aktualisiere WiFi AP - Name: ");
                Serial.print(newApName);
                Serial.print(", Passwort: ");
                Serial.println(newApPassword.length() > 0 ? "[gesetzt]" : "[leer]");
                
                apName = newApName;
                apPassword = newApPassword;
                
                // WiFi neu starten
                WiFi.disconnect();
                WiFi.softAP(apName.c_str(), apPassword.length() > 0 ? apPassword.c_str() : NULL);
                
                Serial.print("WiFi AP neu gestartet als: ");
                Serial.println(apName);
                
                // Einstellungen speichern
                settings.apName = apName;
                settings.apPassword = apPassword;
                saveSettings();
                
                addEspLog("Netzwerk aktualisiert: AP-Name=" + apName);
            }
        });

        // Route für Display-Updates mit Multipart-Formular für Bilder
        server.on("/update-display", HTTP_POST, 
            [](AsyncWebServerRequest *request){
                // Dieser Teil wird nach dem Empfang aller Teile aufgerufen
                Serial.println("Display-Update Anfrage komplett empfangen");
                
                // Prüfen ob Batterie zu niedrig ist
                if (isLowBattery) {
                    Serial.println("Display-Update wegen niedriger Batterie abgebrochen");
                    request->send(403, "text/plain", "Display-Update wegen niedriger Batterie nicht möglich");
                } else {
                    request->send(200, "text/plain", "OK");
                }
            },
            [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
                // Datei-Upload-Handler für Bilder
                static String imagePath = "/badge_image.bmp";
                static File imageFile;
                
                if(!index) {
                    Serial.println("Bildupload empfangen: " + filename);
                    if(SPIFFS.exists(imagePath)) {
                        SPIFFS.remove(imagePath);
                    }
                    imageFile = SPIFFS.open(imagePath, "w");
                }
                
                if(imageFile) {
                    imageFile.write(data, len);
                }
                
                if(final) {
                    Serial.println("Bildupload abgeschlossen");
                    imageFile.close();
                    displayContent.imagePath = imagePath;
                }
            },
            [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
                // Formular-Daten Handler für normale Felder
                static String jsonBuffer;
                
                if(index == 0){
                    jsonBuffer = "";
                }
                
                for(size_t i=0; i<len; i++){
                    jsonBuffer += (char)data[i];
                }
                
                if(index + len == total){
                    // Alle Daten empfangen
                    Serial.println("Formular-Daten empfangen: " + jsonBuffer);
                    
                    // Versuche direkt als JSON zu parsen
                    DynamicJsonDocument doc(1024);
                    DeserializationError error = deserializeJson(doc, jsonBuffer);
                    
                    if(!error){
                        // Extrahiere Formularfelder
                        Serial.println("Formular-Daten gültig, aktualisiere Display-Inhalt");
                        bool updated = false;
                        
                        if(doc.containsKey("name")) {
                            displayContent.name = doc["name"].as<String>();
                            Serial.println("Name: " + displayContent.name);
                            updated = true;
                        }
                        if(doc.containsKey("description")) {
                            displayContent.description = doc["description"].as<String>();
                            Serial.println("Beschreibung: " + displayContent.description);
                            updated = true;
                        }
                        if(doc.containsKey("telegram")) {
                            displayContent.telegram = doc["telegram"].as<String>();
                            Serial.println("Telegram: " + displayContent.telegram);
                            updated = true;
                        }
                        if(doc.containsKey("invertColors")) {
                            displayContent.invertColors = doc["invertColors"].as<bool>();
                            Serial.println("Farben invertiert: " + String(displayContent.invertColors));
                            updated = true;
                        }
                        
                        if (updated) {
                            // Speichern und Display aktualisieren
                            Serial.println("Speichere Display-Inhalt und aktualisiere Display");
                            saveDisplayContent();
                            
                            // Als Fallback versuchen wir, das Display direkt zu aktualisieren
                            if (displayAvailable && displayQueue != NULL) {
                                Serial.println("Versuche direktes Update des Displays als Fallback...");
                                DisplayCommand cmd = CMD_UPDATE;
                                xQueueSend(displayQueue, &cmd, pdMS_TO_TICKS(1000));
                            }
                        }
                    } else {
                        Serial.println("Fehler beim Parsen der Formular-Daten: " + String(error.c_str()));
                        Serial.println("Versuche alternatives Parsing...");
                        
                        // Alternative Methode für Formular-Daten: name=wert&name2=wert2 usw.
                        String formStr = jsonBuffer;
                        bool updated = false;
                        
                        if (formStr.indexOf("name=") >= 0) {
                            int start = formStr.indexOf("name=") + 5;
                            int end = formStr.indexOf("&", start);
                            if (end < 0) end = formStr.length();
                            displayContent.name = formStr.substring(start, end);
                            displayContent.name.replace("+", " "); // Fix spaces
                            Serial.println("Alt-Parse: Name=" + displayContent.name);
                            updated = true;
                        }
                        
                        if (formStr.indexOf("description=") >= 0) {
                            int start = formStr.indexOf("description=") + 12;
                            int end = formStr.indexOf("&", start);
                            if (end < 0) end = formStr.length();
                            displayContent.description = formStr.substring(start, end);
                            displayContent.description.replace("+", " "); // Fix spaces
                            Serial.println("Alt-Parse: Beschreibung=" + displayContent.description);
                            updated = true;
                        }
                        
                        if (formStr.indexOf("telegram=") >= 0) {
                            int start = formStr.indexOf("telegram=") + 9;
                            int end = formStr.indexOf("&", start);
                            if (end < 0) end = formStr.length();
                            displayContent.telegram = formStr.substring(start, end);
                            Serial.println("Alt-Parse: Telegram=" + displayContent.telegram);
                            updated = true;
                        }
                        
                        if (formStr.indexOf("invertColors=") >= 0) {
                            int start = formStr.indexOf("invertColors=") + 13;
                            int end = formStr.indexOf("&", start);
                            if (end < 0) end = formStr.length();
                            String val = formStr.substring(start, end);
                            displayContent.invertColors = (val == "true" || val == "on" || val == "1");
                            Serial.println("Alt-Parse: Invertiert=" + String(displayContent.invertColors));
                            updated = true;
                        }
                        
                        if (updated) {
                            Serial.println("Alternative Parsing erfolgreich. Aktualisiere Display...");
                            saveDisplayContent();
                            
                            // Als Fallback versuchen wir, das Display direkt zu aktualisieren
                            if (displayAvailable && displayQueue != NULL) {
                                Serial.println("Versuche direktes Update des Displays als Fallback...");
                                DisplayCommand cmd = CMD_UPDATE;
                                xQueueSend(displayQueue, &cmd, pdMS_TO_TICKS(1000));
                            }
                        }
                    }
                }
            });

        // Alternative Multipart-Formular-Verarbeitung für bessere Bild-Unterstützung
        AsyncCallbackJsonWebHandler* displayHandler = new AsyncCallbackJsonWebHandler("/update-display-json", [](AsyncWebServerRequest *request, JsonVariant &json) {
            // Prüfen ob Batterie zu niedrig ist
            if (isLowBattery) {
                Serial.println("Display-Update wegen niedriger Batterie abgebrochen");
                request->send(403, "text/plain", "Display-Update wegen niedriger Batterie nicht möglich");
                return;
            }
            
            JsonObject jsonObj = json.as<JsonObject>();
            
            if(jsonObj.containsKey("name")) displayContent.name = jsonObj["name"].as<String>();
            if(jsonObj.containsKey("description")) displayContent.description = jsonObj["description"].as<String>();
            if(jsonObj.containsKey("telegram")) displayContent.telegram = jsonObj["telegram"].as<String>();
            if(jsonObj.containsKey("invertColors")) displayContent.invertColors = jsonObj["invertColors"].as<bool>();
            
            saveDisplayContent();
            request->send(200, "text/plain", "OK");
        });
        server.addHandler(displayHandler);

        // Bild-Upload-Route
        server.on("/upload-image", HTTP_POST, 
            [](AsyncWebServerRequest *request) {
                // Prüfen ob Batterie zu niedrig ist
                if (isLowBattery) {
                    Serial.println("Display-Update wegen niedriger Batterie abgebrochen");
                    request->send(403, "text/plain", "Display-Update wegen niedriger Batterie nicht möglich");
                } else {
                    request->send(200, "text/plain", "OK");
                }
            },
            [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
                static String imagePath = "/badge_image.bmp";
                static File imageFile;
                
                // Unterstützte Formate: Aktuell nur raw 1-bit Bilder für Platzhalter
                // Später zu implementieren: BMP, JPEG oder PNG mit 64x64 Pixel
                
                if(!index) {
                    Serial.println("Starte Bildupload...");
                    if(SPIFFS.exists(imagePath)) {
                        SPIFFS.remove(imagePath);
                    }
                    imageFile = SPIFFS.open(imagePath, "w");
                    if (!imageFile) {
                        Serial.println("Fehler beim Öffnen der Bilddatei zum Schreiben");
                    } else {
                        Serial.println("Datei zum Schreiben geöffnet");
                    }
                }
                
                if(imageFile) {
                    Serial.printf("Bildupload: %d Bytes empfangen\n", len);
                    if (imageFile.write(data, len) != len) {
                        Serial.println("Fehler beim Schreiben der Bilddaten");
                    }
                }
                
                if(final) {
                    Serial.println("Bildupload abgeschlossen");
                    imageFile.close();
                    displayContent.imagePath = imagePath;
                    saveDisplayContent();
                }
            });

        // Route für Display-Test
        server.on("/test-display", HTTP_GET, [](AsyncWebServerRequest *request){
            if (isLowBattery) {
                request->send(403, "text/plain", "Display-Update wegen niedriger Batterie nicht möglich");
            } else {
                testDisplay();
                request->send(200, "text/plain", "OK");
            }
        });

        // Route für Display leeren
        server.on("/clear-display", HTTP_GET, [](AsyncWebServerRequest *request){
            if (isLowBattery) {
                request->send(403, "text/plain", "Display-Update wegen niedriger Batterie nicht möglich");
            } else {
                clearDisplay();
                request->send(200, "text/plain", "OK");
            }
        });

        // Einfacher Test-Endpunkt für direktes Aktualisieren des Displays
        server.on("/simple-update", HTTP_GET, [](AsyncWebServerRequest *request){
            Serial.println("Simple-Update Endpunkt aufgerufen");
            
            // Prüfen ob Batterie zu niedrig ist
            if (isLowBattery) {
                Serial.println("Display-Update wegen niedriger Batterie abgebrochen");
                request->send(403, "text/plain", "Display-Update wegen niedriger Batterie nicht möglich");
                return;
            }
            
            // Parameter auslesen, falls vorhanden
            if (request->hasParam("name")) {
                displayContent.name = request->getParam("name")->value();
                Serial.println("Name gesetzt: " + displayContent.name);
            }
            
            if (request->hasParam("description")) {
                displayContent.description = request->getParam("description")->value();
                Serial.println("Beschreibung gesetzt: " + displayContent.description);
            }
            
            if (request->hasParam("telegram")) {
                displayContent.telegram = request->getParam("telegram")->value();
                Serial.println("Telegram gesetzt: " + displayContent.telegram);
            }
            
            if (request->hasParam("invert")) {
                String invert = request->getParam("invert")->value();
                displayContent.invertColors = (invert == "1" || invert == "true" || invert == "on");
                Serial.println("Invertiert gesetzt: " + String(displayContent.invertColors));
            }
            
            // Speichern und Display aktualisieren
            saveDisplayContent();
            
            // Direktes Update an das Display senden
            if (displayAvailable && displayQueue != NULL) {
                Serial.println("Sende direktes CMD_UPDATE an Display-Queue");
                DisplayCommand cmd = CMD_UPDATE;
                xQueueSend(displayQueue, &cmd, pdMS_TO_TICKS(1000));
            }
            
            request->send(200, "text/plain", "Simple Update gesendet");
        });

        // Handler für Bildanzeige
        server.on("/image/(.*)", HTTP_GET, [](AsyncWebServerRequest *request){
            String path = request->pathArg(0);
            if(SPIFFS.exists(path)){
                request->send(SPIFFS, path, "image/jpeg");
            } else {
                request->send(404, "text/plain", "Bild nicht gefunden");
            }
        });

        // Route für die API zur Abfrage des Akkustands
        server.on("/battery-status", HTTP_GET, [](AsyncWebServerRequest *request){
            // Alle Messwerte auslesen
            float vbat = charger->getVBAT();
            float vsys = charger->getVSYS();
            float vbus = charger->getVBUS();
            float ichg = charger->getICHG() * 1000; // Umrechnung in mA
            float iin = charger->getIIN() * 1000;   // Umrechnung in mA
            String tempStatus = charger->getTemperatureStatus();
            String chargeStatus = charger->getChargeStatus();
            
            // Wichtige Register direkt auslesen
            uint8_t reg0B = charger->readRegister(0x0B); // System Status
            uint8_t reg0C = charger->readRegister(0x0C); // Fault
            uint8_t reg0E = charger->readRegister(0x0E); // VBAT_MSB
            uint8_t reg0F = charger->readRegister(0x0F); // VBAT_LSB/VSYS
            uint8_t reg11 = charger->readRegister(0x11); // VBUS
            uint8_t reg12 = charger->readRegister(0x12); // ICHG
            uint8_t reg13 = charger->readRegister(0x13); // IIN
            uint8_t reg00 = charger->readRegister(0x00); // Input Source Control
            
            // Fault-Status direkt auslesen
            uint8_t fault = reg0C;
            String faultStatus = "Keine Fehler";
            
            if (fault != 0 && fault != 0xFF) {
                faultStatus = "";
                if (fault & 0x80) faultStatus += "WATCHDOG ";
                if (fault & 0x40) faultStatus += "BOOST ";
                if (fault & 0x20) faultStatus += "CHARGE ";
                if (fault & 0x10) faultStatus += "BATTERY ";
                if (fault & 0x08) {
                    faultStatus += "NTC ";
                    uint8_t ntcFault = (fault & 0x07);
                    switch (ntcFault) {
                        case 1: faultStatus += "(TS COLD) "; break;
                        case 2: faultStatus += "(TS COOL) "; break;
                        case 3: faultStatus += "(TS WARM) "; break;
                        case 4: faultStatus += "(TS HOT) "; break;
                        default: faultStatus += "(NTC UNKNOWN) ";
                    }
                }
            } else if (fault == 0xFF) {
                faultStatus = "Fehler beim Lesen";
            }
            
            float power = vbat * (ichg / 1000); // Berechnung in Watt
            
            // Batterie-Prozentsatz basierend auf Spannung berechnen
            int percentage = 0;
            if (vbat >= 4.2f) {
                percentage = 100;
            } else if (vbat >= 3.0f) {
                // Lineare Interpolation zwischen 3.0V (0%) und 4.2V (100%)
                percentage = (int)((vbat - 3.0f) * 100.0f / 1.2f);
            }
            
            // JSON-Antwort erstellen
            String response = "{";
            response += "\"vbat\":" + String(vbat, 3) + ",";           // Batteriespannung
            response += "\"vsys\":" + String(vsys, 3) + ",";           // Systemspannung
            response += "\"vbus\":" + String(vbus, 3) + ",";           // USB-Eingangsspannung
            response += "\"ichg\":" + String(ichg, 0) + ",";           // Ladestrom in mA
            response += "\"iin\":" + String(iin, 0) + ",";             // Eingangsstrom in mA
            response += "\"power\":" + String(power, 3) + ",";         // Leistung in W
            response += "\"percentage\":" + String(percentage) + ",";  // Batteriestand in %
            response += "\"temperature\":\"" + tempStatus + "\",";     // Temperaturstatus
            response += "\"chargeStatus\":\"" + chargeStatus + "\",";  // Ladestatus
            response += "\"faultStatus\":\"" + faultStatus + "\",";    // Fehlerstatus
            response += "\"isCharging\":" + String(charger->isVBUSPresent() ? "true" : "false") + ",";
            
            // Register-Werte hinzufügen
            response += "\"registers\":{";
            response += "\"REG0B_Status\":" + String(reg0B) + ",";
            response += "\"REG0C_Fault\":" + String(reg0C) + ",";
            response += "\"REG0E_VBAT\":" + String(reg0E) + ",";
            response += "\"REG0F_VSYS\":" + String(reg0F) + ",";
            response += "\"REG11_VBUS\":" + String(reg11) + ",";
            response += "\"REG12_ICHG\":" + String(reg12) + ",";
            response += "\"REG13_IIN\":" + String(reg13) + ",";
            response += "\"REG00_InputCtrl\":" + String(reg00);
            response += "}";
            
            response += "}";
            
            request->send(200, "application/json", response);
        });

        // Route zum Ein-/Ausschalten der LEDs
        server.on("/toggle-leds", HTTP_GET, [](AsyncWebServerRequest *request){
            if (request->hasParam("enabled")) {
                String enabledStr = request->getParam("enabled")->value();
                bool enabled = (enabledStr == "1" || enabledStr == "true");
                
                // LED-Status global aktualisieren
                ledsEnabled = enabled;
                
                // LED-Zustand ändern
                if (ledsEnabled) {
                    // LEDs einschalten - normaler Modus
                    FastLED.setBrightness(BRIGHTNESS);
                    digitalWrite(LIGHT_EN, HIGH); // GPIO35 auf HIGH setzen wenn LEDs an
                    Serial.println("LEDs eingeschaltet");
                } else {
                    // LEDs ausschalten - Helligkeit auf 0 setzen
                    FastLED.setBrightness(0);
                    digitalWrite(LIGHT_EN, LOW); // GPIO35 auf LOW setzen wenn LEDs aus
                    Serial.println("LEDs ausgeschaltet");
                }
                
                // Sofort aktualisieren
                FastLED.show();
            }
            
            request->send(200, "text/plain", "OK");
        });

        // Route zum Einstellen des Ladestroms
        server.on("/set-charge-current", HTTP_GET, [](AsyncWebServerRequest *request){
            String message = "Fehler: Parameter 'current' fehlt";
            
            if (request->hasParam("current")) {
                int current = request->getParam("current")->value().toInt();
                
                if (charger != nullptr) {
                    // Setze den Ladestrom
                    charger->setChargeCurrent(current);
                    message = "Ladestrom auf " + String(current) + " mA gesetzt";
                    // Log-Eintrag hinzufügen
                    addEspLog("Ladestrom auf " + String(current) + " mA gesetzt");
                } else {
                    message = "Fehler: Ladegerät nicht verfügbar";
                    addEspLog("Fehler beim Setzen des Ladestroms: Ladegerät nicht verfügbar");
                }
            }
            
            request->send(200, "text/plain", message);
        });

        // Route für Log-Abfrage
        server.on("/get-logs", HTTP_GET, [](AsyncWebServerRequest *request){
            StaticJsonDocument<8192> doc; // Größeres JSON-Dokument für Logs
            
            // ESP-Logs
            JsonArray espArr = doc.createNestedArray("esp");
            for (const String& log : espLogs) {
                espArr.add(log);
            }
            
            // BQ-Logs
            JsonArray bqArr = doc.createNestedArray("bq");
            for (const String& log : bqLogs) {
                bqArr.add(log);
            }
            
            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        });
        
        // Route für Log-Löschen
        server.on("/clear-logs", HTTP_GET, [](AsyncWebServerRequest *request){
            String type = "all";
            if (request->hasParam("type")) {
                type = request->getParam("type")->value();
            }
            
            if (type == "esp" || type == "all") {
                espLogs.clear();
                addEspLog("ESP-Logs wurden gelöscht");
            }
            
            if (type == "bq" || type == "all") {
                bqLogs.clear();
                addBqLog("BQ25895-Logs wurden gelöscht");
            }
            
            request->send(200, "text/plain", "Logs gelöscht");
        });

        Serial.println("WebServer-Routen konfiguriert");
        
        // Starte den WebServer in einem separaten Task auf Core 0
        xTaskCreatePinnedToCore(
            webServerTask,         // Task Funktion
            "WebServerTask",       // Name des Tasks
            8192,                  // Stack-Größe (mehr Speicher)
            NULL,                  // Parameter
            1,                     // Priorität (1 ist niedrig)
            &webServerTaskHandle,  // Task-Handle
            0                      // Auf Core 0 ausführen (Arduino-Loop läuft auf Core 1)
        );
        
        Serial.println("WebServer-Task gestartet");
    } catch (const std::exception& e) {
        Serial.print("Fehler bei WebServer-Konfiguration: ");
        Serial.println(e.what());
    } catch (...) {
        Serial.println("Unbekannter Fehler bei WebServer-Konfiguration");
    }
}

// Funktion zum Hinzufügen eines ESP-Logs
void addEspLog(const String& message) {
    if (espLogs.size() >= MAX_LOG_ENTRIES) {
        espLogs.erase(espLogs.begin());
    }
    
    String timestamp = "[" + String(millis() / 1000) + "s] ";
    espLogs.push_back(timestamp + message);
}

// Funktion zum Hinzufügen eines BQ25895-Logs
void addBqLog(const String& message) {
    if (bqLogs.size() >= MAX_LOG_ENTRIES) {
        bqLogs.erase(bqLogs.begin());
    }
    
    String timestamp = "[" + String(millis() / 1000) + "s] ";
    bqLogs.push_back(timestamp + message);
} 