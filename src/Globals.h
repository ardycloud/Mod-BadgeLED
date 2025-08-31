#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>

// Globale LED-Variablen (volatile für Thread-Sicherheit)
extern volatile uint8_t primaryHue;      // Primärfarbe (0-255)
extern volatile uint8_t secondaryHue;    // Sekundärfarbe (0-255)
extern volatile uint8_t currentMode;     // Aktueller Modus (0-24)
extern volatile uint8_t brightness;      // Helligkeit (0-255)
extern volatile uint16_t animSpeed;      // Animationsgeschwindigkeit

// WiFi und OTA Variablen
extern bool wifiConnectEnabled;          // WiFi-Verbindung aktiviert
extern String wifiSSID;                  // WiFi Netzwerkname
extern String wifiPassword;              // WiFi Passwort
extern String deviceID;                  // Geräte-ID für OTA (MAC-Adresse)
extern const char* firmwareVersion;      // Firmware Version (in Globals.cpp definieren!)
extern String dynamicVersion;            // Dynamische Version für OTA-Updates
extern const char* hardwaretype;         // Hardware-Typ
extern const char* otaCheckinUrl;        // OTA Checkin URL

// Externe Funktionsdeklarationen
extern void checkForOTAUpdate();

#endif // GLOBALS_H 