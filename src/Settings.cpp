#include "Settings.h"
#include "Globals.h"  // Für die neuen globalen Variablen

// Externe alte Variablen
extern uint8_t NOISE_LEVEL;
extern uint16_t animationSpeed;
extern uint16_t micFrequency;

// Globale Einstellungen
Settings settings;

// Definition des Dateinamens für die Einstellungen
const char* settingsFile = "/settings.json";

// Implementierung der Funktionen
bool saveSettings() {
    File file = SPIFFS.open("/settings.json", "w");
    if (!file) {
        Serial.println("Fehler beim Öffnen der Einstellungsdatei zum Schreiben");
        return false;
    }

    Serial.println("Speichere Einstellungen: Mode=" + String(settings.mode) + 
                  ", Brightness=" + String(settings.brightness) + 
                  ", NoiseLevel=" + String(settings.noiseLevel));

    // Aktualisiere die Settings-Struktur mit den aktuellen globalen Werten
    settings.mode = currentMode;
    settings.brightness = brightness;
    settings.noiseLevel = NOISE_LEVEL;
    settings.animationSpeed = animSpeed;
    settings.micFrequency = micFrequency;
    settings.primaryHue = primaryHue;      // Farbwerte hinzufügen
    settings.secondaryHue = secondaryHue;  // Farbwerte hinzufügen
    // WiFi-Einstellungen aus globalen Variablen
    settings.wifiEnabled = wifiConnectEnabled;
    settings.wifiSSID = wifiSSID;
    settings.wifiPassword = wifiPassword;
    // apName und apPassword werden direkt in der WebUI gesetzt
    
    Serial.print("Speichere WiFi AP-Name: ");
    Serial.println(settings.apName);
    
    StaticJsonDocument<512> doc;
    doc["mode"] = settings.mode;
    doc["brightness"] = settings.brightness;
    doc["noiseLevel"] = settings.noiseLevel;
    doc["animationSpeed"] = settings.animationSpeed;
    doc["micFrequency"] = settings.micFrequency;
    doc["primaryHue"] = settings.primaryHue;        // Farbwerte speichern
    doc["secondaryHue"] = settings.secondaryHue;    // Farbwerte speichern
    doc["apName"] = settings.apName;
    doc["apPassword"] = settings.apPassword;
    // WiFi-Einstellungen speichern
    doc["wifiEnabled"] = settings.wifiEnabled;
    doc["wifiSSID"] = settings.wifiSSID;
    doc["wifiPassword"] = settings.wifiPassword;
    // OTA-Einstellungen speichern
    doc["otaEnabled"] = settings.otaEnabled;
    doc["dynamicVersion"] = settings.dynamicVersion;

    if (serializeJson(doc, file) == 0) {
        Serial.println("Fehler beim Schreiben der Einstellungen");
        file.close();
        return false;
    }

    file.close();
    Serial.println("Einstellungen erfolgreich gespeichert");
    return true;
}

bool loadSettings() {
    if (!SPIFFS.exists("/settings.json")) {
        Serial.println("Keine Einstellungsdatei gefunden, verwende Standardwerte");
        return false;
    }

    File file = SPIFFS.open("/settings.json", "r");
    if (!file) {
        Serial.println("Fehler beim Öffnen der Einstellungsdatei zum Lesen");
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.println("Fehler beim Parsen der Einstellungen");
        return false;
    }

    settings.mode = doc["mode"] | 0;  // Standard: Mode 0 (SolidColor)
    settings.brightness = doc["brightness"] | 12;  // Standard: 12 wie gewünscht
    settings.noiseLevel = doc["noiseLevel"] | 10;
    settings.animationSpeed = doc["animationSpeed"] | 50;  // Standard: 50 statt 20
    settings.micFrequency = doc["micFrequency"] | 50;
    settings.primaryHue = doc["primaryHue"] | 0;        // Standard: Rot
    settings.secondaryHue = doc["secondaryHue"] | 128;  // Standard: Cyan
    settings.apName = doc["apName"] | "LED-Badge";
    settings.apPassword = doc["apPassword"] | "";
    // WiFi-Einstellungen laden
    settings.wifiEnabled = doc["wifiEnabled"] | true;
    settings.wifiSSID = doc["wifiSSID"] | "";
    settings.wifiPassword = doc["wifiPassword"] | "";
    // OTA-Einstellungen laden
    settings.otaEnabled = doc["otaEnabled"] | true;
    settings.dynamicVersion = doc["dynamicVersion"] | "";
    
    Serial.printf("Geladene OTA-Einstellungen: aktiviert=%s, dynamicVersion='%s'\n", 
                  settings.otaEnabled ? "Ja" : "Nein", settings.dynamicVersion.c_str());

    Serial.print("Geladene Einstellungen: WiFi AP-Name=");
    Serial.println(settings.apName);

    // Globale Variablen aktualisieren
    currentMode = settings.mode;
    brightness = settings.brightness;
    NOISE_LEVEL = settings.noiseLevel;
    animSpeed = settings.animationSpeed;
    micFrequency = settings.micFrequency;
    primaryHue = settings.primaryHue;      // Farbwerte laden
    secondaryHue = settings.secondaryHue;  // Farbwerte laden
    // WiFi-Einstellungen in globale Variablen laden
    wifiConnectEnabled = settings.wifiEnabled;
    wifiSSID = settings.wifiSSID;
    wifiPassword = settings.wifiPassword;

    return true;
} 