#ifndef WEBUI_H
#define WEBUI_H

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <vector>
#include "Settings.h"
#include "Config.h"
#include "Display.h"
#include "BQ25895CONFIG.h"  // Für Akku-Funktionen

// Benötigte Typedefs für den JSON-Handler
typedef std::function<void(AsyncWebServerRequest *request, JsonVariant &json)> ArJsonRequestHandlerFunction;
typedef std::function<bool(AsyncWebServerRequest *request)> ArRequestFilterFunction;

// Webserver auf Port 80
extern AsyncWebServer server;

// WiFi Credentials
extern String apName;
extern String apPassword;

// LED-Status
extern bool ledsEnabled;

// HTML Templates
extern const char index_html[] PROGMEM;
extern const char network_html[] PROGMEM;
extern const char display_html[] PROGMEM;

// Externe Variablen
extern uint8_t gCurrentMode;
extern uint8_t BRIGHTNESS;
extern bool wifiEnabled;
extern uint8_t NOISE_LEVEL;
extern uint16_t animationSpeed;
extern uint16_t micFrequency;
extern const bool USE_DISPLAY;

// Display Content
extern DisplayContent displayContent;

// Log-Funktionen
void addEspLog(const String& message);
void addBqLog(const String& message);

// Handler für JSON-Daten
class AsyncCallbackJsonWebHandler: public AsyncWebHandler {
private:
    String _uri;
    ArJsonRequestHandlerFunction _callback;
    ArRequestFilterFunction _filter;

public:
    AsyncCallbackJsonWebHandler(const String& uri, ArJsonRequestHandlerFunction callback) : _uri(uri), _callback(callback) {}
    virtual ~AsyncCallbackJsonWebHandler() {}

    void setFilter(ArRequestFilterFunction filter){
        _filter = filter;
    }

    virtual bool canHandle(AsyncWebServerRequest *request){
        if (_filter && !_filter(request))
            return false;

        if (request->url() != _uri)
            return false;

        return true;
    }

    virtual void handleRequest(AsyncWebServerRequest *request){
        if (request->method() == HTTP_POST || request->method() == HTTP_PUT){
            if (request->contentType() == "application/json"){
                DynamicJsonDocument jsonBuffer(1024);
                DeserializationError error = deserializeJson(jsonBuffer, (const char*)request->_tempObject);
                if (!error){
                    JsonVariant json = jsonBuffer.as<JsonVariant>();
                    _callback(request, json);
                } else {
                    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                }
            } else {
                request->send(400, "application/json", "{\"error\":\"Invalid Content-Type\"}");
            }
        } else {
            request->send(400, "application/json", "{\"error\":\"Invalid HTTP Method\"}");
        }
    }

    virtual void handleUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {}
    virtual void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!index){
            if (request->_tempObject != NULL){
                free(request->_tempObject);
            }
            request->_tempObject = malloc(total);
            if(request->_tempObject == NULL){
                request->send(500, "application/json", "{\"error\":\"Memory allocation failed\"}");
                return;
            }
        }
        memcpy((uint8_t*)(request->_tempObject) + index, data, len);
        if (index + len == total){
            ((uint8_t*)(request->_tempObject))[total] = '\0';
        }
    }
};

// WebServer-Initialisierung
void initWebServer();

#endif // WEBUI_H 