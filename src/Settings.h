#ifndef SETTINGS_H
#define SETTINGS_H

#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <FastLED.h>

// Externe Variablen - diese werden in main.cpp definiert
extern uint8_t gCurrentMode;
extern uint8_t BRIGHTNESS;
extern uint8_t NOISE_LEVEL;
extern uint16_t animationSpeed;
extern uint16_t micFrequency;

// Struktur für die Einstellungen
struct Settings {
    uint8_t mode = 1;
    uint8_t brightness = 5;
    uint8_t noiseLevel = 10;
    uint16_t animationSpeed = 20;
    uint16_t micFrequency = 50;
    String apName = "LED-Badge";
    String apPassword = "";
};

// Globale Einstellungsvariable
extern Settings settings;

// Dateiname für die Einstellungen
extern const char* settingsFile;

// Funktionsdeklarationen
bool saveSettings();
bool loadSettings();

#endif // SETTINGS_H 