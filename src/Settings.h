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
    uint8_t currentMode;
    uint8_t brightness;
    uint8_t noiseLevel;
    uint16_t animationSpeed;
    uint16_t micFrequency;
};

// Globale Einstellungsvariable
extern Settings settings;

// Dateiname für die Einstellungen
extern const char* settingsFile;

// Funktionsdeklarationen
bool saveSettings();
bool loadSettings();

#endif // SETTINGS_H 