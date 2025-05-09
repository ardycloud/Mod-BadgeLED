#ifndef DISPLAY_H
#define DISPLAY_H

#define ENABLE_GxEPD2_GFX 1
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
#include "Config.h"

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

// Kommandos für die Display-Queue
enum DisplayCommand {
    CMD_UPDATE,
    CMD_INIT
};

// Display-Objekt (2.9" WeAct b/w/r)
using DISPLAY_TYPE = GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT>;
extern DISPLAY_TYPE display;

// Status-Variable für Display-Verfügbarkeit
extern bool displayAvailable;

// Struktur für Display-Inhalte
struct DisplayContent {
    char name[32];
    char description[256];
    char telegram[32];
};

extern DisplayContent displayContent;

// Display Task Handle
extern TaskHandle_t displayTaskHandle;
extern QueueHandle_t displayQueue;

// Funktionsdeklarationen
bool checkDisplayAvailable();
void initDisplay();
void updateDisplay();
bool saveDisplayContent();
bool loadDisplayContent();
void displayTask(void *parameter);
void startDisplayTask();

#endif // DISPLAY_H 