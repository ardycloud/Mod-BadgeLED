#ifndef WEBUI_H
#define WEBUI_H

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include "Settings.h"
#include "Display.h"
#include "Config.h"

// WiFi Credentials
const char* ssid = "Halle";
const char* password = "Evelyne1606";

// Webserver auf Port 80
AsyncWebServer server(80);

// Externe Variablen
extern uint8_t gCurrentMode;
extern uint8_t BRIGHTNESS;
extern bool wifiEnabled;
extern uint8_t NOISE_LEVEL;
extern uint16_t animationSpeed;
extern uint16_t micFrequency;
extern const bool USE_DISPLAY;

// HTML Template als PROGMEM String
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
            border: 1px solid color:rgb(0, 191, 255);;
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
            background:color:rgb(0, 191, 255);;
            cursor: pointer;
            border-radius: 50%;
        }
        .value-display {
            text-align: right;
            color:rgb(0, 191, 255);
            font-size: 0.9em;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>LED Badge Controller</h1>
        
        <div class="card">
            <div class="control-group">
                <label for="mode">Animation Mode</label>
                <select id="mode" onchange="updateValue('mode', this.value)">
                    <option value="0">Rainbow</option>
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
                <input type="range" id="brightness" min="1" max="25" value="5" 
                       oninput="updateValue('brightness', this.value)">
                <div class="value-display"><span id="brightnessValue">5</span>%</div>
            </div>
        </div>

        <div class="card">
            <div class="control-group">
                <label for="speed">Animation Speed</label>
                <input type="range" id="speed" min="5" max="100" value="20" 
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
            <a href="/display" style="color: #00ff88; text-decoration: none; display: block; text-align: center;">
                Display Konfiguration anpassen
            </a>
        </div>
    </div>

    <script>
        function updateValue(param, value) {
            const xhr = new XMLHttpRequest();
            xhr.open("GET", `/update?param=${param}&value=${value}`, true);
            xhr.send();
            
            // Update display value
            const displayElement = document.getElementById(param + 'Value');
            if (displayElement) {
                displayElement.textContent = value;
            }
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
            });
        }
    </script>
</body>
</html>
)rawliteral";

// HTML für die Display-Konfigurationsseite
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
            <button onclick="updateDisplay()">Aktualisieren</button>
            <button class="test-button" onclick="testDisplay()">Display Test</button>
            <button class="clear-button" style="background: #ff4444 !important; margin-top: 10px;" onclick="clearDisplay()">Display leeren</button>
        </div>
    </div>

    <script>
        // Lade aktuelle Werte
        window.onload = function() {
            fetch('/display-values')
            .then(response => response.json())
            .then(data => {
                document.getElementById('name').value = data.name;
                document.getElementById('description').value = data.description;
                document.getElementById('telegram').value = data.telegram;
            });
        }

        function updateDisplay() {
            const data = {
                name: document.getElementById('name').value,
                description: document.getElementById('description').value,
                telegram: document.getElementById('telegram').value
            };

            fetch('/update-display', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify(data)
            })
            .then(response => response.text())
            .then(result => {
                if(result === 'OK') {
                    alert('Display wird aktualisiert, Bitte warten...');
                } else {
                    alert('Fehler beim Aktualisieren!');
                }
            });
        }

        function testDisplay() {
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
            fetch('/clear-display')
            .then(response => response.text())
            .then(result => {
                if(result === 'OK') {
                    alert('Display wurde geleert!');
                    // Formularfelder leeren
                    document.getElementById('name').value = '';
                    document.getElementById('description').value = '';
                    document.getElementById('telegram').value = '';
                } else {
                    alert('Fehler beim leeren des Displays!');
                }
            });
        }
    </script>
</body>
</html>
)rawliteral";

void initWebServer() {
    // Verbindung zum WiFi herstellen
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    
    wifiEnabled = true;
    Serial.println("Connected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // Route für die Hauptseite
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        String html = String(index_html);
        if (displayAvailable) {
            html.replace("</div>\n    </div>", 
                "        </div>\n"
                "        <div class=\"card\">\n"
                "            <a href=\"/display\" style=\"color: #00ff88; text-decoration: none; display: block; text-align: center;\">\n"
                "                Display Konfiguration anpassen\n"
                "            </a>\n"
                "        </div>\n"
                "    </div>");
        }
        request->send(200, "text/html", html);
    });

    // Route für Aktualisierungen
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
        String param = request->getParam("param")->value();
        int value = request->getParam("value")->value().toInt();
        
        if (param == "mode") {
            gCurrentMode = value;
        }
        else if (param == "brightness") {
            BRIGHTNESS = value;
            FastLED.setBrightness(BRIGHTNESS);
        }
        else if (param == "speed") {
            animationSpeed = value;
        }
        else if (param == "sensitivity") {
            NOISE_LEVEL = value;
        }
        else if (param == "frequency") {
            micFrequency = value;
        }
        
        // Einstellungen nach jeder Änderung speichern
        saveSettings();
        request->send(200, "text/plain", "OK");
    });

    // Route für aktuelle Werte
    server.on("/values", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = "{";
        json += "\"mode\":" + String(gCurrentMode) + ",";
        json += "\"brightness\":" + String(BRIGHTNESS) + ",";
        json += "\"speed\":" + String(animationSpeed) + ",";
        json += "\"sensitivity\":" + String(NOISE_LEVEL) + ",";
        json += "\"frequency\":" + String(micFrequency);
        json += "}";
        request->send(200, "application/json", json);
    });

    // Route für die Display-Konfigurationsseite
    server.on("/display", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!USE_DISPLAY) {
            request->send(200, "text/html", "<html><body style='font-family: Arial; background: #1a1a1a; color: #fff; padding: 20px;'><h1 style='color: #ff4444;'>Display deaktiviert</h1><p>Das Display ist in der Konfiguration deaktiviert. Bitte setzen Sie USE_DISPLAY in main.cpp auf true, wenn Sie ein Display verwenden.</p><a href='/' style='color: #00ff88; text-decoration: none;'>Zur Startseite</a></body></html>");
            return;
        }
        if (!displayAvailable) {
            request->send(200, "text/html", "<html><body style='font-family: Arial; background: #1a1a1a; color: #fff; padding: 20px;'><h1 style='color: #ff4444;'>Kein Display gefunden</h1><p>Das E-Ink Display wurde nicht erkannt. Bitte überprüfen Sie die Verbindung.</p><a href='/' style='color: #00ff88; text-decoration: none;'>Zur Startseite</a></body></html>");
            return;
        }
        request->send(200, "text/html", display_html);
    });

    // Route für aktuelle Display-Werte
    server.on("/display-values", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!USE_DISPLAY || !displayAvailable) {
            request->send(200, "application/json", "{\"error\":\"Display nicht verfügbar\"}");
            return;
        }
        String json = "{";
        json += "\"name\":\"" + String(displayContent.name) + "\",";
        json += "\"description\":\"" + String(displayContent.description) + "\",";
        json += "\"telegram\":\"" + String(displayContent.telegram) + "\"";
        json += "}";
        request->send(200, "application/json", json);
    });

    // Route für Display-Updates
    server.on("/update-display", HTTP_POST, [](AsyncWebServerRequest *request){
        if (!USE_DISPLAY || !displayAvailable) {
            request->send(200, "text/plain", "ERROR: Display nicht verfügbar");
            return;
        }
        request->send(200, "text/plain", "OK");
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        if (!USE_DISPLAY || !displayAvailable) return;
        
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, (const char*)data);
        
        if (!error) {
            strlcpy(displayContent.name, doc["name"] | "ArdyMoon", sizeof(displayContent.name));
            strlcpy(displayContent.description, doc["description"] | "", sizeof(displayContent.description));
            strlcpy(displayContent.telegram, doc["telegram"] | "", sizeof(displayContent.telegram));
            
            saveDisplayContent();
            
            // Display-Update über Queue senden
            if (displayQueue != NULL) {
                DisplayCommand cmd = CMD_UPDATE;
                xQueueSend(displayQueue, &cmd, portMAX_DELAY);
            }
        }
    });

    // Neue Route für Display-Test
    server.on("/test-display", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!USE_DISPLAY || !displayAvailable) {
            request->send(200, "text/plain", "ERROR: Display nicht verfügbar");
            return;
        }
        
        // Display-Test über Queue senden
        if (displayQueue != NULL) {
            DisplayCommand cmd = CMD_INIT;
            if (xQueueSend(displayQueue, &cmd, portMAX_DELAY) == pdTRUE) {
                request->send(200, "text/plain", "OK");
            } else {
                request->send(200, "text/plain", "ERROR: Queue voll");
            }
        } else {
            request->send(200, "text/plain", "ERROR: Display-Queue nicht verfügbar");
        }
    });

    // Neue Route für Display-Löschen
    server.on("/clear-display", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!USE_DISPLAY || !displayAvailable) {
            request->send(200, "text/plain", "ERROR: Display nicht verfügbar");
            return;
        }
        
        // Display-Inhalte löschen
        strlcpy(displayContent.name, "", sizeof(displayContent.name));
        strlcpy(displayContent.description, "", sizeof(displayContent.description));
        strlcpy(displayContent.telegram, "", sizeof(displayContent.telegram));
        
        // Speichern der leeren Inhalte
        saveDisplayContent();
        
        // Display-Update über Queue senden
        if (displayQueue != NULL) {
            DisplayCommand cmd = CMD_UPDATE;
            if (xQueueSend(displayQueue, &cmd, portMAX_DELAY) == pdTRUE) {
                request->send(200, "text/plain", "OK");
            } else {
                request->send(200, "text/plain", "ERROR: Queue voll");
            }
        } else {
            request->send(200, "text/plain", "ERROR: Display-Queue nicht verfügbar");
        }
    });

    server.begin();
}

#endif // WEBUI_H 