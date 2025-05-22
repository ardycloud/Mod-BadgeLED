#include "Settings.h"

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
    settings.mode = gCurrentMode;
    settings.brightness = BRIGHTNESS;
    settings.noiseLevel = NOISE_LEVEL;
    settings.animationSpeed = animationSpeed;
    settings.micFrequency = micFrequency;
    // apName und apPassword werden direkt in der WebUI gesetzt
    
    Serial.print("Speichere WiFi AP-Name: ");
    Serial.println(settings.apName);
    
    StaticJsonDocument<512> doc;
    doc["mode"] = settings.mode;
    doc["brightness"] = settings.brightness;
    doc["noiseLevel"] = settings.noiseLevel;
    doc["animationSpeed"] = settings.animationSpeed;
    doc["micFrequency"] = settings.micFrequency;
    doc["apName"] = settings.apName;
    doc["apPassword"] = settings.apPassword;

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

    settings.mode = doc["mode"] | 1;
    settings.brightness = doc["brightness"] | 5;
    settings.noiseLevel = doc["noiseLevel"] | 10;
    settings.animationSpeed = doc["animationSpeed"] | 20;
    settings.micFrequency = doc["micFrequency"] | 50;
    settings.apName = doc["apName"] | "LED-Badge";
    settings.apPassword = doc["apPassword"] | "";

    Serial.print("Geladene Einstellungen: WiFi AP-Name=");
    Serial.println(settings.apName);

    // Globale Variablen aktualisieren
    gCurrentMode = settings.mode;
    BRIGHTNESS = settings.brightness;
    NOISE_LEVEL = settings.noiseLevel;
    animationSpeed = settings.animationSpeed;
    micFrequency = settings.micFrequency;

    return true;
} 