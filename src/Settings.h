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
    uint8_t mode = 0;            // Standard: Mode 0 (SolidColor)
    uint8_t brightness = 12;     // Standard: 12 statt 5
    uint8_t noiseLevel = 10;
    uint16_t animationSpeed = 50; // Standard: 50 statt 20
    uint16_t micFrequency = 50;
    uint8_t primaryHue = 0;      // Primärfarbe hinzufügen
    uint8_t secondaryHue = 128;  // Sekundärfarbe hinzufügen
    String apName = "LED-Badge";
    String apPassword = "";
    // WiFi-Einstellungen
    bool wifiEnabled = true;     // WiFi-Verbindung aktiviert
    String wifiSSID = "";        // WiFi Netzwerkname
    String wifiPassword = "";    // WiFi Passwort
    // OTA-Einstellungen
    bool otaEnabled = true;      // OTA-Updates aktiviert
    String dynamicVersion = "";  // Dynamische Version für OTA-Updates
};

// Globale Einstellungsvariable
extern Settings settings;

// Dateiname für die Einstellungen
extern const char* settingsFile;

// Funktionsdeklarationen
bool saveSettings();
bool loadSettings();

#endif // SETTINGS_H 