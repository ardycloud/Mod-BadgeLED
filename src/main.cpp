#include <Arduino.h>
#include <FastLED.h>
#include "esp32-hal.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_task_wdt.h>  // Für Watchdog-Funktionen
#include "Config.h"  // Neue Include-Datei
#include "WebUI.h"
#include "Settings.h"
#include "Display.h"
#include "SPIFFS.h"
#include "BQ25895CONFIG.h"
#include "Globals.h"  // KRITISCH: Globale Variablen einbinden
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ESPmDNS.h>  // mDNS für badge.local
#include <esp_sleep.h> // Für Deep Sleep Funktionalität

// Externe Variablen für WiFi
extern String apName;
extern String apPassword;

// Define the WiFi variables that are declared as extern in WebUI.cpp
String apName = "LED-Badge";
String apPassword = "";

// Externe Variablen
extern bool displayAvailable;
extern DisplayContent displayContent;
extern QueueHandle_t displayQueue;
extern BQ25895* charger;

// Externe Funktionen
extern void checkButtons();
extern void displayTask(void *parameter);  // Extern-Deklaration für die displayTask Funktion

// Konstante Definitionen
#define MAIN_LED_PIN    16
#define STATUS_LED_PIN  15
#define NUM_MAIN_LEDS   75
#define NUM_STATUS_LEDS 5
#define LED_TYPE        WS2812B
#define COLOR_ORDER     GRB
#define MIC_PIN        34  // Analoger Pin für MAX4466
#define SAMPLES        16  // Anzahl der Samples für die Durchschnittsberechnung

// Konstante für Boot-Button
#define BOOT_BUTTON_PIN 0
#define MODE_BUTTON     7
#define COLOR_BUTTON    6
#define BRIGHT_BUTTON   5
#define SAO_BUTTON      4
#define LIGHT_EN           35

// Debounce-Variablen für Taster
unsigned long lastSaoButtonPress = 0;
const unsigned long debounceTime = 300; // Entprellzeit in ms

// Globale Variablen
uint8_t BRIGHTNESS = 5;
uint8_t NOISE_LEVEL = 10;
uint16_t animationSpeed = 20;
uint16_t micFrequency = 50;
bool wifiEnabled = false;  // WiFi Status (deprecated - use getCurrentWifiStatusColor() instead)
bool lastWifiStatus = false; // Temporary - remove after cleanup

// Globale Variablen für Button-Handling
unsigned long buttonPressStartTime = 0;
bool buttonWasPressed = false;
unsigned long lastButtonPress = 0;
const unsigned long DEBOUNCE_DELAY = 200; // Entprellzeit in Millisekunden

// Zeitvariable für die Startzeit des Geräts
unsigned long startupTime = 0;
// Mindestlaufzeit bevor Deep-Sleep erlaubt ist (30 Sekunden statt 5 Minuten)
const unsigned long MIN_RUNTIME_BEFORE_SLEEP = 30 * 1000; // 30 Sekunden

// Status LED Farben - KORREKTUR: Status-LEDs verwenden RGB-Reihenfolge (nicht GRB wie Haupt-LEDs)!
#define STATUS_RED     CRGB(255, 0, 0)   // Rot in RGB
#define STATUS_GREEN   CRGB(0, 255, 0)   // Grün in RGB  
#define STATUS_BLUE    CRGB(0, 0, 255)   // Blau in RGB (bleibt gleich)
#define STATUS_YELLOW  CRGB(255, 255, 0)  // Gelb in RGB (bleibt gleich)
#define STATUS_WHITE   CRGB(255, 255, 255) // Weiß in RGB (bleibt gleich)

// Status LED Helligkeit (fester Wert, unabhängig von den Hauptleds)
#define STATUS_LED_BRIGHTNESS 12

// Batterieschwellen für verschiedene Maßnahmen (User-Anforderung)
#define WARNING_BATTERY_THRESHOLD 3.5f   // Orange Warnung
#define LOW_BATTERY_THRESHOLD 3.3f       // Rot - LEDs ausschalten (Main LEDs)
#define BOOST_OFF_THRESHOLD 3.0f         // Boost IC abschalten (LIGHT_EN Pin) - User-Anforderung: bei 3.0V komplett aus
#define DEEP_SLEEP_THRESHOLD 2.9f        // Deep Sleep aktivieren - unter Boost-Off-Schwelle

bool isWarningBattery = false;  // Orange-Warnung bei 3.5V
bool isLowBattery = false;      // Rot-Warnung bei 3.3V
bool isBoostDisabled = false;
unsigned long lastBlinkTime = 0;
bool blinkState = false;

// Häufigere Batterie-Kontrolle - alle 2 Sekunden statt 5 Sekunden
unsigned long lastBatteryCheck = 0;
const unsigned long BATTERY_CHECK_INTERVAL = 2000; // 2 Sekunden

// Globale Variablen
CRGB mainLeds[NUM_MAIN_LEDS];
CRGB statusLeds[NUM_STATUS_LEDS];
unsigned long lastModeChange = 0;
TaskHandle_t ledTaskHandle = NULL;
TaskHandle_t statusLedTaskHandle = NULL; // Separate Task für Status LEDs
SemaphoreHandle_t ledMutex;
SemaphoreHandle_t statusLedMutex; // Separate Mutex für Status LEDs

// LED-Indikator für Tasten
unsigned long modeLedOffTime = 0;  // Zeitpunkt, wann die Mode LED ausgeschaltet wird
unsigned long colorLedOffTime = 0; // Zeitpunkt, wann die Color LED ausgeschaltet wird
unsigned long brightLedOffTime = 0; // Zeitpunkt, wann die Brightness LED ausgeschaltet wird
unsigned long saoLedOffTime = 0;    // Zeitpunkt, wann die SAO LED ausgeschaltet wird
bool modeLedActive = false;         // Status der Mode LED
bool colorLedActive = false;        // Status der Color LED
bool brightLedActive = false;       // Status der Brightness LED
bool saoLedActive = false;          // Status der SAO LED

// Globale Variablen für Status-LED-Feedback
bool statusLedUpdateMode = false;
bool statusLedRebootMode = false;
bool statusLedLockMode = false;
bool statusLedUnlockMode = false;
unsigned long statusLedStartTime = 0;
int statusLedBlinkCount = 0;

// Dynamische Version für OTA-Updates
String dynamicVersion; // Wird in setup() initialisiert

// Funktionsprototypen für alle korrigierten Animationen
void solidColor();
void rainbow();
void rainbowWave();
void colorWipe();
void theaterChase();
void twinkle();
void fire();
void pulse();
void wave();
void sparkle();
void gradient();
void dots();
void comet();
void bounce();
void fireworks();
void lightning();
void confetti();
void breathe();
void rain();
void matrix();
void orbit();
void spiral();
void meteor();
void colorSpin();
void musicReactive();
void addGlitter(uint8_t chance);
void updateStatusLED();
void ledTask(void *parameter);
void statusLedTask(void *parameter); // Neue Task-Funktion für Status LEDs

// OTA Funktionen
void connectToWiFi();
void performOTACheckin();
void updateFirmware(String url);
void checkForOTAUpdate();
void startStatusLedUpdate();
void startStatusLedReboot();
void startStatusLedLock();
void startStatusLedUnlock();
void updateStatusLedFeedback();

// Funktion zum Togglen von WiFi
void toggleWiFi() {
    if (!wifiEnabled) {
        // WiFi-Verbindung starten
        if (wifiConnectEnabled) {
            // Client-Modus aktivieren
            Serial.println("Boot-Button: Aktiviere WiFi Client-Modus");
            connectToWiFi();
            if (WiFi.status() == WL_CONNECTED) {
                wifiEnabled = true;
                Serial.println("WiFi Client-Verbindung hergestellt");
                
                // Status merken
                lastWifiStatus = true;
            } else {
                Serial.println("WiFi Client-Verbindung fehlgeschlagen");
            }
        } else {
            // AP-Modus aktivieren
            Serial.println("Boot-Button: Aktiviere WiFi AP-Modus");
            WiFi.mode(WIFI_AP);
            WiFi.softAP(apName.c_str(), apPassword.length() > 0 ? apPassword.c_str() : NULL);
            wifiEnabled = true;
            Serial.println("WiFi AP-Modus aktiviert: " + apName);
            
            // Status merken
            lastWifiStatus = true;
        }
    } else {
        // WiFi-Verbindung trennen
        Serial.println("Boot-Button: Deaktiviere WiFi");
        WiFi.disconnect();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);  // KRITISCH: WiFi Mode explizit auf OFF setzen!
        wifiEnabled = false;
        Serial.println("WiFi-Verbindung getrennt und auf WIFI_OFF gesetzt");
        
        // Status merken
        lastWifiStatus = false;
    }
}

// Funktion zum Überprüfen des Boot-Buttons
void checkBootButton() {
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {  // Button ist gedrückt (LOW wegen Pullup)
        if (!buttonWasPressed) {
            buttonPressStartTime = millis();
            buttonWasPressed = true;
        } else {  // Button wird gehalten
            if ((millis() - buttonPressStartTime) >= 3000) {  // 3 Sekunden gedrückt
                toggleWiFi();
                buttonWasPressed = false;  // Verhindert mehrfaches Auslösen
                delay(500);  // Entprellzeit
            }
        }
    } else {  // Button ist nicht gedrückt
        buttonWasPressed = false;
    }
}

// LED Task Funktion mit korrigierter Animation-Zuordnung
void ledTask(void *parameter) {
    Serial.println("=== LED Task gestartet (KORRIGIERT) ===");
    Serial.printf("Core: %d, Modus: %d, Primär: %d, Sekundär: %d\n", 
                 xPortGetCoreID(), currentMode, primaryHue, secondaryHue);
    
    unsigned long lastUpdate = 0;
    unsigned long lastMemoryCheck = 0;
    // unsigned long lastWatchdogReset = 0;  // ENTFERNT - nicht mehr benötigt
    unsigned long loopCounter = 0;
    
    // KRITISCH: Watchdog-Registrierung entfernt - verhindert Reboots
    // esp_task_wdt_add(NULL);  // DEAKTIVIERT - verursacht Reboots ohne richtige Watchdog-Init
    
    for(;;) {
        loopCounter++;
        unsigned long currentTime = millis();
        
        // KRITISCH: Watchdog-Reset entfernt - keine Watchdog-Registrierung mehr
        // if (currentTime - lastWatchdogReset > 500) {
        //     esp_task_wdt_reset();
        //     lastWatchdogReset = currentTime;
        // }
        
        // Memory-Check alle 30 Sekunden um Aufhängen zu verhindern
        if (currentTime - lastMemoryCheck > 30000) {
            size_t freeHeap = ESP.getFreeHeap();
            Serial.printf("LED-Task: Freier Heap: %d bytes, Loops: %d\n", freeHeap, loopCounter);
            
            if (freeHeap < 20000) {
                Serial.printf("WARNUNG: Wenig Speicher im LED-Task: %d bytes\n", freeHeap);
                // Kurze Pause für Garbage Collection
                vTaskDelay(pdMS_TO_TICKS(100));
                // esp_task_wdt_reset(); // Nach Pause Watchdog zurücksetzen - ENTFERNT
            }
            lastMemoryCheck = currentTime;
            loopCounter = 0; // Reset counter
        }
        
        // KRITISCH: Button-Abfrage direkt hier auf Core 1
        checkButtons();
        
        // KRITISCH: Lokale Kopie von animSpeed für Race-Condition-Schutz
        uint16_t currentAnimSpeed = animSpeed;  // Atomic read der volatile Variable
        
        // Animation alle animSpeed ms
        if (currentTime - lastUpdate >= currentAnimSpeed) {
            // KRITISCH: Mutex mit Timeout und Fehlerbehandlung
            if(xSemaphoreTake(ledMutex, pdMS_TO_TICKS(100))) {  // Timeout erhöht auf 100ms
                // DEBUG: Status vor LED-Update prüfen
                static bool lastLowBatteryState = false;
                if (isLowBattery != lastLowBatteryState) {
                    Serial.printf("🔴 LED-TASK DEBUG: isLowBattery Status geändert: %s -> %s\n", 
                                 lastLowBatteryState?"JA":"NEIN", isLowBattery?"JA":"NEIN");
                    lastLowBatteryState = isLowBattery;
                }
                
                // Hauptanimation - nur aktualisieren wenn keine Low-Battery-Bedingung
                if (!isLowBattery) {
                    // KORRIGIERTE Animationszuordnung
                    switch(currentMode) {
                        case 0:  solidColor();      break;  // Einfarbig
                        case 1:  rainbow();         break;  // Regenbogen
                        case 2:  rainbowWave();     break;  // Regenbogen-Welle
                        case 3:  colorWipe();       break;  // Farbwisch
                        case 4:  theaterChase();    break;  // Theater-Verfolgung
                        case 5:  twinkle();         break;  // Funkeln
                        case 6:  fire();            break;  // Feuer
                        case 7:  pulse();           break;  // Pulsieren
                        case 8:  wave();            break;  // Welle
                        case 9:  sparkle();         break;  // Glitzern
                        case 10: gradient();        break;  // Gradient
                        case 11: dots();            break;  // Wandernde Punkte
                        case 12: comet();           break;  // Komet
                        case 13: bounce();          break;  // Springender Ball
                        case 14: fireworks();       break;  // Feuerwerk
                        case 15: lightning();       break;  // Blitz
                        case 16: confetti();        break;  // Konfetti
                        case 17: breathe();         break;  // Atmung
                        case 18: rain();            break;  // Regen
                        case 19: matrix();          break;  // Matrix
                        case 20: orbit();           break;  // Umlaufbahn
                        case 21: spiral();          break;  // Spirale
                        case 22: meteor();          break;  // Meteorschauer
                        case 23: colorSpin();       break;  // Farbrotation
                        case 24: musicReactive();   break;  // Musik-reaktiv
                        default: solidColor();      break;  // Fallback
                    }
                    
                    // KRITISCH: Sichere LED-Aktualisierung ohne gefährliche Interrupt-Blockade
                    // Das Mutex schützt bereits vor Race Conditions - keine zusätzlichen Critical Sections nötig
                    // portDISABLE_INTERRUPTS war gefährlich und konnte Watchdog-Reboots verursachen
                    FastLED[0].showLeds(brightness);
                }
                
                xSemaphoreGive(ledMutex);
            } else {
                // Mutex-Timeout - Kritischer Fehler, möglicherweise Deadlock
                Serial.println("KRITISCH: LED-Mutex Timeout - möglicher Deadlock!");
                // esp_task_wdt_reset(); // Watchdog zurücksetzen - ENTFERNT
                
                // Bei wiederholten Timeouts Task neustarten (Notfall-Maßnahme)
                static uint8_t timeoutCounter = 0;
                timeoutCounter++;
                if (timeoutCounter > 5) {
                    Serial.println("NOTFALL: Zu viele Mutex-Timeouts - Task wird neugestartet!");
                    timeoutCounter = 0;
                    // Kurze Pause für System-Recovery
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
            }
            
            lastUpdate = currentTime;
        }
        
        // KRITISCH: Mindest-Delay von 10ms für Watchdog-Stabilität
        // Auch bei schnellsten Animationen (1ms) mindestens 10ms warten
        // Verwende lokale Kopie für Race-Condition-Schutz
        uint32_t delayTime = (currentAnimSpeed / 5 > 10) ? currentAnimSpeed / 5 : 10;
        vTaskDelay(pdMS_TO_TICKS(delayTime));
        
        // Zusätzlicher Watchdog-Reset nach jedem Delay - ENTFERNT
        // esp_task_wdt_reset();
    }
}

// KORRIGIERTE Animationsfunktionen mit richtiger HSV-Konvertierung

void solidColor() {
    // Einfarbig - Primärfarbe
    fill_solid(mainLeds, NUM_MAIN_LEDS, CHSV(primaryHue, 255, 255));
}

void rainbow() {
    // Klassischer Regenbogen - unabhängig von Farbauswahl
    static uint8_t hue = 0;
    
    // KRITISCH: Reset-Mechanismus gegen Überlauf (wie bei anderen Effekten)
    static unsigned long lastReset = 0;
    if (millis() - lastReset > 60000) { // Reset alle 60 Sekunden
        hue = 0;
        lastReset = millis();
    }
    
    fill_rainbow(mainLeds, NUM_MAIN_LEDS, hue, 7);
    hue = (hue + 1) % 256; // KRITISCH: Sichere Inkrement-Operation statt einfachem ++
}

void rainbowWave() {
    // Regenbogen-Welle
    static uint8_t hue = 0;
    
    // KRITISCH: Reset-Mechanismus gegen Überlauf
    static unsigned long lastReset = 0;
    if (millis() - lastReset > 60000) { // Reset alle 60 Sekunden
        hue = 0;
        lastReset = millis();
    }
    
    for(int i = 0; i < NUM_MAIN_LEDS; i++) {
        mainLeds[i] = CHSV((hue + (i * 10)) % 256, 255, 255);
    }
    hue += 2;
}

void colorWipe() {
    // Farbwischeffekt zwischen Primär- und Sekundärfarbe
    static uint8_t pos = 0;
    static bool direction = true;
    
    // KRITISCH: Reset-Mechanismus gegen Überlauf
    static unsigned long lastReset = 0;
    if (millis() - lastReset > 45000) { // Reset alle 45 Sekunden
        pos = 0;
        direction = true;
        lastReset = millis();
    }
    
    for(int i = 0; i < NUM_MAIN_LEDS; i++) {
        if (i <= pos) {
            mainLeds[i] = CHSV(direction ? primaryHue : secondaryHue, 255, 255);
        } else {
            mainLeds[i] = CHSV(direction ? secondaryHue : primaryHue, 255, 255);
        }
    }
    
    pos++;
    if (pos >= NUM_MAIN_LEDS) {
        pos = 0;
        direction = !direction;
    }
}

void theaterChase() {
    // Theater-Verfolgungsjagd
    static uint8_t offset = 0;
    
    // KRITISCH: Reset-Mechanismus gegen Überlauf
    static unsigned long lastReset = 0;
    if (millis() - lastReset > 30000) { // Reset alle 30 Sekunden
        offset = 0;
        lastReset = millis();
    }
    
    for(int i = 0; i < NUM_MAIN_LEDS; i++) {
        if ((i + offset) % 3 == 0) {
            mainLeds[i] = CHSV(primaryHue, 255, 255);
        } else {
            mainLeds[i] = CRGB::Black;
        }
    }
    offset = (offset + 1) % 3;
}

void twinkle() {
    fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 20);
    
    if (random8() < 80) {
        int pos = random16(NUM_MAIN_LEDS);
        mainLeds[pos] = CHSV(random8() % 2 ? primaryHue : secondaryHue, 255, 255);
    }
}

void fire() {
    for(int i = 0; i < NUM_MAIN_LEDS; i++) {
        int flicker = random8(150);
        
        // KRITISCH: Hue-Überlauf verhindern durch sichere Addition (wie bei confetti)
        int16_t hueOffset = random8(30) - 15;  // -15 bis +14
        uint8_t hue = (primaryHue + hueOffset + 256) % 256;   // Sichere Modulo-Operation
        
        mainLeds[i] = CHSV(hue, 255, flicker);
    }
}

void pulse() {
    static uint8_t brightness = 0;
    static int8_t direction = 1;
    static bool useSecondary = false;
    
    // KRITISCH: Reset-Mechanismus gegen Überlauf
    static unsigned long lastReset = 0;
    if (millis() - lastReset > 35000) { // Reset alle 35 Sekunden
        brightness = 0;
        direction = 1;
        useSecondary = false;
        lastReset = millis();
    }
    
    brightness += direction * 3;
    if (brightness >= 250) {
        direction = -1;
    } else if (brightness <= 5) {
        direction = 1;
        useSecondary = !useSecondary;
    }
    
    uint8_t hue = useSecondary ? secondaryHue : primaryHue;
    fill_solid(mainLeds, NUM_MAIN_LEDS, CHSV(hue, 255, brightness));
}

void wave() {
    for(int i = 0; i < NUM_MAIN_LEDS; i++) {
        uint8_t brightness = beatsin8(10, 50, 255, 0, i * 10);
        uint8_t hue = map(i, 0, NUM_MAIN_LEDS-1, primaryHue, secondaryHue);
        mainLeds[i] = CHSV(hue, 255, brightness);
    }
}

void sparkle() {
    fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 80);
    
    for(int i = 0; i < 3; i++) {
        if(random8() < 60) {
            int pos = random16(NUM_MAIN_LEDS);
            mainLeds[pos] = CHSV(random8() % 2 ? primaryHue : secondaryHue, 180, 255);
        }
    }
}

void gradient() {
    fill_gradient(mainLeds, NUM_MAIN_LEDS, 
                  CHSV(primaryHue, 255, 255), 
                  CHSV(secondaryHue, 255, 255));
}

void dots() {
    fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 20);
    
    int pos1 = beatsin16(13, 0, NUM_MAIN_LEDS - 1);
    int pos2 = beatsin16(17, 0, NUM_MAIN_LEDS - 1);
    
    // KRITISCH: Direkte Zuweisung statt += um Überläufe zu vermeiden (wie bei confetti)
    // += kann bei bereits hellen LEDs zu Überläufen führen
    if (mainLeds[pos1].getLuma() < 100) {  // Nur setzen wenn LED dunkel genug ist
        mainLeds[pos1] = CHSV(primaryHue, 255, 192);
    } else {
        // Wenn LED bereits hell ist, sanft blenden
        CRGB newColor = CHSV(primaryHue, 255, 128);  // Helligkeit reduziert
        mainLeds[pos1] = blend(mainLeds[pos1], newColor, 128);
    }
    
    if (mainLeds[pos2].getLuma() < 100) {  // Nur setzen wenn LED dunkel genug ist
        mainLeds[pos2] = CHSV(secondaryHue, 255, 192);
    } else {
        // Wenn LED bereits hell ist, sanft blenden
        CRGB newColor = CHSV(secondaryHue, 255, 128);  // Helligkeit reduziert
        mainLeds[pos2] = blend(mainLeds[pos2], newColor, 128);
    }
}

void comet() {
    fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 20);
    
    static int pos = 0;
    static bool useSecondary = false;
    
    // KRITISCH: Reset-Mechanismus gegen Überlauf
    static unsigned long lastReset = 0;
    if (millis() - lastReset > 40000) { // Reset alle 40 Sekunden
        pos = 0;
        useSecondary = false;
        lastReset = millis();
    }
    
    uint8_t hue = useSecondary ? secondaryHue : primaryHue;
    mainLeds[pos] = CHSV(hue, 255, 255);
    
    pos++;
    if(pos >= NUM_MAIN_LEDS) {
        pos = 0;
        useSecondary = !useSecondary;
    }
}

void bounce() {
    static int pos = 0;
    static int direction = 1;
    static bool useSecondary = false;
    
    // KRITISCH: Reset-Mechanismus gegen Überlauf
    static unsigned long lastReset = 0;
    if (millis() - lastReset > 35000) { // Reset alle 35 Sekunden
        pos = 0;
        direction = 1;
        useSecondary = false;
        lastReset = millis();
    }
    
    fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 20);
    
    uint8_t hue = useSecondary ? secondaryHue : primaryHue;
    mainLeds[pos] = CHSV(hue, 255, 255);
    
    pos += direction;
    if(pos >= NUM_MAIN_LEDS) {
        direction = -1;
        pos = NUM_MAIN_LEDS - 1;
        useSecondary = !useSecondary;
    }
    if(pos < 0) {
        direction = 1;
        pos = 0;
        useSecondary = !useSecondary;
    }
}

void fireworks() {
    static struct {
        int pos;
        int brightness;
        bool active;
        uint8_t hue;
    } sparks[5] = {0};
    
    // KRITISCH: Bounds-Checking für Array-Zugriffe
    static unsigned long lastReset = 0;
    if (millis() - lastReset > 60000) { // Reset alle 60 Sekunden
        for(int i = 0; i < 5; i++) {
            sparks[i].active = false;
            sparks[i].brightness = 0;
        }
        lastReset = millis();
    }
    
    fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 30);
    
    for(int i = 0; i < 5; i++) {
        if(sparks[i].active) {
            // Bounds-Check für LED-Position
            if(sparks[i].pos >= 0 && sparks[i].pos < NUM_MAIN_LEDS) {
                mainLeds[sparks[i].pos] = CHSV(sparks[i].hue, 255, sparks[i].brightness);
            }
            sparks[i].brightness -= 10;
            if(sparks[i].brightness <= 0) {
                sparks[i].active = false;
            }
        } else if(random8() < 20) {
            sparks[i].pos = random16(NUM_MAIN_LEDS);
            sparks[i].brightness = 255;
            sparks[i].active = true;
            sparks[i].hue = random8() % 2 ? primaryHue : secondaryHue;
        }
    }
}

void lightning() {
    static uint8_t flashCounter = 0;
    static uint8_t flashBrightness = 0;
    static uint16_t nextFlashTime = 0;
    
    // KRITISCH: Reset-Mechanismus gegen Überlauf
    static unsigned long lastReset = 0;
    if (millis() - lastReset > 30000) { // Reset alle 30 Sekunden
        flashCounter = 0;
        flashBrightness = 0;
        nextFlashTime = 0;
        lastReset = millis();
    }
    
    if (flashCounter > 0) {
        for (int i = 0; i < NUM_MAIN_LEDS; i++) {
            mainLeds[i] = CHSV(primaryHue, 50, flashBrightness);
        }
        
        if (flashCounter % 2 == 0) {
            flashBrightness = 255;
        } else {
            flashBrightness = 50;
        }
        
        flashCounter--;
    } else {
        fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 90);
        
        if (nextFlashTime == 0) {
            nextFlashTime = random16(50, 1000);
        } else if (nextFlashTime == 1) {
            flashCounter = random8(2, 8) * 2;
            flashBrightness = 255;
            nextFlashTime = 0;
        } else {
            nextFlashTime--;
        }
    }
}

void confetti() {
    // KRITISCH: Rate-Limiting für Konfetti um FastLED-Überlastung zu vermeiden
    static unsigned long lastConfettiUpdate = 0;
    unsigned long currentTime = millis();
    
    // Mindestens 5ms zwischen Konfetti-Updates (200 FPS Maximum)
    if (currentTime - lastConfettiUpdate < 5) {
        return; // Skip this update
    }
    lastConfettiUpdate = currentTime;
    
    fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 20);
    
    int pos = random16(NUM_MAIN_LEDS);
    uint8_t hue = random8() % 2 ? primaryHue : secondaryHue;
    
    // KRITISCH: Hue-Überlauf verhindern durch sichere Addition
    int16_t hueOffset = random8(32) - 16;  // -16 bis +15
    hue = (hue + hueOffset + 256) % 256;   // Sichere Modulo-Operation
    
    // KRITISCH: Direkte Zuweisung statt += um Überläufe zu vermeiden
    // += kann bei bereits hellen LEDs zu Überläufen führen
    if (mainLeds[pos].getLuma() < 100) {  // Nur setzen wenn LED dunkel genug ist
        mainLeds[pos] = CHSV(hue, 200, 255);
    } else {
        // Wenn LED bereits hell ist, sanft addieren
        CRGB newColor = CHSV(hue, 200, 128);  // Helligkeit reduziert
        mainLeds[pos] = blend(mainLeds[pos], newColor, 128);
    }
}

void breathe() {
    static uint8_t brightness = 0;
    static int8_t direction = 1;
    static bool useSecondary = false;
    
    // KRITISCH: Reset-Mechanismus gegen Überlauf
    static unsigned long lastReset = 0;
    if (millis() - lastReset > 40000) { // Reset alle 40 Sekunden
        brightness = 0;
        direction = 1;
        useSecondary = false;
        lastReset = millis();
    }
    
    brightness += direction * 1;
    
    if (brightness >= 170) {
        direction = -1;
    } else if (brightness <= 10) {
        direction = 1;
        useSecondary = !useSecondary;
    }
    
    uint8_t hue = useSecondary ? secondaryHue : primaryHue;
    fill_solid(mainLeds, NUM_MAIN_LEDS, CHSV(hue, 255, brightness));
}

void rain() {
    fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 30);
    
    static uint8_t drops[10] = {0};
    static bool dropColors[10] = {false};
    
    // KRITISCH: Reset-Mechanismus gegen Array-Überlauf
    static unsigned long lastReset = 0;
    if (millis() - lastReset > 45000) { // Reset alle 45 Sekunden
        for(int i = 0; i < 10; i++) {
            drops[i] = 0;
            dropColors[i] = false;
        }
        lastReset = millis();
    }
    
    for (int i = 0; i < 10; i++) {
        if (drops[i] > 0) {
            drops[i]++;
            if (drops[i] > NUM_MAIN_LEDS + 5) { // Erweiterte Bounds
                drops[i] = 0;
            } else if (drops[i] <= NUM_MAIN_LEDS) {
                uint8_t hue = dropColors[i] ? secondaryHue : primaryHue;
                mainLeds[drops[i] - 1] = CHSV(hue, 255, 255);
            }
        } else if (random8() < 15) {
            drops[i] = 1;
            dropColors[i] = random8() % 2;
        }
    }
}

void matrix() {
    fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 30);
    
    static uint8_t drops[8] = {0};
    static uint8_t dropBrightness[8] = {0};
    
    // KRITISCH: Reset-Mechanismus gegen Array-Überlauf
    static unsigned long lastReset = 0;
    if (millis() - lastReset > 40000) { // Reset alle 40 Sekunden
        for(int i = 0; i < 8; i++) {
            drops[i] = 0;
            dropBrightness[i] = 0;
        }
        lastReset = millis();
    }
    
    for (int i = 0; i < 8; i++) {
        if (drops[i] > 0) {
            drops[i]++;
            
            if (dropBrightness[i] > 10) {
                dropBrightness[i] -= 10;
            }
            
            for (int j = 0; j < 5; j++) {
                int pos = drops[i] - j - 1;
                if (pos >= 0 && pos < NUM_MAIN_LEDS) {
                    uint8_t tailBrightness = dropBrightness[i] > j*50 ? dropBrightness[i] - j*50 : 0;
                    mainLeds[pos] = CHSV(primaryHue, 255, tailBrightness);
                }
            }
            
            if (drops[i] > NUM_MAIN_LEDS + 10 || dropBrightness[i] <= 10) {
                drops[i] = 0;
            }
        } else if (random8() < 10) {
            drops[i] = 1;
            dropBrightness[i] = 255;
        }
    }
}

void orbit() {
    fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 20);
    
    uint8_t center = NUM_MAIN_LEDS / 2;
    mainLeds[center] = CHSV(primaryHue, 200, 255);
    
    for (int i = 0; i < 3; i++) {
        int orbitRadius = 5 + i * 3;
        int speed = 5 + i;
        
        float angle = (millis() / (100.0 / speed)) * PI / 180.0;
        if (i % 2 == 1) angle = -angle;
        
        int xOffset = orbitRadius * sin(angle);
        int pos = center + xOffset;
        
        if (pos >= 0 && pos < NUM_MAIN_LEDS) {
            uint8_t hue = (i % 2 == 0) ? primaryHue : secondaryHue;
            mainLeds[pos] = CHSV(hue, 255, 255);
        }
    }
}

void spiral() {
    for(int i = 0; i < NUM_MAIN_LEDS; i++) {
        int timeFactor = millis() / 50;
        int posFactor = i * 7;
        int combinedFactor = (timeFactor + posFactor) % 256;
        
        uint8_t brightnessFactor = sin8(combinedFactor);
        uint8_t hue = map(combinedFactor % 128, 0, 127, primaryHue, secondaryHue);
        mainLeds[i] = CHSV(hue, 255, brightnessFactor);
    }
}

void meteor() {
    fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 20);
    
    static struct {
        int pos;
        int speed;
        int length;
        bool active;
        bool useSecondary;
    } meteors[3] = {0};
    
    // KRITISCH: Reset-Mechanismus gegen Array-Überlauf
    static unsigned long lastReset = 0;
    if (millis() - lastReset > 50000) { // Reset alle 50 Sekunden
        for(int i = 0; i < 3; i++) {
            meteors[i].active = false;
            meteors[i].pos = 0;
            meteors[i].speed = 0;
            meteors[i].length = 0;
        }
        lastReset = millis();
    }
    
    for (int i = 0; i < 3; i++) {
        if (meteors[i].active) {
            meteors[i].pos -= meteors[i].speed;
            
            for (int j = 0; j < meteors[i].length; j++) {
                int pos = meteors[i].pos + j;
                if (pos >= 0 && pos < NUM_MAIN_LEDS) {
                    uint8_t tailBrightness = 255 - (255 * j / meteors[i].length);
                    uint8_t hue = meteors[i].useSecondary ? secondaryHue : primaryHue;
                    mainLeds[pos] = CHSV(hue, 255, tailBrightness);
                }
            }
            
            if (meteors[i].pos + meteors[i].length < -5) { // Erweiterte Bounds
                meteors[i].active = false;
            }
        } else if (random8() < 5) {
            meteors[i].pos = NUM_MAIN_LEDS;
            meteors[i].speed = random8(1, 5);
            meteors[i].length = random8(5, 15);
            meteors[i].active = true;
            meteors[i].useSecondary = random8() % 2;
        }
    }
}

void colorSpin() {
    fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 20);
    
    static uint8_t pos = 0;
    
    // KRITISCH: Reset-Mechanismus gegen Überlauf
    static unsigned long lastReset = 0;
    if (millis() - lastReset > 35000) { // Reset alle 35 Sekunden
        pos = 0;
        lastReset = millis();
    }
    
    int blockLength = NUM_MAIN_LEDS / 4;
    
    for (int i = 0; i < blockLength; i++) {
        int currentPos = (pos + i) % NUM_MAIN_LEDS;
        uint8_t hue = (i < blockLength/2) ? primaryHue : secondaryHue;
        mainLeds[currentPos] = CHSV(hue, 255, 255);
    }
    
    pos = (pos + 1) % NUM_MAIN_LEDS;
}

void musicReactive() {
    uint16_t sample = 0;
    
    for(int i = 0; i < SAMPLES; i++) {
        sample += analogRead(MIC_PIN);
        delayMicroseconds(micFrequency);
    }
    sample /= SAMPLES;

    if(sample < NOISE_LEVEL) {
        sample = 0;
    }

    int brightness = map(sample, 0, 4095, 0, 255);
    
    for(int i = 0; i < NUM_MAIN_LEDS; i++) {
        mainLeds[i] = CHSV(primaryHue, 255, brightness);
    }
}

void addGlitter(uint8_t chance) {
    if(random8() < chance) {
        int pos = random16(NUM_MAIN_LEDS);
        
        // KRITISCH: Direkte Zuweisung statt += um Überläufe zu vermeiden
        // += kann bei bereits hellen LEDs zu Überläufen führen
        if (mainLeds[pos].getLuma() < 200) {  // Nur setzen wenn LED nicht zu hell ist
            mainLeds[pos] = CRGB::White;
        } else {
            // Wenn LED bereits hell ist, sanft blenden
            mainLeds[pos] = blend(mainLeds[pos], CRGB::White, 128);
        }
    }
}

// Funktion zur Initialisierung des SAO-Tasters
void initSaoButton() {
    pinMode(SAO_BUTTON, INPUT); // Ändere auf die korrekte Definition (kein PULLUP)
    Serial.println("SAO-Taster initialisiert");
}

// Funktion zur Abfrage des SAO-Tasters
void checkSaoButton() {
    // Taster: HIGH wenn gedrückt, LOW wenn nicht gedrückt (ohne Pullup)
    static bool lastButtonState = LOW;
    static unsigned long buttonPressStartTime = 0;
    static bool longPressDetected = false;
    bool currentButtonState = digitalRead(SAO_BUTTON);
    
    // Wenn der Taster gedrückt wurde (Zustandswechsel von LOW zu HIGH)
    if (lastButtonState == LOW && currentButtonState == HIGH) {
        // Startzeit speichern
        buttonPressStartTime = millis();
        longPressDetected = false;
        
        // LED-Indikator aktivieren (für 1 Sekunde)
        saoLedActive = true;
        saoLedOffTime = millis() + 1000;
    } 
    // Wenn der Taster gehalten wird
    else if (lastButtonState == HIGH && currentButtonState == HIGH) {
        // Lange Tastendruckdauer prüfen (1 Sekunde)
        if (!longPressDetected && millis() - buttonPressStartTime > 1000) {
            longPressDetected = true;
            
            // LEDs ein-/ausschalten
            extern bool ledsEnabled; // Variable aus WebUI.cpp
            ledsEnabled = !ledsEnabled;
            
            if (ledsEnabled) {
                // LEDs einschalten
                digitalWrite(LIGHT_EN, HIGH); // GPIO35 auf HIGH setzen wenn LEDs an
                FastLED[0].showLeds(BRIGHTNESS); // Hauptleds mit normaler Helligkeit
                Serial.println("SAO-Button: LEDs eingeschaltet");
            } else {
                // LEDs ausschalten
                digitalWrite(LIGHT_EN, LOW); // GPIO35 auf LOW setzen wenn LEDs aus
                FastLED[0].showLeds(0); // Hauptleds ausschalten
                Serial.println("SAO-Button: LEDs ausgeschaltet");
            }
            
            // Sofort aktualisieren - nicht mehr notwendig, da bereits in showLeds() enthalten
        }
    }
    // Wenn der Taster losgelassen wurde (Zustandswechsel von HIGH zu LOW)
    else if (lastButtonState == HIGH && currentButtonState == LOW) {
        unsigned long pressDuration = millis() - buttonPressStartTime;
        
        // Nur bei kurzem Tastendruck (< 1 Sekunde) und nicht nach einem erkannten langen Tastendruck
        if (pressDuration < 1000 && !longPressDetected) {
            unsigned long currentTime = millis();
            
            // Entprellen
            if (currentTime - lastSaoButtonPress > debounceTime) {
                lastSaoButtonPress = currentTime;
                
                // Prüfen ob niedrige Batteriespannung - dann kein Display-Update
                if (isLowBattery) {
                    Serial.println("SAO-Taster kurz gedrückt - Display-Update wegen niedriger Batterie abgebrochen");
                } else {
                    Serial.println("SAO-Taster kurz gedrückt - Display-Update wird gestartet");
                
                    // Display aktualisieren
                    updateDisplayOnButtonPress();
                }
            }
        }
    }
    
    lastButtonState = currentButtonState;
}

// OTA Funktionen
void connectToWiFi() {
    if (!wifiConnectEnabled) {
        Serial.println("WiFi-Verbindung ist deaktiviert");
        return;
    }
    
    Serial.println("🌐 Verbinde mit WiFi...");
    Serial.printf("🌐 SSID: '%s'\n", wifiSSID.c_str());
    Serial.printf("🌐 Passwort: %s\n", wifiPassword.length() > 0 ? "[GESETZT]" : "[LEER]");
    
    // WiFi Mode sicher setzen
    WiFi.mode(WIFI_STA);
    delay(100); // Kurze Verzögerung nach Mode-Wechsel
    
    // Alte Verbindung trennen falls vorhanden
    WiFi.disconnect(false);
    delay(100);
    
    // Neue Verbindung starten
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    
    Serial.print("🌐 Verbinde mit WLAN");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 25) { // Erhöhte Versuche: 25 statt 20
        delay(500);
        Serial.print(".");
        attempts++;
        
        // Debug: WiFi Status während Verbindungsversuch
        if (attempts % 5 == 0) {
            Serial.printf("\n🌐 Verbindungsversuch %d/25, Status: %d\n", attempts, WiFi.status());
        }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.printf("✅ ERFOLGREICH verbunden mit: '%s'\n", WiFi.SSID().c_str());
        Serial.printf("✅ IP-Adresse: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("✅ Signal-Stärke: %d dBm\n", WiFi.RSSI());
        
        // mDNS starten für badge.local
        if (MDNS.begin("badge")) {
            Serial.println("✅ mDNS gestartet! Badge erreichbar unter: badge.local");
        } else {
            Serial.println("⚠️ Warnung: mDNS konnte nicht gestartet werden");
        }
    } else {
        Serial.println();
        Serial.printf("❌ WiFi-Verbindung FEHLGESCHLAGEN nach %d Versuchen\n", attempts);
        Serial.printf("❌ Letzter WiFi Status: %d\n", WiFi.status());
        Serial.printf("❌ Überprüfe SSID: '%s' und Passwort\n", wifiSSID.c_str());
    }
}

void performOTACheckin() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Keine WiFi-Verbindung für OTA-Checkin");
        return;
    }
    
    HTTPClient http;
    http.begin(otaCheckinUrl);
    http.addHeader("Content-Type", "application/json");
    
    String json = "{\"id\":\"" + String(deviceID) + "\",\"version\":\"" + dynamicVersion + "\",\"hardware_type\":\"" + String(hardwaretype) + "\",\"ssid\":\"" + WiFi.SSID() + "\"}";
    Serial.println("=== OTA CHECKIN DEBUG ===");
    Serial.printf("Device ID: %s\n", deviceID.c_str());
    Serial.printf("Firmware Version (fest): %s\n", firmwareVersion);
    Serial.printf("Dynamic Version (wird gesendet): '%s'\n", dynamicVersion.c_str());
    Serial.printf("Settings Dynamic Version: '%s'\n", settings.dynamicVersion.c_str());
    Serial.printf("Hardware Type: %s\n", hardwaretype);
    Serial.println("JSON-Payload:");
    Serial.println(json);
    Serial.println("========================");
    
    int httpResponseCode = http.POST(json);
    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.printf("Antwort [%d]: %s\n", httpResponseCode, response.c_str());
        
        // JSON-Antwort parsen
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, response);
        
        if (!error) {
            Serial.println("JSON erfolgreich geparst:");
            serializeJsonPretty(doc, Serial);
            Serial.println();
            
            // Prüfe auf Version-Update vom Server
            if (doc.containsKey("set_version")) {
                String newVersion = doc["set_version"].as<String>();
                Serial.printf("=== VERSION UPDATE VOM SERVER ===\n");
                Serial.printf("Alte Version: '%s'\n", dynamicVersion.c_str());
                Serial.printf("Neue Version: '%s'\n", newVersion.c_str());
                Serial.printf("Firmware Version (fest): '%s'\n", firmwareVersion);
                
                // Aktualisiere dynamische Version
                dynamicVersion = newVersion;
                settings.dynamicVersion = dynamicVersion;
                
                // Sofort speichern und mehrfach versuchen bei Fehlern
                bool saved = false;
                for (int i = 0; i < 3; i++) {
                    saved = saveSettings();
                    if (saved) break;
                    delay(100);
                }
                Serial.printf("Settings gespeichert (Versuch %d): %s\n", saved ? 1 : 3, saved ? "ERFOLG" : "FEHLER");
                
                // Verification: Lade Settings neu und prüfe
                delay(100); // Kurze Pause
                if (loadSettings()) {
                    Serial.printf("Verification: Geladene dynamicVersion = '%s'\n", settings.dynamicVersion.c_str());
                    dynamicVersion = settings.dynamicVersion; // Sicherstellen, dass globale Variable korrekt ist
                    Serial.printf("Verification: Globale dynamicVersion = '%s'\n", dynamicVersion.c_str());
                    
                    // Zusätzliche Prüfung
                    if (dynamicVersion == newVersion) {
                        Serial.println("✓ Version-Update erfolgreich!");
                    } else {
                        Serial.printf("✗ FEHLER: Version-Update fehlgeschlagen! Erwartet: '%s', Ist: '%s'\n", 
                                    newVersion.c_str(), dynamicVersion.c_str());
                    }
                } else {
                    Serial.println("FEHLER: Settings konnten nicht neu geladen werden!");
                }
                Serial.println("=== VERSION UPDATE ABGESCHLOSSEN ===");
            }
            
            // Prüfe auf Update-Antwort
            if (doc.containsKey("update") && doc["update"] == true) {
                Serial.println("Update verfügbar - starte Download...");
                startStatusLedUpdate(); // Status-LED Feedback starten
                
                if (doc.containsKey("url")) {
                    String firmwareUrl = doc["url"].as<String>();
                    Serial.println("Firmware-URL: " + firmwareUrl);
                    updateFirmware(firmwareUrl);
                } else {
                    Serial.println("Fehler: URL nicht in Antwort gefunden");
                }
            } 
            // Prüfe auf Reboot-Antwort
            else if (doc.containsKey("reboot") && doc["reboot"] == true) {
                Serial.println("Reboot-Befehl vom OTA Server empfangen!");
                startStatusLedReboot(); // Status-LED Feedback starten
                Serial.println("Starte Neustart in 2 Sekunden...");
                
                // Kurze Verzögerung für die Antwort
                delay(2000);
                ESP.restart();
            } 
            // Prüfe auf Lock-Antwort
            else if (doc.containsKey("locked") && doc["locked"] == true) {
                Serial.println("Gerät ist gesperrt - keine Updates möglich");
                startStatusLedLock(); // Status-LED Feedback starten
            }
            // Prüfe auf Unlock-Antwort
            else if (doc.containsKey("unlocked") && doc["unlocked"] == true) {
                Serial.println("Gerät ist entsperrt - Updates wieder möglich");
                startStatusLedUnlock(); // Status-LED Feedback starten
            }
            else {
                Serial.println("Kein Update verfügbar");
            }
        } else {
            Serial.print("JSON-Parsing fehlgeschlagen: ");
            Serial.println(error.c_str());
            
            // Fallback: String-basierte Suche
            if (response.indexOf("\"update\":true") != -1) {
                Serial.println("Update verfügbar (Fallback) - starte Download...");
                
                // Extrahiere die Firmware-URL aus der Antwort
                int urlStart = response.indexOf("\"url\":\"");
                if (urlStart != -1) {
                    urlStart += 7; // Länge von "url":"
                    int urlEnd = response.indexOf("\"", urlStart);
                    if (urlEnd != -1) {
                        String firmwareUrl = response.substring(urlStart, urlEnd);
                        Serial.println("Firmware-URL: " + firmwareUrl);
                        updateFirmware(firmwareUrl);
                    }
                }
            } else if (response.indexOf("\"reboot\":true") != -1) {
                Serial.println("Reboot-Befehl empfangen (Fallback)!");
                Serial.println("Starte Neustart in 2 Sekunden...");
                delay(2000);
                ESP.restart();
            } else {
                Serial.println("Kein Update verfügbar");
            }
        }
    } else {
        Serial.printf("Fehler bei HTTP POST: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    
    http.end();
}

void updateFirmware(String url) {
    Serial.println("=== STARTE FIRMWARE UPDATE ===");
    Serial.println("URL: " + url);
    
    // Prüfe ob es eine HTTPS-URL ist
    bool isHttps = url.startsWith("https://");
    
    if (isHttps) {
        Serial.println("HTTPS-Update wird nicht unterstützt. Verwende HTTP...");
        // Konvertiere HTTPS zu HTTP
        url = "http://" + url.substring(8);
        Serial.println("Konvertierte URL: " + url);
    }
    
    WiFiClient client;
    HTTPClient http;
    http.begin(client, url);
    
    // Timeout für den Download setzen
    http.setTimeout(30000); // 30 Sekunden Timeout
    
    Serial.println("Starte Download...");
    int httpCode = http.GET();
    Serial.printf("HTTP Response Code: %d\n", httpCode);
    
    if (httpCode == 200) {
        int contentLength = http.getSize();
        Serial.printf("Firmware-Größe: %d bytes\n", contentLength);
        
        if (contentLength <= 0) {
            Serial.println("Fehler: Ungültige Firmware-Größe");
            http.end();
            return;
        }
        
        // Prüfe ob genug Speicher verfügbar ist (ESP32-S3 mit 8MB Flash)
        if (contentLength > 0x400000) { // 4MB Limit (sehr großzügig für ESP32-S3)
            Serial.printf("Fehler: Firmware zu groß (%d bytes, Limit: %d bytes)\n", contentLength, 0x400000);
            http.end();
            return;
        }
        
        Serial.printf("Firmware-Größe OK: %d bytes (Limit: %d bytes)\n", contentLength, 0x400000);
        
        Serial.printf("Freier Heap vor Update: %d bytes\n", ESP.getFreeHeap());
        
        // Zusätzliche Debug-Informationen
        Serial.printf("Update-Partition verfügbar: %s\n", Update.canRollBack() ? "Ja" : "Nein");
        Serial.printf("Verfügbarer Update-Speicher: %d bytes\n", ESP.getFreeSketchSpace());
        
        bool canBegin = Update.begin(contentLength);
        Serial.printf("Update.begin() Ergebnis: %s\n", canBegin ? "ERFOLG" : "FEHLGESCHLAGEN");
        
        if (!canBegin) {
            Serial.printf("Update.begin() Fehler-Code: %d\n", Update.getError());
            Serial.printf("Update.begin() Fehler-String: %s\n", Update.errorString());
        }
        
        if (canBegin) {
            Serial.println("Update beginnt...");
            Serial.printf("Update-Rückgabewert vor Stream: %d\n", Update.getError());
            
            // Stream-basiertes Update mit Fortschrittsanzeige
            WiFiClient *stream = http.getStreamPtr();
            size_t written = 0;
            uint8_t buffer[1024];
            
            while (http.connected() && (written < contentLength)) {
                size_t available = stream->available();
                if (available) {
                    size_t readBytes = stream->readBytes(buffer, min(available, sizeof(buffer)));
                    size_t writtenNow = Update.write(buffer, readBytes);
                    written += writtenNow;
                    
                    // Fortschritt alle 10KB anzeigen
                    if (written % 10240 == 0 || written == contentLength) {
                        Serial.printf("Fortschritt: %d/%d bytes (%.1f%%)\n", 
                                    written, contentLength, (written * 100.0) / contentLength);
                    }
                    
                    if (writtenNow != readBytes) {
                        Serial.printf("FEHLER: Nur %d von %d Bytes geschrieben!\n", writtenNow, readBytes);
                        break;
                    }
                } else {
                    delay(1);
                }
            }
            
            Serial.printf("Stream-Update abgeschlossen: %d von %d bytes\n", written, contentLength);
            
            if (written == contentLength) {
                Serial.println("Alle Bytes geschrieben, finalisiere Update...");
                Serial.printf("Update-Status vor end(): Fehler=%d\n", Update.getError());
                
                bool endResult = Update.end();
                bool isFinished = Update.isFinished();
                Serial.printf("Update.end(): %s, Update.isFinished(): %s\n", 
                            endResult ? "ERFOLG" : "FEHLGESCHLAGEN",
                            isFinished ? "ERFOLG" : "FEHLGESCHLAGEN");
                
                if (!endResult) {
                    Serial.printf("Update.end() Fehler-Code: %d\n", Update.getError());
                    Serial.printf("Update.end() Fehler-String: %s\n", Update.errorString());
                }
                
                if (endResult && isFinished) {
                    Serial.println("=== UPDATE ERFOLGREICH ===");
                    
                    // WICHTIG: Nach erfolgreichem Update wird die neue Firmware gestartet
                    // Die dynamicVersion wird automatisch auf die neue firmwareVersion gesetzt
                    // beim nächsten Start in setup()
                    
                    startStatusLedUpdate(); // Status-LED Feedback für erfolgreiches Update
                    delay(2000); // Länger warten für LED-Feedback
                    Serial.println("Starte Neustart...");
                    ESP.restart();
                } else {
                    Serial.println("Update fehlgeschlagen beim Finalisieren.");
                    Serial.print("Update-Fehler: ");
                    Serial.println(Update.errorString());
                    Serial.printf("Update-Rückgabewert: %d\n", Update.getError());
                    
                    // Rote LED für fehlgeschlagenes Update
                    if (xSemaphoreTake(statusLedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        for (int i = 0; i < NUM_STATUS_LEDS; i++) {
                            statusLeds[i] = CRGB::Red;
                        }
                        FastLED[1].showLeds(STATUS_LED_BRIGHTNESS);
                        xSemaphoreGive(statusLedMutex);
                    }
                }
            } else {
                Serial.printf("Fehler: Nur %d von %d Bytes geschrieben\n", written, contentLength);
                Serial.printf("Update-Fehler beim Schreiben: %d\n", Update.getError());
                Serial.printf("Update-Fehler-String: %s\n", Update.errorString());
                Update.abort();
                
                // Rote LED für fehlgeschlagenes Update
                if (xSemaphoreTake(statusLedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    for (int i = 0; i < NUM_STATUS_LEDS; i++) {
                        statusLeds[i] = CRGB::Red;
                    }
                    FastLED[1].showLeds(STATUS_LED_BRIGHTNESS);
                    xSemaphoreGive(statusLedMutex);
                }
            }
        } else {
            Serial.println("Update konnte nicht gestartet werden");
            Serial.print("Update-Fehler: ");
            Serial.println(Update.errorString());
            Serial.printf("Update-Rückgabewert: %d\n", Update.getError());
        }
    } else {
        Serial.printf("HTTP-Fehler beim Download: %d\n", httpCode);
        Serial.printf("Fehler-String: %s\n", http.errorToString(httpCode).c_str());
    }
    
    Serial.println("=== UPDATE BEENDET ===");
    http.end();
}

void checkForOTAUpdate() {
    if (!wifiConnectEnabled || !settings.otaEnabled) {
        Serial.println("OTA-Updates deaktiviert (WiFi: " + String(wifiConnectEnabled) + ", OTA: " + String(settings.otaEnabled) + ")");
        return;
    }
    
    // Prüfe WiFi-Verbindung und versuche Wiederverbindung bei Bedarf
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WLAN getrennt – versuche erneut zu verbinden...");
        WiFi.reconnect();
        delay(2000); // Warte auf Verbindung
        
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Wiederverbindung fehlgeschlagen");
            return;
        }
    }
    
    // Führe OTA-Checkin durch
    performOTACheckin();
}

void setup() {
    // Serial für Debugging
    Serial.begin(115200);
    delay(50); // Kurze Verzögerung für die Stabilisierung der seriellen Verbindung
    Serial.println("Starte LED Badge...");
    
    // Startzeit speichern
    startupTime = millis();
    
    // MAC-Adresse als Device-ID setzen
    String macAddress = WiFi.macAddress();
    deviceID = "Badge-" + macAddress.substring(12, 17); // Letzte 5 Zeichen der MAC
    Serial.println("Device-ID: " + deviceID);
    
    // Watchdog-Timer auf längere Zeit setzen für mehr Stabilität
    Serial.println("Konfiguriere Watchdog...");
    // Hinweis: esp_task_wdt_init ist nicht verfügbar, wir nutzen stattdessen 
    // die Stabilisierungsverzögerungen an kritischen Stellen
    
    // WiFi initialisieren
    Serial.println("WiFi initialisieren...");
    WiFi.persistent(false);  // Verhindert häufiges Schreiben in den Flash
    
    // WiFi-Modus basierend auf Konfiguration setzen
    if (wifiConnectEnabled) {
        WiFi.mode(WIFI_STA);  // Nur Station-Modus (Client) für Test
    } else {
        WiFi.mode(WIFI_AP);      // Nur Access Point Modus
    }
    
    WiFi.setAutoReconnect(true);  // Auto-Reconnect aktivieren für bessere Station Mode Verbindung
    
    // BQ25895 initialisieren
    Serial.println("BQ25895 initialisieren...");
    ensureChargerInitialized();
    
    try {
        // Ladegerät initialisieren
        if (charger != nullptr) {
            charger->begin();
            delay(50);
            Serial.println("BQ25895 initialisiert.");
        } else {
            Serial.println("BQ25895 nicht verfügbar!");
        }
    } catch (...) {
        Serial.println("Fehler bei der BQ25895-Initialisierung, aber fortfahren...");
    }
    
    // SPIFFS initialisieren
    Serial.println("SPIFFS initialisieren...");
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        // Trotzdem fortfahren
    }

    // Einstellungen laden
    Serial.println("Einstellungen laden...");
    if (!loadSettings()) {
        Serial.println("Fehler beim Laden der Einstellungen");
        // Fortfahren mit Standard-Einstellungen
    }

    // Globale Variablen aus Einstellungen setzen
    currentMode = settings.mode;
    brightness = settings.brightness;  // Verwende globale brightness
    BRIGHTNESS = settings.brightness;  // Behalte lokale BRIGHTNESS für Kompatibilität
    NOISE_LEVEL = settings.noiseLevel;
    animSpeed = settings.animationSpeed;  // Verwende globale animSpeed
    animationSpeed = settings.animationSpeed;  // Behalte lokale für Kompatibilität
    micFrequency = settings.micFrequency;
    primaryHue = settings.primaryHue;      // KRITISCH: Aus Settings laden, nicht überschreiben!
    secondaryHue = settings.secondaryHue;  // KRITISCH: Aus Settings laden, nicht überschreiben!
    apName = settings.apName;
    apPassword = settings.apPassword;
    
    // Debug: Geladene Werte ausgeben
    Serial.printf("=== Geladene Settings ===\n");
    Serial.printf("Mode: %d, Brightness: %d, Speed: %d\n", currentMode, brightness, animSpeed);
    Serial.printf("PrimärHue: %d, SekundärHue: %d\n", primaryHue, secondaryHue);
    Serial.printf("AP Name: %s\n", apName.c_str());

    // Dynamic Version initialisieren - IMMER auf aktuelle firmwareVersion setzen
    // Dies stellt sicher, dass nach einem neuen Upload die korrekte Version angezeigt wird
    String currentFirmwareVersion = String(firmwareVersion);
    
    Serial.printf("=== DYNAMIC VERSION INITIALISIERUNG ===\n");
    Serial.printf("Firmware Version (fest): '%s'\n", firmwareVersion);
    Serial.printf("Dynamic Version (alt): '%s'\n", settings.dynamicVersion.c_str());
    
    if (settings.dynamicVersion != currentFirmwareVersion) {
        Serial.printf("Version-Mismatch erkannt! Aktualisiere dynamicVersion von '%s' auf '%s'\n", 
                      settings.dynamicVersion.c_str(), currentFirmwareVersion.c_str());
        
        dynamicVersion = currentFirmwareVersion;
        settings.dynamicVersion = dynamicVersion;
        saveSettings(); // Speichere die neue Version
        Serial.printf("Dynamic Version aktualisiert auf: '%s'\n", dynamicVersion.c_str());
    } else {
        dynamicVersion = currentFirmwareVersion;
        Serial.printf("Dynamic Version ist bereits korrekt: '%s'\n", dynamicVersion.c_str());
    }
    
    Serial.printf("OTA aktiviert: %s\n", settings.otaEnabled ? "Ja" : "Nein");
    Serial.println("=========================================");
    
    // WiFi-Verbindung und OTA-Checkin (falls aktiviert)
    if (wifiConnectEnabled) {
        Serial.println("WiFi-Verbindung und OTA-Checkin...");
        connectToWiFi();
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("Führe OTA-Checkin durch...");
            checkForOTAUpdate();
        }
    } else {
        // Nur Access Point starten wenn WiFi-Verbindung deaktiviert ist
        Serial.println("Starte WiFi Access Point...");
        const char* password = apPassword.length() > 0 ? apPassword.c_str() : NULL;
        if (WiFi.softAP(apName.c_str(), password)) {
            Serial.print("Access Point gestartet: ");
            Serial.println(apName);
            wifiEnabled = true;
            
            // mDNS starten für badge.local
            if (MDNS.begin("badge")) {
                Serial.println("mDNS gestartet! Badge erreichbar unter: badge.local");
            } else {
                Serial.println("Fehler beim Starten von mDNS");
            }
        } else {
            Serial.println("Fehler beim Starten des Access Points");
        }
    }

    // Mutex erstellen
    Serial.println("Mutex erstellen...");
    ledMutex = xSemaphoreCreateMutex();
    statusLedMutex = xSemaphoreCreateMutex();
    
    // Alle LEDs zuerst auf schwarz/aus setzen
    Serial.println("LEDs initialisieren...");
    fill_solid(mainLeds, NUM_MAIN_LEDS, CRGB::Black);
    fill_solid(statusLeds, NUM_STATUS_LEDS, CRGB::Black);
    
    // LED-Streifen initialisieren - separate Controller für Main und Status LEDs
    Serial.println("FastLED initialisieren...");
    FastLED.addLeds<LED_TYPE, MAIN_LED_PIN, COLOR_ORDER>(mainLeds, NUM_MAIN_LEDS);
    FastLED.addLeds<LED_TYPE, STATUS_LED_PIN, COLOR_ORDER>(statusLeds, NUM_STATUS_LEDS);
    
    // Initialisiere beide Controller separat mit ihren eigenen Helligkeitswerten
    FastLED[0].showLeds(BRIGHTNESS); // Hauptleds mit Benutzereinstellung
    FastLED[1].showLeds(STATUS_LED_BRIGHTNESS); // Status LEDs mit fester Helligkeit
    
    // WiFi-Status setzen - ENTFERNT: Direkte LED-Manipulation
    // statusLedTask() übernimmt die komplette STATUS LED Kontrolle!
    if (wifiConnectEnabled && WiFi.status() == WL_CONNECTED) {
        wifiEnabled = true;
    } else {
        wifiEnabled = false;
    }
    
    // WiFi-Status merken
    lastWifiStatus = wifiEnabled;
    
    // Sicherstellen, dass alle Button-LED-Indikatoren ausgeschaltet sind
    modeLedActive = false;
    colorLedActive = false;
    brightLedActive = false;
    saoLedActive = false;
    
    // Status LEDs mit eigener Helligkeit aktualisieren
    FastLED[1].showLeds(STATUS_LED_BRIGHTNESS);
    
    // Status LED als Output konfigurieren
    Serial.println("GPIOs initialisieren...");
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);  // Rot = WiFi aus
    
    // Boot Button als Input mit Pullup konfigurieren
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    
    // Weitere Buttons als Input mit Pulldown konfigurieren
    pinMode(MODE_BUTTON, INPUT);
    pinMode(COLOR_BUTTON, INPUT);
    pinMode(BRIGHT_BUTTON, INPUT);
    pinMode(SAO_BUTTON, INPUT);
    pinMode(LIGHT_EN, OUTPUT);
    digitalWrite(LIGHT_EN, HIGH);

    
    // Display initialisieren
    Serial.println("Display initialisieren...");
    if (USE_DISPLAY) {
        // Prüfen, ob das Display verfügbar ist
        if (checkDisplayAvailable()) {
            Serial.println("E-Ink Display gefunden und initialisiert!");
            
            // Nur Display initialisieren, aber kein Update durchführen
            extern bool fullDisplayInitialized; // Zugriff auf die Variable in Display.cpp
            fullDisplayInitialized = true; // Markiere, dass das Display initialisiert wurde
            Serial.println("E-Ink Display initialisiert ohne Update - manuelles Update über WebUI nötig");
        } else {
            Serial.println("E-Ink Display konnte nicht initialisiert werden!");
        }
    }
    
    // LED Task erstellen
    Serial.println("LED Task erstellen...");
    xTaskCreate(ledTask, "LEDTask", 8192, NULL, 2, &ledTaskHandle);  // Mehr Speicher und höhere Priorität
    
    // Status LED Task erstellen
    Serial.println("Status LED Task erstellen...");
    xTaskCreate(statusLedTask, "StatusLEDTask", 4096, NULL, 1, &statusLedTaskHandle);
    
    // Kleine Verzögerung, um LEDs zu initialisieren
    delay(500);
    Serial.println("LEDs aktiv");
    
    // WebUI initialisieren
    Serial.println("WebUI initialisieren...");
    try {
        // Versuche, den Webserver zu initialisieren, mit Fehlerbehandlung
        initWebServer();
        Serial.println("WebUI Initialisierung gestartet");
    } catch (...) {
        Serial.println("Fehler bei WebUI-Initialisierung, überspringe...");
    }
    
    // SAO-Taster initialisieren
    initSaoButton();
    
    Serial.println("Setup abgeschlossen");
}

void loop() {
    try {
        // Boot Button prüfen
        static unsigned long lastButtonPress = 0;
        static bool buttonPressed = false;
        
        if (digitalRead(BOOT_BUTTON_PIN) == LOW) {  // Button ist gedrückt (LOW wegen Pullup)
            if (!buttonPressed) {
                buttonPressed = true;
                lastButtonPress = millis();
            } else if (millis() - lastButtonPress >= 3000) {  // 3 Sekunden gedrückt
                toggleWiFi();
                lastButtonPress = millis();  // Verhindert mehrfaches Umschalten
            }
        } else {
            buttonPressed = false;
        }
        
        // Weitere Buttons prüfen
        checkButtons();
        
        // Ladegerät-Update mit Fehlerbehandlung
        if (charger != nullptr) {
            try {
                charger->updatePowerMode();
                
                // Entferne automatisches Akku-Display-Update
                // Aktualisierung des Displays nur noch durch Tastendruck!
            } catch (...) {
                // Fehler ignorieren
            }
        }
        
        // SAO-Taster abfragen
        checkSaoButton();
        
        // Status-LED-Feedback für OTA-Befehle aktualisieren
        updateStatusLedFeedback();
        
        // KRITISCH: Batterie-Monitoring ausführen (war vorher nie aufgerufen!)
        updateStatusLED();
        
        // Periodische OTA-Prüfung (alle 60 Sekunden)
        static unsigned long lastOTACheck = 0;
        if (wifiConnectEnabled && settings.otaEnabled && (millis() - lastOTACheck > 60000)) { // 10 Sekunden
            Serial.println("Führe periodische OTA-Prüfung durch...");
            checkForOTAUpdate();
            lastOTACheck = millis();
        }
        
        // Loop läuft auf Core 1 und ist für den Webserver reserviert
        vTaskDelay(pdMS_TO_TICKS(100));
    } catch (...) {
        // Ignoriere alle Ausnahmen im Hauptloop, um Abstürze zu vermeiden
        Serial.println("Fehler in main loop, aber weiter laufen...");
        delay(1000);
    }
}

void updateStatusLED() {
    // Häufigere Akku-Spannung-Kontrolle - alle 2 Sekunden
    if (charger != nullptr && (millis() - lastBatteryCheck >= BATTERY_CHECK_INTERVAL)) {
        lastBatteryCheck = millis();
        
        // Mehrere Messungen durchführen für ein stabileres Ergebnis
        float voltage = 0;
        const int numSamples = 5; // Mehr Samples für bessere Genauigkeit
        int validSamples = 0;
        
        for (int i = 0; i < numSamples; i++) {
            float sample = charger->getVBAT();
            // Nur plausible Werte verwenden
            if (sample >= 2.5f && sample <= 4.5f) {
                voltage += sample;
                validSamples++;
            }
            delay(5); // Kurze Pause zwischen den Messungen
        }
        
        if (validSamples > 0) {
            voltage /= validSamples; // Durchschnitt der gültigen Messungen
            
            // Mit Debug-Output, um Spannungswerte zu sehen - alle 10 Sekunden
            static unsigned long lastVoltageLog = 0;
            if (millis() - lastVoltageLog > 10000) {
                lastVoltageLog = millis();
                Serial.printf("🔋 Batteriespannung: %.3fV (%d von %d Samples gültig)\n", voltage, validSamples, numSamples);
            }
            
            // Deep Sleep prüfen - kritische Batterieschwelle erreicht
            if (voltage <= DEEP_SLEEP_THRESHOLD) {
                // Prüfen, ob das Gerät am Ladegerät angeschlossen ist - wenn ja, KEIN Deep Sleep!
                bool isCharging = false;
                bool isVbusPresent = false;
                
                if (charger != nullptr) {
                    isVbusPresent = charger->isVBUSPresent();
                    
                    // Ladestatus aus Register 0x0B lesen (bits 5-7 enthalten Ladestatus)
                    uint8_t chargeStatus = charger->readRegister(0x0B);
                    isCharging = (chargeStatus != 0xFF) && ((chargeStatus & 0xE0) != 0);
                    
                    Serial.print("🔌 Ladestatus: VBUS = ");
                    Serial.print(isVbusPresent ? "Ja" : "Nein");
                    Serial.print(", Ladevorgang = ");
                    Serial.println(isCharging ? "Ja" : "Nein");
                }
                
                // Wenn das Gerät NICHT am Ladegerät angeschlossen ist, dann Deep Sleep
                if (!isVbusPresent && !isCharging) {
                    // Prüfen, ob das Gerät schon lange genug läuft, um Deep Sleep zu erlauben
                    if (millis() - startupTime < MIN_RUNTIME_BEFORE_SLEEP) {
                        Serial.printf("⚠️ Niedriger Batteriestand erkannt, aber Gerät läuft erst seit %lu Sekunden. Warte noch bis 30 Sekunden.\n", 
                                      (millis() - startupTime) / 1000);
                        return;
                    }
                    
                    Serial.println("💤 KRITISCH: Batteriestand unter " + String(DEEP_SLEEP_THRESHOLD) + "V! Gehe in Deep Sleep...");
                    
                    // KRITISCH: NUR Status LEDs ausschalten, NICHT die Main LEDs hier manipulieren!
                    // Das LED Task wird über isLowBattery Flag informiert
                    fill_solid(statusLeds, NUM_STATUS_LEDS, CRGB::Black);
                    
                    // Boost IC abschalten
                    digitalWrite(LIGHT_EN, LOW);
                    
                    // Status LEDs ein letztes Mal aktualisieren (NUR Status LEDs!)
                    FastLED[1].showLeds(STATUS_LED_BRIGHTNESS); // Status-LEDs aktualisieren
                    
                    // Einstellungen speichern
                    saveSettings();
                    
                    delay(500); // Pause für SPIFFS Schreibvorgang und Kommunikation
                    
                    // WiFi abschalten
                    WiFi.disconnect();
                    WiFi.mode(WIFI_OFF);
                    
                    // Deep Sleep aktivieren (15 Minuten)
                    esp_sleep_enable_timer_wakeup(15 * 60 * 1000000ULL); // 15 Minuten
                    Serial.println("💤 Gehe in Deep Sleep für 15 Minuten...");
                    delay(100); // Zeit für die serielle Ausgabe
                    esp_deep_sleep_start();
                    // Nach dem Deep Sleep startet das Gerät neu
                } else {
                    // Gerät ist am Ladegerät angeschlossen, kein Deep Sleep notwendig
                    Serial.println("🔋 Niedriger Batteriestand, aber Gerät wird geladen - kein Deep Sleep nötig.");
                }
            }
            
            // Boost IC prüfen bei mittlerer Batterieschwelle
            if (voltage <= BOOST_OFF_THRESHOLD) {
                if (!isBoostDisabled) {
                    isBoostDisabled = true;
                    Serial.println("⚠️ WARNUNG: Batteriestand unter " + String(BOOST_OFF_THRESHOLD) + "V! Schalte Boost IC ab...");
                    digitalWrite(LIGHT_EN, LOW); // Boost IC abschalten
                    Serial.printf("🔋 DEBUG: LIGHT_EN Pin auf LOW gesetzt (Boost IC AUS)\n");
                }
            } else if (isBoostDisabled && voltage > BOOST_OFF_THRESHOLD + 0.1f) { // Hysterese von 0.1V
                isBoostDisabled = false;
                Serial.println("✅ Batteriestand wieder über " + String(BOOST_OFF_THRESHOLD + 0.1f) + "V. Boost IC wieder aktiviert.");
                digitalWrite(LIGHT_EN, HIGH); // Boost IC wieder einschalten
                Serial.printf("🔋 DEBUG: LIGHT_EN Pin auf HIGH gesetzt (Boost IC AN)\n");
            }
            
            // KRITISCH: NUR die Flags setzen - die LED-Steuerung übernimmt statusLedTask()!
            // Batterie-Warnstufen aktualisieren (User-Anforderung: Orange bei 3.5V, Rot bei 3.3V)
            
            // DEBUG: Aktueller Status vor Änderungen
            Serial.printf("🔋 DEBUG: Spannung=%.3fV, isLowBattery=%s, isWarningBattery=%s, isBoostDisabled=%s\n", 
                         voltage, isLowBattery?"JA":"NEIN", isWarningBattery?"JA":"NEIN", isBoostDisabled?"JA":"NEIN");
            
            // Warnstufe (Orange bei 3.5V)
            if (voltage <= WARNING_BATTERY_THRESHOLD && voltage > LOW_BATTERY_THRESHOLD) {
                if (!isWarningBattery) {
                    isWarningBattery = true;
                    Serial.println("🟠 WARNUNG: Batterie-Warnstufe erreicht bei " + String(voltage) + "V (Schwelle: " + String(WARNING_BATTERY_THRESHOLD) + "V)");
                }
            } else if (isWarningBattery && voltage > WARNING_BATTERY_THRESHOLD + 0.1f) {
                isWarningBattery = false;
                Serial.println("✅ Batterie-Warnstufe verlassen: " + String(voltage) + "V");
            }
            
            // Kritische Stufe (Rot bei 3.3V) - VERBESSERTE ANTI-OSZILLATIONS-LOGIK
            if (voltage <= LOW_BATTERY_THRESHOLD) {
                // Wenn Low-Battery erkannt wird
                if (!isLowBattery) {
                    isLowBattery = true;
                    isWarningBattery = false; // Warnstufe deaktivieren wenn kritisch erreicht
                    Serial.println("🔴 KRITISCH: Batteriestand kritisch niedrig bei " + String(voltage) + "V (Schwelle: " + String(LOW_BATTERY_THRESHOLD) + "V)!");
                    Serial.printf("🔴 DEBUG: isLowBattery wurde auf TRUE gesetzt! LEDs sollten ausgeschaltet werden.\n");
                    
                    // KRITISCH: NICHT hier die Main LEDs manipulieren!
                    // Das LED Task prüft isLowBattery Flag und schaltet LEDs selbst aus
                    // fill_solid(mainLeds, NUM_MAIN_LEDS, CRGB::Black);  // ENTFERNT!
                    // FastLED[0].showLeds(0);  // ENTFERNT!
                }
            } else if (isLowBattery && voltage > LOW_BATTERY_THRESHOLD + 0.2f) { // ERHÖHTE Hysterese von 0.2V statt 0.1V gegen Oszillation!
                // Wenn Batterie wieder über kritische Schwelle + ERHÖHTE Hysterese (3.5V statt 3.4V)
                // ZUSÄTZLICH: Nur zurücksetzen wenn Boost IC auch wieder aktiv ist (sonst Oszillation)
                Serial.printf("🔴 DEBUG: Prüfe Reset-Bedingung: voltage=%.3fV > %.3fV, isBoostDisabled=%s\n", 
                             voltage, LOW_BATTERY_THRESHOLD + 0.2f, isBoostDisabled?"JA":"NEIN");
                
                if (!isBoostDisabled || voltage > BOOST_OFF_THRESHOLD + 0.2f) {
                    isLowBattery = false;
                    Serial.println("✅ Batteriestand wieder über kritischer Schwelle mit erhöhter Hysterese: " + String(voltage) + "V");
                    Serial.printf("🔴 DEBUG: isLowBattery wurde auf FALSE zurückgesetzt! LEDs sollten wieder eingeschaltet werden.\n");
                    
                    // KRITISCH: NICHT hier die Main LEDs manipulieren!
                    // Das LED Task prüft isLowBattery Flag und schaltet LEDs selbst an
                    // FastLED[0].showLeds(BRIGHTNESS);  // ENTFERNT!
                    
                    // Prüfe ob wir noch in der Warnstufe sind
                    if (voltage <= WARNING_BATTERY_THRESHOLD + 0.1f) {
                        isWarningBattery = true;
                        Serial.println("🟠 Zurück in Batterie-Warnstufe");
                    }
                } else {
                    Serial.printf("🔴 Batterie bei %.3fV über Low-Threshold, aber Boost IC noch deaktiviert - bleibe in Low-Battery Modus\n", voltage);
                    Serial.printf("🔴 DEBUG: Reset-Bedingung NICHT erfüllt - isLowBattery bleibt TRUE\n");
                }
            }
        } else {
            Serial.println("⚠️ WARNUNG: Keine gültigen Batteriespannungs-Messungen erhalten!");
        }
    }
    
    // KRITISCH: Alle LED-Steuerung ENTFERNT!
    // Die statusLedTask() ist ALLEINE zuständig für alle Status LEDs
    // Nur die Flags (isLowBattery, isWarningBattery) werden hier gesetzt
}

void statusLedTask(void *parameter) {
    // Konstante Update-Rate für Status-LEDs
    const int STATUS_UPDATE_RATE = 100; // 100ms feste Update-Rate, unabhängig von Animation Speed
    
    // KRITISCH: Watchdog-Registrierung entfernt - verhindert Reboots  
    // esp_task_wdt_add(NULL);  // DEAKTIVIERT - verursacht Reboots ohne richtige Watchdog-Init
    
    // WiFi LED Status einmal zu Beginn setzen (neue Logik entsprechend User-Anforderung)
    if(xSemaphoreTake(statusLedMutex, portMAX_DELAY)) {
        // Bestimme initiale WiFi-LED-Farbe
        bool isWifiClientConnected = (wifiConnectEnabled && WiFi.status() == WL_CONNECTED);
        bool isWifiAPMode = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
        
        if (isWifiClientConnected) {
            statusLeds[0] = STATUS_BLUE;  // Blau = Client verbunden
        } else if (isWifiAPMode) {
            statusLeds[0] = STATUS_GREEN; // Grün = AP Modus
        } else {
            statusLeds[0] = CRGB::Black;  // Aus = WiFi aus
        }
        
        FastLED[1].showLeds(STATUS_LED_BRIGHTNESS); // Explizit nur Status LEDs mit fester Helligkeit
        xSemaphoreGive(statusLedMutex);
    }
    
    unsigned long lastMemoryCheck = 0;
    // unsigned long lastWatchdogReset = 0;  // ENTFERNT - nicht mehr benötigt  
    uint32_t loopCounter = 0;
    
    for(;;) {
        loopCounter++;
        unsigned long currentTime = millis();
        
        // KRITISCH: Watchdog-Reset entfernt - keine Watchdog-Registrierung mehr
        // if (currentTime - lastWatchdogReset > 2000) {
        //     esp_task_wdt_reset();
        //     lastWatchdogReset = currentTime;
        // }
        
        // Memory-Check alle 2 Minuten
        if (currentTime - lastMemoryCheck > 120000) {
            size_t freeHeap = ESP.getFreeHeap();
            Serial.printf("Status-LED-Task: Freier Heap: %d bytes, Loops: %d\n", freeHeap, loopCounter);
            lastMemoryCheck = currentTime;
            loopCounter = 0; // Reset counter
        }
        
        // KRITISCH: Mutex mit Timeout statt portMAX_DELAY
        if(xSemaphoreTake(statusLedMutex, pdMS_TO_TICKS(200))) {
            // WICHTIG: Zuerst prüfen wir die Batterie-Status - diese hat PRIORITÄT!
            // Die STATUS LED (Index 0) zeigt Batterie-Status falls aktiv, sonst WiFi-Status
            
            CRGB statusLed0Color = CRGB::Black; // Default: Aus
            
            // KRITISCH: Batterie-Status hat HÖCHSTE PRIORITÄT und überschreibt WiFi-Status!
            // DEBUG: Status-Flags prüfen
            static bool lastLowBatteryStatusTask = false;
            static bool lastWarningBatteryStatusTask = false;
            if (isLowBattery != lastLowBatteryStatusTask || isWarningBattery != lastWarningBatteryStatusTask) {
                Serial.printf("🔴 STATUS-TASK DEBUG: isLowBattery=%s, isWarningBattery=%s\n", 
                             isLowBattery?"JA":"NEIN", isWarningBattery?"JA":"NEIN");
                lastLowBatteryStatusTask = isLowBattery;
                lastWarningBatteryStatusTask = isWarningBattery;
            }
            
            if (isLowBattery) {
                // Rot blinken bei kritischem Batteriestand (3.3V)
                if (millis() - lastBlinkTime > 250) {
                    blinkState = !blinkState;
                    lastBlinkTime = millis();
                }
                statusLed0Color = blinkState ? STATUS_RED : CRGB::Black;  // Rot blinkend
                static unsigned long lastStatusRedLog = 0;
                if (millis() - lastStatusRedLog > 2000) {
                    Serial.println("STATUS LED: Batterie KRITISCH - Rot blinken");
                    lastStatusRedLog = millis();
                }
            } else if (isWarningBattery) {
                // Orange blinken bei Batteriewarnung (3.5V)
                if (millis() - lastBlinkTime > 500) { // Langsameres Blinken für Warnung
                    blinkState = !blinkState;
                    lastBlinkTime = millis();
                }
                statusLed0Color = blinkState ? STATUS_YELLOW : CRGB::Black;  // Orange/Gelb blinkend
                static unsigned long lastStatusOrangeLog = 0;
                if (millis() - lastStatusOrangeLog > 2000) {
                    Serial.println("STATUS LED: Batterie WARNUNG - Orange blinken");
                    lastStatusOrangeLog = millis();
                }
            } else {
                // Keine Batteriewarnung - zeige WiFi-Status mit neuen Effekten
                bool isWifiClientConnected = (wifiConnectEnabled && WiFi.status() == WL_CONNECTED);
                bool isWifiAPMode = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
                bool isWifiOff = (WiFi.getMode() == WIFI_OFF);
                
                // DEBUG: WiFi-Status detailliert loggen
                static unsigned long lastDetailedWifiLog = 0;
                if (millis() - lastDetailedWifiLog > 3000) {
                    Serial.printf("📡 WIFI DEBUG: Mode=%d, Status=%d, wifiEnabled=%s, wifiConnectEnabled=%s\n", 
                                  WiFi.getMode(), WiFi.status(), wifiEnabled?"JA":"NEIN", wifiConnectEnabled?"JA":"NEIN");
                    Serial.printf("📡 Conditions: isWifiClientConnected=%s, isWifiAPMode=%s, isWifiOff=%s\n",
                                  isWifiClientConnected?"JA":"NEIN", isWifiAPMode?"JA":"NEIN", isWifiOff?"JA":"NEIN");
                    lastDetailedWifiLog = millis();
                }
                
                // NEUE LOGIK: Verschiedene Effekte für verschiedene WiFi Modi
                static unsigned long lastWifiBlinkTime = 0;
                static bool wifiBlinkState = false;
                static unsigned long lastBreathTime = 0;
                static uint8_t breathBrightness = 0;
                static bool breathDirection = true; // true = heller werden, false = dunkler werden
                
                if (isWifiClientConnected) {
                    // Station Mode verbunden: BLAU SOLID (nicht blinkend)
                    statusLed0Color = STATUS_BLUE;
                    
                    // DEBUG: WiFi Status loggen
                    static unsigned long lastWifiConnectedLog = 0;
                    if (millis() - lastWifiConnectedLog > 5000) {
                        Serial.printf("🌐 WiFi Status: STATION MODE VERBUNDEN (IP: %s)\n", WiFi.localIP().toString().c_str());
                        lastWifiConnectedLog = millis();
                    }
                    
                } else if (isWifiAPMode) {
                    // AP Mode: GRÜN BLINKEND (User-Anforderung)
                    if (millis() - lastWifiBlinkTime > 750) { // 750ms Intervall für sanftes Blinken
                        wifiBlinkState = !wifiBlinkState;
                        lastWifiBlinkTime = millis();
                    }
                    statusLed0Color = wifiBlinkState ? STATUS_GREEN : CRGB::Black;
                    
                    // DEBUG: WiFi Status loggen
                    static unsigned long lastWifiAPLog = 0;
                    if (millis() - lastWifiAPLog > 5000) {
                        Serial.printf("🌐 WiFi Status: AP MODE (Name: %s)\n", WiFi.softAPSSID().c_str());
                        lastWifiAPLog = millis();
                    }
                    
                } else if (!isWifiOff && wifiConnectEnabled) {
                    // Station Mode versucht zu verbinden: BLAU BLINKEND (schneller)
                    if (millis() - lastWifiBlinkTime > 250) { // 250ms für schnelleres Blinken
                        wifiBlinkState = !wifiBlinkState;
                        lastWifiBlinkTime = millis();
                    }
                    statusLed0Color = wifiBlinkState ? STATUS_BLUE : CRGB::Black;
                    
                    // DEBUG: WiFi Status loggen
                    static unsigned long lastWifiConnectingLog = 0;
                    if (millis() - lastWifiConnectingLog > 3000) {
                        Serial.printf("🌐 WiFi Status: STATION MODE VERBINDET... (SSID: %s)\n", wifiSSID.c_str());
                        lastWifiConnectingLog = millis();
                    }
                    
                } else {
                    // WiFi deaktiviert: ATEM-EFFEKT (sanftes Pulsieren)
                    // User-Anforderung: "nicht aggressives" Atmen
                    if (millis() - lastBreathTime > 50) { // 50ms Update-Rate für smooth breathing
                        if (breathDirection) {
                            breathBrightness += 3; // Langsam heller werden
                            if (breathBrightness >= 120) { // Nicht zu hell
                                breathDirection = false;
                            }
                        } else {
                            breathBrightness -= 3; // Langsam dunkler werden
                            if (breathBrightness <= 10) { // Nicht komplett aus
                                breathDirection = true;
                            }
                        }
                        lastBreathTime = millis();
                    }
                    
                    // Weißer Atem-Effekt für WiFi deaktiviert
                    statusLed0Color = CRGB(breathBrightness, breathBrightness, breathBrightness);
                    
                    // DEBUG: WiFi Status loggen
                    static unsigned long lastWifiOffLog = 0;
                    if (millis() - lastWifiOffLog > 5000) {
                        Serial.println("🌐 WiFi Status: DEAKTIVIERT (Atem-Effekt)");
                        lastWifiOffLog = millis();
                    }
                }
            }
            
            // STATUS LED (Index 0) setzen - Batterie hat Priorität vor WiFi!
            statusLeds[0] = statusLed0Color;
            
            // LED 1 (Index 1): Mode Button LED (LED2 in der Anforderung)
            if (modeLedActive && millis() < modeLedOffTime) {
                statusLeds[1] = STATUS_WHITE;
                Serial.println("Mode LED: AN (weiß)");
            } else {
                statusLeds[1] = CRGB(0, 0, 0);  // Aus
                if (modeLedActive) {
                    Serial.println("Mode LED: AUS");
                }
                modeLedActive = false;
            }
            
            // LED 2 (Index 2): Color Button LED (LED3 in der Anforderung)
            if (colorLedActive && millis() < colorLedOffTime) {
                statusLeds[2] = STATUS_WHITE;
            } else {
                statusLeds[2] = CRGB(0, 0, 0);  // Aus
                colorLedActive = false;
            }
            
            // LED 3 (Index 3): Brightness Button LED (LED4 in der Anforderung)
            if (brightLedActive && millis() < brightLedOffTime) {
                statusLeds[3] = STATUS_WHITE;
            } else {
                statusLeds[3] = CRGB(0, 0, 0);  // Aus
                brightLedActive = false;
            }
            
            // LED 4 (Index 4): SAO Button LED - BENUTZER-ANFORDERUNG: NIEMALS leuchten!
            // Nur STATUS LED (Index 0) soll Feedback geben - SAO LED bleibt immer AUS
            statusLeds[4] = CRGB(0, 0, 0);  // SAO LED bleibt IMMER aus
            
            // HINWEIS: Batterie-Status wird bereits oben bei statusLeds[0] behandelt
            // Keine zusätzliche Batterie-Logik hier nötig - wurde nach oben verschoben für Priorität
            
            // Status LEDs aktualisieren mit eigener Helligkeit
            FastLED[1].showLeds(STATUS_LED_BRIGHTNESS);
            
            xSemaphoreGive(statusLedMutex);
        } else {
            // Mutex-Timeout - Warnung ausgeben aber weitermachen
            Serial.println("WARNUNG: Status-LED-Mutex Timeout!");
            // esp_task_wdt_reset(); // Watchdog nach Timeout zurücksetzen - ENTFERNT
        }
        
        // Konstante Verzögerung - völlig unabhängig von der Animation-Geschwindigkeit
        vTaskDelay(pdMS_TO_TICKS(STATUS_UPDATE_RATE));
        
        // Zusätzlicher Watchdog-Reset nach jedem Delay - ENTFERNT
        // esp_task_wdt_reset();
    }
}

// Status-LED-Feedback-Funktionen
void startStatusLedUpdate() {
    statusLedUpdateMode = true;
    statusLedStartTime = millis();
    statusLedBlinkCount = 0;
    Serial.println("Status-LED: Update-Modus aktiviert (grün blinken)");
}

void startStatusLedReboot() {
    statusLedRebootMode = true;
    statusLedStartTime = millis();
    statusLedBlinkCount = 0;
    Serial.println("Status-LED: Reboot-Modus aktiviert (gelb blinken)");
}

void startStatusLedLock() {
    statusLedLockMode = true;
    statusLedStartTime = millis();
    statusLedBlinkCount = 0;
    Serial.println("Status-LED: Lock-Modus aktiviert (rot blinken)");
}

void startStatusLedUnlock() {
    statusLedUnlockMode = true;
    statusLedStartTime = millis();
    statusLedBlinkCount = 0;
    Serial.println("Status-LED: Unlock-Modus aktiviert (blau blinken)");
}

CRGB getCurrentWifiStatusColor() {
    bool isWifiClientConnected = (wifiConnectEnabled && WiFi.status() == WL_CONNECTED);
    bool isWifiAPMode = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
    bool isWifiOff = (WiFi.getMode() == WIFI_OFF);
    
    if (isWifiClientConnected) {
        return STATUS_BLUE;  // Blau = Station Mode verbunden
    } else if (isWifiAPMode) {
        return STATUS_GREEN; // Grün = AP Modus (wird in statusLedTask blinkend gemacht)
    } else if (!isWifiOff && wifiConnectEnabled) {
        return STATUS_BLUE;  // Blau = Station Mode verbindet (wird in statusLedTask blinkend gemacht)
    } else {
        return CRGB(50, 50, 50);  // Gedimmt weiß = WiFi deaktiviert (Atem-Effekt in statusLedTask)
    }
}

// NEUE FUNKTION: Korrekte Priorität von Batterie-Status über WiFi-Status
CRGB getCurrentStatusColor() {
    // KRITISCH: Batterie-Status hat HÖCHSTE PRIORITÄT!
    if (isLowBattery) {
        // Für statische Farbe - ohne Blink-Logik hier (wird in statusLedTask animiert)
        return STATUS_RED;  // Rot bei kritischem Batteriestand
    } else if (isWarningBattery) {
        // Für statische Farbe - ohne Blink-Logik hier (wird in statusLedTask animiert)  
        return STATUS_YELLOW;  // Orange/Gelb bei Batteriewarnung
    } else {
        // Keine Batteriewarnung - verwende WiFi-Status
        return getCurrentWifiStatusColor();
    }
}

void updateStatusLedFeedback() {
    unsigned long currentTime = millis();
    
    if (statusLedUpdateMode) {
        // Grünes Blinken für Update
        if (currentTime - statusLedStartTime > 500) { // Alle 500ms
            if (statusLedBlinkCount < 10) { // 5 mal blinken (an/aus = 10 Zustände)
                if (statusLedBlinkCount % 2 == 0) {
                    statusLeds[0] = STATUS_GREEN; // Grün an
                } else {
                    statusLeds[0] = CRGB(0, 0, 0); // Aus
                }
                statusLedBlinkCount++;
                statusLedStartTime = currentTime;
                FastLED[1].showLeds(STATUS_LED_BRIGHTNESS); // Status-LEDs aktualisieren
            } else {
                statusLedUpdateMode = false;
                statusLeds[0] = getCurrentStatusColor(); // Zurück zu normalem Status (Batterie hat Priorität)
                FastLED[1].showLeds(STATUS_LED_BRIGHTNESS);
            }
        }
    }
    else if (statusLedRebootMode) {
        // Gelbes Blinken für Reboot
        if (currentTime - statusLedStartTime > 500) { // Alle 500ms
            if (statusLedBlinkCount < 10) { // 5 mal blinken
                if (statusLedBlinkCount % 2 == 0) {
                    statusLeds[0] = STATUS_YELLOW; // Gelb an
                } else {
                    statusLeds[0] = CRGB(0, 0, 0); // Aus
                }
                statusLedBlinkCount++;
                statusLedStartTime = currentTime;
                FastLED[1].showLeds(STATUS_LED_BRIGHTNESS);
            } else {
                statusLedRebootMode = false;
                statusLeds[0] = getCurrentStatusColor(); // Zurück zu normalem Status (Batterie hat Priorität)
                FastLED[1].showLeds(STATUS_LED_BRIGHTNESS);
            }
        }
    }
    else if (statusLedLockMode) {
        // Rotes Blinken für Lock
        if (currentTime - statusLedStartTime > 300) { // Alle 300ms (schneller)
            if (statusLedBlinkCount < 10) { // 5 mal blinken
                if (statusLedBlinkCount % 2 == 0) {
                    statusLeds[0] = STATUS_RED; // Rot an
                } else {
                    statusLeds[0] = CRGB(0, 0, 0); // Aus
                }
                statusLedBlinkCount++;
                statusLedStartTime = currentTime;
                FastLED[1].showLeds(STATUS_LED_BRIGHTNESS);
            } else {
                statusLedLockMode = false;
                statusLeds[0] = getCurrentStatusColor(); // Zurück zu normalem Status (Batterie hat Priorität)
                FastLED[1].showLeds(STATUS_LED_BRIGHTNESS);
            }
        }
    }
    else if (statusLedUnlockMode) {
        // Blaues Blinken für Unlock
        if (currentTime - statusLedStartTime > 300) { // Alle 300ms (schneller)
            if (statusLedBlinkCount < 10) { // 5 mal blinken
                if (statusLedBlinkCount % 2 == 0) {
                    statusLeds[0] = STATUS_BLUE; // Blau an
                } else {
                    statusLeds[0] = CRGB(0, 0, 0); // Aus
                }
                statusLedBlinkCount++;
                statusLedStartTime = currentTime;
                FastLED[1].showLeds(STATUS_LED_BRIGHTNESS);
            } else {
                statusLedUnlockMode = false;
                statusLeds[0] = getCurrentStatusColor(); // Zurück zu normalem Status (Batterie hat Priorität)
                FastLED[1].showLeds(STATUS_LED_BRIGHTNESS);
            }
        }
    }
}
  