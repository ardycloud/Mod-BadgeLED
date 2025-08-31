#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include "Config.h"
#include "Display.h"
#include "WebUI.h"
#include "BQ25895CONFIG.h"
#include "Settings.h"
#include "Globals.h"

// Externe Variablen aus main.cpp - die globalen LED-Variablen sind jetzt in Globals.h
extern bool saveSettings();

// Externe LED-Variablen für Feedback
extern bool modeLedActive;
extern bool colorLedActive;
extern bool brightLedActive;
extern unsigned long modeLedOffTime;
extern unsigned long colorLedOffTime;
extern unsigned long brightLedOffTime;

// Externe Variablen
extern bool displayAvailable;
extern DisplayContent displayContent;
extern QueueHandle_t displayQueue;
extern BQ25895* charger;
extern void testDisplay();
extern void clearDisplay();

// Button-Konfiguration
const int BUTTON_PIN = 0;
const int DEBOUNCE_DELAY = 50;
unsigned long lastDebounceTime = 0;
int lastButtonState = HIGH;
int buttonState = HIGH;

// Button-Status-Variablen
unsigned long lastModeButtonPress = 0;
unsigned long lastBrightButtonPress = 0;
unsigned long lastColorButtonPress = 0;
const unsigned long BUTTON_DEBOUNCE = 300;

void checkButtons() {
    unsigned long currentTime = millis();
    
    // Boot-Button wird in main.cpp für WiFi-Toggle behandelt - hier nicht mehr verwenden
    // Entfernt: Boot-Button Display-Update Logik

    // Modus-Button prüfen (Button 7)
    static bool modeButtonPressed = false;
    static unsigned long modeButtonPressStart = 0;
    
    if (digitalRead(MODE_BUTTON) == HIGH) {
        if (!modeButtonPressed) {
            // Button wurde gerade gedrückt
            modeButtonPressed = true;
            modeButtonPressStart = currentTime;
        } else {
            // Button wird gehalten
            unsigned long pressDuration = currentTime - modeButtonPressStart;
            
            // 3 Sekunden gedrückt - Modus wechseln (AP ↔ Client)
            if (pressDuration >= 3000) {
                // Zwischen Client- und AP-Modus wechseln
                if (wifiConnectEnabled) {
                    // Aktuell im Client-Modus -> zu AP-Modus wechseln
                    Serial.println("Mode-Button: Wechsle zu AP-Modus (3s)");
                    wifiConnectEnabled = false;
                    WiFi.disconnect();
                    WiFi.mode(WIFI_AP);
                    WiFi.softAP(apName.c_str(), apPassword.length() > 0 ? apPassword.c_str() : NULL);
                    wifiEnabled = true;
                    Serial.println("AP-Modus aktiviert: " + apName);
                } else {
                    // Aktuell im AP-Modus -> zu Client-Modus wechseln
                    Serial.println("Mode-Button: Wechsle zu Client-Modus (3s)");
                    wifiConnectEnabled = true;
                    WiFi.softAPdisconnect(true);
                    WiFi.mode(WIFI_STA);
                    if (wifiSSID.length() > 0) {
                        WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
                        Serial.println("Client-Modus aktiviert, verbinde mit: " + wifiSSID);
                    } else {
                        Serial.println("Client-Modus aktiviert, aber keine WiFi-Daten konfiguriert");
                    }
                }
                
                // Einstellungen speichern
                settings.wifiEnabled = wifiConnectEnabled;
                settings.wifiSSID = wifiSSID;
                settings.wifiPassword = wifiPassword;
                saveSettings();
                
                // LED-Feedback für Modus-Wechsel
                modeLedActive = true;
                modeLedOffTime = currentTime + 2000; // 2 Sekunden an für Modus-Wechsel
                
                modeButtonPressed = false; // Verhindert mehrfaches Auslösen
                delay(500); // Entprellzeit
                return; // Beende Funktion um kurzen Tastendruck zu verhindern
            }
        }
    } else {
        // Button wurde losgelassen
        if (modeButtonPressed) {
            unsigned long pressDuration = currentTime - modeButtonPressStart;
            
            // Kurzer Tastendruck (< 3 Sekunden) - LED-Modus wechseln
            if (pressDuration < 3000 && pressDuration > 50) { // Mindestens 50ms für gültigen Tastendruck
                if (currentTime - lastModeButtonPress > BUTTON_DEBOUNCE) {
                    // Modus wechseln (0-24 = 25 Modi)
                    currentMode = (currentMode + 1) % 25;
                    
                    // Settings aktualisieren
                    settings.mode = currentMode;
                    saveSettings();
                    
                    // EINFACH: Direkte Ausgabe ohne Threading-Komplexität
                    Serial.printf("BUTTON (Core 1): Modus geändert zu: %d\n", currentMode);
                    
                    // LED-Feedback aktivieren
                    modeLedActive = true;
                    modeLedOffTime = currentTime + 1000; // 1 Sekunde an
                    
                    lastModeButtonPress = currentTime;
                }
            }
            modeButtonPressed = false;
        }
    }
    
    // Color-Button prüfen (Button 6) - Primärfarbe ändern
    if (digitalRead(COLOR_BUTTON) == HIGH) {
        if (currentTime - lastColorButtonPress > BUTTON_DEBOUNCE) {
            // Primärfarbe in 16er-Schritten ändern
            primaryHue = (primaryHue + 16) % 256;
            
            // Sekundärfarbe automatisch als Komplementärfarbe setzen
            secondaryHue = (primaryHue + 128) % 256;
            
            // EINFACH: Direkte Ausgabe ohne Threading-Komplexität
            Serial.printf("BUTTON (Core 1): Farben geändert - Primär: %d, Sekundär: %d\n", primaryHue, secondaryHue);
            
            // LED-Feedback aktivieren
            colorLedActive = true;
            colorLedOffTime = currentTime + 1000; // 1 Sekunde an
            
            lastColorButtonPress = currentTime;
        }
    }

    // Helligkeits-Button prüfen (Button 5)
    if (digitalRead(BRIGHT_BUTTON) == HIGH) {
        if (currentTime - lastBrightButtonPress > BUTTON_DEBOUNCE) {
            // Helligkeit erhöhen (in 25er Schritten)
            brightness = (brightness + 25) % 255;
            if (brightness < 12) brightness = 12;  // Mindesthelligkeit zurück auf 12
            
            FastLED.setBrightness(brightness);
            
            // Settings aktualisieren
            settings.brightness = brightness;
            saveSettings();
            
            // EINFACH: Direkte Ausgabe ohne Threading-Komplexität
            Serial.printf("BUTTON (Core 1): Helligkeit geändert zu: %d\n", brightness);
            
            // LED-Feedback aktivieren
            brightLedActive = true;
            brightLedOffTime = currentTime + 1000; // 1 Sekunde an
            
            lastBrightButtonPress = currentTime;
        }
    }
} 