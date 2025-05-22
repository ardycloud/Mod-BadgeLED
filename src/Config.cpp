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

// Externe Variablen
extern bool displayAvailable;
extern DisplayContent displayContent;
extern QueueHandle_t displayQueue;
extern BQ25895* charger;
extern uint8_t gCurrentMode;
extern uint8_t BRIGHTNESS;
extern bool saveSettings();
extern void saveDisplayContent(); // Diese Funktion ist in Display.cpp definiert
extern void testDisplay(); // Externe Funktion aus Display.cpp
extern void clearDisplay(); // Externe Funktion aus Display.cpp

// LED-Indikator für Tasten - extern definiert in main.cpp
extern unsigned long modeLedOffTime;
extern unsigned long colorLedOffTime;
extern unsigned long brightLedOffTime;
extern bool modeLedActive;
extern bool colorLedActive;
extern bool brightLedActive;

// Button-Konfiguration
const int BUTTON_PIN = 0;
const int DEBOUNCE_DELAY = 50;
unsigned long lastDebounceTime = 0;
int lastButtonState = HIGH;
int buttonState = HIGH;

// Weitere Button-Status-Variablen
unsigned long lastModeButtonPress = 0;
unsigned long lastBrightButtonPress = 0;
unsigned long lastColorButtonPress = 0;
const unsigned long BUTTON_DEBOUNCE = 300; // 300ms Entprellzeit

void checkButtons() {
    // Boot-Button prüfen (Button 0)
    int reading = digitalRead(BUTTON_PIN);
    
    if (reading != lastButtonState) {
        lastDebounceTime = millis();
    }
    
    if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
        if (reading != buttonState) {
            buttonState = reading;
            
            if (buttonState == LOW) {
                // Button wurde gedrückt
                if (displayAvailable) {
                    testDisplay();
                }
            }
        }
    }
    
    lastButtonState = reading;

    // Modus-Button prüfen (Button 7)
    unsigned long currentTime = millis();
    if (digitalRead(MODE_BUTTON) == HIGH) {  // Da Pull-Down verwendet wird, ist HIGH = gedrückt
        if (currentTime - lastModeButtonPress > BUTTON_DEBOUNCE) {
            // Modus wechseln
            gCurrentMode = (gCurrentMode + 1) % 12;  // Zwischen 0-11 wechseln (12 Modi)
            saveSettings();
            lastModeButtonPress = currentTime;
            Serial.print("Modus geändert zu: ");
            Serial.println(gCurrentMode);
            
            // LED-Indikator für Mode-Taste aktivieren (für 1 Sekunde)
            modeLedActive = true;
            modeLedOffTime = currentTime + 1000;
        }
    }
    
    // Color-Button prüfen (Button 6)
    if (digitalRead(COLOR_BUTTON) == HIGH) {  // Da Pull-Down verwendet wird, ist HIGH = gedrückt
        if (currentTime - lastColorButtonPress > BUTTON_DEBOUNCE) {
            // Hier kann die Implementierung für die Color-Taste eingefügt werden
            lastColorButtonPress = currentTime;
            Serial.println("Color-Taste gedrückt");
            
            // LED-Indikator für Color-Taste aktivieren (für 1 Sekunde)
            colorLedActive = true;
            colorLedOffTime = currentTime + 1000;
        }
    }

    // Helligkeits-Button prüfen (Button 5)
    if (digitalRead(BRIGHT_BUTTON) == HIGH) {  // Da Pull-Down verwendet wird, ist HIGH = gedrückt
        if (currentTime - lastBrightButtonPress > BUTTON_DEBOUNCE) {
            // Helligkeit erhöhen (in 10er Schritten)
            BRIGHTNESS = (BRIGHTNESS + 10) % 255;
            if (BRIGHTNESS < 5) BRIGHTNESS = 5;  // Mindesthelligkeit
            FastLED.setBrightness(BRIGHTNESS);
            saveSettings();
            lastBrightButtonPress = currentTime;
            Serial.print("Helligkeit geändert zu: ");
            Serial.println(BRIGHTNESS);
            
            // LED-Indikator für Brightness-Taste aktivieren (für 1 Sekunde)
            brightLedActive = true;
            brightLedOffTime = currentTime + 1000;
        }
    }
}

// Die doppelten Funktionen wurden entfernt
// testDisplay() und clearDisplay() sind jetzt nur noch in Display.cpp definiert 