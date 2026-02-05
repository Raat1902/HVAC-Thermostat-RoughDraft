#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Ticker.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include "secrets.h"

// —— CONFIG ——  
const char* SSID = WIFI_SSID;
const char* PASS = WIFI_PASS;
const char* HOSTNAME   = "esp32-thermo";  

#define DHT_PIN         4
#define DHT_TYPE        DHT22
#define RELAY_PIN       5
#define LED_PIN         2

// OLED  
#define SCREEN_W 128  
#define SCREEN_H 64  
#define OLED_RST  -1  

// Timing  
const unsigned long SENSOR_INTERVAL = 2000; // ms  
const unsigned long SCHEDULE_CHECK  = 60000; // ms  

// —— GLOBALS ——  
float currentTemp = 0, targetTemp = 22.0;
float minToday = 100, maxToday = -100;
Preferences prefs;
DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, OLED_RST);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Ticker sensorTicker, scheduleTicker;

// —— HELPERS ——  
void saveTarget() {
  prefs.putFloat("target", targetTemp);
}
void broadcastState() {
  StaticJsonDocument<200> doc;
  doc["current"] = currentTemp;
  doc["target"]  = targetTemp;
  doc["min"]     = minToday;
  doc["max"]     = maxToday;
  doc["heating"] = digitalRead(RELAY_PIN);
  String json;
  serializeJson(doc, json);
  ws.textAll(json);
}

// —— SENSOR READ TASK ——  
void readSensor() {
  float t = dht.readTemperature();
  if (isnan(t)) return;
  currentTemp = t;
  minToday = min(minToday, t);
  maxToday = max(maxToday, t);

  // Hysteresis control
  static bool heating = false;
  const float HYS = 1.0;
  if (!heating && t < targetTemp - HYS) {
    digitalWrite(RELAY_PIN, HIGH);
    heating = true;
  } else if (heating && t > targetTemp + HYS) {
    digitalWrite(RELAY_PIN, LOW);
    heating = false;
  }
  digitalWrite(LED_PIN, heating);

  // Update display
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  display.printf("Now: %.1fC\nTgt: %.1fC\nMin: %.1fC  Max: %.1fC",  
                 currentTemp, targetTemp, minToday, maxToday);
  display.display();

  // Push state
  broadcastState();
}

// —— RESET DAILY STATS AT MIDNIGHT ——  
void resetDaily() {
  // crude check: when hour==0 and minute==0
  time_t now = time(nullptr);
  struct tm* tm = localtime(&now);
  if (tm->tm_hour==0 && tm->tm_min==0) {
    minToday = 100; maxToday = -100;
    broadcastState();
  }
}

// —— WEBSOCKET EVENTS ——  
void onWsEvent(AsyncWebSocket* ws, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type != WS_EVT_DATA) return;
  // expect JSON: {"target":23.5}
  StaticJsonDocument<100> doc;
  DeserializationError err = deserializeJson(doc, data, len);
  if (err) return;
  if (doc.containsKey("target")) {
    targetTemp = doc["target"];
    saveTarget();
    broadcastState();
  }
}

// —— HTTP HANDLERS ——  
void handleStatus(AsyncWebServerRequest* req) {
  StaticJsonDocument<200> doc;
  doc["current"] = currentTemp;
  doc["target"]  = targetTemp;
  doc["min"]     = minToday;
  doc["max"]     = maxToday;
  doc["heating"] = digitalRead(RELAY_PIN);
  String json;
  serializeJson(doc, json);
  req->send(200, "application/json", json);
}

void handleSet(AsyncWebServerRequest* req) {
  if (req->hasParam("t")) {
    targetTemp = req->getParam("t")->value().toFloat();
    saveTarget();
  }
  req->redirect("/");
}

// —— SETUP ——  
void setup() {
  Serial.begin(115200);

  // pins  
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  // DHT + OLED  
  dht.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed"); while(true);
  }
  display.clearDisplay();

  // load prefs  
  prefs.begin("thermo", false);
  targetTemp = prefs.getFloat("target", 22.0);

  // Wi-Fi + mDNS  
  WiFi.begin(SSID, PASS);
  while (WiFi.status()!=WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi up. IP=" + WiFi.localIP().toString());
  if (MDNS.begin(HOSTNAME)) {
    Serial.println("mDNS: http://" + String(HOSTNAME) + ".local");
  }

  // OTA  
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.begin();

  // WebSocket  
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // API endpoints  
  server.on("/api/status", HTTP_GET,  handleStatus);
  server.on("/api/set",    HTTP_GET,  handleSet);
  server.on("/",           HTTP_GET, [&](AsyncWebServerRequest* req){
    req->send(200, "text/plain", "Thermostat is running. Use API or WS.");
  });
  server.onNotFound([](AsyncWebServerRequest* req){
    req->send(404, "text/plain", "Not found");
  });
  server.begin();

  // schedule recurring tasks  
  sensorTicker.attach_ms(SENSOR_INTERVAL, readSensor);
  scheduleTicker.attach_ms(SCHEDULE_CHECK, resetDaily);
}

// —— LOOP ——  
void loop() {
  ArduinoOTA.handle();
  MDNS.update();  
  // everything else is on timers
}
