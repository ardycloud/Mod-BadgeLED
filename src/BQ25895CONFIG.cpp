#include "BQ25895CONFIG.h"
#include <Arduino.h>

// Globale Instanz mit nullptr-Check
BQ25895* charger = nullptr;

// Globale Variablen
static bool chargerInitialized = false;

void initializeBQ25895() {
    // Sicherheitsmaßnahme - nur einmal initialisieren
    if (chargerInitialized) return;
    
    // Versuche, den Charger zu instanziieren
    try {
        charger = new BQ25895();
        chargerInitialized = true;
    } catch (...) {
        Serial.println("Fehler beim Instanziieren von BQ25895");
    }
}

// Diese Funktion wird in der main.cpp aufgerufen, bevor charger verwendet wird
void ensureChargerInitialized() {
    if (!chargerInitialized) {
        initializeBQ25895();
    }
} 