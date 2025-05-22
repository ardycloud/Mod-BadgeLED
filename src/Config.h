#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <cstdint>
#include <FastLED.h>
#include "Settings.h"

// Button Pins
#define MODE_BUTTON     7
#define COLOR_BUTTON    6
#define BRIGHT_BUTTON   5
#define SAO_BUTTON      4

// LED Konfiguration
#define NUM_LEDS 75
#define NUM_STATUS_LEDS 5

// Display Konfiguration
const bool USE_DISPLAY = true;

// Globale Variablen
extern uint8_t gCurrentMode;
extern uint8_t BRIGHTNESS;
extern uint8_t NOISE_LEVEL;
extern uint16_t animationSpeed;
extern uint16_t micFrequency;

// Display Kommandos
enum DisplayCommand {
    CMD_INIT,
    CMD_UPDATE,
    CMD_CLEAR,
    CMD_UPLOAD_IMAGE   // Neues Kommando für Bildupload
};

// Funktionen
void checkButtons();
void testDisplay();
void clearDisplay();

#endif // CONFIG_H 