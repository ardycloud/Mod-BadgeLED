# ota_server.py
# Moderner Flask-basierter OTA-Server mit schöner WebUI, Geräte-Checkin, Firmware-Download, Upload-Funktion und Logs

from flask import Flask, request, jsonify, send_from_directory, render_template_string, redirect, url_for, flash
import os
from datetime import datetime, timedelta
import json
from werkzeug.utils import secure_filename
import requests
import re

app = Flask(__name__)
app.secret_key = 'supersecret'

FIRMWARE_DIR = "firmwares"
DEVICE_DB = "devices.json"
LOG_FILE = "checkins.log"
FIRMWARE_METADATA_FILE = "firmware_metadata.json"

os.makedirs(FIRMWARE_DIR, exist_ok=True)

devices = {}
if os.path.exists(DEVICE_DB):
    with open(DEVICE_DB, "r") as f:
        devices = json.load(f)
else:
    devices = {}

# Firmware-Metadaten laden
firmware_metadata = {}
if os.path.exists(FIRMWARE_METADATA_FILE):
    with open(FIRMWARE_METADATA_FILE, "r") as f:
        firmware_metadata = json.load(f)
else:
    firmware_metadata = {}

def cleanup_old_logs():
    """Lösche Log-Einträge älter als 7 Tage"""
    if not os.path.exists(LOG_FILE):
        return
    
    cutoff_date = datetime.utcnow() - timedelta(days=7)
    temp_log_file = LOG_FILE + ".tmp"
    
    try:
        with open(LOG_FILE, "r") as infile, open(temp_log_file, "w") as outfile:
            for line in infile:
                # Versuche das Datum aus der Log-Zeile zu extrahieren
                if line.strip():
                    try:
                        # Format: 2024-01-01T12:00:00.123456 - Badge-12345 - LED-Modd.Badge - SSID - 1.4.1 - 192.168.1.100
                        date_str = line.split(" - ")[0]
                        log_date = datetime.fromisoformat(date_str)
                        if log_date > cutoff_date:
                            outfile.write(line)
                    except:
                        # Wenn Datum-Parsing fehlschlägt, behalte die Zeile
                        outfile.write(line)
        
        # Ersetze die alte Datei
        os.replace(temp_log_file, LOG_FILE)
        print(f"Log-Bereinigung abgeschlossen. Einträge älter als 7 Tage wurden gelöscht.")
    except Exception as e:
        print(f"Fehler bei der Log-Bereinigung: {e}")
        if os.path.exists(temp_log_file):
            os.remove(temp_log_file)

def get_log_entries(search_term="", limit=50):
    """Lade Log-Einträge mit optionaler Suche"""
    if not os.path.exists(LOG_FILE):
        return []
    
    entries = []
    try:
        with open(LOG_FILE, "r") as f:
            lines = f.readlines()
            
        # Filtere nach Suchbegriff
        if search_term:
            search_pattern = re.compile(search_term, re.IGNORECASE)
            lines = [line for line in lines if search_pattern.search(line)]
        
        # Nehme nur die letzten Einträge
        entries = lines[-limit:] if len(lines) > limit else lines
        
    except Exception as e:
        print(f"Fehler beim Lesen der Logs: {e}")
    
    return entries

@app.route("/api/device_checkin", methods=["POST"])
def device_checkin():
    data = request.get_json()
    device_id = data.get("id")
    version = data.get("version")
    ssid = data.get("ssid")
    hardware_type = data.get("hardware_type", "LED-Modd.Badge")  # Neuer Parameter
    wan_ip = request.remote_addr
    now = datetime.utcnow().isoformat()

    print(f"Checkin von {device_id}: Version='{version}', Hardware='{hardware_type}'")
    print(f"Device in DB: {device_id in devices}")
    if device_id in devices:
        print(f"Device Version in DB: '{devices[device_id].get('version', 'N/A')}'")
        print(f"Device Firmware in DB: '{devices[device_id].get('firmware', 'N/A')}'")

    if not device_id:
        return jsonify({"error": "Missing device ID"}), 400

    device = devices.get(device_id, {})
    
    # WICHTIG: Prüfe ob das Gerät in der DB auf "force" steht (Force-Update angefordert)
    db_version = device.get("version", "")
    if db_version == "force" and version != "force" and not device.get("update_sent", False):
        print(f"Force-Update in DB erkannt! DB-Version: '{db_version}', ESP32-Version: '{version}'")
        print(f"Sende set_version='force' an ESP32")
        
        # Sende set_version an ESP32, damit es seine lokale Version auf "force" setzt
        response_data = {
            "update": False,  # Noch kein Update, erst Version synchronisieren
            "locked": False,
            "set_version": "force"  # ESP32 soll seine Version auf "force" setzen
        }
        print(f"Sende Version-Sync-Antwort: {response_data}")
        return jsonify(response_data)
    if device.get("locked", False):
        return jsonify({"locked": True})

    device.update({
        "ssid": ssid,
        "version": version,
        "hardware_type": hardware_type,  # Speichere Hardware-Typ
        "last_seen": now,
        "type": device.get("type", hardware_type),  # Verwende Hardware-Typ als Standard
        "firmware": device.get("firmware", ""),
        "status": "Online",
        "wan_ip": wan_ip,
        "locked": device.get("locked", False)
    })
    
    # Prüfe ob ein Reboot angefordert wurde
    if device.get("reboot_requested", False):
        print(f"Reboot für {device_id} wurde angefordert - sende Reboot-Befehl")
        # Reboot-Flag zurücksetzen
        device["reboot_requested"] = False
        device["reboot_requested_at"] = ""
        
        with open(DEVICE_DB, "w") as f:
            json.dump(devices, f, indent=2)
        
        return jsonify({"reboot": True, "message": "Reboot angefordert"})
    
    devices[device_id] = device

    with open(DEVICE_DB, "w") as f:
        json.dump(devices, f, indent=2)

    with open(LOG_FILE, "a") as log:
        log.write(f"{now} - {device_id} - {hardware_type} - {ssid} - {version} - {wan_ip}\n")

    assigned_firmware = device.get("firmware")
    
    # NOTFALL-FIX: Wenn ESP32 bereits in Force-Update-Schleife steckt und kontinuierlich "force" sendet,
    # aber kein update_sent Flag gesetzt ist, dann ist er in einer Endlosschleife gefangen.
    # In diesem Fall setzen wir ihn auf die aktuelle Firmware-Version zurück.
    if version == "force" and not device.get("update_sent", False) and assigned_firmware:
        if assigned_firmware in firmware_metadata:
            fw_version = firmware_metadata[assigned_firmware].get("version", "")
            if fw_version:
                print(f"NOTFALL-FIX: ESP32 {device_id} aus Force-Update-Schleife befreien")
                print(f"Setze Version von 'force' auf '{fw_version}' zurück")
                
                # Setze ESP32 auf echte Firmware-Version zurück
                device["version"] = fw_version
                device["status"] = "Online"
                
                # Speichere Änderungen
                devices[device_id] = device
                with open(DEVICE_DB, "w") as f:
                    json.dump(devices, f, indent=2)
                
                response_data = {
                    "update": False,
                    "locked": False,
                    "set_version": fw_version,  # Setze ESP32 auf echte Version
                    "message": "Aus Force-Update-Schleife befreit"
                }
                print(f"Sende Notfall-Fix-Antwort: {response_data}")
                return jsonify(response_data)
    
    # Prüfe ob Force-Update angefordert wurde (unabhängig von zugewiesener Firmware)
    if version == "force":
        print(f"Force-Update angefordert für {device_id}")
        
        # Wenn keine Firmware zugewiesen ist, verwende die neueste verfügbare
        if not assigned_firmware:
            available_firmwares = [f for f in os.listdir(FIRMWARE_DIR) if f.endswith('.bin')]
            if available_firmwares:
                # Verwende die neueste Firmware basierend auf Upload-Datum
                latest_firmware = None
                latest_date = None
                
                for fw in available_firmwares:
                    if fw in firmware_metadata:
                        upload_date = firmware_metadata[fw].get("upload_date", "")
                        if upload_date and (latest_date is None or upload_date > latest_date):
                            latest_firmware = fw
                            latest_date = upload_date
                
                if latest_firmware:
                    assigned_firmware = latest_firmware
                    device["firmware"] = assigned_firmware
                    print(f"Verwende neueste Firmware: {assigned_firmware}")
        
        if assigned_firmware and assigned_firmware in firmware_metadata:
            fw_meta = firmware_metadata[assigned_firmware]
            fw_version = fw_meta.get("version", "")
            
            # Sende Update nur einmal - ESP32 wird nach erfolgreichem Update mit neuer Version checken
            response_data = {
                "update": True, 
                "url": f"http://{request.host}/firmwares/{assigned_firmware}",
                "set_version": fw_version  # Setze auf echte Firmware-Version, nicht "force"
            }
            print(f"Sende Force-Update-Antwort: {response_data}")
            
            # Setze Status auf "Update läuft" und merke dir, dass Update gesendet wurde
            device["status"] = "Update läuft"
            device["update_sent"] = True
            device["expected_version"] = fw_version  # Erwartete Version nach Update
            
            # Speichere Änderungen
            devices[device_id] = device
            with open(DEVICE_DB, "w") as f:
                json.dump(devices, f, indent=2)
            
            return jsonify(response_data)
        else:
            print(f"Keine kompatible Firmware für Force-Update gefunden")
            return jsonify({"update": False, "locked": False, "reason": "no_firmware_available"})
    
    # Prüfe ob das Gerät nach einem Force-Update mit neuer Version zurückgekommen ist
    if device.get("update_sent", False) and device.get("expected_version"):
        expected_version = device.get("expected_version")
        print(f"Prüfe Update-Erfolg: Erwartet='{expected_version}', Erhalten='{version}'")
        
        if version == expected_version:
            print(f"Force-Update erfolgreich abgeschlossen! {device_id} läuft jetzt Version {version}")
            
            # Räume Force-Update-Status auf
            device["version"] = version  # Setze auf echte Version
            device["status"] = "Online"
            device.pop("update_sent", None)
            device.pop("expected_version", None)
            
            # Speichere Änderungen
            devices[device_id] = device
            with open(DEVICE_DB, "w") as f:
                json.dump(devices, f, indent=2)
            
            return jsonify({"update": False, "locked": False, "message": "Update erfolgreich abgeschlossen"})
        elif version != "force":
            print(f"Update-Fehler oder unerwartete Version: Erwartet='{expected_version}', Erhalten='{version}'")
            
            # Räume auf, auch wenn Version nicht stimmt (verhindert Endlosschleife)
            device["version"] = version
            device["status"] = "Online"
            device.pop("update_sent", None)
            device.pop("expected_version", None)
            
            devices[device_id] = device
            with open(DEVICE_DB, "w") as f:
                json.dump(devices, f, indent=2)
            
            return jsonify({"update": False, "locked": False, "message": "Update abgeschlossen, aber Version stimmt nicht überein"})
    
    print(f"Kein Force-Update - Version ist '{version}', nicht 'force'")
    
    # Normale Update-Logik für zugewiesene Firmware
    if assigned_firmware and assigned_firmware in firmware_metadata:
        # Prüfe Hardware-Typ-Kompatibilität
        fw_meta = firmware_metadata[assigned_firmware]
        fw_hardware_type = fw_meta.get("hardware_type", "")
        fw_version = fw_meta.get("version", "")
        
        # Prüfe ob Hardware-Typ kompatibel ist
        if fw_hardware_type and fw_hardware_type != hardware_type:
            print(f"Hardware-Typ nicht kompatibel: Gerät={hardware_type}, Firmware={fw_hardware_type}")
            return jsonify({"update": False, "locked": False, "reason": "hardware_type_mismatch"})
        
        # Prüfe ob Version-Update nötig ist
        if fw_version and version != fw_version:
            print(f"Update verfügbar: Aktuell={version}, Verfügbar={fw_version}")
            return jsonify({"update": True, "url": f"http://{request.host}/firmwares/{assigned_firmware}"})
        else:
            print(f"Kein Update nötig: Aktuell={version}, Verfügbar={fw_version}")
    
    return jsonify({"update": False, "locked": False})

@app.route("/api/reboot/<device_id>", methods=["POST"])
def reboot_device(device_id):
    try:
        if device_id in devices:
            device = devices[device_id]
            if device.get("wan_ip"):
                print(f"Reboot-Anfrage für {device_id} (IP: {device['wan_ip']})")
                
                # Da der OTA Server im Internet ist, können wir die ESP32-Geräte 
                # nicht direkt über ihre lokale IP erreichen
                # Stattdessen markieren wir das Gerät für einen Reboot beim nächsten Checkin
                
                device["reboot_requested"] = True
                device["reboot_requested_at"] = datetime.utcnow().isoformat()
                
                with open(DEVICE_DB, "w") as f:
                    json.dump(devices, f, indent=2)
                
                print(f"Reboot für {device_id} beim nächsten Checkin angefordert")
                return jsonify({
                    "success": True, 
                    "message": f"Reboot für {device_id} angefordert. Das Gerät wird beim nächsten Checkin neustarten."
                })
            else:
                return jsonify({"success": False, "message": f"Keine IP-Adresse für {device_id} verfügbar"})
        else:
            return jsonify({"success": False, "message": "Gerät nicht gefunden"})
    except Exception as e:
        print(f"Fehler beim Reboot von {device_id}: {str(e)}")
        return jsonify({"success": False, "message": f"Server-Fehler: {str(e)}"})

@app.route("/api/devices", methods=["GET"])
def get_devices():
    """API-Endpunkt für AJAX-Updates der Geräteliste"""
    now = datetime.utcnow()
    
    for dev in devices.values():
        last_seen = datetime.fromisoformat(dev.get("last_seen", "1970-01-01T00:00:00"))
        if now - last_seen < timedelta(minutes=2):
            dev["status"] = dev.get("status") if dev.get("status") == "Update läuft" else "Online"
        else:
            dev["status"] = "Offline"
    
    return jsonify(devices)

@app.route("/api/logs", methods=["GET"])
def get_logs():
    """API-Endpunkt für AJAX-Updates der Logs"""
    search_term = request.args.get("search", "")
    limit = int(request.args.get("limit", 50))
    
    entries = get_log_entries(search_term, limit)
    return jsonify({"logs": entries, "count": len(entries)})

@app.route("/api/stats", methods=["GET"])
def get_stats():
    """API-Endpunkt für AJAX-Updates der Statistiken"""
    now = datetime.utcnow()
    
    online_count = 0
    locked_count = 0
    
    for dev in devices.values():
        last_seen = datetime.fromisoformat(dev.get("last_seen", "1970-01-01T00:00:00"))
        if now - last_seen < timedelta(minutes=2):
            online_count += 1
        if dev.get("locked", False):
            locked_count += 1
    
    stats = {
        "total_devices": len(devices),
        "online_devices": online_count,
        "locked_devices": locked_count,
        "available_firmwares": len(os.listdir(FIRMWARE_DIR))
    }
    
    return jsonify(stats)

@app.route("/firmwares/<filename>")
def download_firmware(filename):
    return send_from_directory(FIRMWARE_DIR, filename)

@app.route("/force_update/<device_id>")
def force_update(device_id):
    print(f"Force-Update angefordert für {device_id}")
    
    if device_id in devices:
        devices[device_id]["version"] = "force"
        devices[device_id]["status"] = "Update läuft"
        print(f"Version für {device_id} auf 'force' gesetzt")
    else:
        # Gerät noch nicht in DB - erstelle Eintrag
        devices[device_id] = {
            "version": "force",
            "status": "Update läuft",
            "last_seen": datetime.utcnow().isoformat(),
            "hardware_type": "LED-Modd.Badge"
        }
        print(f"Neues Gerät {device_id} mit Version 'force' erstellt")
    
    with open(DEVICE_DB, "w") as f:
        json.dump(devices, f, indent=2)
    
    flash(f"Update für {device_id} forciert.", "info")
    return redirect(url_for('index'))

@app.route("/set_type/<device_id>", methods=["POST"])
def set_type(device_id):
    dtype = request.form.get("type")
    firmware = request.form.get("firmware")
    if device_id in devices:
        devices[device_id]["type"] = dtype
        devices[device_id]["firmware"] = firmware
        with open(DEVICE_DB, "w") as f:
            json.dump(devices, f, indent=2)
        flash(f"Gerät {device_id} aktualisiert.", "success")
    return redirect(url_for('index'))

@app.route("/upload", methods=["POST"])
def upload():
    file = request.files['firmware']
    version = request.form.get('version', '')
    hardware_type = request.form.get('hardware_type', 'LED-Modd.Badge')
    
    if file:
        filename = secure_filename(file.filename)
        filepath = os.path.join(FIRMWARE_DIR, filename)
        file.save(filepath)
        
        # Metadaten für die Firmware speichern
        firmware_metadata[filename] = {
            "version": version,
            "hardware_type": hardware_type,
            "upload_date": datetime.utcnow().isoformat(),
            "file_size": os.path.getsize(filepath)
        }
        
        with open(FIRMWARE_METADATA_FILE, "w") as f:
            json.dump(firmware_metadata, f, indent=2)
        
        flash(f"Firmware hochgeladen: {filename} (v{version}, {hardware_type})", "success")
    return redirect(url_for('index'))

@app.route("/lock/<device_id>")
def lock(device_id):
    if device_id in devices:
        devices[device_id]["locked"] = True
        flash(f"Gerät {device_id} gesperrt.", "warning")
        with open(DEVICE_DB, "w") as f:
            json.dump(devices, f, indent=2)
    return redirect(url_for('index'))

@app.route("/unlock/<device_id>")
def unlock(device_id):
    if device_id in devices:
        devices[device_id]["locked"] = False
        flash(f"Gerät {device_id} entsperrt.", "success")
        with open(DEVICE_DB, "w") as f:
            json.dump(devices, f, indent=2)
    return redirect(url_for('index'))

@app.route("/delete/<device_id>")
def delete_device(device_id):
    if device_id in devices:
        del devices[device_id]
        flash(f"Gerät {device_id} gelöscht.", "info")
        with open(DEVICE_DB, "w") as f:
            json.dump(devices, f, indent=2)
    return redirect(url_for('index'))

@app.route("/")
def index():
    # Log-Bereinigung alle 7 Tage
    cleanup_old_logs()
    
    log_entries = get_log_entries(limit=50)
    available_firmwares = os.listdir(FIRMWARE_DIR)
    now = datetime.utcnow()

    for dev in devices.values():
        last_seen = datetime.fromisoformat(dev.get("last_seen", "1970-01-01T00:00:00"))
        if now - last_seen < timedelta(minutes=2):
            dev["status"] = dev.get("status") if dev.get("status") == "Update läuft" else "Online"
        else:
            dev["status"] = "Offline"

    html = '''
    <!DOCTYPE html>
    <html lang="de">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>LED Badge OTA Manager</title>
        <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css" rel="stylesheet">
        <link href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0/css/all.min.css" rel="stylesheet">
        <style>
            body {
                background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
                min-height: 100vh;
                font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            }
            .card {
                border: none;
                border-radius: 15px;
                box-shadow: 0 10px 30px rgba(0,0,0,0.1);
                backdrop-filter: blur(10px);
                background: rgba(255,255,255,0.95);
            }
            .card-header {
                background: linear-gradient(45deg, #667eea, #764ba2);
                color: white;
                border-radius: 15px 15px 0 0 !important;
                border: none;
            }
            .btn-primary {
                background: linear-gradient(45deg, #667eea, #764ba2);
                border: none;
                border-radius: 25px;
                padding: 10px 25px;
            }
            .btn-primary:hover {
                background: linear-gradient(45deg, #5a6fd8, #6a4190);
                transform: translateY(-2px);
                box-shadow: 0 5px 15px rgba(0,0,0,0.2);
            }
            .status-online {
                color: #28a745;
                font-weight: bold;
            }
            .status-offline {
                color: #dc3545;
                font-weight: bold;
            }
            .status-updating {
                color: #ffc107;
                font-weight: bold;
            }
            .device-card {
                transition: all 0.3s ease;
                margin-bottom: 15px;
            }
            .device-card:hover {
                transform: translateY(-5px);
                box-shadow: 0 15px 35px rgba(0,0,0,0.1);
            }
            .upload-area {
                border: 2px dashed #667eea;
                border-radius: 15px;
                padding: 30px;
                text-align: center;
                background: rgba(102, 126, 234, 0.05);
                transition: all 0.3s ease;
            }
            .upload-area:hover {
                border-color: #764ba2;
                background: rgba(118, 75, 162, 0.05);
            }
            .log-container {
                max-height: 400px;
                overflow-y: auto;
                background: #f8f9fa;
                border-radius: 10px;
                padding: 15px;
                font-family: 'Courier New', monospace;
                font-size: 12px;
            }
            .navbar-brand {
                font-weight: bold;
                font-size: 1.5rem;
            }
            .stats-card {
                background: linear-gradient(45deg, #667eea, #764ba2);
                color: white;
                border-radius: 15px;
                padding: 20px;
                margin-bottom: 20px;
            }
            .action-buttons .btn {
                margin: 2px;
                border-radius: 20px;
                font-size: 0.8rem;
            }
            .firmware-info {
                font-size: 0.8em;
                color: #666;
                margin-top: 5px;
            }
            .log-search {
                margin-bottom: 15px;
            }
            .log-entry {
                padding: 2px 0;
                border-bottom: 1px solid #eee;
            }
            .log-entry:hover {
                background: #f0f0f0;
            }
            .update-indicator {
                position: fixed;
                top: 20px;
                right: 20px;
                background: rgba(0,0,0,0.8);
                color: white;
                padding: 10px 15px;
                border-radius: 5px;
                z-index: 1000;
                display: none;
            }
        </style>
    </head>
    <body>
        <div class="update-indicator" id="updateIndicator">
            <i class="fas fa-sync-alt fa-spin me-2"></i>Daten werden aktualisiert...
        </div>

        <nav class="navbar navbar-expand-lg navbar-dark" style="background: rgba(0,0,0,0.1); backdrop-filter: blur(10px);">
            <div class="container">
                <a class="navbar-brand" href="#">
                    <i class="fas fa-microchip me-2"></i>LED Badge OTA Manager
                </a>
                <div class="navbar-nav ms-auto">
                    <span class="navbar-text">
                        <i class="fas fa-clock me-1"></i><span id="currentTime">{{ now.strftime('%H:%M:%S') }}</span>
                    </span>
                </div>
            </div>
        </nav>

        <div class="container mt-4">
            {% with messages = get_flashed_messages(with_categories=true) %}
                {% if messages %}
                    {% for category, message in messages %}
                        <div class="alert alert-{{ 'danger' if category == 'error' else category }} alert-dismissible fade show" role="alert">
                            {{ message }}
                            <button type="button" class="btn-close" data-bs-dismiss="alert"></button>
                        </div>
                    {% endfor %}
                {% endif %}
            {% endwith %}

            <!-- Statistiken -->
            <div class="row mb-4" id="statsContainer">
                <div class="col-md-3">
                    <div class="stats-card text-center">
                        <i class="fas fa-microchip fa-2x mb-2"></i>
                        <h4 id="totalDevices">{{ devices|length }}</h4>
                        <p class="mb-0">Geräte</p>
                    </div>
                </div>
                <div class="col-md-3">
                    <div class="stats-card text-center">
                        <i class="fas fa-wifi fa-2x mb-2"></i>
                        <h4 id="onlineDevices">{{ devices.values() | selectattr('status', 'equalto', 'Online') | list | length }}</h4>
                        <p class="mb-0">Online</p>
                    </div>
                </div>
                <div class="col-md-3">
                    <div class="stats-card text-center">
                        <i class="fas fa-lock fa-2x mb-2"></i>
                        <h4 id="lockedDevices">{{ devices.values() | selectattr('locked', 'equalto', true) | list | length }}</h4>
                        <p class="mb-0">Gesperrt</p>
                    </div>
                </div>
                <div class="col-md-3">
                    <div class="stats-card text-center">
                        <i class="fas fa-upload fa-2x mb-2"></i>
                        <h4 id="availableFirmwares">{{ available_firmwares|length }}</h4>
                        <p class="mb-0">Firmwares</p>
                    </div>
                </div>
            </div>

            <!-- Firmware Upload -->
            <div class="card mb-4">
                <div class="card-header">
                    <h5 class="mb-0"><i class="fas fa-upload me-2"></i>Firmware hochladen</h5>
                </div>
                <div class="card-body">
                    <form action="/upload" method="post" enctype="multipart/form-data">
                        <div class="upload-area">
                            <i class="fas fa-cloud-upload-alt fa-3x text-primary mb-3"></i>
                            <h5>Firmware-Datei auswählen</h5>
                            <p class="text-muted">Wähle eine .bin Datei für dein LED Badge</p>
                            
                            <div class="row">
                                <div class="col-md-6">
                                    <label class="form-label">Firmware-Datei:</label>
                                    <input type="file" name="firmware" class="form-control" accept=".bin" required>
                                </div>
                                <div class="col-md-3">
                                    <label class="form-label">Version:</label>
                                    <input type="text" name="version" class="form-control" placeholder="z.B. 1.4.1" required>
                                </div>
                                <div class="col-md-3">
                                    <label class="form-label">Geräte-Typ:</label>
                                    <select name="hardware_type" class="form-select" required>
                                        <option value="LED-Modd.Badge">LED-Modd.Badge</option>
                                        <option value="LED-Badge">LED-Badge</option>
                                        <option value="LED-Badge-Pro">LED-Badge-Pro</option>
                                        <option value="LED-Badge-Mini">LED-Badge-Mini</option>
                                    </select>
                                </div>
                            </div>
                            
                            <button type="submit" class="btn btn-primary mt-3">
                                <i class="fas fa-upload me-2"></i>Hochladen
                            </button>
                        </div>
                    </form>
                </div>
            </div>

            <!-- Geräte -->
            <div class="card">
                <div class="card-header d-flex justify-content-between align-items-center">
                    <h5 class="mb-0"><i class="fas fa-microchip me-2"></i>Geräteverwaltung</h5>
                    <button class="btn btn-outline-light btn-sm" onclick="updateDevices()">
                        <i class="fas fa-sync-alt me-1"></i>Aktualisieren
                    </button>
                </div>
                <div class="card-body" id="devicesContainer">
                    {% if devices %}
                        <div class="row">
                            {% for dev_id, info in devices.items() %}
                                <div class="col-lg-6 col-xl-4">
                                    <div class="card device-card">
                                        <div class="card-body">
                                            <div class="d-flex justify-content-between align-items-start mb-3">
                                                <div>
                                                    <h6 class="card-title mb-1">
                                                        <i class="fas fa-microchip me-2"></i>{{ dev_id }}
                                                    </h6>
                                                    <small class="text-muted">
                                                        <strong>Typ:</strong> {{ info.hardware_type or info.type }}
                                                    </small>
                                                </div>
                                                <span class="badge {% if info.status == 'Online' %}bg-success{% elif info.status == 'Update läuft' %}bg-warning{% else %}bg-danger{% endif %}">
                                                    {{ info.status }}
                                                </span>
                                            </div>
                                            
                                            <div class="row mb-3">
                                                <div class="col-6">
                                                    <small class="text-muted">SSID:</small><br>
                                                    <strong>{{ info.ssid }}</strong>
                                                </div>
                                                <div class="col-6">
                                                    <small class="text-muted">Version:</small><br>
                                                    <strong>{{ info.version }}</strong>
                                                </div>
                                            </div>
                                            
                                            <div class="row mb-3">
                                                <div class="col-6">
                                                    <small class="text-muted">IP:</small><br>
                                                    <strong>{{ info.wan_ip }}</strong>
                                                </div>
                                                <div class="col-6">
                                                    <small class="text-muted">Letzter Kontakt:</small><br>
                                                    <strong>{{ info.last_seen[:16] }}</strong>
                                                </div>
                                            </div>

                                            <form action="/set_type/{{ dev_id }}" method="post" class="mb-3">
                                                <div class="row">
                                                    <div class="col-6">
                                                        <input name="type" value="{{ info.type }}" class="form-control form-control-sm" placeholder="Typ">
                                                    </div>
                                                    <div class="col-6">
                                                        <select name="firmware" class="form-select form-select-sm">
                                                            <option value="">Keine Firmware</option>
                                                            {% for fw in available_firmwares %}
                                                                {% set fw_meta = firmware_metadata.get(fw, {}) %}
                                                                <option value="{{ fw }}" {% if fw == info.firmware %}selected{% endif %}>
                                                                    {{ fw }}
                                                                    {% if fw_meta.get('version') %}
                                                                        (v{{ fw_meta.get('version') }})
                                                                    {% endif %}
                                                                </option>
                                                            {% endfor %}
                                                        </select>
                                                    </div>
                                                </div>
                                                <button type="submit" class="btn btn-primary btn-sm w-100 mt-2">
                                                    <i class="fas fa-save me-1"></i>Speichern
                                                </button>
                                            </form>

                                            <div class="action-buttons text-center">
                                                <a href="/force_update/{{ dev_id }}" class="btn btn-warning btn-sm">
                                                    <i class="fas fa-download me-1"></i>Update
                                                </a>
                                                <button onclick="rebootDevice('{{ dev_id }}')" class="btn btn-info btn-sm">
                                                    <i class="fas fa-power-off me-1"></i>Reboot
                                                </button>
                                                {% if info.locked %}
                                                    <a href="/unlock/{{ dev_id }}" class="btn btn-success btn-sm">
                                                        <i class="fas fa-unlock me-1"></i>Entsperren
                                                    </a>
                                                {% else %}
                                                    <a href="/lock/{{ dev_id }}" class="btn btn-secondary btn-sm">
                                                        <i class="fas fa-lock me-1"></i>Sperren
                                                    </a>
                                                {% endif %}
                                                <a href="/delete/{{ dev_id }}" class="btn btn-danger btn-sm" onclick="return confirm('Gerät wirklich löschen?')">
                                                    <i class="fas fa-trash me-1"></i>Löschen
                                                </a>
                                            </div>
                                        </div>
                                    </div>
                                </div>
                            {% endfor %}
                        </div>
                    {% else %}
                        <div class="text-center py-5">
                            <i class="fas fa-microchip fa-3x text-muted mb-3"></i>
                            <h5 class="text-muted">Keine Geräte gefunden</h5>
                            <p class="text-muted">LED Badges werden hier angezeigt, sobald sie sich verbinden.</p>
                        </div>
                    {% endif %}
                </div>
            </div>

            <!-- Logs -->
            <div class="card mt-4">
                <div class="card-header">
                    <h5 class="mb-0"><i class="fas fa-list me-2"></i>System Logs</h5>
                </div>
                <div class="card-body">
                    <div class="log-search">
                        <div class="row">
                            <div class="col-md-6">
                                <input type="text" id="logSearch" class="form-control" placeholder="Logs durchsuchen...">
                            </div>
                            <div class="col-md-3">
                                <select id="logLimit" class="form-select">
                                    <option value="5">5 Einträge</option>
                                    <option value="10">10 Einträge</option>
                                    <option value="25">25 Einträge</option>
                                    <option value="50" selected>50 Einträge</option>
                                </select>
                            </div>
                            <div class="col-md-3">
                                <button onclick="updateLogs()" class="btn btn-primary btn-sm">
                                    <i class="fas fa-search me-1"></i>Suchen
                                </button>
                            </div>
                        </div>
                    </div>
                    <div class="log-container" id="logContainer">
                        {% for entry in log_entries %}
                            <div class="log-entry">{{ entry.strip() }}</div>
                        {% endfor %}
                    </div>
                </div>
            </div>
        </div>

        <script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
        <script>
            let updateInterval;
            
            function showUpdateIndicator() {
                document.getElementById('updateIndicator').style.display = 'block';
            }
            
            function hideUpdateIndicator() {
                document.getElementById('updateIndicator').style.display = 'none';
            }
            
            function updateTime() {
                const now = new Date();
                const timeString = now.toLocaleTimeString('de-DE');
                document.getElementById('currentTime').textContent = timeString;
            }
            
            function updateStats() {
                showUpdateIndicator();
                fetch('/api/stats')
                    .then(response => response.json())
                    .then(data => {
                        document.getElementById('totalDevices').textContent = data.total_devices;
                        document.getElementById('onlineDevices').textContent = data.online_devices;
                        document.getElementById('lockedDevices').textContent = data.locked_devices;
                        document.getElementById('availableFirmwares').textContent = data.available_firmwares;
                        hideUpdateIndicator();
                    })
                    .catch(error => {
                        console.error('Fehler beim Aktualisieren der Statistiken:', error);
                        hideUpdateIndicator();
                    });
            }
            
            function updateDevices() {
                showUpdateIndicator();
                fetch('/api/devices')
                    .then(response => response.json())
                    .then(data => {
                        // Hier könnte man die Geräteliste dynamisch aktualisieren
                        // Für jetzt machen wir ein einfaches Reload
                        location.reload();
                    })
                    .catch(error => {
                        console.error('Fehler beim Aktualisieren der Geräte:', error);
                        hideUpdateIndicator();
                    });
            }
            
            function updateLogs() {
                const searchTerm = document.getElementById('logSearch').value;
                const limit = document.getElementById('logLimit').value;
                
                showUpdateIndicator();
                fetch(`/api/logs?search=${encodeURIComponent(searchTerm)}&limit=${limit}`)
                    .then(response => response.json())
                    .then(data => {
                        const logContainer = document.getElementById('logContainer');
                        logContainer.innerHTML = '';
                        
                        data.logs.forEach(entry => {
                            const logEntry = document.createElement('div');
                            logEntry.className = 'log-entry';
                            logEntry.textContent = entry.trim();
                            logContainer.appendChild(logEntry);
                        });
                        
                        hideUpdateIndicator();
                    })
                    .catch(error => {
                        console.error('Fehler beim Aktualisieren der Logs:', error);
                        hideUpdateIndicator();
                    });
            }
            
            function rebootDevice(deviceId) {
                if (confirm('Möchtest du das Gerät ' + deviceId + ' wirklich neustarten?')) {
                    showUpdateIndicator();
                    fetch('/api/reboot/' + deviceId, {
                        method: 'POST',
                        headers: {
                            'Content-Type': 'application/json',
                        }
                    }).then(response => {
                        if (!response.ok) {
                            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
                        }
                        return response.text().then(text => {
                            try {
                                return JSON.parse(text);
                            } catch (e) {
                                console.error('JSON Parse Error:', e);
                                console.error('Response text:', text);
                                throw new Error('Ungültige Server-Antwort: ' + text);
                            }
                        });
                    })
                    .then(data => {
                        if (data.success) {
                            alert(data.message);
                        } else {
                            alert('Reboot fehlgeschlagen: ' + data.message);
                        }
                        hideUpdateIndicator();
                    }).catch(error => {
                        console.error('Reboot error:', error);
                        alert('Reboot fehlgeschlagen: ' + error.message);
                        hideUpdateIndicator();
                    });
                }
            }
            
            // Event-Listener für Log-Suche
            document.getElementById('logSearch').addEventListener('keypress', function(e) {
                if (e.key === 'Enter') {
                    updateLogs();
                }
            });
            
            document.getElementById('logLimit').addEventListener('change', function() {
                updateLogs();
            });
            
            // Initialisierung
            document.addEventListener('DOMContentLoaded', function() {
                // Zeit jede Sekunde aktualisieren
                setInterval(updateTime, 1000);
                
                // Statistiken alle 30 Sekunden aktualisieren
                setInterval(updateStats, 30000);
                
                // Logs alle 60 Sekunden aktualisieren
                setInterval(updateLogs, 60000);
            });
        </script>
    </body>
    </html>
    '''
    return render_template_string(html, devices=devices, logs=log_entries, available_firmwares=available_firmwares, firmware_metadata=firmware_metadata, now=now)

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)