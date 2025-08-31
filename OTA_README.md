# LED Badge OTA Server

Ein moderner Flask-basierter OTA (Over-The-Air) Server für die Verwaltung von LED Badges mit intelligenter Version-Verwaltung und Hardware-Typ-Unterstützung.

## Features

- 🎨 **Moderne WebUI** mit Bootstrap 5 und Font Awesome Icons
- 📱 **Responsive Design** für Desktop und Mobile
- 🔒 **Geräte-Sperrung** - Verhindert Updates für bestimmte Geräte
- 🔄 **Reboot-Funktion** - Neustart von Geräten über die WebUI
- 📊 **Live-Statistiken** - Anzahl Geräte, Online-Status, etc.
- 📝 **System-Logs** - Übersicht aller Checkins mit Hardware-Typ
- 🚀 **Intelligente Firmware-Verwaltung** - Version und Hardware-Typ-Unterstützung
- 🔄 **Auto-Refresh** - Automatische Aktualisierung alle 30 Sekunden
- 🎯 **Hardware-Typ-Kompatibilität** - Verhindert falsche Updates
- 📋 **Version-Vergleich** - Intelligente Update-Entscheidungen

## Installation

1. **Python-Abhängigkeiten installieren:**
   ```bash
   pip install -r requirements.txt
   ```

2. **OTA Server starten:**
   ```bash
   # Windows
   start_ota_server.bat
   
   # Oder manuell
   python src/ota_server.py
   ```

3. **WebUI öffnen:**
   Öffne deinen Browser und gehe zu: `http://localhost:5000`

## Konfiguration

### ESP32-Code anpassen

Stelle sicher, dass die OTA-URL in `src/Globals.cpp` auf deine lokale IP-Adresse zeigt:

```cpp
const char* otaCheckinUrl = "http://DEINE_IP:5000/api/device_checkin";
const char* hardwaretype = "LED-Modd.Badge";   // Geräte-Typ
const char* firmwareVersion = "1.4.6";         // Aktuelle Version
```

Ersetze `DEINE_IP` mit der IP-Adresse deines Computers, auf dem der OTA Server läuft.

### Netzwerk-Konfiguration

- Der OTA Server läuft standardmäßig auf Port 5000
- Stelle sicher, dass dein ESP32 und der Computer im gleichen Netzwerk sind
- Die Firewall muss Port 5000 erlauben

## Verwendung

### 1. Firmware hochladen
- Klicke auf "Firmware hochladen"
- Wähle eine .bin Datei aus
- **Gib die Version an** (z.B. "1.4.1")
- **Wähle den Geräte-Typ** (z.B. "LED-Modd.Badge")
- Klicke auf "Hochladen"

### 2. Geräte verwalten
- **Geräte-Typ anzeigen:** Wird automatisch aus den ESP32-Globals übernommen
- **Typ ändern:** Bearbeite das "Typ" Feld und klicke "Speichern"
- **Firmware zuweisen:** Wähle eine Firmware aus dem Dropdown (mit Version) und klicke "Speichern"
- **Update forcieren:** Klicke auf "Update" um ein sofortiges Update zu erzwingen
- **Reboot:** Klicke auf "Reboot" um das Gerät neuzustarten
- **Sperren/Entsperren:** Verhindert Updates für das Gerät
- **Löschen:** Entfernt das Gerät aus der Datenbank

### 3. Intelligente Updates
- **Version-Vergleich:** Updates werden nur gesendet, wenn die Firmware-Version neuer ist
- **Hardware-Typ-Kompatibilität:** Updates werden nur an kompatible Geräte gesendet
- **Force-Update:** Überschreibt die Version-Prüfung für Downgrades oder spezielle Updates

### 4. Status überwachen
- **Online/Offline:** Grün = Online, Rot = Offline, Gelb = Update läuft
- **Statistiken:** Anzahl Geräte, Online-Geräte, gesperrte Geräte, verfügbare Firmwares
- **Logs:** Letzte 50 Checkin-Einträge mit Hardware-Typ-Informationen

## API-Endpunkte

### Geräte-Checkin
```
POST /api/device_checkin
Content-Type: application/json

{
  "id": "Badge-12345",
  "version": "1.4.1",
  "hardware_type": "LED-Modd.Badge",
  "ssid": "MeinWLAN"
}
```

### Reboot-Befehl
```
POST /api/reboot/{device_id}
```

### Firmware-Download
```
GET /firmwares/{filename}
```

## Dateien

- `src/ota_server.py` - Hauptserver-Code
- `requirements.txt` - Python-Abhängigkeiten
- `firmwares/` - Verzeichnis für Firmware-Dateien
- `devices.json` - Gerätedatenbank (wird automatisch erstellt)
- `firmware_metadata.json` - Firmware-Metadaten (Version, Hardware-Typ)
- `checkins.log` - Checkin-Logs (wird automatisch erstellt)

## Neue Features

### Hardware-Typ-Unterstützung
- ESP32 sendet automatisch den Hardware-Typ beim Checkin
- Server speichert und zeigt den Hardware-Typ an
- Verhindert Updates an inkompatible Geräte

### Verbesserte Version-Verwaltung
- Firmware-Upload mit Version und Hardware-Typ
- Intelligente Version-Vergleiche
- Force-Update für Downgrades oder spezielle Updates

### Bessere Reboot-Funktion
- Verbesserte Fehlerbehandlung
- Detaillierte Fehlermeldungen
- Logging für Debugging

## Troubleshooting

### ESP32 verbindet sich nicht
1. Prüfe die IP-Adresse in `Globals.cpp`
2. Stelle sicher, dass der OTA Server läuft
3. Prüfe die Netzwerkverbindung

### Reboot funktioniert nicht
1. Prüfe, ob der ESP32 im gleichen Netzwerk ist
2. Stelle sicher, dass Port 80 auf dem ESP32 erreichbar ist
3. Prüfe die Firewall-Einstellungen
4. Schaue in die Server-Logs für detaillierte Fehlermeldungen

### Update wird nicht gesendet
1. Prüfe die Hardware-Typ-Kompatibilität
2. Vergleiche die Versionen (Update nur bei neuerer Version)
3. Verwende Force-Update für Downgrades
4. Prüfe ob das Gerät gesperrt ist

### Firmware-Upload fehlgeschlagen
1. Prüfe die Dateigröße (max. 1.25MB)
2. Stelle sicher, dass es eine gültige .bin Datei ist
3. Gib Version und Hardware-Typ an
4. Prüfe die Schreibberechtigungen im `firmwares/` Verzeichnis

## Entwicklung

### Neue Features hinzufügen
1. Bearbeite `src/ota_server.py`
2. Füge neue Routen hinzu
3. Aktualisiere die WebUI im HTML-Template
4. Teste die Funktionalität

### WebUI anpassen
Das HTML-Template ist in der `index()` Funktion in `ota_server.py` definiert. Es verwendet:
- Bootstrap 5 für das Layout
- Font Awesome für Icons
- CSS für das Design
- JavaScript für Interaktivität

### Hardware-Typen erweitern
Füge neue Hardware-Typen in der Upload-Form hinzu:
```html
<option value="NEUER-TYP">NEUER-TYP</option>
```

## Lizenz

Dieser Code ist Teil des LED Badge Projekts. 