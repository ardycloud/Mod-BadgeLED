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
#include <U8g2_for_Adafruit_GFX.h>  // Für Unicode-Support

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

// U8g2-Instanz für Unicode-Support
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

// Display-Status
bool displayAvailable = false;

// Flag für vollständiges Display-Initialisierung
bool fullDisplayInitialized = true; // Standardmäßig auf true, um erste Initialisierung nicht zu blockieren

// Flag für optimiertes Schwarz/Weiß partielles Update (für SSD1680)
bool preferBWPartialUpdate = true; // Standard: Verwende optimiertes BW-Update

// Display-Inhalte
DisplayContent displayContent = {"ArdyMoon", "", "", "", false, true};

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
    
    // U8g2-Fonts für Unicode-Support initialisieren
    u8g2Fonts.begin(display);
    
    // Name mit wählbarer Farbe und Unicode-Support, zentriert oben
    u8g2Fonts.setFontMode(1);  // Transparenter Hintergrund (1 = transparent, 0 = solid)
    u8g2Fonts.setFont(u8g2_font_ncenB24_tr);  // Größerer verfügbarer Font (24px, fett)
    
    // Name-Farbe basierend auf Einstellung wählen
    uint16_t nameColor;
    if (displayContent.nameColorRed) {
        nameColor = GxEPD_RED;  // Rot
    } else {
        nameColor = fgColor;    // Schwarz oder Weiß je nach Hintergrund
    }
    u8g2Fonts.setForegroundColor(nameColor);
    u8g2Fonts.setBackgroundColor(bgColor);  // Explizit Hintergrundfarbe setzen
    
    // Text-Breite für Zentrierung berechnen
    int16_t nameWidth = u8g2Fonts.getUTF8Width(displayContent.name.c_str());
    uint16_t x = (DISPLAY_WIDTH - nameWidth) / 2;
    u8g2Fonts.setCursor(x, 30);  // Höher positioniert (war 40)
    u8g2Fonts.print(displayContent.name);
    
    // Trennlinie unter dem Namen (volle Breite)
    display.drawLine(0, LINE_Y_POS, DISPLAY_WIDTH, LINE_Y_POS, fgColor);
    
    // Bildanzeige
    bool hasImage = false;
    if (displayContent.imagePath.length() > 0 && SPIFFS.exists(displayContent.imagePath)) {
        hasImage = true;
        
        // Versuche das echte Bild zu laden und anzuzeigen
        Serial.printf("🖼️ Lade Bild: %s\n", displayContent.imagePath.c_str());
        Serial.printf("📍 Bildposition: x=%d, y=%d, Größe=%dx%d\n", IMAGE_X_POS, IMAGE_Y_POS, IMAGE_SIZE, IMAGE_SIZE);
        
        // Rahmen um das Bild zeichnen
        display.drawRect(IMAGE_X_POS - 1, IMAGE_Y_POS - 1, IMAGE_SIZE + 2, IMAGE_SIZE + 2, fgColor);
        
        // Versuche echte Bitmap-Dekodierung
        bool imageLoaded = drawBitmapFromFile(displayContent.imagePath, IMAGE_X_POS, IMAGE_Y_POS, fgColor, bgColor);
        
        if (!imageLoaded) {
            // Fallback: Platzhalter-Symbol wenn Dekodierung fehlschlägt
            Serial.println("❌ Bitmap-Dekodierung fehlgeschlagen, zeige Fallback-Symbol");
            display.fillRect(IMAGE_X_POS, IMAGE_Y_POS, IMAGE_SIZE, IMAGE_SIZE, bgColor);
            
            // Einfaches Kamera-Symbol als Fallback
            int centerX = IMAGE_X_POS + IMAGE_SIZE / 2;
            int centerY = IMAGE_Y_POS + IMAGE_SIZE / 2;
            
            display.fillRect(centerX - 12, centerY - 8, 24, 16, fgColor);
            display.fillRect(centerX - 10, centerY - 6, 20, 12, bgColor);
            display.fillCircle(centerX, centerY, 6, fgColor);
            display.fillCircle(centerX, centerY, 4, bgColor);
            display.fillRect(centerX - 16, centerY - 10, 6, 4, fgColor);
            
            Serial.println("📷 Fallback-Kamera-Symbol angezeigt");
        } else {
            Serial.println("✅ Bild erfolgreich geladen und angezeigt");
        }
    } else {
        Serial.printf("❌ Kein Bild gefunden: imagePath='%s', exists=%s\n", 
                     displayContent.imagePath.c_str(), 
                     SPIFFS.exists(displayContent.imagePath) ? "ja" : "nein");
    }
    
    // Beschreibung mit Unicode-Support - KORRIGIERT: Ermögliche 3 Zeilen
    u8g2Fonts.setFont(u8g2_font_ncenB14_tr);  // Kleinerer Font für Beschreibung (14px, fett)
    u8g2Fonts.setFontMode(1);  // Transparenter Hintergrund (1 = transparent, 0 = solid)
    u8g2Fonts.setForegroundColor(fgColor);
    u8g2Fonts.setBackgroundColor(bgColor);  // Explizit Hintergrundfarbe setzen
    
    String desc = displayContent.description;
    int currentY = DESC_Y_POS;
    int lineHeight = 18;  // Angepasst für 14px Font
    int maxWidth = hasImage ? (IMAGE_X_POS - 20) : (DISPLAY_WIDTH - 20);
    String currentLine = "";
    int lineCount = 0;
    const int maxLines = 3; // Maximal 3 Zeilen für Beschreibung
    
    for (int i = 0; i < desc.length() && lineCount < maxLines; i++) {
        currentLine += desc[i];
        int16_t lineWidth = u8g2Fonts.getUTF8Width(currentLine.c_str());
        
        if (lineWidth > maxWidth || desc[i] == '\n') {
            u8g2Fonts.setCursor(10, currentY);
            u8g2Fonts.print(currentLine);
            currentY += lineHeight;
            lineCount++;
            currentLine = "";
        }
    }
    
    // Letzte Zeile ausgeben, falls noch Platz
    if (currentLine.length() > 0 && lineCount < maxLines) {
        u8g2Fonts.setCursor(10, currentY);
        u8g2Fonts.print(currentLine);
    }
    
    // Telegram Handle - Position angepasst wenn Bild vorhanden
    if (displayContent.telegram.length() > 0) {
        u8g2Fonts.setFont(u8g2_font_ncenB12_tr);  // Kleinerer Font für Telegram (12px, fett)
        u8g2Fonts.setFontMode(1);  // Transparenter Hintergrund (1 = transparent, 0 = solid)
        u8g2Fonts.setForegroundColor(fgColor);
        u8g2Fonts.setBackgroundColor(bgColor);  // Explizit Hintergrundfarbe setzen
        String telegramHandle = "@" + displayContent.telegram;
        int16_t telegramWidth = u8g2Fonts.getUTF8Width(telegramHandle.c_str());
        
        // Position anpassen: Wenn Bild vorhanden, links positionieren, sonst rechts
        int x;
        if (hasImage) {
            x = 10;  // Links positionieren wenn Bild vorhanden
        } else {
            x = DISPLAY_WIDTH - telegramWidth - 10;  // Rechts positionieren wenn kein Bild
        }
        
        u8g2Fonts.setCursor(x, TELEGRAM_Y_POS);
        u8g2Fonts.print(telegramHandle);
    }
    
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
    displayContent.nameColorRed = doc["nameColorRed"] | true;  // Standard: Rot

    return true;
}

// Diese Funktion speichert die Display-Inhalte in SPIFFS
void saveDisplayContent() {
    saveDisplayContent(true); // Standard: Mit Update
}

void saveDisplayContent(bool sendUpdate) {
    Serial.println("saveDisplayContent() aufgerufen");
    
    StaticJsonDocument<512> doc;
    doc["name"] = displayContent.name;
    doc["description"] = displayContent.description;
    doc["telegram"] = displayContent.telegram;
    doc["imagePath"] = displayContent.imagePath;
    doc["invertColors"] = displayContent.invertColors;
    doc["nameColorRed"] = displayContent.nameColorRed;
    
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
    
    // Display aktualisieren nur wenn gewünscht
    if (sendUpdate && displayQueue != NULL) {
        Serial.println("Sende CMD_UPDATE Kommando an Display-Queue");
        DisplayCommand cmd = CMD_UPDATE;
        if (xQueueSend(displayQueue, &cmd, pdMS_TO_TICKS(100)) == pdPASS) {
            Serial.println("CMD_UPDATE Kommando erfolgreich gesendet!");
        } else {
            Serial.println("ERROR: CMD_UPDATE Kommando konnte nicht gesendet werden!");
        }
    } else if (!sendUpdate) {
        Serial.println("Display-Update übersprungen (sendUpdate=false)");
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
    
    // Vollständiges Display-Update durchführen - verwende drawDisplayContent() für Unicode-Support
    display.setFullWindow();
    display.firstPage();
    do {
        drawDisplayContent();  // Verwende die Unicode-fähige Funktion
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

// Funktion zur Dekodierung und Anzeige von 1-Bit Bitmaps
bool drawBitmapFromFile(const String& filePath, int16_t x, int16_t y, uint16_t fgColor, uint16_t bgColor) {
    Serial.printf("🖼️ Versuche Bild zu laden: %s\n", filePath.c_str());
    
    // Debug: Liste alle SPIFFS-Dateien auf
    Serial.println("📂 SPIFFS-Dateien:");
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while(file) {
        Serial.printf("   - %s (%d Bytes)\n", file.name(), file.size());
        file = root.openNextFile();
    }
    root.close();
    
    // Prüfe ob Datei existiert
    if (!SPIFFS.exists(filePath)) {
        Serial.printf("❌ Datei existiert nicht: %s\n", filePath.c_str());
        return false;
    }
    
    File imageFile = SPIFFS.open(filePath, "r");
    if (!imageFile) {
        Serial.printf("❌ Kann Bilddatei nicht öffnen: %s\n", filePath.c_str());
        return false;
    }
    
    size_t fileSize = imageFile.size();
    Serial.printf("📁 Dateigröße: %d Bytes\n", fileSize);
    
    // Prüfe Dateierweiterung
    bool isJpg = filePath.endsWith(".jpg") || filePath.endsWith(".jpeg") || filePath.endsWith(".JPG") || filePath.endsWith(".JPEG");
    
    if (isJpg) {
        Serial.println("🖼️ JPG-Datei erkannt - zeige einfaches Platzhalter-Symbol");
        
        imageFile.close();
        
        // Für JPG-Dateien: Zeige ein einfaches Bild-Symbol
        // Da JPG-Dekodierung sehr komplex ist, zeigen wir ein stilisiertes Bild-Symbol
        
        // Hintergrund füllen
        display.fillRect(x, y, IMAGE_SIZE, IMAGE_SIZE, bgColor);
        
        // Einfaches Bild-Symbol zeichnen
        int centerX = x + IMAGE_SIZE / 2;
        int centerY = y + IMAGE_SIZE / 2;
        
        // Rahmen
        display.drawRect(x + 8, y + 8, IMAGE_SIZE - 16, IMAGE_SIZE - 16, fgColor);
        display.drawRect(x + 9, y + 9, IMAGE_SIZE - 18, IMAGE_SIZE - 18, fgColor);
        
        // Berge/Landschaft
        display.drawLine(x + 12, y + 45, x + 25, y + 30, fgColor);
        display.drawLine(x + 25, y + 30, x + 35, y + 40, fgColor);
        display.drawLine(x + 35, y + 40, x + 45, y + 25, fgColor);
        display.drawLine(x + 45, y + 25, x + 52, y + 35, fgColor);
        
        // Sonne
        display.fillCircle(x + 45, y + 20, 4, fgColor);
        
        // Text "JPG"
        display.setCursor(x + 20, y + 55);
        display.setTextSize(1);
        display.print("JPG");
        
        Serial.println("✅ JPG-Platzhalter erfolgreich angezeigt");
        return true;
    }
    
    // Erste 54 Bytes lesen um BMP-Header zu prüfen
    uint8_t header[54];
    size_t headerRead = imageFile.read(header, 54);
    
    if (headerRead < 54) {
        Serial.printf("❌ Kann nur %d von 54 Header-Bytes lesen\n", headerRead);
        imageFile.close();
        return false;
    }
    
    // Debug: Erste Bytes ausgeben
    Serial.printf("🔍 Erste 4 Bytes: 0x%02X 0x%02X 0x%02X 0x%02X\n", 
                  header[0], header[1], header[2], header[3]);
    
    // BMP-Signatur prüfen
    if (header[0] != 'B' || header[1] != 'M') {
        Serial.printf("❌ Keine BMP-Signatur gefunden (0x%02X 0x%02X statt 'BM')\n", header[0], header[1]);
        
        // Versuche als Raw-Bitmap zu behandeln
        Serial.println("🔄 Versuche als Raw 1-Bit Bitmap...");
        imageFile.seek(0); // Zurück zum Anfang
        
        // Raw 1-Bit Bitmap: 64x64 = 512 Bytes
        if (fileSize >= 512) {
            uint8_t buffer[8]; // 8 Bytes = 64 Pixel pro Zeile
            
            for (int row = 0; row < 64; row++) {
                size_t bytesRead = imageFile.read(buffer, 8);
                if (bytesRead != 8) {
                    Serial.printf("❌ Zeile %d: Nur %d von 8 Bytes gelesen\n", row, bytesRead);
                    break;
                }
                
                for (int col = 0; col < 64; col++) {
                    int byteIndex = col / 8;
                    int bitIndex = 7 - (col % 8); // MSB first
                    bool pixelOn = (buffer[byteIndex] >> bitIndex) & 1;
                    
                    if (pixelOn) {
                        display.drawPixel(x + col, y + row, fgColor);
                    } else {
                        display.drawPixel(x + col, y + row, bgColor);
                    }
                }
            }
            
            imageFile.close();
            Serial.println("✅ Raw Bitmap erfolgreich geladen");
            return true;
        } else {
            Serial.printf("❌ Datei zu klein für Raw Bitmap (%d < 512 Bytes)\n", fileSize);
            imageFile.close();
            return false;
        }
    }
    
    // BMP-Header analysieren
    uint32_t dataOffset = *(uint32_t*)&header[10];
    uint32_t dibHeaderSize = *(uint32_t*)&header[14];
    uint32_t width = *(uint32_t*)&header[18];
    uint32_t height = *(uint32_t*)&header[22];
    uint16_t bitsPerPixel = *(uint16_t*)&header[28];
    uint32_t compression = *(uint32_t*)&header[30];
    
    Serial.printf("📊 BMP-Info:\n");
    Serial.printf("   - Daten-Offset: %d\n", dataOffset);
    Serial.printf("   - DIB-Header-Größe: %d\n", dibHeaderSize);
    Serial.printf("   - Größe: %dx%d\n", width, height);
    Serial.printf("   - Bits pro Pixel: %d\n", bitsPerPixel);
    Serial.printf("   - Kompression: %d\n", compression);
    
    // Validierung
    if (compression != 0) {
        Serial.printf("❌ Komprimierte BMPs werden nicht unterstützt (Kompression: %d)\n", compression);
        imageFile.close();
        return false;
    }
    
    if (bitsPerPixel != 1 && bitsPerPixel != 8 && bitsPerPixel != 24 && bitsPerPixel != 32) {
        Serial.printf("❌ Nicht unterstützte Farbtiefe: %d bpp\n", bitsPerPixel);
        imageFile.close();
        return false;
    }
    
    // Zu Datenbereich springen
    imageFile.seek(dataOffset);
    Serial.printf("🔄 Springe zu Daten-Offset: %d\n", dataOffset);
    
    if (bitsPerPixel == 1) {
        // 1-Bit BMP (Monochrom)
        int bytesPerRow = (width + 7) / 8;
        int paddedBytesPerRow = (bytesPerRow + 3) & ~3; // 4-Byte-Ausrichtung
        
        Serial.printf("📏 Bytes pro Zeile: %d, mit Padding: %d\n", bytesPerRow, paddedBytesPerRow);
        
        uint8_t* rowBuffer = (uint8_t*)malloc(paddedBytesPerRow);
        if (!rowBuffer) {
            Serial.println("❌ Speicher-Allokation fehlgeschlagen");
            imageFile.close();
            return false;
        }
        
        // BMP ist von unten nach oben gespeichert
        for (int row = height - 1; row >= 0; row--) {
            size_t bytesRead = imageFile.read(rowBuffer, paddedBytesPerRow);
            if (bytesRead != paddedBytesPerRow) {
                Serial.printf("❌ Zeile %d: Nur %d von %d Bytes gelesen\n", row, bytesRead, paddedBytesPerRow);
                break;
            }
            
            // Nur die ersten 64 Pixel und 64 Zeilen anzeigen
            if (row < IMAGE_SIZE) {
                for (int col = 0; col < width && col < IMAGE_SIZE; col++) {
                    int byteIndex = col / 8;
                    int bitIndex = 7 - (col % 8); // MSB first
                    bool pixelOn = (rowBuffer[byteIndex] >> bitIndex) & 1;
                    
                    // BMP 1-Bit: 0 = schwarz, 1 = weiß
                    // Für E-Paper: schwarz = fgColor, weiß = bgColor
                    if (!pixelOn) { // 0 = schwarz
                        display.drawPixel(x + col, y + (IMAGE_SIZE - 1 - row), fgColor);
                    } else { // 1 = weiß
                        display.drawPixel(x + col, y + (IMAGE_SIZE - 1 - row), bgColor);
                    }
                }
            }
        }
        
        free(rowBuffer);
        imageFile.close();
        Serial.println("✅ 1-Bit BMP erfolgreich geladen");
        return true;
    }
    else if (bitsPerPixel == 8) {
        // 8-Bit BMP (Graustufen oder indiziert)
        Serial.println("🔄 Lade 8-Bit BMP als Graustufen...");
        
        // Palette überspringen (256 * 4 Bytes)
        imageFile.seek(dataOffset);
        
        int bytesPerRow = width;
        int paddedBytesPerRow = (bytesPerRow + 3) & ~3;
        
        uint8_t* rowBuffer = (uint8_t*)malloc(paddedBytesPerRow);
        if (!rowBuffer) {
            Serial.println("❌ Speicher-Allokation fehlgeschlagen");
            imageFile.close();
            return false;
        }
        
        for (int row = height - 1; row >= 0; row--) {
            size_t bytesRead = imageFile.read(rowBuffer, paddedBytesPerRow);
            if (bytesRead != paddedBytesPerRow) break;
            
            if (row < IMAGE_SIZE) {
                for (int col = 0; col < width && col < IMAGE_SIZE; col++) {
                    uint8_t grayValue = rowBuffer[col];
                    // Schwellwert bei 128: < 128 = schwarz, >= 128 = weiß
                    if (grayValue < 128) {
                        display.drawPixel(x + col, y + (IMAGE_SIZE - 1 - row), fgColor);
                    } else {
                        display.drawPixel(x + col, y + (IMAGE_SIZE - 1 - row), bgColor);
                    }
                }
            }
        }
        
        free(rowBuffer);
        imageFile.close();
        Serial.println("✅ 8-Bit BMP erfolgreich geladen");
        return true;
    }
    else if (bitsPerPixel == 24) {
        // 24-Bit BMP (RGB)
        Serial.println("🔄 Lade 24-Bit BMP als RGB...");
        
        int bytesPerRow = width * 3;
        int paddedBytesPerRow = (bytesPerRow + 3) & ~3;
        
        uint8_t* rowBuffer = (uint8_t*)malloc(paddedBytesPerRow);
        if (!rowBuffer) {
            Serial.println("❌ Speicher-Allokation fehlgeschlagen");
            imageFile.close();
            return false;
        }
        
        for (int row = height - 1; row >= 0; row--) {
            size_t bytesRead = imageFile.read(rowBuffer, paddedBytesPerRow);
            if (bytesRead != paddedBytesPerRow) break;
            
            if (row < IMAGE_SIZE) {
                for (int col = 0; col < width && col < IMAGE_SIZE; col++) {
                    int pixelIndex = col * 3;
                    uint8_t blue = rowBuffer[pixelIndex];
                    uint8_t green = rowBuffer[pixelIndex + 1];
                    uint8_t red = rowBuffer[pixelIndex + 2];
                    
                    // RGB zu Graustufen: 0.299*R + 0.587*G + 0.114*B
                    uint8_t gray = (uint8_t)(0.299f * red + 0.587f * green + 0.114f * blue);
                    
                    // VEREINFACHTER ANSATZ: Einfacher Schwellwert ohne Dithering
                    // Das verhindert die Streifen-Artefakte
                    bool isBlack = gray < 128;
                    
                    // KORRIGIERT: Bildorientierung - BMP ist von unten nach oben, aber wir wollen es richtig herum
                    int displayRow = row; // Verwende row direkt, nicht invertiert
                    if (isBlack) {
                        display.drawPixel(x + col, y + displayRow, fgColor);
                    } else {
                        display.drawPixel(x + col, y + displayRow, bgColor);
                    }
                }
            }
        }
        
        free(rowBuffer);
        imageFile.close();
        Serial.println("✅ 24-Bit BMP erfolgreich geladen");
        return true;
    }
    else if (bitsPerPixel == 32) {
        // 32-Bit BMP (RGBA oder RGBX)
        Serial.println("🔄 Lade 32-Bit BMP als RGBA...");
        
        int bytesPerRow = width * 4;
        int paddedBytesPerRow = (bytesPerRow + 3) & ~3;
        
        Serial.printf("📏 32-Bit: Bytes pro Zeile: %d, mit Padding: %d\n", bytesPerRow, paddedBytesPerRow);
        
        uint8_t* rowBuffer = (uint8_t*)malloc(paddedBytesPerRow);
        if (!rowBuffer) {
            Serial.println("❌ Speicher-Allokation fehlgeschlagen");
            imageFile.close();
            return false;
        }
        
        int processedRows = 0;
        for (int row = height - 1; row >= 0; row--) {
            size_t bytesRead = imageFile.read(rowBuffer, paddedBytesPerRow);
            if (bytesRead != paddedBytesPerRow) {
                Serial.printf("⚠️ Zeile %d: Nur %d von %d Bytes gelesen\n", row, bytesRead, paddedBytesPerRow);
                break;
            }
            
            if (row < IMAGE_SIZE) {
                processedRows++;
                for (int col = 0; col < width && col < IMAGE_SIZE; col++) {
                    int pixelIndex = col * 4;
                    uint8_t blue = rowBuffer[pixelIndex];
                    uint8_t green = rowBuffer[pixelIndex + 1];
                    uint8_t red = rowBuffer[pixelIndex + 2];
                    uint8_t alpha = rowBuffer[pixelIndex + 3];
                    
                    // Alpha-Blending mit weißem Hintergrund
                    float alphaF = alpha / 255.0f;
                    uint8_t blendedRed = (uint8_t)(red * alphaF + 255 * (1 - alphaF));
                    uint8_t blendedGreen = (uint8_t)(green * alphaF + 255 * (1 - alphaF));
                    uint8_t blendedBlue = (uint8_t)(blue * alphaF + 255 * (1 - alphaF));
                    
                    // RGB zu Graustufen mit Alpha-Berücksichtigung
                    uint8_t gray = (uint8_t)(0.299f * blendedRed + 0.587f * blendedGreen + 0.114f * blendedBlue);
                    
                    // VEREINFACHTER ANSATZ: Einfacher Schwellwert ohne Dithering
                    // Das verhindert die Streifen-Artefakte
                    bool isBlack = gray < 128;
                    
                    // KORRIGIERT: Bildorientierung - BMP ist von unten nach oben, aber wir wollen es richtig herum
                    int displayRow = row; // Verwende row direkt, nicht invertiert
                    if (isBlack) {
                        display.drawPixel(x + col, y + displayRow, fgColor);
                    } else {
                        display.drawPixel(x + col, y + displayRow, bgColor);
                    }
                }
            }
        }
        
        free(rowBuffer);
        imageFile.close();
        Serial.printf("✅ 32-Bit BMP mit Alpha-Blending erfolgreich geladen (%d Zeilen verarbeitet)\n", processedRows);
        return true;
    }
    
    imageFile.close();
    Serial.printf("❌ Unbekanntes BMP-Format: %d bpp\n", bitsPerPixel);
    return false;
}
