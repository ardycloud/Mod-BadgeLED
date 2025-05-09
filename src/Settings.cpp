#include "Settings.h"

// Globale Einstellungsvariable
Settings settings;

// Definition des Dateinamens für die Einstellungen
const char* settingsFile = "/settings.json";

// Implementierung der Funktionen
bool saveSettings() {
    // Aktuelle Werte in die Einstellungsstruktur übernehmen
    settings.currentMode = gCurrentMode;
    settings.brightness = BRIGHTNESS;
    settings.noiseLevel = NOISE_LEVEL;
    settings.animationSpeed = animationSpeed;
    settings.micFrequency = micFrequency;

    StaticJsonDocument<512> doc;
    
    doc["mode"] = settings.currentMode;
    doc["brightness"] = settings.brightness;
    doc["sensitivity"] = settings.noiseLevel;
    doc["speed"] = settings.animationSpeed;
    doc["frequency"] = settings.micFrequency;

    File file = SPIFFS.open(settingsFile, "w");
    if (!file) {
        Serial.println("Fehler beim Öffnen der Einstellungsdatei zum Schreiben!");
        return false;
    }

    if (serializeJson(doc, file) == 0) {
        Serial.println("Fehler beim Schreiben der Einstellungen!");
        file.close();
        return false;
    }

    file.close();
    return true;
}

bool loadSettings() {
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS konnte nicht initialisiert werden!");
        return false;
    }

    if (!SPIFFS.exists(settingsFile)) {
        // Standardwerte setzen wenn keine Datei existiert
        settings.currentMode = 0;
        settings.brightness = 5;
        settings.noiseLevel = 10;
        settings.animationSpeed = 20;
        settings.micFrequency = 50;
        return saveSettings(); // Standardwerte speichern
    }

    File file = SPIFFS.open(settingsFile, "r");
    if (!file) {
        Serial.println("Einstellungsdatei konnte nicht geöffnet werden!");
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.println("Fehler beim Parsen der JSON Datei!");
        return false;
    }

    // Einstellungen laden
    settings.currentMode = doc["mode"] | 0;
    settings.brightness = doc["brightness"] | 5;
    settings.noiseLevel = doc["sensitivity"] | 10;
    settings.animationSpeed = doc["speed"] | 20;
    settings.micFrequency = doc["frequency"] | 50;

    // Globale Variablen aktualisieren
    gCurrentMode = settings.currentMode;
    BRIGHTNESS = settings.brightness;
    NOISE_LEVEL = settings.noiseLevel;
    animationSpeed = settings.animationSpeed;
    micFrequency = settings.micFrequency;

    return true;
} 