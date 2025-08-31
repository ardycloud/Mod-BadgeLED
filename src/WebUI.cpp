#include "WebUI.h"
#include <ArduinoJson.h>
#include <ElegantOTA.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>  // mDNS für badge.local
#include <HTTPClient.h>  // Für OTA-Server-Status-Check
#include "Settings.h"
#include "Config.h"
#include "Display.h"
#include "BQ25895CONFIG.h"  // Für Akku-Funktionen
#include <Arduino.h>
#include "Globals.h"

// Externe Variablen für LED
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
            width: calc(100% - 16px);
            padding: 12px;
            border-radius: 5px;
            background: #404040;
            color: white;
            border: 1px solid rgb(0, 191, 255);
            margin-right: 8px;
            font-size: 16px;
        }
        input[type="range"] {
            -webkit-appearance: none;
            height: 15px;
            background: #404040;
            outline: none;
            opacity: 0.7;
            transition: opacity .2s;
            padding: 0;
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
            color: #fff;
            padding: 10px 20px;
            cursor: pointer;
            border-radius: 5px 5px 0 0;
            margin-right: 5px;
        }
        .tab-button.active {
            background: #555;
            border-bottom: 1px solid #555;
        }
        .tab-content {
            display: none;
        }
        .tab-content.active {
            display: block;
        }
        .color-grid {
            display: grid;
            grid-template-columns: repeat(8, 1fr);
            gap: 8px;
            margin-bottom: 10px;
            max-width: 100%;
        }
        .color-box {
            width: 40px;
            height: 40px;
            border-radius: 8px;
            cursor: pointer;
            border: 3px solid transparent;
            transition: all 0.2s ease;
            box-shadow: 0 2px 4px rgba(0,0,0,0.3);
        }
        .color-box:hover {
            border-color: #fff;
            transform: scale(1.1);
            box-shadow: 0 4px 8px rgba(0,0,0,0.5);
        }
        .color-box.selected {
            border-color: rgb(0, 191, 255);
            box-shadow: 0 0 10px rgba(0, 191, 255, 0.5);
        }
        @media (max-width: 600px) {
            .color-grid {
                grid-template-columns: repeat(6, 1fr);
                gap: 6px;
            }
            .color-box {
                width: 35px;
                height: 35px;
            }
            select {
                font-size: 18px;
                padding: 14px;
            }
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
                <label for="mode">LED-Modus</label>
                <select id="mode" onchange="updateValue('mode', this.value)">
                    <option value="0">Einfarbig</option>
                    <option value="1">Regenbogen</option>
                    <option value="2">Regenbogen-Welle</option>
                    <option value="3">Farbwisch</option>
                    <option value="4">Theater-Verfolgung</option>
                    <option value="5">Funkeln</option>
                    <option value="6">Feuer</option>
                    <option value="7">Pulsieren</option>
                    <option value="8">Welle</option>
                    <option value="9">Glitzern</option>
                    <option value="10">Gradient</option>
                    <option value="11">Wandernde Punkte</option>
                    <option value="12">Komet</option>
                    <option value="13">Springender Ball</option>
                    <option value="14">Feuerwerk</option>
                    <option value="15">Blitz</option>
                    <option value="16">Konfetti</option>
                    <option value="17">Atmung</option>
                    <option value="18">Regen</option>
                    <option value="19">Matrix</option>
                    <option value="20">Umlaufbahn</option>
                    <option value="21">Spirale</option>
                    <option value="22">Meteorschauer</option>
                    <option value="23">Farbrotation</option>
                    <option value="24">Musik-reaktiv</option>
                </select>
            </div>
            
            <div class="control-group">
                <label>Hauptfarbe</label>
                <div class="color-grid">
                    <div class="color-box" style="background-color: hsl(0, 100%, 50%)" onclick="setColor('primary', 0)" title="Rot"></div>
                    <div class="color-box" style="background-color: hsl(30, 100%, 50%)" onclick="setColor('primary', 21)" title="Orange"></div>
                    <div class="color-box" style="background-color: hsl(60, 100%, 50%)" onclick="setColor('primary', 43)" title="Gelb"></div>
                    <div class="color-box" style="background-color: hsl(90, 100%, 50%)" onclick="setColor('primary', 64)" title="Gelbgrün"></div>
                    <div class="color-box" style="background-color: hsl(120, 100%, 50%)" onclick="setColor('primary', 85)" title="Grün"></div>
                    <div class="color-box" style="background-color: hsl(150, 100%, 50%)" onclick="setColor('primary', 107)" title="Grüncyan"></div>
                    <div class="color-box" style="background-color: hsl(180, 100%, 50%)" onclick="setColor('primary', 128)" title="Cyan"></div>
                    <div class="color-box" style="background-color: hsl(210, 100%, 50%)" onclick="setColor('primary', 149)" title="Blau"></div>
                    <div class="color-box" style="background-color: hsl(240, 100%, 50%)" onclick="setColor('primary', 171)" title="Blauviolett"></div>
                    <div class="color-box" style="background-color: hsl(270, 100%, 50%)" onclick="setColor('primary', 192)" title="Violett"></div>
                    <div class="color-box" style="background-color: hsl(300, 100%, 50%)" onclick="setColor('primary', 213)" title="Magenta"></div>
                    <div class="color-box" style="background-color: hsl(330, 100%, 50%)" onclick="setColor('primary', 235)" title="Rosa"></div>
                    <div class="color-box" style="background-color: hsl(15, 100%, 50%)" onclick="setColor('primary', 11)" title="Rotorange"></div>
                    <div class="color-box" style="background-color: hsl(45, 100%, 50%)" onclick="setColor('primary', 32)" title="Goldgelb"></div>
                    <div class="color-box" style="background-color: hsl(75, 100%, 50%)" onclick="setColor('primary', 53)" title="Hellgrün"></div>
                    <div class="color-box" style="background-color: hsl(195, 100%, 50%)" onclick="setColor('primary', 139)" title="Himmelblau"></div>
                </div>
            </div>
            
            <div class="control-group">
                <label>Akzentfarbe</label>
                <div class="color-grid">
                    <div class="color-box" style="background-color: hsl(0, 100%, 50%)" onclick="setColor('secondary', 0)" title="Rot"></div>
                    <div class="color-box" style="background-color: hsl(30, 100%, 50%)" onclick="setColor('secondary', 21)" title="Orange"></div>
                    <div class="color-box" style="background-color: hsl(60, 100%, 50%)" onclick="setColor('secondary', 43)" title="Gelb"></div>
                    <div class="color-box" style="background-color: hsl(90, 100%, 50%)" onclick="setColor('secondary', 64)" title="Gelbgrün"></div>
                    <div class="color-box" style="background-color: hsl(120, 100%, 50%)" onclick="setColor('secondary', 85)" title="Grün"></div>
                    <div class="color-box" style="background-color: hsl(150, 100%, 50%)" onclick="setColor('secondary', 107)" title="Grüncyan"></div>
                    <div class="color-box" style="background-color: hsl(180, 100%, 50%)" onclick="setColor('secondary', 128)" title="Cyan"></div>
                    <div class="color-box" style="background-color: hsl(210, 100%, 50%)" onclick="setColor('secondary', 149)" title="Blau"></div>
                    <div class="color-box" style="background-color: hsl(240, 100%, 50%)" onclick="setColor('secondary', 171)" title="Blauviolett"></div>
                    <div class="color-box" style="background-color: hsl(270, 100%, 50%)" onclick="setColor('secondary', 192)" title="Violett"></div>
                    <div class="color-box" style="background-color: hsl(300, 100%, 50%)" onclick="setColor('secondary', 213)" title="Magenta"></div>
                    <div class="color-box" style="background-color: hsl(330, 100%, 50%)" onclick="setColor('secondary', 235)" title="Rosa"></div>
                    <div class="color-box" style="background-color: hsl(15, 100%, 50%)" onclick="setColor('secondary', 11)" title="Rotorange"></div>
                    <div class="color-box" style="background-color: hsl(45, 100%, 50%)" onclick="setColor('secondary', 32)" title="Goldgelb"></div>
                    <div class="color-box" style="background-color: hsl(75, 100%, 50%)" onclick="setColor('secondary', 53)" title="Hellgrün"></div>
                    <div class="color-box" style="background-color: hsl(195, 100%, 50%)" onclick="setColor('secondary', 139)" title="Himmelblau"></div>
                </div>
            </div>
        </div>

        <div class="card">
            <div class="control-group">
                <label for="brightness">Brightness</label>
                <input type="range" id="brightness" min="12" max="255" value="12" 
                       oninput="updateValue('brightness', this.value)">
                <div class="value-display"><span id="brightnessValue">12</span></div>
            </div>
        </div>

        <div class="card">
            <div class="control-group">
                <label for="speed">Animation Speed</label>
                <input type="range" id="speed" min="1" max="50" value="30" 
                       oninput="updateSpeedValue(this.value)">
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
                    <div class="value" id="batterySystemVoltage">--</div>
                    <div class="label">System-Spannung</div>
                </div>
                <div class="battery-stat">
                    <div class="value" id="batteryBusVoltage">--</div>
                    <div class="label">USB-Spannung</div>
                </div>
                <div class="battery-stat">
                    <div class="value" id="batteryChargeStatus">--</div>
                    <div class="label">Lade-Status</div>
                </div>
            </div>
            
            <div style="margin-top: 15px; background: #333; padding: 10px; border-radius: 5px; display: none;">
                <h3 style="margin-top: 0; color: rgb(0, 191, 255);">Register-Werte</h3>
                <div id="registerValues" style="font-family: monospace; font-size: 12px; color: #ddd;"></div>
            </div>
            
            <div class="control-group">
                <label for="chargeCurrent">Ladestrom</label>
                <select id="chargeCurrent" onchange="setChargeCurrent(this.value)">
                    <option value="500">0.5A (500mA)</option>
                    <option value="1000">1.0A (1000mA)</option>
                    <option value="1500" selected>1.5A (1500mA)</option>
                    <option value="2000">2.0A (2000mA)</option>
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
        
        <!-- Reboot Button -->
        <div style="text-align: center; margin-top: 20px;">
            <button onclick="rebootESP()" style="background: #ff6666; color: white; border: none; padding: 8px 16px; border-radius: 5px; cursor: pointer; font-size: 12px;">ESP32 Neustart</button>
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

        // Spezielle Funktion für invertierte Geschwindigkeit
        function updateSpeedValue(sliderValue) {
            // Invertiere den Wert: Slider links (1) = langsam (50ms), Slider rechts (50) = schnell (1ms)
            const invertedValue = 51 - parseInt(sliderValue);
            
            // Sende den invertierten Wert an den Server
            const xhr = new XMLHttpRequest();
            xhr.open("GET", `/settings?param=speed&value=${invertedValue}`, true);
            xhr.send();
            
            // Zeige den invertierten Wert in der Anzeige
            const displayElement = document.getElementById('speedValue');
            if (displayElement) {
                displayElement.textContent = invertedValue;
            }
        }

        // Neue Farbauswahl-Funktion
        function setColor(type, hueValue) {
            // Alle Boxen des entsprechenden Typs zurücksetzen (vereinfachter Selektor)
            let colorBoxes;
            if (type === 'primary') {
                // Primärfarbe ist die erste Farbgruppe (2. control-group)
                colorBoxes = document.querySelectorAll('.control-group:nth-child(2) .color-box');
            } else {
                // Sekundärfarbe ist die zweite Farbgruppe (3. control-group)
                colorBoxes = document.querySelectorAll('.control-group:nth-child(3) .color-box');
            }
            
            colorBoxes.forEach(box => box.classList.remove('selected'));
            
            // Geklickte Box markieren
            event.target.classList.add('selected');
            
            // Wert senden
            const param = type === 'primary' ? 'primaryColor' : 'secondaryColor';
            const xhr = new XMLHttpRequest();
            xhr.open("GET", `/settings?param=${param}&value=${hueValue}`, true);
            xhr.send();
            
            // Anzeige aktualisieren - ENTFERNT: Farbwerte werden nicht mehr angezeigt
            // const displayElement = document.getElementById(param + 'Value');
            // if (displayElement) {
            //     displayElement.textContent = hueValue;
            // }
            
            console.log(`${type} Farbe gesetzt auf: ${hueValue}`);
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

        // Initial values - verwende addEventListener statt window.onload
        document.addEventListener('DOMContentLoaded', function() {
            fetch('/values')
            .then(response => response.json())
            .then(data => {
                document.getElementById('mode').value = data.mode;
                document.getElementById('brightness').value = data.brightness;
                document.getElementById('brightnessValue').textContent = data.brightness;
                
                // Invertierte Geschwindigkeit: Server-Wert zu Slider-Wert
                const serverSpeed = data.speed;
                const sliderSpeed = 51 - serverSpeed; // Invertierung
                document.getElementById('speed').value = sliderSpeed;
                document.getElementById('speedValue').textContent = serverSpeed;
                
                document.getElementById('sensitivity').value = data.sensitivity;
                document.getElementById('sensitivityValue').textContent = data.sensitivity;
                document.getElementById('frequency').value = data.frequency;
                document.getElementById('frequencyValue').textContent = data.frequency;
                
                // Neue Farbwerte laden und Boxen markieren
                if (data.primaryColor !== undefined) {
                    // Entsprechende Farbbox markieren
                    const primaryBoxes = document.querySelectorAll('.control-group:nth-child(2) .color-box');
                    const primaryIndex = Math.floor(data.primaryColor / 16);
                    if (primaryBoxes[primaryIndex]) {
                        primaryBoxes[primaryIndex].classList.add('selected');
                    }
                }
                if (data.secondaryColor !== undefined) {
                    // Entsprechende Farbbox markieren
                    const secondaryBoxes = document.querySelectorAll('.control-group:nth-child(3) .color-box');
                    const secondaryIndex = Math.floor(data.secondaryColor / 16);
                    if (secondaryBoxes[secondaryIndex]) {
                        secondaryBoxes[secondaryIndex].classList.add('selected');
                    }
                }
                
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
            
            // Initiale Akku-Daten abrufen - mit Verzögerung um sicherzustellen dass alle Elemente geladen sind
            setTimeout(function() {
                updateBatteryInfo();
                fetchLogs();
            }, 500);
            
            // Akku-Daten alle 5 Sekunden aktualisieren (häufiger für bessere Überwachung)
            setInterval(updateBatteryInfo, 5000);
            
            // Logs regelmäßig abrufen
            setInterval(fetchLogs, 2000);
        });
        
        // ZUSÄTZLICHE SICHERUNG: Starte Akku-Updates auch nach 3 Sekunden falls sie noch nicht laufen
        setTimeout(function() {
            console.log('🔄 Starte Backup-Timer für Akku-Updates');
            updateBatteryInfo();
            fetchLogs();
            
            // Starte auch die Intervalle nochmal zur Sicherheit
            setInterval(updateBatteryInfo, 5000);
            setInterval(fetchLogs, 2000);
        }, 3000);
        
        // Akku-Informationen aktualisieren
        function updateBatteryInfo() {
            console.log('🔍 updateBatteryInfo() gestartet');
            fetch('/battery-status')
            .then(response => {
                console.log('🔍 Response erhalten:', response.status, response.statusText);
                if (!response.ok) {
                    throw new Error(`HTTP ${response.status}: ${response.statusText}`);
                }
                return response.json();
            })
            .then(data => {
                console.log('🔍 Batterie-Daten erhalten:', data);
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
                
                // Leistung in Watt anzeigen
                document.getElementById('batteryPower').textContent = data.power.toFixed(3) + ' W';
                
                // Status anzeigen
                document.getElementById('batteryChargeStatus').textContent = data.chargeStatus;
                
                // Register-Werte anzeigen - ENTFERNT
                /*
                if (data.registers) {
                    let regHtml = '';
                    for (const [reg, value] of Object.entries(data.registers)) {
                        regHtml += `<div>${reg}: 0x${value.toString(16).padStart(2, '0')} (${value})</div>`;
                    }
                    document.getElementById('registerValues').innerHTML = regHtml;
                }
                */
            })
            .catch(error => {
                console.error('❌ Fehler beim Abrufen der Akku-Daten:', error);
                // Fallback-Anzeige bei Fehlern
                document.getElementById('batteryVoltage').textContent = 'Fehler';
                document.getElementById('batteryPercentage').textContent = '?%';
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
            console.log('🔍 fetchLogs() gestartet');
            fetch('/get-logs')
            .then(response => {
                console.log('🔍 Logs Response:', response.status, response.statusText);
                if (!response.ok) {
                    throw new Error(`HTTP ${response.status}: ${response.statusText}`);
                }
                return response.json();
            })
            .then(data => {
                console.log('🔍 Log-Daten erhalten:', data);
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
                console.error('❌ Fehler beim Abrufen der Logs:', error);
            });
        }
        
        // ESP32 Neustart-Funktion
        function rebootESP() {
            if (confirm('ESP32 wirklich neu starten? Die Verbindung wird kurz unterbrochen.')) {
                fetch('/reboot')
                .then(response => response.text())
                .then(result => {
                    alert('ESP32 wird neu gestartet. Bitte warten Sie 10-15 Sekunden und laden Sie die Seite neu.');
                })
                .catch(error => {
                    console.error('Fehler beim Neustart:', error);
                    alert('Neustart wurde gesendet. Bitte warten Sie 10-15 Sekunden und laden Sie die Seite neu.');
                });
            }
        }

        function updateDisplay() {
            // Daten direkt als einfaches Objekt zusammenstellen
            const name = document.getElementById('name').value;
            const description = document.getElementById('description').value;
            const telegram = document.getElementById('telegram').value;
            const invertColors = document.getElementById('invertColors').checked;
            const nameColorRed = document.getElementById('nameColorRed').checked;
            
            // Erst das Bild hochladen, falls vorhanden
            if (imageData) {
                const formData = new FormData();
                
                // Konvertiere Base64 zu Blob
                const byteCharacters = atob(imageData.split(',')[1]);
                const byteNumbers = new Array(byteCharacters.length);
                for (let i = 0; i < byteCharacters.length; i++) {
                    byteNumbers[i] = byteCharacters.charCodeAt(i);
                }
                const byteArray = new Uint8Array(byteNumbers);
                const blob = new Blob([byteArray], {type: 'image/jpeg'});
                
                formData.append('image', blob, 'badge_image.jpg');
                
                fetch('/upload-image', {
                    method: 'POST',
                    body: formData
                })
                .then(response => response.text())
                .then(result => {
                    if(result === 'OK') {
                        console.log('Bild erfolgreich hochgeladen');
                        // Nach erfolgreichem Bild-Upload die anderen Daten aktualisieren
                        updateDisplayData(name, description, telegram, invertColors, nameColorRed);
                    } else {
                        alert('Fehler beim Hochladen des Bildes!');
                    }
                })
                .catch(error => {
                    console.error('Fehler beim Bild-Upload:', error);
                    alert('Fehler beim Hochladen des Bildes!');
                });
            } else {
                // Kein Bild, direkt die Daten aktualisieren
                updateDisplayData(name, description, telegram, invertColors, nameColorRed);
            }
        }
        
        function updateDisplayData(name, description, telegram, invertColors, nameColorRed) {
            // Verwende ein einfaches GET wie beim simple-update Endpunkt, der funktioniert
            const url = `/simple-update?name=${encodeURIComponent(name)}&description=${encodeURIComponent(description)}&telegram=${encodeURIComponent(telegram)}&invert=${invertColors ? '1' : '0'}&nameRed=${nameColorRed ? '1' : '0'}`;
            
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
                    document.getElementById('nameColorRed').checked = true;  // Standard: Rot
                    previewImg.style.display = 'none';
                    noImageText.style.display = 'block';
                    imageData = null;
                    imageUpload.value = '';
                } else {
                    alert('Fehler beim leeren des Displays!');
                }
            });
        }

        function deleteImage() {
            if (confirm('Bild wirklich löschen?')) {
                fetch('/delete-image')
                .then(response => response.text())
                .then(result => {
                    if(result === 'OK') {
                        alert('Bild wurde erfolgreich gelöscht!');
                        previewImg.style.display = 'none';
                        noImageText.style.display = 'block';
                        imageData = null;
                        imageUpload.value = '';
                    } else {
                        alert('Fehler beim Löschen des Bildes!');
                    }
                });
            }
        }

        // Lade aktuelle Werte
        window.onload = function() {
            fetch('/network-values')
            .then(response => response.json())
            .then(data => {
                document.getElementById('apname').value = data.apname || 'LED-Badge';
                document.getElementById('appassword').value = data.appassword || '';
                document.getElementById('wifiSSID').value = data.wifiSSID || '';
                document.getElementById('wifiPassword').value = data.wifiPassword || '';
                document.getElementById('wifiEnabled').value = data.wifiEnabled || 'true';
                document.getElementById('macaddress').value = data.macAddress || '';
                document.getElementById('deviceid').value = data.deviceID || '';
                document.getElementById('otaEnabled').value = data.otaEnabled || 'true';
                document.getElementById('currentVersion').value = data.currentVersion || 'Unbekannt';
                document.getElementById('otaStatus').value = data.otaStatus || 'Nicht verbunden';
            });
        }

        function checkForUpdates() {
            const button = document.querySelector('button[onclick="checkForUpdates()"]');
            button.textContent = 'Prüfe Updates...';
            button.disabled = true;
            
            fetch('/check-updates', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                }
            })
            .then(response => response.json())
            .then(result => {
                if(result.success) {
                    if(result.updateAvailable) {
                        if(confirm('Update verfügbar! Möchten Sie es jetzt installieren?')) {
                            installUpdate();
                        } else {
                            button.textContent = 'Updates prüfen und installieren';
                            button.disabled = false;
                        }
                    } else {
                        alert('Kein Update verfügbar.');
                        button.textContent = 'Updates prüfen und installieren';
                        button.disabled = false;
                    }
                } else {
                    alert('Fehler beim Prüfen der Updates: ' + result.message);
                    button.textContent = 'Updates prüfen und installieren';
                    button.disabled = false;
                }
            })
            .catch(error => {
                alert('Fehler beim Prüfen der Updates: ' + error);
                button.textContent = 'Updates prüfen und installieren';
                button.disabled = false;
            });
        }

        function installUpdate() {
            const button = document.querySelector('button[onclick="checkForUpdates()"]');
            button.textContent = 'Installiere Update...';
            
            fetch('/install-update', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                }
            })
            .then(response => response.json())
            .then(result => {
                if(result.success) {
                    alert('Update wird installiert. Das Gerät startet neu...');
                    // Warte kurz und dann zur Startseite
                    setTimeout(() => {
                        window.location.href = '/';
                    }, 3000);
                } else {
                    alert('Fehler beim Installieren: ' + result.message);
                    button.textContent = 'Updates prüfen und installieren';
                    button.disabled = false;
                }
            })
            .catch(error => {
                alert('Fehler beim Installieren: ' + error);
                button.textContent = 'Updates prüfen und installieren';
                button.disabled = false;
            });
        }

        function updateNetwork() {
            const data = {
                apname: document.getElementById('apname').value,
                appassword: document.getElementById('appassword').value,
                wifiSSID: document.getElementById('wifiSSID').value,
                wifiPassword: document.getElementById('wifiPassword').value,
                wifiEnabled: document.getElementById('wifiEnabled').value,
                otaEnabled: document.getElementById('otaEnabled').value
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
            <h3 style="color: #00ff88; margin-top: 0;">Device Information</h3>
            <div class="control-group">
                <label>MAC-Adresse</label>
                <input type="text" id="macaddress" readonly style="background: #303030;">
            </div>
            <div class="control-group">
                <label>Device-ID (OTA)</label>
                <input type="text" id="deviceid" readonly style="background: #303030;">
            </div>
        </div>

        <div class="card">
            <h3 style="color: #00ff88; margin-top: 0;">WiFi Client (STA) Einstellungen</h3>
            <div class="control-group">
                <label for="wifiEnabled">WiFi-Verbindung aktiviert</label>
                <select id="wifiEnabled" style="width: 100%; padding: 8px; border-radius: 5px; background: #404040; color: white; border: 1px solid #00ff88;">
                    <option value="true">Ja</option>
                    <option value="false">Nein</option>
                </select>
            </div>
            <div class="control-group">
                <label for="wifiSSID">WiFi Netzwerkname (SSID)</label>
                <input type="text" id="wifiSSID" maxlength="32" placeholder="WiFi Netzwerkname">
            </div>
            <div class="control-group">
                <label for="wifiPassword">WiFi Passwort</label>
                <input type="password" id="wifiPassword" maxlength="64" placeholder="WiFi Passwort">
            </div>
        </div>

        <div class="card">
            <h3 style="color: #00ff88; margin-top: 0;">Access Point (AP) Einstellungen</h3>
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
            <h3 style="color: #00ff88; margin-top: 0;">OTA Update Einstellungen</h3>
            <div class="control-group">
                <label for="otaEnabled">Automatische Updates aktiviert</label>
                <select id="otaEnabled" style="width: 100%; padding: 8px; border-radius: 5px; background: #404040; color: white; border: 1px solid #00ff88;">
                    <option value="true">Ja</option>
                    <option value="false">Nein</option>
                </select>
            </div>
            <div class="control-group">
                <label>Aktuelle Firmware Version</label>
                <input type="text" id="currentVersion" readonly style="background: #303030;">
            </div>
            <div class="control-group">
                <label>OTA Server Status</label>
                <input type="text" id="otaStatus" readonly style="background: #303030;">
            </div>
            <div class="control-group">
                <button onclick="checkForUpdates()" style="background: #00ff88; color: #1a1a1a; border: none; padding: 10px 20px; border-radius: 5px; cursor: pointer; width: 100%; font-weight: bold; margin-bottom: 10px;">Updates prüfen und installieren</button>
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
                document.getElementById('wifiSSID').value = data.wifiSSID || '';
                document.getElementById('wifiPassword').value = data.wifiPassword || '';
                document.getElementById('wifiEnabled').value = data.wifiEnabled ? 'true' : 'false';
                document.getElementById('macaddress').value = data.macAddress || '';
                document.getElementById('deviceid').value = data.deviceID || '';
                document.getElementById('otaEnabled').value = data.otaEnabled ? 'true' : 'false';
                document.getElementById('currentVersion').value = data.currentVersion || 'Unbekannt';
                document.getElementById('otaStatus').value = data.otaStatus || 'Nicht verbunden';
            });
        }

        function checkForUpdates() {
            const button = document.querySelector('button[onclick="checkForUpdates()"]');
            button.textContent = 'Prüfe Updates...';
            button.disabled = true;
            
            fetch('/check-updates', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                }
            })
            .then(response => response.json())
            .then(result => {
                if(result.success) {
                    if(result.updateAvailable) {
                        if(confirm('Update verfügbar! Möchten Sie es jetzt installieren?')) {
                            installUpdate();
                        } else {
                            button.textContent = 'Updates prüfen und installieren';
                            button.disabled = false;
                        }
                    } else {
                        alert('Kein Update verfügbar.');
                        button.textContent = 'Updates prüfen und installieren';
                        button.disabled = false;
                    }
                } else {
                    alert('Fehler beim Prüfen der Updates: ' + result.message);
                    button.textContent = 'Updates prüfen und installieren';
                    button.disabled = false;
                }
            })
            .catch(error => {
                alert('Fehler beim Prüfen der Updates: ' + error);
                button.textContent = 'Updates prüfen und installieren';
                button.disabled = false;
            });
        }

        function installUpdate() {
            const button = document.querySelector('button[onclick="checkForUpdates()"]');
            button.textContent = 'Installiere Update...';
            
            fetch('/install-update', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                }
            })
            .then(response => response.json())
            .then(result => {
                if(result.success) {
                    alert('Update wird installiert. Das Gerät startet neu...');
                    // Warte kurz und dann zur Startseite
                    setTimeout(() => {
                        window.location.href = '/';
                    }, 3000);
                } else {
                    alert('Fehler beim Installieren: ' + result.message);
                    button.textContent = 'Updates prüfen und installieren';
                    button.disabled = false;
                }
            })
            .catch(error => {
                alert('Fehler beim Installieren: ' + error);
                button.textContent = 'Updates prüfen und installieren';
                button.disabled = false;
            });
        }

        function updateNetwork() {
            const data = {
                apname: document.getElementById('apname').value,
                appassword: document.getElementById('appassword').value,
                wifiSSID: document.getElementById('wifiSSID').value,
                wifiPassword: document.getElementById('wifiPassword').value,
                wifiEnabled: document.getElementById('wifiEnabled').value,
                otaEnabled: document.getElementById('otaEnabled').value
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
                <label for="name">Badge Name / Besitzer</label>
                <input type="text" id="name" maxlength="31" value="ArdyMoon">
            </div>
            <div class="control-group">
                <div class="toggle-container">
                    <label>Name in Rot anzeigen</label>
                    <label class="toggle-switch">
                        <input type="checkbox" id="nameColorRed" checked>
                        <span class="toggle-slider"></span>
                    </label>
                </div>
                <p style="font-size: 12px; color: #aaa; margin-top: 5px;">
                    Aus: Name wird Weiss angezeigt 
                </p>
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
                    <div class="file-upload-button">Bild auswahl</div>
                </div>
                <button class="clear-button" onclick="deleteImage()" style="background: #ff4444 !important; margin-top: 10px;">Bild entfernen</button>
                <p style="font-size: 12px; color: #aaa; margin-top: 5px;">
                    <strong>Bild-Upload-Anleitung:</strong><br>
                    • Optimale Pixel: 64x64 Pixel<br>
                    • Unterstuetzte Formate: BMP<br>
                    • Online-Konverter: <a href="https://convertio.co/de/jpg-bmp/" target="_blank" style="color: #00ff88;">convertio.co</a><br>
                    • Das Bild wird rechts unter der Trennlinie angezeigt
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
                document.getElementById('nameColorRed').checked = data.nameColorRed !== undefined ? data.nameColorRed : true;
                
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
            const nameColorRed = document.getElementById('nameColorRed').checked;
            
            // Erst das Bild hochladen, falls vorhanden
            if (imageData) {
                const formData = new FormData();
                
                // Konvertiere Base64 zu Blob
                const byteCharacters = atob(imageData.split(',')[1]);
                const byteNumbers = new Array(byteCharacters.length);
                for (let i = 0; i < byteCharacters.length; i++) {
                    byteNumbers[i] = byteCharacters.charCodeAt(i);
                }
                const byteArray = new Uint8Array(byteNumbers);
                const blob = new Blob([byteArray], {type: 'image/jpeg'});
                
                formData.append('image', blob, 'badge_image.jpg');
                
                fetch('/upload-image', {
                    method: 'POST',
                    body: formData
                })
                .then(response => response.text())
                .then(result => {
                    if(result === 'OK') {
                        console.log('Bild erfolgreich hochgeladen');
                        // Nach erfolgreichem Bild-Upload die anderen Daten aktualisieren
                        updateDisplayData(name, description, telegram, invertColors, nameColorRed);
                    } else {
                        alert('Fehler beim Hochladen des Bildes!');
                    }
                })
                .catch(error => {
                    console.error('Fehler beim Bild-Upload:', error);
                    alert('Fehler beim Hochladen des Bildes!');
                });
            } else {
                // Kein Bild, direkt die Daten aktualisieren
                updateDisplayData(name, description, telegram, invertColors, nameColorRed);
            }
        }
        
        function updateDisplayData(name, description, telegram, invertColors, nameColorRed) {
            // Verwende ein einfaches GET wie beim simple-update Endpunkt, der funktioniert
            const url = `/simple-update?name=${encodeURIComponent(name)}&description=${encodeURIComponent(description)}&telegram=${encodeURIComponent(telegram)}&invert=${invertColors ? '1' : '0'}&nameRed=${nameColorRed ? '1' : '0'}`;
            
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
                    document.getElementById('nameColorRed').checked = true;  // Standard: Rot
                    previewImg.style.display = 'none';
                    noImageText.style.display = 'block';
                    imageData = null;
                    imageUpload.value = '';
                } else {
                    alert('Fehler beim leeren des Displays!');
                }
            });
        }

        function deleteImage() {
            if (confirm('Bild wirklich löschen?')) {
                fetch('/delete-image')
                .then(response => response.text())
                .then(result => {
                    if(result === 'OK') {
                        alert('Bild wurde erfolgreich gelöscht!');
                        previewImg.style.display = 'none';
                        noImageText.style.display = 'block';
                        imageData = null;
                        imageUpload.value = '';
                    } else {
                        alert('Fehler beim Löschen des Bildes!');
                    }
                });
            }
        }
    </script>
</body>
</html>
)rawliteral";

// Webserver-Task zum Starten des Servers auf einem separaten Core
void webServerTask(void *parameter) {
    Serial.println("WebServer Task gestartet auf Core: " + String(xPortGetCoreID()));
    
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
    
    // Task-spezifische Variablen für Monitoring
    unsigned long lastMemoryCheck = 0;
    unsigned long taskStartTime = millis();
    uint32_t loopCounter = 0;
    
    // Task bleibt aktiv und bearbeitet WebServer-Events
    for(;;) {
        unsigned long currentTime = millis();
        
        // Memory-Check alle 2 Minuten
        if (currentTime - lastMemoryCheck > 120000) {
            lastMemoryCheck = currentTime;
            size_t freeHeap = ESP.getFreeHeap();
            Serial.printf("WebServer Task - Freier Heap: %d bytes, Loops: %d\n", freeHeap, loopCounter);
            
            // Bei kritisch wenig Speicher: Cleanup
            if (freeHeap < 15000) {
                Serial.println("WebServer: Kritisch wenig Speicher - führe Cleanup durch");
                
                // Log-Arrays begrenzen
                if (espLogs.size() > 50) {
                    espLogs.erase(espLogs.begin(), espLogs.begin() + 25);
                }
                if (bqLogs.size() > 50) {
                    bqLogs.erase(bqLogs.begin(), bqLogs.begin() + 25);
                }
                
                // Kurze Pause für Garbage Collection
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            
            loopCounter = 0; // Reset counter
        }
        
        loopCounter++;
        
        // Längere Delays für WebServer Task (weniger kritisch als LED Task)
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Task-Yield alle 10 Loops für bessere Core-Balance
        if (loopCounter % 10 == 0) {
            taskYIELD();
        }
    }
}

void initWebServer() {
    try {
        Serial.println("Konfiguriere WebServer...");
        addEspLog("Starte WebServer-Konfiguration");
        
        // mDNS initialisieren für badge.local
        Serial.println("Initialisiere mDNS...");
        if (MDNS.begin("badge")) {
            Serial.println("mDNS gestartet! Badge erreichbar unter: badge.local");
            addEspLog("mDNS gestartet: badge.local");
        } else {
            Serial.println("Fehler beim Starten von mDNS");
            addEspLog("Fehler beim Starten von mDNS");
        }
        
        // Server-Konfiguration
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
        
        // DEBUG: Alle HTTP-Requests loggen
        server.onNotFound([](AsyncWebServerRequest *request){
            Serial.printf("🔍 HTTP-Request: %s %s\n", request->methodToString(), request->url().c_str());
            request->send(404, "text/plain", "Not Found");
        });
        
        // ElegantOTA initialisieren
        Serial.println("Initialisiere ElegantOTA...");
        ElegantOTA.begin(&server, "", "");    // Mit leeren Authentifizierungsparametern aufrufen
        Serial.println("OTA-Server initialisiert");
        addEspLog("OTA-Server initialisiert");
        
        // Route für die Hauptseite
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(200, "text/html", index_html);
        });

        // DIAGNOSE: Einfache Test-Route
        server.on("/test", HTTP_GET, [](AsyncWebServerRequest *request){
            Serial.println("🔍 TEST-Route aufgerufen!");
            addEspLog("TEST-Route aufgerufen - WebServer funktioniert!");
            request->send(200, "text/plain", "WebServer funktioniert! Timestamp: " + String(millis()));
        });

        // DIAGNOSE: Einfache Akku-Test-Route (ohne BQ25895)
        server.on("/battery-test", HTTP_GET, [](AsyncWebServerRequest *request){
            Serial.println("🔍 /battery-test Route aufgerufen");
            String response = "{";
            response += "\"vbat\":4.12,";
            response += "\"vsys\":4.10,";
            response += "\"vbus\":5.00,";
            response += "\"ichg\":250,";
            response += "\"iin\":150,";
            response += "\"power\":1.030,";
            response += "\"percentage\":85,";
            response += "\"temperature\":\"Normal\",";
            response += "\"chargeStatus\":\"Test-Modus\",";
            response += "\"isCharging\":true";
            response += "}";
            request->send(200, "application/json", response);
        });

        // DIAGNOSE: Einfache Log-Test-Route
        server.on("/logs-test", HTTP_GET, [](AsyncWebServerRequest *request){
            Serial.println("🔍 /logs-test Route aufgerufen");
            String response = "{";
            response += "\"esp\":[\"Test ESP Log 1\",\"Test ESP Log 2\"],";
            response += "\"bq\":[\"Test BQ Log 1\",\"Test BQ Log 2\"]";
            response += "}";
            request->send(200, "application/json", response);
        });

        // Route für die Netzwerk-Konfigurationsseite
        server.on("/network", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(200, "text/html", network_html);
        });

        // Route für die Display-Konfigurationsseite
        server.on("/display", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(200, "text/html", display_html);
        });

        // DIAGNOSE: Test-Seite für Akku und Logs
        server.on("/diagnose", HTTP_GET, [](AsyncWebServerRequest *request){
            String html = R"rawliteral(
<!DOCTYPE html>
<html><head><title>Diagnose</title></head><body>
<h1>Diagnose-Seite</h1>
<button onclick="testBattery()">Test Akku-Route</button>
<button onclick="testLogs()">Test Log-Route</button>
<button onclick="testRealBattery()">Test Echte Akku-Route</button>
<button onclick="testRealLogs()">Test Echte Log-Route</button>
<div id="results"></div>
<script>
function testBattery() {
    fetch('/battery-test')
    .then(r => r.json())
    .then(d => document.getElementById('results').innerHTML = '<h3>Akku-Test:</h3><pre>' + JSON.stringify(d, null, 2) + '</pre>')
    .catch(e => document.getElementById('results').innerHTML = '<h3>Akku-Test FEHLER:</h3>' + e);
}
function testLogs() {
    fetch('/logs-test')
    .then(r => r.json())
    .then(d => document.getElementById('results').innerHTML = '<h3>Log-Test:</h3><pre>' + JSON.stringify(d, null, 2) + '</pre>')
    .catch(e => document.getElementById('results').innerHTML = '<h3>Log-Test FEHLER:</h3>' + e);
}
function testRealBattery() {
    fetch('/battery-status')
    .then(r => r.json())
    .then(d => document.getElementById('results').innerHTML = '<h3>Echte Akku-Daten:</h3><pre>' + JSON.stringify(d, null, 2) + '</pre>')
    .catch(e => document.getElementById('results').innerHTML = '<h3>Echte Akku-Daten FEHLER:</h3>' + e);
}
function testRealLogs() {
    fetch('/get-logs')
    .then(r => r.json())
    .then(d => document.getElementById('results').innerHTML = '<h3>Echte Log-Daten:</h3><pre>' + JSON.stringify(d, null, 2) + '</pre>')
    .catch(e => document.getElementById('results').innerHTML = '<h3>Echte Log-Daten FEHLER:</h3>' + e);
}
</script>
</body></html>
)rawliteral";
            request->send(200, "text/html", html);
        });

        // Route für die aktuellen Werte
        server.on("/values", HTTP_GET, [](AsyncWebServerRequest *request){
            StaticJsonDocument<300> doc;
            doc["mode"] = currentMode;
            doc["brightness"] = brightness;
            doc["speed"] = animSpeed;
            doc["sensitivity"] = NOISE_LEVEL;
            doc["frequency"] = micFrequency;
            doc["primaryColor"] = primaryHue;
            doc["secondaryColor"] = secondaryHue;
            doc["ledsEnabled"] = ledsEnabled;

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        });

        // Route für die Netzwerk-Werte
        server.on("/network-values", HTTP_GET, [](AsyncWebServerRequest *request){
            StaticJsonDocument<512> doc;
            doc["apname"] = apName;
            doc["appassword"] = apPassword;
            doc["wifiSSID"] = wifiSSID;
            doc["wifiPassword"] = wifiPassword;
            doc["wifiEnabled"] = wifiConnectEnabled;
            doc["macAddress"] = WiFi.macAddress();
            doc["deviceID"] = deviceID;
            doc["otaEnabled"] = settings.otaEnabled;
            doc["currentVersion"] = dynamicVersion.length() > 0 ? dynamicVersion : String(firmwareVersion);
            
            // OTA-Server-Status prüfen
            String otaStatus = "Nicht verbunden";
            if (wifiConnectEnabled && WiFi.status() == WL_CONNECTED) {
                otaStatus = "WiFi verbunden";
                // TODO: Echter Server-Check kann später hinzugefügt werden
            }
            doc["otaStatus"] = otaStatus;

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        });

        // Route für Update-Prüfung
        server.on("/check-updates", HTTP_POST, [](AsyncWebServerRequest *request){
            Serial.println("Update-Prüfung angefordert");
            addEspLog("Update-Prüfung angefordert");
            
            // Antwort senden - Update-Prüfung erfolgt automatisch alle 10 Sekunden
            StaticJsonDocument<200> doc;
            doc["success"] = true;
            doc["updateAvailable"] = false;
            doc["message"] = "Update-Prüfung wird beim nächsten automatischen Check durchgeführt";
            
            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        });

        // Route für Update-Installation
        server.on("/install-update", HTTP_POST, [](AsyncWebServerRequest *request){
            Serial.println("Update-Installation angefordert");
            addEspLog("Update-Installation angefordert");
            
            // Antwort senden - Update-Installation erfolgt automatisch wenn verfügbar
            StaticJsonDocument<200> doc;
            doc["success"] = true;
            doc["message"] = "Update-Installation wird beim nächsten automatischen Check durchgeführt";
            
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
            doc["nameColorRed"] = displayContent.nameColorRed;

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

        // Route für Reboot-Befehl (für OTA Server)
        server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request){
            Serial.println("Reboot-Befehl empfangen!");
            addEspLog("Reboot-Befehl von OTA Server empfangen");
            
            // Sofortige Antwort senden
            request->send(200, "application/json", "{\"status\":\"rebooting\"}");
            
            // Timer für verzögerten Reboot setzen
            xTaskCreate([](void* parameter) {
                vTaskDelay(pdMS_TO_TICKS(1000)); // 1 Sekunde warten
                ESP.restart();
                vTaskDelete(NULL);
            }, "reboot_task", 2048, NULL, 1, NULL);
        });

        // Alternative GET-Route für Reboot (einfacher)
        server.on("/api/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
            Serial.println("Reboot-Befehl (GET) empfangen!");
            addEspLog("Reboot-Befehl (GET) von OTA Server empfangen");
            
            // Sofortige Antwort senden
            request->send(200, "application/json", "{\"status\":\"rebooting\"}");
            
            // Timer für verzögerten Reboot setzen
            xTaskCreate([](void* parameter) {
                vTaskDelay(pdMS_TO_TICKS(1000)); // 1 Sekunde warten
                ESP.restart();
                vTaskDelete(NULL);
            }, "reboot_task", 2048, NULL, 1, NULL);
        });

        // Einfache Reboot-Route ohne Timer (direkt)
        server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
            Serial.println("Einfacher Reboot-Befehl empfangen!");
            addEspLog("Einfacher Reboot-Befehl empfangen");
            
            // Antwort senden und sofort rebooten
            request->send(200, "text/plain", "Rebooting...");
            
            // Kurze Verzögerung für die Antwort
            delay(100);
            ESP.restart();
        });

        // Test-Route für Erreichbarkeit
        server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request){
            Serial.println("Ping-Anfrage empfangen!");
            request->send(200, "application/json", "{\"status\":\"ok\",\"device\":\"" + deviceID + "\"}");
        });

        // Route für Einstellungs-Updates
        server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request){
            if (request->hasParam("param") && request->hasParam("value")) {
                String param = request->getParam("param")->value();
                String value = request->getParam("value")->value();
                
                if (param == "mode") {
                    currentMode = value.toInt();
                    if (currentMode > 24) currentMode = 24; // Max 25 Modi (0-24)
                    settings.mode = currentMode;
                } else if (param == "brightness") {
                    brightness = value.toInt();
                    if (brightness < 12) brightness = 12; // Mindesthelligkeit zurück auf 12
                    FastLED.setBrightness(brightness);
                    settings.brightness = brightness;
                } else if (param == "speed") {
                    animSpeed = value.toInt();
                    settings.animationSpeed = animSpeed;
                } else if (param == "sensitivity") {
                    NOISE_LEVEL = value.toInt();
                    settings.noiseLevel = NOISE_LEVEL;
                } else if (param == "frequency") {
                    micFrequency = value.toInt();
                    settings.micFrequency = micFrequency;
                } else if (param == "primaryColor") {
                    primaryHue = value.toInt();
                    if (primaryHue > 255) primaryHue = 255;
                    settings.primaryHue = primaryHue;  // In Settings speichern
                } else if (param == "secondaryColor") {
                    secondaryHue = value.toInt();
                    if (secondaryHue > 255) secondaryHue = 255;
                    settings.secondaryHue = secondaryHue;  // In Settings speichern
                }
                
                saveSettings();
                Serial.printf("Parameter %s auf %s gesetzt\n", param.c_str(), value.c_str());
            }
            request->send(200, "text/plain", "OK");
        });

        // Route für Netzwerk-Updates
        server.on("/update-network", HTTP_POST, [](AsyncWebServerRequest *request){
            request->send(200, "text/plain", "OK");
        }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
            if (index + len == total) {
                StaticJsonDocument<512> doc;
                deserializeJson(doc, (char*)data);
                
                String newApName = doc["apname"].as<String>();
                String newApPassword = doc["appassword"].as<String>();
                String newWifiSSID = doc["wifiSSID"].as<String>();
                String newWifiPassword = doc["wifiPassword"].as<String>();
                bool newWifiEnabled = doc["wifiEnabled"].as<String>() == "true";
                bool newOtaEnabled = doc["otaEnabled"].as<String>() == "true";
                
                Serial.println("Aktualisiere Netzwerk-Einstellungen:");
                Serial.print("AP Name: "); Serial.println(newApName);
                Serial.print("AP Passwort: "); Serial.println(newApPassword.length() > 0 ? "[gesetzt]" : "[leer]");
                Serial.print("WiFi SSID: "); Serial.println(newWifiSSID);
                Serial.print("WiFi Passwort: "); Serial.println(newWifiPassword.length() > 0 ? "[gesetzt]" : "[leer]");
                Serial.print("WiFi aktiviert: "); Serial.println(newWifiEnabled ? "Ja" : "Nein");
                Serial.print("OTA aktiviert: "); Serial.println(newOtaEnabled ? "Ja" : "Nein");
                
                // Globale Variablen aktualisieren
                apName = newApName;
                apPassword = newApPassword;
                wifiConnectEnabled = newWifiEnabled;
                settings.otaEnabled = newOtaEnabled;
                
                // WiFi-Einstellungen nur aktualisieren wenn sie sich geändert haben
                if (newWifiSSID != wifiSSID || newWifiPassword != wifiPassword) {
                    wifiSSID = newWifiSSID;
                    wifiPassword = newWifiPassword;
                    
                    // Wenn WiFi aktiviert ist, versuche neue Verbindung
                    if (wifiConnectEnabled && newWifiSSID.length() > 0) {
                        Serial.println("Versuche neue WiFi-Verbindung...");
                        WiFi.disconnect();
                        WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
                    }
                }
                
                // WiFi-Modus basierend auf Einstellungen setzen
                if (wifiConnectEnabled) {
                    WiFi.mode(WIFI_STA);
                } else {
                    WiFi.mode(WIFI_AP);
                }
                
                // Access Point neu starten
                WiFi.softAP(apName.c_str(), apPassword.length() > 0 ? apPassword.c_str() : NULL);
                
                Serial.print("WiFi AP neu gestartet als: ");
                Serial.println(apName);
                
                // Einstellungen speichern
                settings.apName = apName;
                settings.apPassword = apPassword;
                settings.wifiEnabled = wifiConnectEnabled;
                settings.wifiSSID = wifiSSID;
                settings.wifiPassword = wifiPassword;
                // OTA-Einstellung wurde bereits oben gesetzt
                bool saved = saveSettings();
                Serial.printf("Netzwerk-Settings gespeichert: %s\n", saved ? "ERFOLG" : "FEHLER");
                Serial.printf("OTA aktiviert: %s\n", settings.otaEnabled ? "Ja" : "Nein");
                
                addEspLog("Netzwerk aktualisiert: AP=" + apName + ", WiFi=" + newWifiSSID);
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
                        
                        if (formStr.indexOf("nameRed=") >= 0) {
                            int start = formStr.indexOf("nameRed=") + 7;
                            int end = formStr.indexOf("&", start);
                            if (end < 0) end = formStr.length();
                            String val = formStr.substring(start, end);
                            displayContent.nameColorRed = (val == "true" || val == "on" || val == "1");
                            Serial.println("Alt-Parse: Name-Farbe Rot=" + String(displayContent.nameColorRed));
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
                    Serial.println("Bild-Upload wegen niedriger Batterie abgebrochen");
                    request->send(403, "text/plain", "Bild-Upload wegen niedriger Batterie nicht möglich");
                } else {
                    request->send(200, "text/plain", "OK");
                }
            },
            [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
                static String imagePath;
                static File imageFile;
                
                if(!index) {
                    Serial.println("Starte Bild-Upload: " + filename);
                    addEspLog("Starte Bild-Upload: " + filename);
                    
                    // Debug: Erste Bytes ausgeben
                    if (len >= 10) {
                        Serial.printf("🔍 Erste 10 Bytes: ");
                        for (int i = 0; i < 10; i++) {
                            Serial.printf("0x%02X ", data[i]);
                        }
                        Serial.println();
                        addEspLog("Erste Bytes: " + String(data[0], HEX) + " " + String(data[1], HEX) + " " + String(data[2], HEX));
                    }
                    
                    // Dateiformat basierend auf Inhalt (Header) oder Dateiname erkennen
                    // WICHTIG: Header-Erkennung hat Vorrang vor Dateiname!
                    String extension = "";
                    
                    // Zuerst: Header-basierte Erkennung (hat Vorrang!)
                    if (len >= 2 && data[0] == 0x42 && data[1] == 0x4D) { // 'B' = 0x42, 'M' = 0x4D
                        extension = ".bmp";
                        Serial.println("🖼️ BMP-Format anhand Header erkannt (0x42 0x4D) - VORRANG!");
                        addEspLog("BMP-Format anhand Header erkannt - VORRANG!");
                    } else if (len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
                        extension = ".jpg";
                        Serial.println("🖼️ JPG-Format anhand Header erkannt (0xFF 0xD8 0xFF) - VORRANG!");
                        addEspLog("JPG-Format anhand Header erkannt - VORRANG!");
                    } 
                    // Nur wenn Header-Erkennung fehlschlägt: Dateiname verwenden
                    else if (filename.endsWith(".bmp") || filename.endsWith(".BMP")) {
                        extension = ".bmp";
                        Serial.println("🖼️ BMP-Datei erkannt (Dateiname als Fallback)");
                        addEspLog("BMP-Datei erkannt (Dateiname als Fallback)");
                    } else if (filename.endsWith(".jpg") || filename.endsWith(".jpeg") || 
                               filename.endsWith(".JPG") || filename.endsWith(".JPEG")) {
                        extension = ".jpg";
                        Serial.println("🖼️ JPG-Datei erkannt (Dateiname als Fallback)");
                        addEspLog("JPG-Datei erkannt (Dateiname als Fallback)");
                    } else {
                        // Letzter Fallback: .bmp für bessere Kompatibilität
                        extension = ".bmp";
                        Serial.println("⚠️ Unbekanntes Format, verwende .bmp als Fallback");
                        addEspLog("Unbekanntes Format, verwende .bmp als Fallback");
                    }
                    
                    imagePath = "/badge_image" + extension;
                    Serial.println("📁 Speichere als: " + imagePath);
                    addEspLog("Speichere als: " + imagePath);
                    
                    // Alte Bilddateien löschen (sowohl .jpg als auch .bmp)
                    if(SPIFFS.exists("/badge_image.jpg")) {
                        SPIFFS.remove("/badge_image.jpg");
                        Serial.println("Alte JPG-Bilddatei gelöscht");
                        addEspLog("Alte JPG-Bilddatei gelöscht");
                    }
                    if(SPIFFS.exists("/badge_image.bmp")) {
                        SPIFFS.remove("/badge_image.bmp");
                        Serial.println("Alte BMP-Bilddatei gelöscht");
                        addEspLog("Alte BMP-Bilddatei gelöscht");
                    }
                    
                    // Neue Datei zum Schreiben öffnen
                    imageFile = SPIFFS.open(imagePath, "w");
                    if (!imageFile) {
                        Serial.println("FEHLER: Kann Bilddatei nicht zum Schreiben öffnen");
                        addEspLog("FEHLER: Kann Bilddatei nicht zum Schreiben öffnen");
                        return;
                    }
                    Serial.println("Bilddatei zum Schreiben geöffnet: " + imagePath);
                    addEspLog("Bilddatei zum Schreiben geöffnet: " + imagePath);
                }
                
                if(imageFile) {
                    size_t written = imageFile.write(data, len);
                    if (written != len) {
                        Serial.printf("WARNUNG: Nur %d von %d Bytes geschrieben\n", written, len);
                        addEspLog("WARNUNG: Nur " + String(written) + " von " + String(len) + " Bytes geschrieben");
                    }
                }
                
                if(final) {
                    if(imageFile) {
                        imageFile.close();
                        Serial.printf("Bild-Upload abgeschlossen: %d Bytes\n", index + len);
                        addEspLog("Bild-Upload abgeschlossen: " + String(index + len) + " Bytes");
                        
                        // Bildpfad in displayContent speichern
                        displayContent.imagePath = imagePath;
                        Serial.println("Bildpfad in displayContent gespeichert: " + imagePath);
                        addEspLog("Bildpfad in displayContent gespeichert: " + imagePath);
                        
                        // WICHTIG: Speichern OHNE automatisches Update, da wir manuell ein Update senden
                        saveDisplayContent(false);  // Verhindert doppeltes Update!
                        
                        // Manuelles Display-Update senden
                        if (displayAvailable && displayQueue != NULL) {
                            Serial.println("Sende manuelles Display-Update nach Bild-Upload");
                            addEspLog("Sende manuelles Display-Update nach Bild-Upload");
                            DisplayCommand cmd = CMD_UPDATE;
                            if (xQueueSend(displayQueue, &cmd, pdMS_TO_TICKS(1000)) == pdPASS) {
                                Serial.println("✅ Display-Update erfolgreich gesendet");
                                addEspLog("✅ Display-Update erfolgreich gesendet");
                            } else {
                                Serial.println("❌ Display-Update konnte nicht gesendet werden");
                                addEspLog("❌ Display-Update konnte nicht gesendet werden");
                            }
                        }
                        
                        Serial.println("Bildpfad gespeichert und Display-Update gesendet: " + imagePath);
                    } else {
                        Serial.println("FEHLER: Bilddatei war nicht geöffnet beim Abschluss");
                        addEspLog("FEHLER: Bilddatei war nicht geöffnet beim Abschluss");
                    }
                }
            });

        // Route zum Löschen des Bildes
        server.on("/delete-image", HTTP_GET, [](AsyncWebServerRequest *request){
            if (isLowBattery) {
                request->send(403, "text/plain", "Bild-Löschung wegen niedriger Batterie nicht möglich");
                return;
            }
            
            bool success = false;
            
            // Beide möglichen Bilddateien löschen
            if(SPIFFS.exists("/badge_image.jpg")) {
                if(SPIFFS.remove("/badge_image.jpg")) {
                    Serial.println("JPG-Bilddatei erfolgreich gelöscht: /badge_image.jpg");
                    addEspLog("JPG-Bilddatei gelöscht: /badge_image.jpg");
                    success = true;
                } else {
                    Serial.println("FEHLER: Konnte JPG-Bilddatei nicht löschen");
                    addEspLog("FEHLER: Konnte JPG-Bilddatei nicht löschen");
                }
            }
            
            if(SPIFFS.exists("/badge_image.bmp")) {
                if(SPIFFS.remove("/badge_image.bmp")) {
                    Serial.println("BMP-Bilddatei erfolgreich gelöscht: /badge_image.bmp");
                    addEspLog("BMP-Bilddatei gelöscht: /badge_image.bmp");
                    success = true;
                } else {
                    Serial.println("FEHLER: Konnte BMP-Bilddatei nicht löschen");
                    addEspLog("FEHLER: Konnte BMP-Bilddatei nicht löschen");
                }
            }
            
            if(!SPIFFS.exists("/badge_image.jpg") && !SPIFFS.exists("/badge_image.bmp")) {
                Serial.println("Keine Bilddateien vorhanden - erfolgreich gelöscht");
                success = true;
            }
            
            if(success) {
                // Bildpfad aus displayContent entfernen
                displayContent.imagePath = "";
                saveDisplayContent();  // Speichert und sendet CMD_UPDATE
                request->send(200, "text/plain", "OK");
            } else {
                request->send(500, "text/plain", "Fehler beim Löschen");
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
            
            if (request->hasParam("nameRed")) {
                String nameRed = request->getParam("nameRed")->value();
                displayContent.nameColorRed = (nameRed == "1" || nameRed == "true" || nameRed == "on");
                Serial.println("Name-Farbe Rot gesetzt: " + String(displayContent.nameColorRed));
            }
            
            // Speichern und Display aktualisieren
            saveDisplayContent();  // Diese Funktion sendet bereits CMD_UPDATE!
            
            // ENTFERNT: Doppeltes Update vermeiden
            // Das saveDisplayContent() sendet bereits ein CMD_UPDATE an die Display-Queue
            // Ein zweites Update hier würde zu doppelten Aktualisierungen führen
            
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
            Serial.println("🔍 /battery-status Route aufgerufen");
            
            // KRITISCH: Prüfe ob charger initialisiert ist
            if (charger == nullptr) {
                Serial.println("❌ FEHLER: charger ist nullptr!");
                // Fallback-Daten senden statt Fehler
                String response = "{";
                response += "\"vbat\":3.70,";
                response += "\"vsys\":3.70,";
                response += "\"vbus\":0.00,";
                response += "\"ichg\":0,";
                response += "\"iin\":0,";
                response += "\"power\":0.000,";
                response += "\"percentage\":50,";
                response += "\"temperature\":\"Normal\",";
                response += "\"chargeStatus\":\"Charger nicht verfügbar\",";
                response += "\"isCharging\":false";
                response += "}";
                request->send(200, "application/json", response);
                return;
            }
            
            Serial.println("✅ Charger ist initialisiert, lese Werte aus...");
            
            // Alle Messwerte auslesen mit Fehlerbehandlung
            float vbat = -1.0f, vsys = -1.0f, vbus = -1.0f, ichg = 0.0f, iin = 0.0f;
            String tempStatus = "Unbekannt";
            String chargeStatus = "Unbekannt";
            
            try {
                vbat = charger->getVBAT();
                vsys = charger->getVSYS();
                vbus = charger->getVBUS();
                ichg = charger->getICHG() * 1000; // Umrechnung in mA
                iin = charger->getIIN() * 1000;   // Umrechnung in mA
                tempStatus = charger->getTemperatureStatus();
                chargeStatus = charger->getChargeStatus();
            } catch (...) {
                Serial.println("❌ Fehler beim Lesen der BQ25895-Werte");
            }
            
            // DEBUG: Echte Werte ausgeben ohne Korrektur
            Serial.printf("🔍 WebUI DEBUG - Rohe Messwerte: VBAT=%.3fV, VSYS=%.3fV, VBUS=%.3fV, ICHG=%.0fmA\n", 
                         vbat, vsys, vbus, ichg);
            
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
            
            // Plausible Werte sicherstellen
            if (vbat < 0 || vbat > 5.0f) vbat = 3.7f; // Fallback
            if (vsys < 0 || vsys > 5.0f) vsys = vbat; // Fallback
            if (vbus < 0 || vbus > 20.0f) vbus = 0.0f; // Fallback
            if (ichg < -5000 || ichg > 5000) ichg = 0.0f; // Fallback
            if (iin < 0 || iin > 5000) iin = 0.0f; // Fallback
            
            float power = vbat * (abs(ichg) / 1000.0f); // Berechnung in Watt (absoluter Wert)
            
            // Batterie-Prozentsatz basierend auf Spannung berechnen
            int percentage = 0;
            if (vbat >= 4.2f) {
                percentage = 100;
            } else if (vbat >= 3.0f) {
                // Lineare Interpolation zwischen 3.0V (0%) und 4.2V (100%)
                percentage = (int)((vbat - 3.0f) * 100.0f / 1.2f);
                if (percentage > 100) percentage = 100;
                if (percentage < 0) percentage = 0;
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
            response += "\"isCharging\":" + String(charger->isVBUSPresent() ? "true" : "false");
            
            // ENTFERNT: Fehler-Status und Register-Werte
            
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
                    FastLED.setBrightness(brightness);
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
            Serial.println("🔍 /get-logs Route aufgerufen");
            Serial.printf("ESP-Logs: %d Einträge, BQ-Logs: %d Einträge\n", espLogs.size(), bqLogs.size());
            
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
        
        // Route für ESP32-Neustart
        server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
            request->send(200, "text/plain", "ESP32 wird neu gestartet...");
            
            // Kurze Verzögerung, damit die Antwort gesendet wird
            delay(1000);
            
            // ESP32 neu starten
            ESP.restart();
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
    // Automatische Größenbegrenzung
    if (espLogs.size() >= MAX_LOG_ENTRIES) {
        // Entferne die ältesten 20 Einträge auf einmal für bessere Performance
        espLogs.erase(espLogs.begin(), espLogs.begin() + 20);
    }
    
    // Timestamp mit reduzierter Präzision für weniger Memory-Verbrauch
    String timestamp = "[" + String(millis() / 1000) + "s] ";
    
    // String-Optimierung: Reserve Speicher im Voraus
    String logEntry;
    logEntry.reserve(timestamp.length() + message.length() + 5);
    logEntry = timestamp + message;
    
    espLogs.push_back(logEntry);
    
    // Periodische Speicher-Optimierung
    static unsigned long lastOptimization = 0;
    if (millis() - lastOptimization > 300000) { // Alle 5 Minuten
        lastOptimization = millis();
        espLogs.shrink_to_fit(); // Reduziere reservierten Speicher
    }
}

// Funktion zum Hinzufügen eines BQ25895-Logs
void addBqLog(const String& message) {
    // Automatische Größenbegrenzung
    if (bqLogs.size() >= MAX_LOG_ENTRIES) {
        // Entferne die ältesten 20 Einträge auf einmal für bessere Performance
        bqLogs.erase(bqLogs.begin(), bqLogs.begin() + 20);
    }
    
    // Timestamp mit reduzierter Präzision für weniger Memory-Verbrauch
    String timestamp = "[" + String(millis() / 1000) + "s] ";
    
    // String-Optimierung: Reserve Speicher im Voraus
    String logEntry;
    logEntry.reserve(timestamp.length() + message.length() + 5);
    logEntry = timestamp + message;
    
    bqLogs.push_back(logEntry);
    
    // Periodische Speicher-Optimierung
    static unsigned long lastOptimization = 0;
    if (millis() - lastOptimization > 300000) { // Alle 5 Minuten
        lastOptimization = millis();
        bqLogs.shrink_to_fit(); // Reduziere reservierten Speicher
    }
} 