#include "Display.h"
#include "esp_task_wdt.h"
#include <Fonts/FreeMonoBold18pt7b.h>  // Größere Schrift für den Namen

// Display-Objekt Definition
DISPLAY_TYPE display(GxEPD2_290_C90c(EINK_CS, EINK_DC, EINK_RES, EINK_BUSY));

// Display-Status
bool displayAvailable = false;

// Display-Inhalte
DisplayContent displayContent = {"ArdyMoon", "", ""};

// Task Handle und Queue
TaskHandle_t displayTaskHandle = NULL;
QueueHandle_t displayQueue = NULL;

void displayTask(void *parameter) {
    // Task vom Watchdog ausschließen
    esp_task_wdt_delete(NULL);
    
    DisplayCommand cmd;
    TickType_t waitTime = pdMS_TO_TICKS(100); // 100ms Timeout
    
    while(true) {
        if (xQueueReceive(displayQueue, &cmd, waitTime) == pdTRUE) {
            switch(cmd) {
                case CMD_INIT:
                    initDisplay();
                    break;
                case CMD_UPDATE:
                    updateDisplay();
                    break;
            }
        }
        // Task-Yield für Stabilität
        vTaskDelay(pdMS_TO_TICKS(10));
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

bool checkDisplayAvailable() {
    displayAvailable = false;
    
    try {
        // Verbesserte Initialisierung mit optimierten Parametern für WeAct Display
        display.init(115200, true, 50, false);
        display.setRotation(1); // Querformat
        display.setTextColor(GxEPD_BLACK);
        
        // Prüfe Partial Update Unterstützung
        if (display.epd2.hasFastPartialUpdate) {
            Serial.println("Fast Partial Update unterstützt!");
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
    if (!displayAvailable) return;

    display.setFullWindow();
    display.firstPage();
    do {
        // Hintergrund weiß
        display.fillScreen(GxEPD_WHITE);
        
        // Name in Rot mit größerer Schrift
        display.setFont(&FreeMonoBold18pt7b);  // Größere Schrift
        display.setTextColor(GxEPD_RED);
        int16_t tbx, tby; uint16_t tbw, tbh;
        display.getTextBounds(displayContent.name, 0, 0, &tbx, &tby, &tbw, &tbh);
        uint16_t x = (DISPLAY_WIDTH - tbw) / 2;
        display.setCursor(x, 30);  // Y-Position angepasst für größere Schrift
        display.print(displayContent.name);
        
        // Trennlinie
        display.drawLine(0, LINE_Y_POS, DISPLAY_WIDTH, LINE_Y_POS, GxEPD_BLACK);
        
        // Beschreibung
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_BLACK);
        String desc = String(displayContent.description);
        int currentY = DESC_Y_POS;
        int lineHeight = 20;
        String currentLine;
        
        for (int i = 0; i < desc.length(); i++) {
            currentLine += desc[i];
            display.getTextBounds(currentLine.c_str(), 0, 0, &tbx, &tby, &tbw, &tbh);
            
            if (tbw > DISPLAY_WIDTH - 20 || desc[i] == '\n') {
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
        String telegramHandle = "@" + String(displayContent.telegram);
        display.getTextBounds(telegramHandle.c_str(), 0, 0, &tbx, &tby, &tbw, &tbh);
        x = DISPLAY_WIDTH - tbw - 10;  // 10 Pixel Abstand vom rechten Rand
        display.setCursor(x, TELEGRAM_Y_POS);
        display.print(telegramHandle);
        
    } while (display.nextPage());
}

bool saveDisplayContent() {
    StaticJsonDocument<512> doc;
    
    doc["name"] = displayContent.name;
    doc["description"] = displayContent.description;
    doc["telegram"] = displayContent.telegram;

    File file = SPIFFS.open("/display.json", "w");
    if (!file) {
        return false;
    }

    if (serializeJson(doc, file) == 0) {
        file.close();
        return false;
    }

    file.close();
    return true;
}

bool loadDisplayContent() {
    if (!SPIFFS.exists("/display.json")) {
        return saveDisplayContent(); // Speichere Standardwerte
    }

    File file = SPIFFS.open("/display.json", "r");
    if (!file) {
        return false;
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        return false;
    }

    strlcpy(displayContent.name, doc["name"] | "ArdyMoon", sizeof(displayContent.name));
    strlcpy(displayContent.description, doc["description"] | "", sizeof(displayContent.description));
    strlcpy(displayContent.telegram, doc["telegram"] | "", sizeof(displayContent.telegram));

    return true;
} 