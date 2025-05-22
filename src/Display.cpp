#define CONFIG_INCLUDE_FIRST
#include "Config.h"  // Wichtig: Explizit Config.h importieren für DisplayCommand
#include "Display.h"
#include "esp_task_wdt.h"
#include <Fonts/FreeMonoBold18pt7b.h>  // Größere Schrift für den Namen
#include "BQ25895CONFIG.h"  // Wichtig!
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <time.h>  // Für time()-Funktionen

// Externe Variablen
extern BQ25895* charger;

// Display Konfiguration
#define MAX_DISPLAY_BUFFER_SIZE 800
#define MAX_HEIGHT(EPD) (EPD::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8) ? EPD::HEIGHT : MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8))

// Akku-Symbol Konfiguration
#define BATTERY_X 10
#define BATTERY_Y DISPLAY_HEIGHT - 25
#define BATTERY_WIDTH 40
#define BATTERY_HEIGHT 18
#define BATTERY_KNOB_WIDTH 4
#define BATTERY_KNOB_HEIGHT 8
#define BATTERY_PADDING 2

// Timer für Akku-Update
unsigned long lastBatteryUpdateTime = 0;
const unsigned long BATTERY_UPDATE_INTERVAL = 10000; // 10 Sekunden - nur für Akku-Symbol
float lastBatteryVoltage = 0.0f;

// Display-Objekt (2.9" WeAct b/w/r)
DISPLAY_TYPE display(GxEPD2_290_C90c(/*CS=*/ 10, /*DC=*/ 9, /*RST=*/ 8, /*BUSY=*/ 13));

// Display-Status
bool displayAvailable = false;

// Flag für vollständiges Display-Initialisierung
bool fullDisplayInitialized = true; // Standardmäßig auf true, um erste Initialisierung nicht zu blockieren

// Flag für optimiertes Schwarz/Weiß partielles Update (für SSD1680)
bool preferBWPartialUpdate = true; // Standard: Verwende optimiertes BW-Update

// Display-Inhalte
DisplayContent displayContent = {"ArdyMoon", "", "", "", false};

// Task Handle und Queue
TaskHandle_t displayTaskHandle = NULL;
QueueHandle_t displayQueue = NULL;

// Zusätzliche Task für nicht-blockierendes Display-Update
TaskHandle_t displayUpdateTaskHandle = NULL;

// Zusätzlicher Status für Display-Updates
enum DisplayUpdateStep {
    STEP_BEGIN,
    STEP_SET_WINDOW,
    STEP_FIRST_PAGE,
    STEP_DRAW_CONTENT,
    STEP_NEXT_PAGE,
    STEP_COMPLETE
};

// Globale Variablen für den Update-Zustand
DisplayUpdateStep currentUpdateStep = STEP_BEGIN;
unsigned long lastUpdateTime = 0;
const unsigned long UPDATE_INTERVAL = 50; // 50ms zwischen Update-Schritten

// Flag zum Tracking, ob Display-Update angefordert wurde
bool displayUpdateRequested = false;

// Funktionsdeklarationen 
void updateDisplayTask(void *parameter);
void drawDisplayContent();
void updateDisplay();
void updateBatteryStatus(bool forceUpdate);  // Standardargument entfernt

// Hilfsfunktion zum Zeichnen des Akkusymbols (nicht mehr verwendet)
void drawBatterySymbol(float voltage) {
    // Diese Funktion ist nicht mehr in Verwendung, da das Akku-Symbol vom E-Paper-Display entfernt wurde.
    // Die Akku-Daten werden nur noch in der WebUI angezeigt.
    return;
}

// Hilfs-Funktionen für Display-Update
void drawDisplayContent() {
    // Hintergrund entsprechend der Invertierungs-Einstellung
    uint16_t bgColor = displayContent.invertColors ? GxEPD_BLACK : GxEPD_WHITE;
    uint16_t fgColor = displayContent.invertColors ? GxEPD_WHITE : GxEPD_BLACK;
    uint16_t accentColor = GxEPD_RED;  // Rot bleibt immer Rot
    
    display.fillScreen(bgColor);
    
    // Name in Rot mit größerer Schrift, zentriert oben
    display.setFont(&FreeMonoBold18pt7b);
    display.setTextColor(accentColor);
    int16_t tbx, tby; 
    uint16_t tbw, tbh;
    display.getTextBounds(displayContent.name, 0, 0, &tbx, &tby, &tbw, &tbh);
    uint16_t x = (DISPLAY_WIDTH - tbw) / 2;
    display.setCursor(x, 30);
    display.print(displayContent.name);
    
    // Trennlinie unter dem Namen (volle Breite)
    display.drawLine(0, LINE_Y_POS, DISPLAY_WIDTH, LINE_Y_POS, fgColor);
    
    // Bildanzeige
    bool hasImage = false;
    if (displayContent.imagePath.length() > 0 && SPIFFS.exists(displayContent.imagePath)) {
        File imageFile = SPIFFS.open(displayContent.imagePath, "r");
        if (imageFile) {
            hasImage = true;
            display.drawRect(IMAGE_X_POS, IMAGE_Y_POS, IMAGE_SIZE, IMAGE_SIZE, fgColor);
            display.drawLine(IMAGE_X_POS, IMAGE_Y_POS, 
                         IMAGE_X_POS + IMAGE_SIZE, IMAGE_Y_POS + IMAGE_SIZE, fgColor);
            display.drawLine(IMAGE_X_POS + IMAGE_SIZE, IMAGE_Y_POS, 
                         IMAGE_X_POS, IMAGE_Y_POS + IMAGE_SIZE, fgColor);
            imageFile.close();
        }
    }
    
    // Beschreibung
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(fgColor);
    String desc = displayContent.description;
    int currentY = DESC_Y_POS;
    int lineHeight = 20;
    int maxWidth = hasImage ? (IMAGE_X_POS - 20) : (DISPLAY_WIDTH - 20);
    String currentLine = "";
    
    for (int i = 0; i < desc.length(); i++) {
        currentLine += desc[i];
        display.getTextBounds(currentLine.c_str(), 0, 0, &tbx, &tby, &tbw, &tbh);
        
        if (tbw > maxWidth || desc[i] == '\n') {
            display.setCursor(10, currentY);
            display.print(currentLine);
            currentY += lineHeight;
            currentLine = "";
            
            if (currentY >= TELEGRAM_Y_POS - lineHeight) {
                break;
            }
        }
    }
    
    if (currentLine.length() > 0 && currentY < TELEGRAM_Y_POS - lineHeight) {
        display.setCursor(10, currentY);
        display.print(currentLine);
    }
    
    // Telegram Handle rechts unten
    if (displayContent.telegram.length() > 0) {
        String telegramHandle = "@" + displayContent.telegram;
        display.getTextBounds(telegramHandle.c_str(), 0, 0, &tbx, &tby, &tbw, &tbh);
        x = DISPLAY_WIDTH - tbw - 10;  // 10 Pixel Abstand vom rechten Rand
        display.setCursor(x, TELEGRAM_Y_POS);
        display.print(telegramHandle);
    }
    
    // Aktualisierungszeit anzeigen (basierend auf millis)
    unsigned long uptime = millis() / 1000; // Sekunden seit Start
    int hours = uptime / 3600;
    int mins = (uptime % 3600) / 60;
    int secs = uptime % 60;
    char timeStr[12];
    sprintf(timeStr, "%02d:%02d:%02d", hours, mins, secs);
    display.setCursor(10, TELEGRAM_Y_POS);
    display.print(timeStr);
    
    // Entferne Akku-Symbol (wird nicht mehr angezeigt)
}

// Task-Funktion für nicht-blockierendes Display-Update
void updateDisplayTask(void *parameter) {
    Serial.println("Display-Update-Task gestartet");
    
    // Einfache Zustände ohne switch/case
    int state = 0;
    unsigned long lastTime = millis();
    const unsigned long DELAY = 50; // 50ms zwischen Schritten
    bool isComplete = false;
    
    // Event-Loop
    while (!isComplete) {
        // Pause zwischen den Schritten
        if (millis() - lastTime < DELAY) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        lastTime = millis();
        esp_task_wdt_reset(); // Watchdog zurücksetzen
        
        // Zustandsmaschine mit if/else
        if (state == 0) {
            // Initialisierung
            Serial.println("Display-Update: Starte");
            state = 1;
        }
        else if (state == 1) {
            // Fenster setzen
            Serial.println("Display-Update: Setze Fenster");
            display.setFullWindow();
            state = 2;
        }
        else if (state == 2) {
            // Erste Seite beginnen
            Serial.println("Display-Update: Erste Seite");
            display.firstPage();
            state = 3;
        }
        else if (state == 3) {
            // Inhalt zeichnen
            Serial.println("Display-Update: Zeichne Inhalt");
            drawDisplayContent();
            state = 4;
        }
        else if (state == 4) {
            // Nächste Seite
            Serial.println("Display-Update: Nächste Seite");
            if (display.nextPage() == false) {
                // Fertig - keine weiteren Seiten
                Serial.println("Display-Update: Keine weiteren Seiten, Update abgeschlossen");
                isComplete = true;
            } else {
                // Es gibt noch mehr Seiten, zurück zum Inhalt zeichnen
                Serial.println("Display-Update: Weitere Seite vorhanden, zeichne Inhalt");
                state = 3;
            }
        }
        else {
            // Unbekannter Zustand
            Serial.println("Display-Update: Unbekannter Zustand, breche ab");
            isComplete = true;
        }
        
        // Kurze Pause für andere Tasks
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    Serial.println("Display-Update-Task beendet");
    
    // Task beenden
    displayUpdateTaskHandle = NULL;
    vTaskDelete(NULL);
}

// Diese Funktion lädt die Display-Inhalte aus SPIFFS
bool loadDisplayContent() {
    if (!SPIFFS.exists("/display.json")) {
        saveDisplayContent(); // Speichere Standardwerte
        return true;
    }

    File file = SPIFFS.open("/display.json", "r");
    if (!file) {
        Serial.println("Failed to open display.json for reading");
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.println("Failed to parse display.json");
        return false;
    }

    // Direkte Zuweisung für String-Typen
    displayContent.name = doc["name"] | "ArdyMoon";
    displayContent.description = doc["description"] | "";
    displayContent.telegram = doc["telegram"] | "";
    displayContent.imagePath = doc["imagePath"] | "";
    displayContent.invertColors = doc["invertColors"] | false;

    return true;
}

// Diese Funktion speichert die Display-Inhalte in SPIFFS
void saveDisplayContent() {
    Serial.println("saveDisplayContent() aufgerufen");
    
    StaticJsonDocument<512> doc;
    doc["name"] = displayContent.name;
    doc["description"] = displayContent.description;
    doc["telegram"] = displayContent.telegram;
    doc["imagePath"] = displayContent.imagePath;
    doc["invertColors"] = displayContent.invertColors;
    
    Serial.println("Speichere Display-Inhalte in SPIFFS: Name=" + displayContent.name + 
                  ", Beschreibung=" + displayContent.description + 
                  ", Telegram=" + displayContent.telegram);
    
    File file = SPIFFS.open("/display.json", "w");
    if (!file) {
        Serial.println("ERROR: Failed to open display.json for writing");
        return;
    }
    
    size_t bytesWritten = serializeJson(doc, file);
    if (bytesWritten == 0) {
        Serial.println("ERROR: Failed to write to display.json");
    } else {
        Serial.println("Display-Inhalte erfolgreich in SPIFFS gespeichert (" + String(bytesWritten) + " Bytes)");
    }
    
    file.close();
    
    // Display aktualisieren
    if (displayQueue != NULL) {
        Serial.println("Sende CMD_UPDATE Kommando an Display-Queue");
        DisplayCommand cmd = CMD_UPDATE;
        if (xQueueSend(displayQueue, &cmd, pdMS_TO_TICKS(100)) == pdPASS) {
            Serial.println("CMD_UPDATE Kommando erfolgreich gesendet!");
        } else {
            Serial.println("ERROR: CMD_UPDATE Kommando konnte nicht gesendet werden!");
        }
    } else {
        Serial.println("ERROR: displayQueue ist NULL - kann kein Update senden!");
    }
}

void displayTask(void *parameter) {
    Serial.println("displayTask gestartet");
    
    // Display initialisieren
    display.init(115200);
    display.setRotation(1);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    
    // Display-Inhalte laden
    Serial.println("Lade Display-Inhalte aus SPIFFS...");
    if (loadDisplayContent()) {
        Serial.println("Display-Inhalte erfolgreich geladen");
    } else {
        Serial.println("Fehler beim Laden der Display-Inhalte, verwende Standardwerte");
    }
    
    // Kommando-Queue für Display-Updates
    DisplayCommand cmd;
    Serial.println("Display-Task wartet auf Kommandos...");
    
    while(true) {
        // Auf Kommandos warten, nicht automatisch den Akku-Status updaten
        if(xQueueReceive(displayQueue, &cmd, pdMS_TO_TICKS(1000))) {
            Serial.print("Display-Task hat Kommando empfangen: ");
            
            switch(cmd) {
                case CMD_INIT:
                    Serial.println("CMD_INIT (Display-Test)");
                    // Display-Test durchführen
                    display.setFullWindow();
                    display.firstPage();
                    do {
                        display.fillScreen(GxEPD_WHITE);
                        display.setCursor(10, 30);
                        display.print("Display Test");
                    } while (display.nextPage());
                    fullDisplayInitialized = true;
                    break;
                    
                case CMD_UPDATE:
                    Serial.println("CMD_UPDATE (Display aktualisieren)");
                    // Direktes vollständiges Update
                    updateDisplay();
                    break;
                    
                case CMD_CLEAR:
                    Serial.println("CMD_CLEAR (Display leeren)");
                    // Display leeren
                    display.setFullWindow();
                    display.firstPage();
                    do {
                        display.fillScreen(displayContent.invertColors ? GxEPD_BLACK : GxEPD_WHITE);
                    } while (display.nextPage());
                    fullDisplayInitialized = false; // Nach dem Löschen muss neu initialisiert werden
                    break;
                    
                default:
                    Serial.println("Unbekanntes Kommando: " + String(cmd));
                    break;
            }
        }
        
        // Kurze Pause
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void startDisplayTask() {
    // Wenn Task oder Queue bereits existieren, erst löschen
    if (displayTaskHandle != NULL) {
        vTaskDelete(displayTaskHandle);
        displayTaskHandle = NULL;
    }
    if (displayQueue != NULL) {
        vQueueDelete(displayQueue);
        displayQueue = NULL;
    }
    
    // Display-Queue erstellen
    displayQueue = xQueueCreate(3, sizeof(DisplayCommand));
    if (displayQueue == NULL) {
        Serial.println("Fehler beim Erstellen der Display-Queue");
        return;
    }
    
    // Display-Task auf Core 0 starten
    BaseType_t result = xTaskCreatePinnedToCore(
        displayTask,
        "Display_Task",
        8192,           // Stack-Größe
        NULL,
        2,              // Höhere Priorität für bessere Reaktionszeit
        &displayTaskHandle,
        0               // Core 0
    );
    
    if (result != pdPASS) {
        Serial.println("Fehler beim Erstellen des Display-Tasks");
        vQueueDelete(displayQueue);
        displayQueue = NULL;
    }
}

// Aktualisierte Initialisierungsfunktion für WeAct 2.9" SSD1680 Display
bool checkDisplayAvailable() {
    displayAvailable = false;
    
    try {
        // Verbesserte Initialisierung mit optimierten Parametern für WeAct Display mit SSD1680
        display.init(115200, true, 50, false);
        display.setRotation(1); // Querformat
        display.setTextColor(GxEPD_BLACK);
        
        // SSD1680 Controller Konfiguration für partielles Update - wichtig!
        if (display.epd2.hasFastPartialUpdate) {
            Serial.println("Fast Partial Update unterstützt! Aktiviere diese Option.");
            // Der SSD1680 unterstützt partielles Update, aber nur für Schwarz/Weiß
            
            // Kleine Verzögerung für bessere Stabilität
            delay(100);
        } else {
            Serial.println("Fast Partial Update wird nicht unterstützt oder ist deaktiviert.");
        }
        
        displayAvailable = true;
        Serial.println("WeAct E-Ink Display gefunden!");
        
        // Display-Task nur starten, wenn noch nicht gestartet
        if (displayTaskHandle == NULL) {
            startDisplayTask();
        }
        
    } catch (...) {
        Serial.println("Fehler bei der Display-Initialisierung");
        displayAvailable = false;
    }
    
    return displayAvailable;
}

void initDisplay() {
    if (!displayAvailable) return;

    try {
        display.setRotation(1);
        display.setTextColor(GxEPD_BLACK);
        display.setFont(&FreeMonoBold9pt7b);
        
        display.setFullWindow();
        display.firstPage();
        do {
            display.fillScreen(GxEPD_WHITE);
            display.setCursor(10, 30);
            display.print("ArdyMoon");
            display.setCursor(10, 60);
            display.print("Initialisierung...");            
        } while (display.nextPage());
        
        // Kurze Pause für die Anzeige
        delay(2000);
        
    } catch (...) {
        Serial.println("Fehler bei der Display-Konfiguration");
        displayAvailable = false;
    }
}

void updateDisplay() {
    Serial.println("updateDisplay() Funktion aufgerufen");
    
    if (!displayAvailable) {
        Serial.println("ERROR: Display nicht verfügbar - kann nicht aktualisieren");
        return;
    }

    Serial.println("Display ist verfügbar, starte Update-Prozess...");
    Serial.println("Display-Inhalte: Name=" + displayContent.name + 
                  ", Beschreibung=" + displayContent.description + 
                  ", Telegram=" + displayContent.telegram +
                  ", Invertiert=" + String(displayContent.invertColors));
    
    // WICHTIG: Wir führen ein vollständiges Update durch - direkter Ansatz
    display.setFullWindow();
    display.firstPage();
    do {
        drawDisplayContent();
    } while (display.nextPage());
    
    // Display wurde vollständig aktualisiert
    fullDisplayInitialized = true;
    Serial.println("✅ Vollständiges Display-Update abgeschlossen");
}

void testDisplay() {
    Serial.println("Display-Test angefordert...");
    
    if (!displayAvailable) {
        Serial.println("FEHLER: Display nicht verfügbar für Test!");
        return;
    }
    
    if (displayQueue != NULL) {
        Serial.println("Sende CMD_INIT Kommando an Display-Queue...");
        DisplayCommand cmd = CMD_INIT;
        if (xQueueSend(displayQueue, &cmd, pdMS_TO_TICKS(500)) == pdPASS) {
            Serial.println("CMD_INIT Kommando erfolgreich gesendet");
        } else {
            Serial.println("FEHLER: CMD_INIT Kommando konnte nicht gesendet werden - Queue voll?");
            // Fallback: Direktes Zeichnen versuchen
            Serial.println("Versuche direktes Zeichnen als Fallback...");
            display.setFullWindow();
            display.firstPage();
            do {
                display.fillScreen(GxEPD_WHITE);
                display.setCursor(10, 30);
                display.print("Display Test");
            } while (display.nextPage());
        }
    } else {
        Serial.println("FEHLER: Display-Queue nicht initialisiert");
    }
}

void clearDisplay() {
    Serial.println("Display-Löschen angefordert...");
    
    if (!displayAvailable) {
        Serial.println("FEHLER: Display nicht verfügbar für Löschen!");
        return;
    }
    
    if (displayQueue != NULL) {
        Serial.println("Sende CMD_CLEAR Kommando an Display-Queue...");
        DisplayCommand cmd = CMD_CLEAR;
        if (xQueueSend(displayQueue, &cmd, pdMS_TO_TICKS(500)) == pdPASS) {
            Serial.println("CMD_CLEAR Kommando erfolgreich gesendet");
        } else {
            Serial.println("FEHLER: CMD_CLEAR Kommando konnte nicht gesendet werden - Queue voll?");
            // Fallback: Direktes Löschen versuchen
            Serial.println("Versuche direktes Löschen als Fallback...");
            display.setFullWindow();
            display.firstPage();
            do {
                display.fillScreen(displayContent.invertColors ? GxEPD_BLACK : GxEPD_WHITE);
            } while (display.nextPage());
        }
    } else {
        Serial.println("FEHLER: Display-Queue nicht initialisiert");
    }
}

// Funktion zum Aktualisieren des Displays bei Tastendruck
void updateDisplayOnButtonPress() {
    if (!displayAvailable) {
        Serial.println("❌ Display nicht verfügbar für Update durch Tastendruck");
        return;
    }
    
    Serial.println("🔄 Display-Update durch Tastendruck angefordert");
    
    // Vollständiges Display-Update durchführen
    display.setFullWindow();
    display.firstPage();
    do {
        // Hintergrund
        uint16_t bgColor = displayContent.invertColors ? GxEPD_BLACK : GxEPD_WHITE;
        uint16_t fgColor = displayContent.invertColors ? GxEPD_WHITE : GxEPD_BLACK;
        uint16_t accentColor = GxEPD_RED;  // Rot bleibt immer Rot
        
        display.fillScreen(bgColor);
        
        // Name in Rot mit größerer Schrift, zentriert oben
        display.setFont(&FreeMonoBold18pt7b);
        display.setTextColor(accentColor);
        int16_t tbx, tby; 
        uint16_t tbw, tbh;
        display.getTextBounds(displayContent.name, 0, 0, &tbx, &tby, &tbw, &tbh);
        uint16_t x = (DISPLAY_WIDTH - tbw) / 2;
        display.setCursor(x, 30);
        display.print(displayContent.name);
        
        // Trennlinie unter dem Namen (volle Breite)
        display.drawLine(0, LINE_Y_POS, DISPLAY_WIDTH, LINE_Y_POS, fgColor);
        
        // Bildanzeige
        bool hasImage = false;
        if (displayContent.imagePath.length() > 0 && SPIFFS.exists(displayContent.imagePath)) {
            File imageFile = SPIFFS.open(displayContent.imagePath, "r");
            if (imageFile) {
                hasImage = true;
                display.drawRect(IMAGE_X_POS, IMAGE_Y_POS, IMAGE_SIZE, IMAGE_SIZE, fgColor);
                display.drawLine(IMAGE_X_POS, IMAGE_Y_POS, 
                             IMAGE_X_POS + IMAGE_SIZE, IMAGE_Y_POS + IMAGE_SIZE, fgColor);
                display.drawLine(IMAGE_X_POS + IMAGE_SIZE, IMAGE_Y_POS, 
                             IMAGE_X_POS, IMAGE_Y_POS + IMAGE_SIZE, fgColor);
                imageFile.close();
            }
        }
        
        // Beschreibung
        display.setFont(&FreeSans9pt7b);
        display.setTextColor(fgColor);
        String desc = displayContent.description;
        int currentY = DESC_Y_POS;
        int lineHeight = 20;
        int maxWidth = hasImage ? (IMAGE_X_POS - 20) : (DISPLAY_WIDTH - 20);
        String currentLine = "";
        
        for (int i = 0; i < desc.length(); i++) {
            currentLine += desc[i];
            display.getTextBounds(currentLine.c_str(), 0, 0, &tbx, &tby, &tbw, &tbh);
            
            if (tbw > maxWidth || desc[i] == '\n') {
                display.setCursor(10, currentY);
                display.print(currentLine);
                currentY += lineHeight;
                currentLine = "";
                
                if (currentY >= TELEGRAM_Y_POS - lineHeight) {
                    break;
                }
            }
        }
        
        if (currentLine.length() > 0 && currentY < TELEGRAM_Y_POS - lineHeight) {
            display.setCursor(10, currentY);
            display.print(currentLine);
        }
        
        // Telegram Handle rechts unten
        if (displayContent.telegram.length() > 0) {
            String telegramHandle = "@" + displayContent.telegram;
            display.getTextBounds(telegramHandle.c_str(), 0, 0, &tbx, &tby, &tbw, &tbh);
            x = DISPLAY_WIDTH - tbw - 10;  // 10 Pixel Abstand vom rechten Rand
            display.setCursor(x, TELEGRAM_Y_POS);
            display.print(telegramHandle);
        }
        
        // Aktualisierungszeit anzeigen (basierend auf millis)
        unsigned long uptime = millis() / 1000; // Sekunden seit Start
        int hours = uptime / 3600;
        int mins = (uptime % 3600) / 60;
        int secs = uptime % 60;
        char timeStr[12];
        sprintf(timeStr, "%02d:%02d:%02d", hours, mins, secs);
        display.setCursor(10, TELEGRAM_Y_POS);
        display.print(timeStr);
        
    } while (display.nextPage());
    
    Serial.println("✅ Display-Update durch Tastendruck abgeschlossen");
    
    // Aktualisiere den Zeitpunkt des letzten Updates
    lastBatteryUpdateTime = millis();
}

// Modifizierte Funktion updateBatteryStatus
void updateBatteryStatus(bool forceUpdate) {
    if (!displayAvailable) return;
    
    // Wichtig: Wenn das Display noch nicht vollständig initialisiert wurde, müssen wir zuerst ein vollständiges Update machen
    if (!fullDisplayInitialized) {
        Serial.println("⚠️ Erster Aufruf - volles Display-Update erforderlich");
        // Beim ersten Mal vollständiges Update durchführen
        updateDisplay();
        return; // Nach dem vollständigen Update beenden
    }
    
    // Wenn forceUpdate oder displayUpdateRequested, dann aktualisieren
    if (forceUpdate || displayUpdateRequested) {
        updateAkkuDisplayContent();
    }
}

// Neue Funktion für das Zeichnen des Akku-Inhalts
void updateAkkuDisplayContent() {
    // Da wir das Akku-Symbol nicht mehr anzeigen, führen wir einfach ein normales Display-Update durch
    Serial.println("🔄 Akku-Update nicht mehr nötig, führe normales Display-Update durch");
    
    // Vollständiges Display-Update durchführen
    display.setFullWindow();
    display.firstPage();
    do {
        // Hintergrund
        uint16_t bgColor = displayContent.invertColors ? GxEPD_BLACK : GxEPD_WHITE;
        display.fillScreen(bgColor);
        
        // Zeichne den gesamten Display-Inhalt (ohne Akku-Symbol)
        drawDisplayContent();
    } while (display.nextPage());
    
    // Timer zurücksetzen
    lastBatteryUpdateTime = millis();
    Serial.println("✅ Display vollständig aktualisiert");
}

// Neue Funktion: Spezielles partielles Update nur mit Schwarz/Weiß-Farben
// Optimiert für SSD1680-Controller, die nur Schwarz/Weiß partiell aktualisieren können
void partialBWUpdate(int16_t x, int16_t y, int16_t w, int16_t h, bool forceUpdate) {
    if (!displayAvailable) {
        Serial.println("❌ Display nicht verfügbar für partielles BW-Update");
        return;
    }
    
    if (!fullDisplayInitialized) {
        Serial.println("⚠️ Vollständiges Display-Update erforderlich vor partiellem BW-Update");
        updateDisplay();
        return;
    }
    
    Serial.printf("🔄 Partielles BW-Update für Bereich: x=%d, y=%d, w=%d, h=%d\n", x, y, w, h);
    
    // Setze das Fenster für partielles Update
    display.setPartialWindow(x, y, w, h);
    
    // Beginne die Seiten-basierte Aktualisierung
    display.firstPage();
    
    // Hier würde normalerweise der Inhalt gezeichnet werden
    // Dies ist aber Aufgabe des Aufrufers, der die Zeichenfunktion
    // zwischen firstPage() und nextPage() ausführen muss
    
    // Diese Funktion wartet normalerweise auf den nächsten Seitenaufruf,
    // der in der aufrufenden Funktion als do-while Schleife implementiert ist:
    // do { /* Zeichenoperationen */ } while (display.nextPage());
    
    // Hinweis: Für optimiertes BW-Update mit dem SSD1680-Controller
    // wird empfohlen, nur GxEPD_BLACK und GxEPD_WHITE zu verwenden,
    // nicht GxEPD_RED, da dies zu Anzeigefehlern führen kann.
    
    Serial.println("➡️ Partielles BW-Update gestartet");
}
