#include <Arduino.h>
#include <FastLED.h>
#include "esp32-hal.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "Config.h"  // Neue Include-Datei
#include "WebUI.h"
#include "Settings.h"
#include "Display.h"
#include "SPIFFS.h"
#include "BQ25895CONFIG.h"
#include <ArduinoJson.h>
#include <WiFi.h>
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
uint8_t gCurrentMode = 0;
uint16_t animationSpeed = 20;
uint16_t micFrequency = 50;
bool wifiEnabled = false;  // WiFi Status
bool lastWifiStatus = false; // Speichert den letzten WiFi-Status

// Globale Variablen für Button-Handling
unsigned long buttonPressStartTime = 0;
bool buttonWasPressed = false;
unsigned long lastButtonPress = 0;
const unsigned long DEBOUNCE_DELAY = 200; // Entprellzeit in Millisekunden

// Zeitvariable für die Startzeit des Geräts
unsigned long startupTime = 0;
// Mindestlaufzeit bevor Deep-Sleep erlaubt ist (5 Minuten)
const unsigned long MIN_RUNTIME_BEFORE_SLEEP = 5 * 60 * 1000;

// Status LED Farben (angepasst für GRB-Reihenfolge)
#define STATUS_GREEN   CRGB(255, 0, 0)   // Grün in GRB
#define STATUS_BLUE    CRGB(0, 0, 255)   // Blau in GRB
#define STATUS_ORANGE  CRGB(165, 255, 0) // Orange in GRB
#define STATUS_RED     CRGB(0, 255, 0)   // Rot in GRB
#define STATUS_WHITE   CRGB(255, 255, 255) // Weiß in GRB

// Status LED Helligkeit (fester Wert, unabhängig von den Hauptleds)
#define STATUS_LED_BRIGHTNESS 12

// Batterieschwellen für verschiedene Maßnahmen
#define LOW_BATTERY_THRESHOLD 3.3f     // LEDs ausschalten (Main LEDs)
#define BOOST_OFF_THRESHOLD 3.1f       // Boost IC abschalten (LIGHT_EN Pin)
#define DEEP_SLEEP_THRESHOLD 2.9f      // Deep Sleep aktivieren

bool isLowBattery = false;
bool isBoostDisabled = false;
unsigned long lastBlinkTime = 0;
bool blinkState = false;

// Globale Variablen
CRGB mainLeds[NUM_MAIN_LEDS];
CRGB statusLeds[NUM_STATUS_LEDS];
uint8_t gHue = 0;  // Für Farbrotation
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

// Funktionsprototypen
void rainbow();
void colorWipe();
void twinkle();
void fire();
void pulse();
void wave();
void sparkle();
void gradient();
void dots();
void comet();
void bounce();
void musicReactive();
void addGlitter(uint8_t chance);
void updateStatusLED();
void ledTask(void *parameter);
void statusLedTask(void *parameter); // Neue Task-Funktion für Status LEDs

// Funktion zum Togglen von WiFi
void toggleWiFi() {
    if (!wifiEnabled) {
        // Access Point starten
        WiFi.mode(WIFI_AP);
        const char* password = apPassword.length() > 0 ? apPassword.c_str() : NULL;
        WiFi.softAP(apName.c_str(), password);
        wifiEnabled = true;
        
        Serial.println("Access Point gestartet");
        Serial.print("AP Name: ");
        Serial.println(apName);
        Serial.print("IP Address: ");
        Serial.println(WiFi.softAPIP());
        
        // Status LED auf Blau setzen - DIREKT, nicht über updateStatusLED
        statusLeds[0] = STATUS_BLUE;
        FastLED[1].showLeds(STATUS_LED_BRIGHTNESS);
        
        // Status merken
        lastWifiStatus = true;
    } else {
        // Access Point stoppen
        WiFi.softAPdisconnect(true);
        WiFi.disconnect();
        wifiEnabled = false;
        
        Serial.println("Access Point gestoppt");
        
        // Status LED auf Grün setzen - DIREKT, nicht über updateStatusLED
        statusLeds[0] = STATUS_GREEN;
        FastLED[1].showLeds(STATUS_LED_BRIGHTNESS);
        
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

// LED Task Funktion mit angepasster Geschwindigkeit
void ledTask(void *parameter) {
    for(;;) {
        unsigned long currentTime = millis();
        
        if(xSemaphoreTake(ledMutex, portMAX_DELAY)) {
            // Hauptanimation - nur aktualisieren wenn keine Low-Battery-Bedingung
            if (!isLowBattery) {
                switch(gCurrentMode) {
                    case 0:  rainbow();     break;
                    case 1:  colorWipe();   break;
                    case 2:  twinkle();     break;
                    case 3:  fire();        break;
                    case 4:  pulse();       break;
                    case 5:  wave();        break;
                    case 6:  sparkle();     break;
                    case 7:  gradient();    break;
                    case 8:  dots();        break;
                    case 9:  comet();       break;
                    case 10: bounce();      break;
                    case 11: musicReactive(); break;
                    default: rainbow();     break;
                }
                
                // Hauptleds mit aktueller Helligkeit anzeigen - NUR MAIN LEDS
                // Controller 0 separat steuern mit eigener Helligkeitseinstellung
                FastLED[0].showLeds(BRIGHTNESS); // Controller 0 (Main LEDs) mit eigener Helligkeit
            }
            
            xSemaphoreGive(ledMutex);
        }
        
        // Kürzere Verzögerung für flüssigere Animationen - maximale Aktualisierungsrate 
        // aber mit der vom Benutzer eingestellten Animation Speed
        vTaskDelay(pdMS_TO_TICKS(animationSpeed < 5 ? 5 : animationSpeed));
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

void setup() {
    // Serial für Debugging
    Serial.begin(115200);
    delay(50); // Kurze Verzögerung für die Stabilisierung der seriellen Verbindung
    Serial.println("Starte LED Badge...");
    
    // Startzeit speichern
    startupTime = millis();
    
    // Watchdog-Timer auf längere Zeit setzen für mehr Stabilität
    Serial.println("Konfiguriere Watchdog...");
    // Hinweis: esp_task_wdt_init ist nicht verfügbar, wir nutzen stattdessen 
    // die Stabilisierungsverzögerungen an kritischen Stellen
    
    // WiFi initialisieren
    Serial.println("WiFi initialisieren...");
    WiFi.persistent(false);  // Verhindert häufiges Schreiben in den Flash
    WiFi.mode(WIFI_AP);      // Nur Access Point Modus
    WiFi.setAutoReconnect(false);  // Auto-Reconnect deaktivieren
    
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
    gCurrentMode = settings.mode;
    BRIGHTNESS = settings.brightness;
    NOISE_LEVEL = settings.noiseLevel;
    animationSpeed = settings.animationSpeed;
    micFrequency = settings.micFrequency;
    apName = settings.apName;
    apPassword = settings.apPassword;
    
    // Access Point mit dem konfigurierten Namen starten
    Serial.println("Starte WiFi Access Point...");
    const char* password = apPassword.length() > 0 ? apPassword.c_str() : NULL;
    if (WiFi.softAP(apName.c_str(), password)) {
        Serial.print("Access Point gestartet: ");
        Serial.println(apName);
        wifiEnabled = true;
    } else {
        Serial.println("Fehler beim Starten des Access Points");
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
    
    // Status LED für WiFi setzen (blau wenn aktiviert)
    if (wifiEnabled) {
        statusLeds[0] = STATUS_BLUE;
    } else {
        statusLeds[0] = STATUS_GREEN;
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
        
        // Loop läuft auf Core 1 und ist für den Webserver reserviert
        vTaskDelay(pdMS_TO_TICKS(100));
    } catch (...) {
        // Ignoriere alle Ausnahmen im Hauptloop, um Abstürze zu vermeiden
        Serial.println("Fehler in main loop, aber weiter laufen...");
        delay(1000);
    }
}

void updateStatusLED() {
    // Akku-Spannung prüfen mit mehrfacher Messung
    if (charger != nullptr) {
        // Mehrere Messungen durchführen für ein stabileres Ergebnis
        float voltage = 0;
        const int numSamples = 3;
        
        for (int i = 0; i < numSamples; i++) {
            voltage += charger->getVBAT();
            delay(10); // Kurze Pause zwischen den Messungen
        }
        voltage /= numSamples; // Durchschnitt der Messungen
        
        // Spannungswert im plausiblen Bereich prüfen
        if (voltage < 2.5f || voltage > 4.5f) {
            Serial.println("WARNUNG: Unplausible Batteriespan: " + String(voltage) + "V - Messung ignoriert");
            return; // Vorzeitig beenden, wenn die Spannung unplausibel ist
        }
        
        // Mit Debug-Output, um Spannungswerte zu sehen
        static unsigned long lastVoltageLog = 0;
        if (millis() - lastVoltageLog > 5000) { // Alle 5 Sekunden
            lastVoltageLog = millis();
            Serial.println("Batteriespannung: " + String(voltage, 2) + "V");
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
                
                Serial.print("Ladestatus: VBUS = ");
                Serial.print(isVbusPresent ? "Ja" : "Nein");
                Serial.print(", Ladevorgang = ");
                Serial.println(isCharging ? "Ja" : "Nein");
            }
            
            // Wenn das Gerät NICHT am Ladegerät angeschlossen ist, dann Deep Sleep
            if (!isVbusPresent && !isCharging) {
                // Prüfen, ob das Gerät schon lange genug läuft, um Deep Sleep zu erlauben
                if (millis() - startupTime < MIN_RUNTIME_BEFORE_SLEEP) {
                    Serial.println("Niedriger Batteriestand erkannt, aber Gerät läuft erst seit " + 
                                  String((millis() - startupTime) / 1000) + " Sekunden. Warte noch bis 5 Minuten.");
                    return;
                }
                
                Serial.println("KRITISCH: Batteriestand unter " + String(DEEP_SLEEP_THRESHOLD) + "V! Gehe in Deep Sleep...");
                
                // Alle LEDs und Boost IC ausschalten
                fill_solid(mainLeds, NUM_MAIN_LEDS, CRGB::Black);
                fill_solid(statusLeds, NUM_STATUS_LEDS, CRGB::Black);
                digitalWrite(LIGHT_EN, LOW); // Boost IC abschalten
                
                // Status LEDs ein letztes Mal aktualisieren
                FastLED[0].showLeds(0); // Hauptleds ausschalten mit Helligkeit 0
                FastLED[1].showLeds(STATUS_LED_BRIGHTNESS); // Status-LEDs aktualisieren
                
                // Einstellungen speichern
                saveSettings();
                
                delay(500); // Pause für SPIFFS Schreibvorgang und Kommunikation
                
                // WiFi abschalten
                WiFi.disconnect();
                WiFi.mode(WIFI_OFF);
                
                // Deep Sleep aktivieren (15 Minuten)
                esp_sleep_enable_timer_wakeup(15 * 60 * 1000000ULL); // 15 Minuten
                Serial.println("Gehe in Deep Sleep für 15 Minuten...");
                delay(100); // Zeit für die serielle Ausgabe
                esp_deep_sleep_start();
                // Nach dem Deep Sleep startet das Gerät neu
            } else {
                // Gerät ist am Ladegerät angeschlossen, kein Deep Sleep notwendig
                Serial.println("Niedriger Batteriestand, aber Gerät wird geladen - kein Deep Sleep nötig.");
            }
        }
        
        // Boost IC prüfen bei mittlerer Batterieschwelle
        if (voltage <= BOOST_OFF_THRESHOLD) {
            if (!isBoostDisabled) {
                isBoostDisabled = true;
                Serial.println("WARNUNG: Batteriestand unter " + String(BOOST_OFF_THRESHOLD) + "V! Schalte Boost IC ab...");
                digitalWrite(LIGHT_EN, LOW); // Boost IC abschalten
            }
        } else if (isBoostDisabled && voltage > BOOST_OFF_THRESHOLD + 0.1f) { // Hysterese von 0.1V
            isBoostDisabled = false;
            Serial.println("Batteriestand wieder über " + String(BOOST_OFF_THRESHOLD + 0.1f) + "V. Boost IC wieder aktiviert.");
            digitalWrite(LIGHT_EN, HIGH); // Boost IC wieder einschalten
        }
        
        // Low Battery Status aktualisieren
        if (voltage <= LOW_BATTERY_THRESHOLD) {
            // Wenn Low-Battery erkannt wird
            if (!isLowBattery) {
                isLowBattery = true;
                Serial.println("WARNUNG: Niedriger Batteriestand erkannt, unter " + String(LOW_BATTERY_THRESHOLD) + "V!");
                
                // Hauptleds ausschalten wenn Batterie niedrig
                fill_solid(mainLeds, NUM_MAIN_LEDS, CRGB::Black);
                FastLED[0].showLeds(0); // Hauptleds mit Helligkeit 0 aktualisieren
            }
        } else if (isLowBattery && voltage > LOW_BATTERY_THRESHOLD + 0.1f) { // Hysterese von 0.1V
            // Wenn Batterie wieder über Schwelle + Hysterese
            isLowBattery = false;
            Serial.println("Batteriestand wieder normal: " + String(voltage) + "V");
            
            // Hauptleds wieder einschalten
            FastLED[0].showLeds(BRIGHTNESS); // Hauptleds mit normaler Helligkeit aktualisieren
        }
    }
    
    // Status LEDs aktualisieren
    
    // Die erste Status-LED (Index 0) zeigt WiFi-Status - STABIL, nicht blinkend
    // NUR aktualisieren, wenn sich der Status tatsächlich geändert hat
    if (wifiEnabled != lastWifiStatus) {
        statusLeds[0] = wifiEnabled ? CRGB(0, 0, 255) : CRGB(0, 255, 0);
        lastWifiStatus = wifiEnabled;
        Serial.println("WiFi-Status geändert, LED aktualisiert: " + String(wifiEnabled ? "AN" : "AUS"));
    }
    // Sonst lassen wir die erste LED komplett unverändert!
    
    // LED 1 (Index 1): Mode Button LED (LED2 in der Anforderung)
    if (modeLedActive && millis() < modeLedOffTime) {
        statusLeds[1] = STATUS_WHITE;
    } else {
        statusLeds[1] = CRGB(0, 0, 0);  // Aus
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
    
    // LED 4 (Index 4): SAO Button LED oder Batterie-Warnung
    if (saoLedActive && millis() < saoLedOffTime) {
        statusLeds[4] = STATUS_WHITE;
    } else {
        saoLedActive = false;
        
        // Wenn Batterie niedrig ist, blinken lassen
        if (isLowBattery) {
            // Blink-Effekt: alle 250ms umschalten
            if (millis() - lastBlinkTime > 250) {
                blinkState = !blinkState;
                lastBlinkTime = millis();
            }
            
            // Beim Blinken zwischen Rot und Aus wechseln
            if (blinkState) {
                statusLeds[4] = STATUS_RED;
            } else {
                statusLeds[4] = CRGB(0, 0, 0);  // Aus
            }
        } else {
            // Normale Anzeige wenn keine SAO-Taste aktiv
            if (charger != nullptr) {
                float voltage = charger->getVBAT();
                if (voltage <= 3.2f) {  // Niedrige Batteriespannung bei 3.2V oder darunter (noch statisch rot)
                    statusLeds[4] = STATUS_RED;  // Rot
                } else {
                    statusLeds[4] = CRGB(0, 0, 0);  // Aus
                }
            } else {
                statusLeds[4] = CRGB(0, 0, 0);  // Aus
            }
        }
    }
    
    // Status LEDs aktualisieren mit eigener Helligkeit
    FastLED[1].showLeds(STATUS_LED_BRIGHTNESS);
}

void rainbow() {
    static uint8_t hue = 0;
    fill_rainbow(mainLeds, NUM_MAIN_LEDS, hue++, 7);
}

void colorWipe() {
    static uint8_t hue = 0;
    fill_solid(mainLeds, NUM_MAIN_LEDS, CHSV(hue++, 255, 255));
}

void twinkle() {
    fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 20);
    int pos = random16(NUM_MAIN_LEDS);
    mainLeds[pos] += CHSV(random8(), 200, 255);
}

void fire() {
    for(int i = 0; i < NUM_MAIN_LEDS; i++) {
        int flicker = random8(150);
        mainLeds[i] = CRGB(flicker,flicker/2,0);
    }
}

void pulse() {
    static uint8_t brightness = 0;
    static int8_t direction = 1;
    
    // Sanftes Pulsieren
    EVERY_N_MILLISECONDS(20) {
        brightness += direction * 2;
        if (brightness >= 255 || brightness <= 0) {
            direction = -direction;
        }
    }
    
    // Regenbogenfarben mit sanftem Pulsieren
    for(int i = 0; i < NUM_MAIN_LEDS; i++) {
        uint8_t hue = (gHue + (i * 255 / NUM_MAIN_LEDS));
        mainLeds[i] = CHSV(hue, 255, brightness);
    }
    
    EVERY_N_MILLISECONDS(50) {
        gHue++;
    }
}

void wave() {
    static uint8_t hue = 0;
    for(int i = 0; i < NUM_MAIN_LEDS; i++) {
        mainLeds[i] = CHSV(hue + (i * 10), 255, 255);
    }
    hue++;
}

void addGlitter(uint8_t chance) {
    if(random8() < chance) {
        mainLeds[random16(NUM_MAIN_LEDS)] += CRGB::White;
    }
}

void sparkle() {
    fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 80);
    addGlitter(80);
}

void gradient() {
    static uint8_t startHue = 0;
    fill_gradient(mainLeds, NUM_MAIN_LEDS, CHSV(startHue, 255, 255), CHSV(startHue + 128, 255, 255));
    startHue++;
}

void dots() {
    fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 20);
    int pos = beatsin16(13, 0, NUM_MAIN_LEDS - 1);
    mainLeds[pos] += CHSV(gHue, 255, 192);
}

void comet() {
    fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 20);
    static int pos = 0;
    pos = pos + 1;
    if(pos >= NUM_MAIN_LEDS) pos = 0;
    mainLeds[pos] = CHSV(gHue, 255, 255);
}

void bounce() {
    static int pos = 0;
    static int direction = 1;
    fadeToBlackBy(mainLeds, NUM_MAIN_LEDS, 20);
    pos += direction;
    if(pos >= NUM_MAIN_LEDS) {
        direction = -1;
        pos = NUM_MAIN_LEDS - 1;
    }
    if(pos < 0) {
        direction = 1;
        pos = 0;
    }
    mainLeds[pos] = CHSV(gHue, 255, 255);
}

void musicReactive() {
    uint16_t sample = 0;
    
    // Mehrere Samples nehmen für bessere Genauigkeit
    for(int i = 0; i < SAMPLES; i++) {
        sample += analogRead(MIC_PIN);
        delayMicroseconds(micFrequency);  // Angepasste Abtastrate
    }
    sample /= SAMPLES;

    // Grundrauschen herausfiltern
    if(sample < NOISE_LEVEL) {
        sample = 0;
    }

    // Wert auf LED-Helligkeit mappen
    int brightness = map(sample, 0, 4095, 0, 255);
    
    // Visuellen Effekt basierend auf Lautstärke erzeugen
    for(int i = 0; i < NUM_MAIN_LEDS; i++) {
        mainLeds[i] = CHSV(gHue, 255, brightness);
    }
    
    EVERY_N_MILLISECONDS(20) {
        gHue++;  // Langsame Farbrotation
    }
}

// Separate Status LED Task - komplett unabhängig von der Hauptanimation
void statusLedTask(void *parameter) {
    // Konstante Update-Rate für Status-LEDs
    const int STATUS_UPDATE_RATE = 100; // 100ms feste Update-Rate, unabhängig von Animation Speed
    
    // WiFi LED Status einmal zu Beginn setzen
    if(xSemaphoreTake(statusLedMutex, portMAX_DELAY)) {
        statusLeds[0] = wifiEnabled ? CRGB(0, 0, 255) : CRGB(0, 255, 0);
        FastLED[1].showLeds(STATUS_LED_BRIGHTNESS); // Explizit nur Status LEDs mit fester Helligkeit
        lastWifiStatus = wifiEnabled;
        xSemaphoreGive(statusLedMutex);
    }
    
    for(;;) {
        if(xSemaphoreTake(statusLedMutex, portMAX_DELAY)) {
            // Die erste Status-LED (Index 0) zeigt WiFi-Status - STABIL, nicht blinkend
            // NUR aktualisieren, wenn sich der Status tatsächlich geändert hat
            if (wifiEnabled != lastWifiStatus) {
                statusLeds[0] = wifiEnabled ? CRGB(0, 0, 255) : CRGB(0, 255, 0);
                lastWifiStatus = wifiEnabled;
                Serial.println("WiFi-Status geändert, LED aktualisiert: " + String(wifiEnabled ? "AN" : "AUS"));
            }
            
            // LED 1 (Index 1): Mode Button LED (LED2 in der Anforderung)
            if (modeLedActive && millis() < modeLedOffTime) {
                statusLeds[1] = STATUS_WHITE;
            } else {
                statusLeds[1] = CRGB(0, 0, 0);  // Aus
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
            
            // LED 4 (Index 4): SAO Button LED oder Batterie-Warnung
            if (saoLedActive && millis() < saoLedOffTime) {
                statusLeds[4] = STATUS_WHITE;
            } else {
                saoLedActive = false;
                
                // Wenn Batterie niedrig ist, blinken lassen
                if (isLowBattery) {
                    // Blink-Effekt: alle 250ms umschalten
                    if (millis() - lastBlinkTime > 250) {
                        blinkState = !blinkState;
                        lastBlinkTime = millis();
                    }
                    
                    // Beim Blinken zwischen Rot und Aus wechseln
                    if (blinkState) {
                        statusLeds[4] = STATUS_RED;
                    } else {
                        statusLeds[4] = CRGB(0, 0, 0);  // Aus
                    }
                } else {
                    // Normale Anzeige wenn keine SAO-Taste aktiv
                    if (charger != nullptr) {
                        float voltage = charger->getVBAT();
                        if (voltage <= 3.2f) {  // Niedrige Batteriespannung bei 3.2V oder darunter (noch statisch rot)
                            statusLeds[4] = STATUS_RED;  // Rot
                        } else {
                            statusLeds[4] = CRGB(0, 0, 0);  // Aus
                        }
                    } else {
                        statusLeds[4] = CRGB(0, 0, 0);  // Aus
                    }
                }
            }
            
            // Status LEDs aktualisieren mit eigener Helligkeit
            FastLED[1].showLeds(STATUS_LED_BRIGHTNESS);
            
            xSemaphoreGive(statusLedMutex);
        }
        
        // Konstante Verzögerung - völlig unabhängig von der Animation-Geschwindigkeit
        vTaskDelay(pdMS_TO_TICKS(STATUS_UPDATE_RATE));
    }
} 