#ifndef BQ25895CONFIG_H
#define BQ25895CONFIG_H

#include <Wire.h>
#include <Arduino.h>

// Forward-Deklaration für Log-Funktionen
void addBqLog(const String& message);

// I2C Pins
#define SDA_PIN 45
#define SCL_PIN 46
#define INT_PIN 3

// Funktionsprototypen für Charger-Initialisierung
void initializeBQ25895();
void ensureChargerInitialized();

// BQ25895 I2C Adresse
#define BQ25895_ADDRESS 0x6A

// Register Adressen
#define REG_00_INPUT_SOURCE_CTRL    0x00
#define REG_01_POWER_ON_CONFIG      0x01
#define REG_02_CHARGE_CURRENT_CTRL  0x02
#define REG_03_PRECHG_TERM_CURRENT  0x03
#define REG_04_CHARGE_VOLTAGE_CTRL  0x04
#define REG_05_CHARGE_TERM_TIMER    0x05
#define REG_06_IR_COMP_THERM_CTRL   0x06
#define REG_07_MONITOR_CTRL         0x07
#define REG_08_SYS_FUNCTION_CTRL    0x08
#define REG_09_OTG_CTRL             0x09
#define REG_0A_MISC_OPERATION_CTRL  0x0A
#define REG_0B_SYSTEM_STATUS        0x0B
#define REG_0C_FAULT_STATUS         0x0C
#define REG_0D_MASK_CTRL            0x0D
#define REG_0E_ADC_VBAT_MSB         0x0E
#define REG_0F_ADC_VBAT_LSB         0x0F
#define REG_10_ADC_VSYS_MSB         0x10
#define REG_11_ADC_VSYS_LSB         0x11
#define REG_12_ADC_VBUS_MSB         0x12
#define REG_13_ADC_VBUS_LSB         0x13
#define REG_14_ADC_ICHG_MSB         0x14
#define REG_15_ADC_ICHG_LSB         0x15
#define REG_16_ADC_IDPM_MSB         0x16
#define REG_17_ADC_IDPM_LSB         0x17
#define REG_18_ADC_DIE_TEMP_MSB     0x18
#define REG_19_ADC_DIE_TEMP_LSB     0x19

class BQ25895 {
public:
    BQ25895() {}

    void begin() {
        try {
            // Initialisiere I2C
            Wire.begin(SDA_PIN, SCL_PIN);
            Wire.setClock(50000); // Reduziere I2C-Geschwindigkeit auf 50kHz
            delay(500);
            Serial.println("🔋 Initialisiere BQ25895...");
            addBqLog("Initialisiere BQ25895...");

            if (!isConnected()) {
                Serial.println("❌ BQ25895 nicht erreichbar!");
                addBqLog("ERROR: BQ25895 nicht erreichbar!");
                return;
            }

            // Setze einen Timeout für Wire-Operationen
            Wire.setTimeOut(200);  // 200ms Timeout

            // Versuche, den Chip zu resetten
            if (!writeRegister(0x14, 0x80)) { // Soft Reset
                addBqLog("Fehler beim Soft Reset");
            }
            delay(500); // Längere Wartezeit nach Reset
            addBqLog("Soft Reset durchgeführt");

            // Prüfe, ob der Chip nach dem Reset erreichbar ist
            if (!isConnected()) {
                Serial.println("❌ BQ25895 nach Reset nicht erreichbar!");
                addBqLog("ERROR: BQ25895 nach Reset nicht erreichbar!");
                return;
            }

            // ADC und Temperaturmessung aktivieren
            if (!writeRegister(0x07, 0x95)) { // ADC aktivieren, TS-Überwachung aktivieren
                addBqLog("Fehler beim Aktivieren des ADC");
            }
            delay(100);
            Serial.println("🛠️ ADC aktiviert, TS-Überwachung aktiviert");
            addBqLog("ADC aktiviert, TS-Überwachung aktiviert");
            
            // Prüfe ADC-Status
            uint8_t reg07 = readRegister(0x07);
            Serial.printf("ADC Status nach Aktivierung (REG07): 0x%02X\n", reg07);
            
            // Register 0x06: IR-Kompensation und Thermische Regulierung aktivieren
            if (!writeRegister(0x06, 0x32)) { // Thermische Regulierung aktivieren
                addBqLog("Fehler beim Aktivieren der thermischen Regulierung");
            }
            delay(100);
            Serial.println("🛠️ Thermische Regulierung aktiviert");
            addBqLog("Thermische Regulierung aktiviert");
            
            // FAULT Register zurücksetzen
            uint8_t fault = readRegister(0x0C);
            addBqLog("FAULT-Register vor Reset: 0x" + String(fault, HEX));
            
            // Register 0x05: JEITA aktivieren/deaktivieren
            if (!writeRegister(0x05, 0x00)) { // JEITA deaktivieren
                addBqLog("Fehler beim Deaktivieren von JEITA");
            }
            delay(100);
            addBqLog("JEITA (temperaturabhängige Ladung) deaktiviert");

            // Zurücksetzen aller Fehlerbedingungen
            clearFaults();
            delay(100);

            // Ladeparameter erst nach Löschung der Fehler setzen
            setChargingParams();
            delay(100);

            // SYS min voltage auf 3.3V setzen
            if (!writeRegister(0x03, 0x1A)) {
                addBqLog("Fehler beim Setzen der SYS min voltage");
            }
            delay(100);
            Serial.println("⚙️ SYS min voltage auf 3.3V gesetzt");
            addBqLog("SYS min voltage auf 3.3V gesetzt");

            if (!writeRegister(0x01, 0x3C)) { // Safety Timer deaktivieren
                addBqLog("Fehler beim Deaktivieren des Safety Timers");
            }
            if (!writeRegister(0x08, 0x7A)) { // Watchdog deaktivieren
                addBqLog("Fehler beim Deaktivieren des Watchdogs");
            }
            delay(100);
            Serial.println("⏱️ Watchdog deaktiviert");
            addBqLog("Safety Timer und Watchdog deaktiviert");
            
            // Kontinuierliche Ladung aktivieren und STAT-Pin-Blinken deaktivieren
            uint8_t reg0A = readRegister(0x0A);
            if (reg0A != 0xFF) {
                // BATFET-Kontrolle deaktivieren und STAT-Pin auf Hi-Z setzen
                if (!writeRegister(0x0A, (reg0A & ~0x08) | 0x00)) {
                    addBqLog("Fehler beim Deaktivieren des STAT-Pin-Blinkens");
                }
                delay(100);
                Serial.println("🔧 STAT-Pin-Blinken deaktiviert");
                addBqLog("STAT-Pin-Blinken deaktiviert");
            }
            
            // Nochmals Fehler zurücksetzen
            clearFaults();
            delay(100);

            // Teste Spannungsmessung direkt nach Initialisierung
            float vbat = getVBAT();
            Serial.printf("Initiale Spannungsmessung: %.3fV\n", vbat);

            Serial.println("✅ Setup abgeschlossen.");
            addBqLog("Setup erfolgreich abgeschlossen");
            printStatus();
        } catch (...) {
            Serial.println("❌ Schwerwiegender Fehler bei BQ25895-Initialisierung");
            addBqLog("KRITISCHER FEHLER: Schwerwiegender Fehler bei BQ25895-Initialisierung");
        }
    }

    void updatePowerMode() {
        // Daten weniger häufig lesen, um Stabilität zu verbessern
        static unsigned long lastUpdateTime = 0;
        unsigned long currentTime = millis();
        
        // Nur alle 2 Sekunden aktualisieren
        if (currentTime - lastUpdateTime < 2000) {
            return;
        }
        lastUpdateTime = currentTime;
        
        bool vbusNow = isVBUSPresent();

        if (vbusNow != lastVBUSState) {
            if (vbusNow) {
                Serial.println("🔌 VBUS wieder verfügbar → Ladebetrieb");
                addBqLog("VBUS erkannt - Wechsel in Ladebetrieb");
                delay(100);
                
                // Fehler zurücksetzen
                clearFaults();
                
                // REMOVED: forceDPDMDetection() um Charging-Resets zu verhindern
                // Das forced protocol resettet den Ladevorgang ständig
                Serial.println("ℹ️ DPDM-Erkennung übersprungen um Charging-Resets zu vermeiden");
                addBqLog("DPDM-Erkennung übersprungen um Charging-Resets zu vermeiden");
                delay(200);
                
                // Ladeparameter neu setzen
                setChargingParams();
            } else {
                Serial.println("🔋 Kein VBUS → Entladebetrieb");
                addBqLog("VBUS entfernt - Wechsel in Entladebetrieb");
            }
            lastVBUSState = vbusNow;
        }
        
        // Periodisch den Status überprüfen
        static unsigned long lastStatusCheck = 0;
        if (currentTime - lastStatusCheck > 60000) { // Alle 60 Sekunden
            // Fehler überprüfen und zurücksetzen wenn nötig
            uint8_t fault = readRegister(0x0C);
            if (fault != 0) {
                addBqLog("Periodische Prüfung - Fehler gefunden: 0x" + String(fault, HEX));
                clearFaults();
                
                // Ladeparameter neu setzen nach Fehler
                setChargingParams();
            }
            lastStatusCheck = currentTime;
        }
    }

    void forceDPDMDetection() {
        uint8_t reg = readRegister(0x02);
        if (reg != 0xFF) {
            reg |= 0x80;
            (void)writeRegister(0x02, reg);
            Serial.println("🔁 DPDM-Erkennung neu gestartet");
            addBqLog("DPDM-Erkennung neu gestartet");
        }
    }

    void setChargingParams() {
        // Sicherstellen, dass keine Fehler vorliegen
        clearFaults();
        
        // Eingangs-Strom setzen (1500mA)
        (void)writeRegister(0x00, 0x30);
        
        // Lade-Strom setzen (Standard: 2048mA = 0x20 für Register 0x02)
        // Prüfen, ob der zuletzt angeforderte Ladestrom gesetzt ist
        if (lastRequestedChargeCurrent > 0) {
            setChargeCurrent(lastRequestedChargeCurrent);
        } else {
            // Standard-Ladestrom setzen - KORRIGIERT: Register 0x02 für Charge Current
            // 2048mA / 64mA = 32 = 0x20
            (void)writeRegister(0x02, 0x20);
            addBqLog("Ladeparameter gesetzt: Input=1500mA, Charge=2048mA (Register 0x02)");
        }
        
        // Jetzt die Register kontrollieren
        uint8_t reg00 = readRegister(0x00);
        uint8_t reg02 = readRegister(0x02);  // Korrigiert: 0x02 statt 0x04
        
        addBqLog("Lade-Register überprüft: REG00=0x" + String(reg00, HEX) + 
                 ", REG02=0x" + String(reg02, HEX) + " (Charge Current)");
        
        Serial.println("⚡ Ladeparameter neu gesetzt");
    }

    bool setChargeCurrent(int currentMA) {
        // Ladestrom-Limitierung implementieren
        int limitedCurrent = currentMA;
        
        // Erlaubte Werte: 500mA, 1000mA, 1500mA, 2000mA
        if (currentMA <= 500) {
            limitedCurrent = 500;
        } else if (currentMA <= 1000) {
            limitedCurrent = 1000;
        } else if (currentMA <= 1500) {
            limitedCurrent = 1500;
        } else if (currentMA <= 2000) {
            limitedCurrent = 2000;
        } else {
            // Maximum auf 2A begrenzen für Sicherheit
            limitedCurrent = 2000;
            Serial.printf("WARNUNG: Ladestrom auf 2A begrenzt (angefordert: %dmA)\n", currentMA);
        }
        
        // KRITISCH: Speichere den angeforderten Wert für spätere Verwendung
        lastRequestedChargeCurrent = limitedCurrent;
        
        // CRITICAL FIX: Berechnung des Register-Werts für Fast Charge Current (Register 0x02 - NICHT 0x04!)
        // Register 0x04 ist für CHARGE_VOLTAGE_CTRL, Register 0x02 ist für CHARGE_CURRENT_CTRL!
        // Formel: ICHG = 0mA + REG02[6:0] × 64mA
        // Minimum: 0mA, Maximum: 5056mA (aber wir begrenzen auf 2A)
        int regValue = limitedCurrent / 64;
        
        // Register 0x02 lesen und nur die Ladestrom-Bits ändern (Bits 6:0)
        uint8_t reg02 = readRegister(0x02);
        if (reg02 == 0xFF) {
            Serial.println("Fehler beim Lesen von Register 0x02 (CHARGE_CURRENT_CTRL)");
            return false;
        }
        
        // Nur die unteren 7 Bits (Ladestrom) ändern, obere Bits beibehalten
        uint8_t newReg02 = (reg02 & 0x80) | (regValue & 0x7F);
        
        bool success = writeRegister(0x02, newReg02);
        
        if (success) {
            Serial.printf("Ladestrom erfolgreich auf %dmA gesetzt (Register 0x02: 0x%02X)\n", limitedCurrent, newReg02);
            
            // Verifikation: Register zurücklesen
            uint8_t verification = readRegister(0x02);
            if (verification != newReg02) {
                Serial.printf("WARNUNG: Verifikation fehlgeschlagen! Erwartet: 0x%02X, Gelesen: 0x%02X\n", newReg02, verification);
            } else {
                Serial.println("Ladestrom-Einstellung verifiziert");
            }
        } else {
            Serial.printf("Fehler beim Setzen des Ladestroms auf %dmA\n", limitedCurrent);
        }
        
        return success;
    }

    void printStatus() {
        Serial.println("====== BQ25895 Status ======");
        addBqLog("--- BQ25895 Status ---");
        uint8_t status = readRegister(0x0B);
        uint8_t fault  = readRegister(0x0C);
        
        Serial.print("VBUS Status (0x0B): 0x"); 
        Serial.println(status, HEX);
        addBqLog("VBUS Status (0x0B): 0x" + String(status, HEX));
        
        Serial.print("FAULT (0x0C): 0x"); 
        Serial.println(fault, HEX);
        addBqLog("FAULT (0x0C): 0x" + String(fault, HEX));
        
        // VBUS Status anzeigen
        if ((status >> 5) & 0x07) {
            Serial.println("🔌 VBUS erkannt");
            addBqLog("VBUS erkannt");
        } else {
            Serial.println("❌ Kein VBUS erkannt");
            addBqLog("Kein VBUS erkannt");
        }

        // Temperatur-Status ausgeben
        String tempStatus = getTemperatureStatus();
        Serial.println("🌡️ Temperatur: " + tempStatus);
        addBqLog("Temperatur: " + tempStatus);

        // Lade-Status ausgeben
        String chargeStatus = getChargeStatus();
        Serial.println("⚡ Ladestatus: " + chargeStatus);
        addBqLog("Ladestatus: " + chargeStatus);

        // Fehler prüfen
        if (fault != 0x00) {
            // Erstelle Fehlertext direkt
            String faultText = "";
            if (fault & 0x80) faultText += "WATCHDOG ";
            if (fault & 0x40) faultText += "BOOST ";
            if (fault & 0x20) faultText += "CHARGE ";
            if (fault & 0x10) faultText += "BATTERY ";
            if (fault & 0x08) {
                faultText += "NTC ";
                uint8_t ntcFault = (fault & 0x07);
                switch (ntcFault) {
                    case 1: faultText += "(TS COLD) "; break;
                    case 2: faultText += "(TS COOL) "; break;
                    case 3: faultText += "(TS WARM) "; break;
                    case 4: faultText += "(TS HOT) "; break;
                    default: faultText += "(NTC UNKNOWN) ";
                }
            }
            
            Serial.println("⚠️ FAULT vorhanden! " + faultText);
            addBqLog("Fehler erkannt: " + faultText);
        } else {
            Serial.println("✅ Kein Fehler");
            addBqLog("Kein Fehler erkannt");
        }

        // Spannungen und Ströme ausgeben
        float vbat = getVBAT();
        float vsys = getVSYS();
        float vbus = getVBUS();
        float ichg = getICHG();
        float iin = getIIN();
        
        Serial.printf("VBAT: %.2fV, VSYS: %.2fV, VBUS: %.2fV\n", vbat, vsys, vbus);
        Serial.printf("ICHG: %.3fA, IIN: %.3fA\n", ichg, iin);
        
        addBqLog("VBAT: " + String(vbat, 2) + "V, VSYS: " + String(vsys, 2) + "V, VBUS: " + String(vbus, 2) + "V");
        addBqLog("ICHG: " + String(ichg * 1000, 0) + "mA, IIN: " + String(iin * 1000, 0) + "mA");
        
        Serial.println("=============================");
        addBqLog("--- Ende Status ---");
    }

    String getTemperatureStatus() {
        uint8_t status = readRegister(0x0B);
        if (status == 0xFF) {
            addBqLog("Fehler beim Lesen des Temperatur-Status (Register 0x0B)");
            return "Fehler";
        }
        
        // Extrahiere die Temperatur-Status-Bits (Bits 2:0)
        uint8_t tempStatus = status & 0x07;
        
        String statusText;
        switch (tempStatus) {
            case 0: statusText = "Normal"; break;
            case 1: statusText = "Zu kalt"; break;
            case 2: statusText = "Kühl"; break;
            case 3: statusText = "Warm"; break;
            case 4: statusText = "Zu heiß"; break;
            default: statusText = "Unbekannt"; break;
        }
        
        return statusText;
    }

    float getVBAT() {
        // ADC aktivieren falls nötig
        uint8_t reg07 = readRegister(0x07);
        if (reg07 != 0xFF && !(reg07 & 0x80)) {
            (void)writeRegister(0x07, reg07 | 0x80);
            delay(50);
        }

        // VBAT-Spannung aus Register 0x0E lesen
        uint8_t regVal = readRegister(0x0E);
        
        if (regVal != 0xFF) {
            // KORRIGIERT: Berechnung laut BQ25895-Datenblatt
            // VBAT ADC: 2.304V + ADC_Code × 20mV
            float voltage = 2.304f + (regVal * 0.02f);
            
            // DEBUG: Nur gelegentlich ausgeben um serielle Schnittstelle nicht zu überlasten
            static unsigned long lastVbatDebug = 0;
            if (millis() - lastVbatDebug > 10000) { // Alle 10 Sekunden
                Serial.printf("🔍 VBAT DEBUG: Register=0x%02X (%d), Berechnet=%.3fV\n", regVal, regVal, voltage);
                lastVbatDebug = millis();
            }
            
            return voltage;
        }
        
        Serial.printf("❌ FEHLER: VBAT-Register lesen fehlgeschlagen (0x%02X)\n", regVal);
        return -1.0f;
    }

    float getVSYS() {
        // ADC aktivieren falls nötig
        uint8_t reg07 = readRegister(0x07);
        if (reg07 != 0xFF && !(reg07 & 0x80)) {
            (void)writeRegister(0x07, reg07 | 0x80);
            delay(50);
        }

        // System-Spannung aus Register 0x0F lesen
        uint8_t regVal = readRegister(0x0F);
        
        if (regVal != 0xFF) {
            // KORRIGIERT: Berechnung laut BQ25895-Datenblatt (ohne falschen Korrekturfaktor)
            float voltage = 2.304f + (regVal * 0.02f);  // 2.304V + (ADC_Code × 20mV)
            
            return voltage;
        }
        return -1.0f;
    }

    float getVBUS() {
        // ADC aktivieren falls nötig
        uint8_t reg07 = readRegister(0x07);
        if (reg07 != 0xFF && !(reg07 & 0x80)) {
            (void)writeRegister(0x07, reg07 | 0x80);
            delay(50);
        }

        // VBUS-Spannung aus Register 0x11 lesen
        uint8_t regVal = readRegister(0x11);
        
        if (regVal != 0xFF) {
            // KORRIGIERT: Berechnung laut BQ25895-Datenblatt für VBUS
            // VBUS ADC: 2.6V + ADC_Code × 64mV (korrekte Formel!)
            float vbus = 2.6f + (regVal * 0.064f);
            
            // DEBUG: Nur gelegentlich ausgeben um serielle Schnittstelle nicht zu überlasten
            static unsigned long lastVbusDebug = 0;
            if (millis() - lastVbusDebug > 15000) { // Alle 15 Sekunden
                Serial.printf("🔍 VBUS DEBUG: Register=0x%02X (%d), Roh=%.2fV\n", regVal, regVal, vbus);
                lastVbusDebug = millis();
            }
            
            return vbus;
        }
        return -1.0f;
    }

    float getICHG() {
        // ADC aktivieren falls nötig
        uint8_t reg07 = readRegister(0x07);
        if (reg07 != 0xFF && !(reg07 & 0x80)) {
            (void)writeRegister(0x07, reg07 | 0x80);
            delay(50);
        }

        // Ladestrom aus Register 0x12 lesen
        uint8_t regVal = readRegister(0x12);
        
        if (regVal != 0xFF) {
            // Berechnung laut Datenblatt
            float ichg = regVal * 0.05f; // 50mA pro Schritt
            
            return ichg;
        }
        return -1.0f;
    }

    float getIIN() {
        // ADC aktivieren falls nötig
        uint8_t reg07 = readRegister(0x07);
        if (reg07 != 0xFF && !(reg07 & 0x80)) {
            (void)writeRegister(0x07, reg07 | 0x80);
            delay(50);
        }

        // Eingangsstrom aus Register 0x13 lesen
        uint8_t regVal = readRegister(0x13);
        
        if (regVal != 0xFF) {
            // Berechnung laut Datenblatt
            float iin = regVal * 0.05f; // 50mA pro Schritt
            
            return iin;
        }
        return -1.0f;
    }

    String getChargeStatus() {
        uint8_t status = readRegister(0x0B);
        if (status == 0xFF) {
            return "Unbekannt";
        }
        
        // Ladestatus aus Bits 7:6 extrahieren
        uint8_t chargeStatus = (status >> 6) & 0x03;
        
        switch (chargeStatus) {
            case 0: return "Nicht ladend";
            case 1: return "Vorladung";
            case 2: return "Schnellladung";
            case 3: return "Ladung beendet";
            default: return "Unbekannt";
        }
    }

    float getTemperature() {
        // Versuche kontinuierliches Lesen, manchmal schlägt ein einzelnes Lesen fehl
        for (int attempt = 0; attempt < 3; attempt++) {
            Serial.printf("Temperatur-Leseversuch %d/3\n", attempt + 1);
            
            // Prüfe ADC-Status
            uint8_t reg07 = readRegister(0x07);
            Serial.printf("ADC Status (REG07): 0x%02X\n", reg07);
            
            if (reg07 != 0xFF) {
                if (!(reg07 & 0x80)) { // ADC nicht aktiv
                    Serial.println("ADC nicht aktiv, aktiviere...");
                    (void)writeRegister(0x07, reg07 | 0x80); // ADC aktivieren
                    delay(100); // Warte auf ADC-Start
                    
                    // Prüfe ADC-Status nach Aktivierung
                    reg07 = readRegister(0x07);
                    Serial.printf("ADC Status nach Aktivierung (REG07): 0x%02X\n", reg07);
                }
            }

            // Versuche zuerst das MSB zu lesen
            Serial.println("Lese Temperatur MSB...");
            uint8_t msb = readRegister(REG_18_ADC_DIE_TEMP_MSB);
            Serial.printf("Temperatur MSB (0x%02X): 0x%02X\n", REG_18_ADC_DIE_TEMP_MSB, msb);
            
            if (msb == 0xFF) {
                Serial.println("Fehler beim Lesen des MSB");
                delay(100);
                continue;
            }
            
            // Dann das LSB
            Serial.println("Lese Temperatur LSB...");
            uint8_t lsb = readRegister(REG_19_ADC_DIE_TEMP_LSB);
            Serial.printf("Temperatur LSB (0x%02X): 0x%02X\n", REG_19_ADC_DIE_TEMP_LSB, lsb);
            
            if (lsb == 0xFF) {
                Serial.println("Fehler beim Lesen des LSB");
                delay(100);
                continue;
            }
            
            // Korrekte Berechnung laut Datenblatt
            uint16_t raw = ((msb << 8) | lsb) >> 6;
            float temp = 0.465f * raw - 273.15f; // 0.465°C pro LSB, Offset -273.15°C
            
            // Debug-Ausgabe
            Serial.printf("Temperatur Debug: MSB=0x%02X, LSB=0x%02X, Raw=%d, Temp=%.1f°C\n", 
                        msb, lsb, raw, temp);
            
            return temp;
        }
        
        Serial.println("⚠️ Fehler beim Lesen der Temperatur-Register nach mehreren Versuchen");
        addBqLog("ERROR: Kann Temperatur-Register nach mehreren Versuchen nicht lesen");
        return -1.0f;
    }

    float getIBAT() {
        return getICHG(); // Verwende die korrekte Funktion für den Ladestrom
    }

    bool isConnected() {
        try {
            // Sprungmarke für den Fall, dass Wire.endTransmission() abstürzt
            Wire.beginTransmission(BQ25895_ADDRESS);
            byte errorCode = Wire.endTransmission();
            if (errorCode == 0) {
                addBqLog("BQ25895 erfolgreich verbunden");
                return true;
            } else {
                addBqLog("BQ25895 I2C-Fehler: " + String(errorCode));
                return false;
            }
        } catch (...) {
            Serial.println("❌ Fehler bei I2C-Kommunikation");
            addBqLog("KRITISCHER FEHLER: I2C-Kommunikation fehlgeschlagen");
            return false;
        }
    }

    bool isVBUSPresent() {
        uint8_t stat = readRegister(0x0B);
        return (stat != 0xFF && (stat & 0xE0));
    }

    bool writeRegister(uint8_t reg, uint8_t value) {
        try {
            Wire.beginTransmission(BQ25895_ADDRESS);
            Wire.write(reg);
            Wire.write(value);
            byte error = Wire.endTransmission();
            
            if (error != 0) {
                Serial.printf("❌ I2C Fehler beim Schreiben von Register 0x%02X: %d\n", reg, error);
                return false;
            }
            
            Serial.printf("Register 0x%02X = 0x%02X geschrieben\n", reg, value);
            return true;
        } catch (...) {
            Serial.println("❌ Fehler beim Schreiben an I2C");
            addBqLog("ERROR: Kann Register 0x" + String(reg, HEX) + " nicht schreiben");
            return false;
        }
    }

    uint8_t readRegister(uint8_t reg) {
        Wire.beginTransmission(BQ25895_ADDRESS);
        Wire.write(reg);
        
        uint8_t error = Wire.endTransmission(false);
        if (error != 0) {
            return 0xFF;
        }
        
        Wire.requestFrom((uint8_t)BQ25895_ADDRESS, (uint8_t)1);
        
        if (Wire.available()) {
            uint8_t value = Wire.read();
            return value;
        }
        
        return 0xFF;
    }

    // Funktion zum Zurücksetzen aller Fehlerbedingungen
    void clearFaults() {
        // ADC zurücksetzen
        uint8_t reg02 = readRegister(0x02);
        if (reg02 != 0xFF) {
            // Force DPDM detection to reset ADC
            (void)writeRegister(0x02, reg02 | 0x80);
            delay(50);
            addBqLog("DPDM Detection erzwungen");
        }
        
        // Fault Register lesen und loggen
        uint8_t fault = readRegister(0x0C);
        if (fault != 0) {
            addBqLog("Löschen von Fehlern: 0x" + String(fault, HEX));
            
            // Wir behalten NTC-Fehler bei, da der NTC nun korrekt angeschlossen ist
            // Kein spezielles Handling mehr für TS_COLD
        }
        
        // REG00 neu schreiben (klärt manchmal Fehler)
        (void)writeRegister(0x00, 0x30);
        delay(10);
        
        // REG01 neu schreiben (klärt manchmal Fehler)
        (void)writeRegister(0x01, 0x3C);
        delay(10);
    }

private:
    bool lastVBUSState = false;
    int lastRequestedChargeCurrent = 0; // Speichert den zuletzt angeforderten Ladestrom
};

// Externe Deklaration
extern BQ25895* charger;

#endif // BQ25895CONFIG_H