#include "Globals.h"

// Globale LED-Variablen (volatile für Thread-Sicherheit)
volatile uint8_t currentMode = 0;        // Aktueller LED-Modus (0-24)
volatile uint8_t primaryHue = 0;         // Primärfarbe (0-255)
volatile uint8_t secondaryHue = 128;     // Sekundärfarbe (0-255)
volatile uint8_t brightness = 12;        // Helligkeit (0-255)
volatile uint16_t animSpeed = 50;        // Animationsgeschwindigkeit

// WiFi und OTA Variablen
bool wifiConnectEnabled = true;          // WiFi-Verbindung aktiviert (auf true gesetzt)
String wifiSSID = "OTA-Network";                    // WiFi Netzwerkname (wird über WebUI konfiguriert)
String wifiPassword = "init99init99";                // WiFi Passwort (wird über WebUI konfiguriert)
String deviceID = "";                    // Geräte-ID für OTA (wird mit MAC-Adresse gefüllt)

// =============================================================================
// FIRMWARE VERSION - HIER NEUE VERSION EINTRAGEN!
// =============================================================================
const char* firmwareVersion = "1.4.9";   // ← HIER NEUE VERSION ÄNDERN
// =============================================================================

const char* hardwaretype = "LED-Modd.Badge";   // Geräte Type
const char* otaCheckinUrl = "http://ota.ardyconnect.com/api/device_checkin"; // OTA Checkin URL (HTTP) 