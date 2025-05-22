#ifndef DISPLAY_H
#define DISPLAY_H

#define ENABLE_GxEPD2_GFX 1
#include "Config.h"  // Importiere Config.h zuerst für die DisplayCommand-Enum
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <SPIFFS.h>
#include <FS.h>

// Display Pins
#define EINK_SDA  11
#define EINK_SCL  12
#define EINK_BUSY 13
#define EINK_RES  8
#define EINK_DC   9
#define EINK_CS   10

// Display-Einstellungen
#define DISPLAY_WIDTH 296
#define DISPLAY_HEIGHT 128
#define LINE_Y_POS 40
#define DESC_Y_POS 60
#define TELEGRAM_Y_POS 120
#define IMAGE_SIZE 64
#define IMAGE_X_POS (DISPLAY_WIDTH - IMAGE_SIZE - 10)  // 10 Pixel vom rechten Rand
#define IMAGE_Y_POS (DESC_Y_POS + 5)  // 5 Pixel unter der Trennlinie

// Display-Objekt (2.9" WeAct b/w/r)
using DISPLAY_TYPE = GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT>;
extern DISPLAY_TYPE display;

// Status-Variable für Display-Verfügbarkeit
extern bool displayAvailable;

// Flag für vollständiges Display-Initialisierung
extern bool fullDisplayInitialized;

// Flag für optimiertes Schwarz/Weiß partielles Update für SSD1680
extern bool preferBWPartialUpdate;

// Struktur für Display-Inhalte
struct DisplayContent {
    String name;
    String description;
    String telegram;
    String imagePath;    // Pfad zur Bilddatei in SPIFFS
    bool invertColors;   // true = schwarzer Hintergrund, rot/weißer Text
};

extern DisplayContent displayContent;

// Display Task Handles
extern TaskHandle_t displayTaskHandle;
extern QueueHandle_t displayQueue;
extern TaskHandle_t displayUpdateTaskHandle;  // Für nicht-blockierendes Update

// Funktionsdeklarationen
bool checkDisplayAvailable();
void initDisplay();
void updateDisplay();
void updateBatteryStatus(bool forceUpdate = false);  // Partielles Update für Akkustatus
// Neue Funktion: Spezielles optimiertes Update nur mit Schwarz/Weiß-Farben
void partialBWUpdate(int16_t x, int16_t y, int16_t w, int16_t h, bool forceUpdate = false);
// Neue Funktion: Display-Update bei Tastendruck
void updateDisplayOnButtonPress();
// Neue Funktion: Aktualisiert den Akku-Inhalt
void updateAkkuDisplayContent();
void updateDisplayTask(void *parameter);  // Nicht-blockierende Update-Task
void displayTask(void *parameter);
void startDisplayTask();
bool loadDisplayContent();
void saveDisplayContent();
void drawBatteryStatus(float voltage);
bool uploadImage(uint8_t* imageData, size_t length);
void testDisplay();
void clearDisplay();

#endif // DISPLAY_H 