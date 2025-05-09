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
#define BOOT_BUTTON 0  // GPIO0 ist der Boot-Button auf dem ESP32
#define MODE_BUTTON 7  // Taster1 für Moduswechsel
#define COLOR_BUTTON 6 // Taster2 für Farbe (noch nicht verwendet)
#define BRIGHT_BUTTON 5 // Taster3 für Helligkeit
#define SAO_BUTTON 4   // Taster4 für SAO (noch nicht verwendet)

// Globale Variablen
uint8_t BRIGHTNESS = 5;
uint8_t NOISE_LEVEL = 10;
uint8_t gCurrentMode = 0;
uint16_t animationSpeed = 20;
uint16_t micFrequency = 50;
bool wifiEnabled = false;  // WiFi Status

// Globale Variablen für Button-Handling
unsigned long buttonPressStartTime = 0;
bool buttonWasPressed = false;
unsigned long lastButtonPress = 0;
const unsigned long DEBOUNCE_DELAY = 200; // Entprellzeit in Millisekunden

// Status LED Farben (angepasst für GRB-Reihenfolge)
#define STATUS_GREEN   CRGB(255, 0, 0)   // Grün in GRB
#define STATUS_BLUE    CRGB(0, 0, 255)   // Blau in GRB
#define STATUS_ORANGE  CRGB(165, 255, 0) // Orange in GRB
#define STATUS_RED     CRGB(0, 255, 0)   // Rot in GRB

// Globale Variablen
CRGB mainLeds[NUM_MAIN_LEDS];
CRGB statusLeds[NUM_STATUS_LEDS];
uint8_t gHue = 0;  // Für Farbrotation
unsigned long lastModeChange = 0;
TaskHandle_t ledTaskHandle = NULL;
SemaphoreHandle_t ledMutex;

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

// Funktion zum Togglen von WiFi
void toggleWiFi() {
    if (wifiEnabled) {
        // WiFi ausschalten
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        wifiEnabled = false;
        Serial.println("WiFi wurde deaktiviert");
        statusLeds[0] = STATUS_GREEN;
    } else {
        // WiFi einschalten
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, password);
        
        // Warte maximal 10 Sekunden auf Verbindung
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            wifiEnabled = true;
            Serial.println("\nWiFi wurde aktiviert");
            Serial.print("IP Adresse: ");
            Serial.println(WiFi.localIP());
            statusLeds[0] = STATUS_BLUE;
        } else {
            Serial.println("\nWiFi-Verbindung fehlgeschlagen");
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            statusLeds[0] = STATUS_RED;
            delay(2000);  // Zeige Fehler kurz an
            statusLeds[0] = STATUS_GREEN;
        }
    }
    FastLED.show();  // Status-LED aktualisieren
}

// Funktion zum Überprüfen des Boot-Buttons
void checkBootButton() {
    if (digitalRead(BOOT_BUTTON) == LOW) {  // Button ist gedrückt
        if (!buttonWasPressed) {  // Button wurde gerade erst gedrückt
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
    if(xSemaphoreTake(ledMutex, portMAX_DELAY)) {
      // Hauptanimation
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

      // Status LED aktualisieren
      updateStatusLED();
      
      FastLED.show();
      
      xSemaphoreGive(ledMutex);
    }
    
    // Delay mit variabler Geschwindigkeit
    vTaskDelay(pdMS_TO_TICKS(animationSpeed));
  }
}

// Funktion zum Überprüfen der Taster
void checkButtons() {
    unsigned long currentTime = millis();
    
    // Nur prüfen wenn genug Zeit seit dem letzten Tastendruck vergangen ist (Entprellung)
    if (currentTime - lastButtonPress >= DEBOUNCE_DELAY) {
        // Mode Button (Taster1)
        if (digitalRead(MODE_BUTTON) == HIGH) {
            gCurrentMode = (gCurrentMode + 1) % 12; // 12 Modi (0-11)
            saveSettings(); // Einstellungen speichern
            lastButtonPress = currentTime;
        }
        
        // Brightness Button (Taster3)
        if (digitalRead(BRIGHT_BUTTON) == HIGH) {
            BRIGHTNESS = (BRIGHTNESS + 2) % 26; // 2% Schritte bis 25%
            if (BRIGHTNESS == 0) BRIGHTNESS = 2; // Von 24% auf 2% springen
            FastLED.setBrightness(BRIGHTNESS);
            saveSettings(); // Einstellungen speichern
            lastButtonPress = currentTime;
        }
    }
    
    // Boot Button für WiFi weiterhin separat prüfen
    checkBootButton();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting...");

  // SPIFFS initialisieren
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS-Initialisierung fehlgeschlagen!");
    return;
  }

  // Einstellungen laden
  if (!loadSettings()) {
    Serial.println("Fehler beim Laden der Einstellungen!");
  }

  // Mutex erstellen
  ledMutex = xSemaphoreCreateMutex();
  
  // Alle LEDs zuerst auf schwarz/aus setzen
  fill_solid(mainLeds, NUM_MAIN_LEDS, CRGB::Black);
  fill_solid(statusLeds, NUM_STATUS_LEDS, CRGB::Black);
  
  // LED-Streifen initialisieren
  FastLED.addLeds<LED_TYPE, MAIN_LED_PIN, COLOR_ORDER>(mainLeds, NUM_MAIN_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.addLeds<LED_TYPE, STATUS_LED_PIN, COLOR_ORDER>(statusLeds, NUM_STATUS_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
  
  // Status-LED auf Grün setzen, Rest aus
  statusLeds[0] = STATUS_GREEN;
  for(int i = 1; i < NUM_STATUS_LEDS; i++) {
    statusLeds[i] = CRGB::Black;
  }
  FastLED.show();
  
  // Analogeingang für MAX4466 konfigurieren
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // Display nur initialisieren wenn aktiviert
  if (USE_DISPLAY) {
    // Mehrere Initialisierungsversuche
    int retryCount = 0;
    const int maxRetries = 3;
    
    while (retryCount < maxRetries && !displayAvailable) {
      if (checkDisplayAvailable()) {
        Serial.println("Display erfolgreich initialisiert");
        loadDisplayContent();
        break;
      }
      retryCount++;
      if (retryCount < maxRetries) {
        Serial.printf("Display-Initialisierung fehlgeschlagen, Versuch %d von %d\n", retryCount + 1, maxRetries);
        delay(1000); // Kurze Pause zwischen den Versuchen
      }
    }
    
    if (!displayAvailable) {
      Serial.println("Display-Initialisierung endgültig fehlgeschlagen - Fortfahren ohne Display");
    }
  } else {
    Serial.println("Display deaktiviert in Konfiguration");
    displayAvailable = false;
  }

  // Webserver initialisieren
  initWebServer();

  // LED Task auf Core 0 starten
  xTaskCreatePinnedToCore(
    ledTask,           // Task Funktion
    "LED_Task",        // Task Name
    4096,             // Stack Größe
    NULL,             // Parameter
    1,                // Priorität
    &ledTaskHandle,   // Task Handle
    0                 // Core ID (0)
  );

  // Boot-Button als Eingang konfigurieren
  pinMode(BOOT_BUTTON, INPUT);

  // Taster als Eingänge konfigurieren
  pinMode(MODE_BUTTON, INPUT);
  pinMode(COLOR_BUTTON, INPUT);
  pinMode(BRIGHT_BUTTON, INPUT);
  pinMode(SAO_BUTTON, INPUT);
}

void loop() {
  // Taster überprüfen (inkl. Boot-Button)
  checkButtons();
  
  // Loop läuft auf Core 1 und ist für den Webserver reserviert
  vTaskDelay(pdMS_TO_TICKS(100));
}

void updateStatusLED() {
  // Status LED (erste LED)
  if (wifiEnabled) {
    statusLeds[0] = STATUS_BLUE;
  } else {
    statusLeds[0] = STATUS_GREEN;
  }
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

