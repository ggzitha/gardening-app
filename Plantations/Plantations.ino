// =============================================================================
//  🌿 Garden Monitoring System - ESP32 Firmware (Enhanced)
//  Board  : ESP32 WROOM-32 (4MB)
//  Author : Auto-generated
//  Date   : 2026-06-20
// =============================================================================
//
//  ENHANCEMENTS:
//  ✓ MQTT Optional (non-blocking, ping-check before connect)
//  ✓ Local SQLite-style database using Preferences (FIFO)
//  ✓ Embedded ESP32 Web Server (no external Docker needed)
//  ✓ Cloud sync when MQTT server is available
//  ✓ Reboot, OTA mode, Clear DB via Web UI
//
//  PARTITION SCHEME: "Huge App (3MB APP / 1MB SPIFFS)" - NO OTA
//
// =============================================================================

// ───── Library Includes ──────────────────────────────────────────────────────
#include <WiFi.h>
#include <WiFiMulti.h>
#include <esp_wifi.h>
#include <time.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <ClosedCube_HDC1080.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <INA226.h>
#include <RTClib.h>
#include <Preferences.h>

// ── Web Server & Async (ESP32Async - NOT me-no-dev) ─────────────────────────
// Install from: https://github.com/ESP32Async/ESPAsyncWebServer (includes WebSocket)
// Install from: https://github.com/ESP32Async/AsyncTCP
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <LittleFS.h>  // Use LittleFS for huge_app partition

// ── OTA (Manual trigger only, not auto-OTA) ─────────────────────────────────
#include <Update.h>

// ───── Preferences (NVM) ────────────────────────────────────────────────────
Preferences preferences;

// ───── Configuration ────────────────────────────────────────────────────────
struct Config {
  bool isAutoInterval = true;
  unsigned long targetCycleIntervalMs = 120000UL;
  bool mqttEnabled = true;
  bool otaEnabled = false;
};

Config config;

// ───── Pin Definitions ──────────────────────────────────────────────────────
// Soil Moisture (capacitive) – ADC1 channels (safe with WiFi)
#define PIN_SOIL1 34  // ADC1_CH6
#define PIN_SOIL2 35  // ADC1_CH7
#define PIN_SOIL3 32  // ADC1_CH4
// Soil PH Sensor
#define PIN_PH 36  // ADC1_CH0

// UV Sensor (GUVA-S12SD)
#define PIN_UV 33  // ADC1_CH5

// DS18B20 Soil Temperature
#define PIN_DS18B20 4  // 1-Wire data

// MOSFET Gates
#define PIN_MOSFET_VALVE 26  // Controls solenoid water valve
#define PIN_MOSFET_3V3 25    // Controls 3.3V sensors power (AHT10, etc)
#define PIN_MOSFET_5V 27     // Controls 5V sensors power (NPK RS485)

// RS485 for NPK Sensor
#define PIN_RS485_RO 16    // Connect to RO (Receiver Output) on MAX485
#define PIN_RS485_DI 17    // Connect to DI (Data In) on MAX485
#define PIN_RS485_DE_RE 5  // Connect to both DE and RE pins (Drive Enable)

// I2C (SDA=21, SCL=22 default) → AHT10 + INA226 + ADS1015

// ───── Calibration Configuration ─────────────────────────────────────────────
#define MULT_SOIL_TEMP 1.3494f
#define OFFSET_SOIL_TEMP -9.2477f
#define MULT_AHT_TEMP 1.1817f
#define OFFSET_AHT_TEMP -5.9871f
#define MULT_AHT_HUMIDITY 1.0562f
#define OFFSET_AHT_HUMIDITY -14.9680f
#define MULT_HDC_TEMP 1.0199f
#define OFFSET_HDC_TEMP -1.1547f
#define MULT_HDC_HUM 1.0190f
#define OFFSET_HDC_HUM -5.1205f
#define MULT_BMP_TEMP 1.0239f
#define OFFSET_BMP_TEMP -3.9511f
#define MULT_BMP_PRES 1.0f
#define OFFSET_BMP_PRES 0.0f
#define MULT_SOC_TEMP 1.0f
#define OFFSET_SOC_TEMP 0.0f
#define MULT_N 1.0f
#define OFFSET_N 0.0f
#define MULT_P 1.0f
#define OFFSET_P 0.0f
#define MULT_K 1.0f
#define OFFSET_K 0.0f
#define MULT_UV 1.0f
#define OFFSET_UV 0.0f
#define MULT_PH 1.0f
#define OFFSET_PH 0.0f

// ───── WiFi Credentials ────────────────────────────────────────────────────
const char* WIFI_SSID1 = "DCLXVI";
const char* WIFI_PASS1 = "1029384756";
const char* WIFI_SSID2 = "BBWV_Oprasional";
const char* WIFI_PASS2 = "Balai5-OPRA123!";
const char* WIFI_SSID3 = "BMKG-JAYAPURA";
const char* WIFI_PASS3 = "bmkg@123";

// ───── NTP ──────────────────────────────────────────────────────────────────
const char* NTP1_SERVER = "pool.ntp.org";
const char* NTP2_SERVER = "time.google.com";
const char* NTP3_SERVER = "time.cloudflare.com";
const long GMT_OFFSET_SEC = 9 * 3600;  // UTC+9
const int DAYLIGHT_OFFSET_SEC = 0;

// ───── MQTT Configuration ────────────────────────────────────────────────────
const char* MQTT_SERVER = "192.168.88.8";
const int MQTT_PORT = 1883;
const char* MQTT_USER   = "inskal";
const char* MQTT_PASS   = "admin_inskal_mqtt";
const char* MQTT_CLIENT_ID = "garden_esp32";
const char* MQTT_TOPIC_PUB = "garden/sensors";
const char* MQTT_TOPIC_CMD = "garden/commands";

// ───── Timing ───────────────────────────────────────────────────────────────
const unsigned long CYCLE_INTERVAL_MS = 120000UL;  // 2 minutes

// ───── Thresholds ───────────────────────────────────────────────────────────
const int SOIL_DRY_THRESHOLD = 2800;
const int SOIL_WET_THRESHOLD = 1800;
const float TEMP_HOT_THRESHOLD = 35.0f;
const unsigned long WATER_MAX_MS = 180000UL;  // 3 minutes max

// ───── INA226 Calibration ───────────────────────────────────────────────────
#define INA226_SHUNT_OHM 0.0115f
#define INA226_Current_LSB_mA 0.20f
#define INA226_Current_Zero_Offset 0.0f
#define INA226_Bus_V_scaling 10000
#define INA226_I_OFFSET_mA -198.593f
#define INA226_V_OFFSET_v -0.155f

// ───── Global Objects ───────────────────────────────────────────────────────
WiFiMulti wifiMulti;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

OneWire oneWire(PIN_DS18B20);
DallasTemperature ds18b20(&oneWire);

Adafruit_AHTX0 aht10;
Adafruit_BMP280 bmp280;
ClosedCube_HDC1080 hdc1080;
INA226 ina226(0x45);
RTC_DS3231 rtc;

HardwareSerial rs485Serial(2);  // UART2

// ───── Web Server ──────────────────────────────────────────────────────────
AsyncWebServer server(80);
AsyncWebSocket wsServer("/ws");
bool wsClientConnected = false;

// ───── State Variables ─────────────────────────────────────────────────────
enum CycleState {
  STATE_SENSORS_OFF,
  STATE_SENSORS_WARMUP,
  STATE_SENSORS_SAMPLING
};
CycleState cycleState = STATE_SENSORS_WARMUP;
unsigned long stateTimer = 0;
int sampleCount = 0;

unsigned long wateringStartMs = 0;
bool isWatering = false;
float wateringStartMoisture = 0.0f;
bool manualWateringActive = false;

// ───── Sensor Data Structure ───────────────────────────────────────────────
struct SensorData {
  int soil1Raw, soil2Raw, soil3Raw;
  float soilMoistureAvgPct;
  float soilTempC;
  float ambientTempC;
  float humidityPct;
  float uvIndex;
  int rainRaw;
  bool isRaining;
  float nitrogenPpm;
  float phosphorusPpm;
  float potassiumPpm;
  float phValue;
  bool npkValid;
  float busVoltageV;
  float currentMA;
  float powerMW;
  bool isCharging;
  float envTempC;
  float envHumPct;
  float intTempC;
  float intPresHPa;
  float socTempC;
  time_t timestamp;
  uint8_t stat_rtc, stat_npk, stat_aht, stat_ds18, stat_ina, stat_bmp, stat_hdc;
  float presHistory[12];
  int presIdx = 0;
  bool presHistReady = false;
  unsigned long lastPresHistoryUpdate = 0;
  bool remoteRainPredicted = false;
  unsigned long lastRemoteRainMs = 0;
} sensorData;

// ───── Accumulator ─────────────────────────────────────────────────────────
struct SensorAccumulator {
  float soilMoistureAvgPct; int c_soilMoisture = 0;
  float soilTempC; int c_soilTemp = 0;
  float ambientTempC; int c_ambientTemp = 0;
  float humidityPct; int c_humidity = 0;
  float uvIndex; int c_uv = 0;
  float nitrogenPpm; int c_nitrogen = 0;
  float phosphorusPpm; int c_phosphorus = 0;
  float potassiumPpm; int c_potassium = 0;
  float phValue; int c_ph = 0;
  float busVoltageV; int c_busVoltage = 0;
  float currentMA; int c_current = 0;
  float powerMW; int c_power = 0;
  float envTempC; int c_envTemp = 0;
  float envHumPct; int c_envHum = 0;
  float intTempC; int c_intTemp = 0;
  float intPresHPa; int c_intPres = 0;
  float socTempC; int c_socTemp = 0;
  void reset() { memset(this, 0, sizeof(SensorAccumulator)); }
} acc;

// ───── LittleFS Debug Function ─────────────────────────────────────────────
void listSPIFFSFiles() {
  Serial.println(F("\n📁 LittleFS Files Listing:"));
  File root = LittleFS.open("/");
  if (!root) {
    Serial.println(F("❌ Failed to open root directory"));
    return;
  }
  
  if (!root.isDirectory()) {
    Serial.println(F("❌ Root is not a directory"));
    root.close();
    return;
  }
  
  File file = root.openNextFile();
  int fileCount = 0;
  while (file) {
    if (file.isDirectory()) {
      Serial.printf("  📂 %s/\n", file.name());
    } else {
      Serial.printf("  📄 %s (%d bytes)\n", file.name(), file.size());
      fileCount++;
    }
    file = root.openNextFile();
  }
  
  if (fileCount == 0) {
    Serial.println(F("  ⚠️  No files found in LittleFS!"));
    Serial.println(F("  💡 Make sure to upload data folder using LittleFS Upload"));
  } else {
    Serial.printf("  ✅ Total: %d files\n", fileCount);
  }
  
  root.close();
  Serial.printf("  💾 LittleFS Stats: Total=%d, Used=%d, Free=%d bytes\n", 
                LittleFS.totalBytes(), LittleFS.usedBytes(), LittleFS.totalBytes() - LittleFS.usedBytes());
  Serial.println();
}

// ───── Local Database (LittleFS-based) ─────────────────────────────────────
#define DB_MAGIC "GDBS"
#define DB_VERSION 1
#define DB_MAX_RECORDS 10000  // FIFO limit

struct __attribute__((packed)) SensorRecord {
  uint32_t magic;
  uint32_t timestamp;
  int16_t soil1Raw, soil2Raw, soil3Raw;
  float soilMoisture, soilTemp, ambientTemp, humidity;
  float uvIndex, batteryV, currentMA, powerMW;
  float nitrogen, phosphorus, potassium, ph;
  float envTemp, envHum, intTemp, intPres;
  uint8_t isRaining, valveOpen;
  uint8_t statusFlags;
};

bool initDatabase();
bool dbAddRecord();
void dbSyncToCloud();
void dbClear();
void dbGetStats(JsonObject &doc);
uint32_t dbGetRecordCount();
void dbPruneOldest(uint32_t keepCount);
bool dbInitialized = false;  // Track if database is ready

// ───── Database Filename (declared early for use by serveHistory) ─────────────
const char* DB_FILENAME = "/garden.db";

// ───── Forward Declarations ─────────────────────────────────────────────────
void connectWiFi();
void syncNTP();
void initMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
bool pingMQTTHost();
void readSensors();
void readNPKandPH();
void processAndPublishData();
void logSystemEvents();
void checkAutoWatering();
void controlValve(bool open, bool isManual = false);
String buildJsonPayload();
float rawToMoisturePct(int raw);
float rawToUVIndex(int raw);
void setupWebServer();
void sendWebSocketData();
void initSensors();
void resetI2CBus();
void serveHistory(AsyncWebServerRequest* request, const String& sensor);

// ═════════════════════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println(F("\n🌿 Garden Monitor Booting (Enhanced v2.0)..."));
  Serial.printf("   Partition: Huge APP (3MB) / SPIFFS (1MB)\n");
  Serial.printf("   MQTT: Optional (syncs when available)\n");
  Serial.printf("   Local DB: LittleFS with FIFO\n");

  // Pin modes
  pinMode(PIN_MOSFET_VALVE, OUTPUT);
  digitalWrite(PIN_MOSFET_VALVE, LOW);
  pinMode(PIN_MOSFET_3V3, OUTPUT);
  digitalWrite(PIN_MOSFET_3V3, HIGH);
  pinMode(PIN_MOSFET_5V, OUTPUT);
  digitalWrite(PIN_MOSFET_5V, HIGH);
  pinMode(PIN_RS485_DE_RE, OUTPUT);
  digitalWrite(PIN_RS485_DE_RE, LOW);

  // ADC attenuation
  analogSetAttenuation(ADC_11db);

  // RS485 Serial
  rs485Serial.begin(4800, SERIAL_8N1, PIN_RS485_RO, PIN_RS485_DI);

  // Initialize Preferences
  preferences.begin("garden", false);
  config.isAutoInterval = preferences.getBool("auto_interval", true);
  config.targetCycleIntervalMs = preferences.getULong("interval_ms", 120000UL);
  config.mqttEnabled = preferences.getBool("mqtt_enabled", true);
  if (config.targetCycleIntervalMs < 120000UL) config.targetCycleIntervalMs = 120000UL;

  // Initialize LittleFS for web files
  if (!LittleFS.begin(true)) {
    Serial.println(F("⚠️  LittleFS mount failed"));
  } else {
    Serial.println(F("✅ LittleFS initialized"));
  }
  
  // Debug: List all files in SPIFFS
  listSPIFFSFiles();

  // Initialize sensors
  initSensors();

  // RTC DS3231
  if (!rtc.begin()) {
    Serial.println(F("⚠️  RTC not found"));
    sensorData.stat_rtc = 2;
  } else {
    sensorData.stat_rtc = rtc.lostPower() ? 1 : 0;
  }

  // WiFi
  connectWiFi();

  // NTP
  syncNTP();

  // Initialize local database
  initDatabase();

  // MQTT (optional)
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);
  if (config.mqttEnabled) initMQTT();

  // Web Server
  setupWebServer();

  // Enable WiFi power saving
  esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
  Serial.println(F("✅ WiFi Auto Modem Sleep enabled"));

  Serial.println(F("✅ Setup complete!"));
  acc.reset();
  stateTimer = millis();
}

// ═════════════════════════════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  // AsyncWebSocket is event-driven - no loop() needed

  // Keep MQTT alive (optional)
  if (config.mqttEnabled && mqttClient.connected()) {
    mqttClient.loop();
  } else if (config.mqttEnabled && WiFi.status() == WL_CONNECTED) {
    // Try reconnecting MQTT periodically
    static unsigned long lastMqttRetry = 0;
    if (now - lastMqttRetry > 60000) {  // Every 60 seconds
      lastMqttRetry = now;
      initMQTT();
      // If connected, sync local DB to cloud
      if (mqttClient.connected()) {
        dbSyncToCloud();
      }
    }
  }

  // State machine
  switch (cycleState) {
    case STATE_SENSORS_OFF: {
      unsigned long offDurationMs = config.targetCycleIntervalMs - 15000UL;
      if (now - stateTimer >= offDurationMs) {
        digitalWrite(PIN_MOSFET_3V3, HIGH);
        digitalWrite(PIN_MOSFET_5V, HIGH);
        stateTimer = now;
        cycleState = STATE_SENSORS_WARMUP;
        Serial.println(F("⚡ Sensors ON, warming up..."));
      }
      break;
    }

    case STATE_SENSORS_WARMUP:
      if (now - stateTimer >= 5000UL) {
        stateTimer = now;
        sampleCount = 0;
        acc.reset();
        cycleState = STATE_SENSORS_SAMPLING;
        Serial.println(F("🔍 Warmup done, initializing sensors..."));
        
        pinMode(PIN_RS485_DE_RE, OUTPUT);
        digitalWrite(PIN_RS485_DE_RE, LOW);
        rs485Serial.begin(4800, SERIAL_8N1, PIN_RS485_RO, PIN_RS485_DI);
        initSensors();
      }
      break;

    case STATE_SENSORS_SAMPLING:
      if (now - stateTimer >= 2000UL) {
        stateTimer = now;
        readSensors();
        sampleCount++;
        if (sampleCount >= 5) {
          processAndPublishData();
          checkAutoWatering();
          logSystemEvents();

          // Store to local DB
          dbAddRecord();

          // Disable sensors to save power
          digitalWrite(PIN_MOSFET_3V3, LOW);
          digitalWrite(PIN_MOSFET_5V, LOW);
          Wire.end();
          pinMode(21, INPUT);
          pinMode(22, INPUT);
          rs485Serial.end();
          pinMode(PIN_RS485_RO, INPUT);
          pinMode(PIN_RS485_DI, INPUT);
          pinMode(PIN_RS485_DE_RE, INPUT);
          pinMode(PIN_DS18B20, INPUT);
          
          // Dynamic interval if AUTO
          if (config.isAutoInterval) {
            if (sensorData.busVoltageV > 1.0f && sensorData.busVoltageV < 3.7f) {
              config.targetCycleIntervalMs = 600000UL;
              Serial.println(F("🔋 Low battery - slowing to 10m interval"));
            } else {
              config.targetCycleIntervalMs = 120000UL;
            }
          }

          stateTimer = now;
          cycleState = STATE_SENSORS_OFF;
          Serial.println(F("💤 Publish done, sensors OFF."));
        }
      }
      break;
  }

  // Watering watchdog
  if (isWatering && (now - wateringStartMs >= WATER_MAX_MS)) {
    Serial.println(F("⏱️  Watering timeout – closing valve"));
    controlValve(false);
  }

  delay(100);
}

// ═════════════════════════════════════════════════════════════════════════════
//  WiFi
// ═════════════════════════════════════════════════════════════════════════════
void connectWiFi() {
  Serial.println(F("📶 Connecting to WiFi..."));
  wifiMulti.addAP(WIFI_SSID1, WIFI_PASS1);
  wifiMulti.addAP(WIFI_SSID2, WIFI_PASS2);
  wifiMulti.addAP(WIFI_SSID3, WIFI_PASS3);

  unsigned long t = millis();
  while (wifiMulti.run() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
    if (millis() - t > 30000) {
      Serial.println(F("\n⚠️  WiFi timeout – continuing offline"));
      return;
    }
  }
  Serial.printf("\n✅ WiFi connected: %s (IP: %s)\n",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

// ═════════════════════════════════════════════════════════════════════════════
//  NTP Sync
// ═════════════════════════════════════════════════════════════════════════════
void syncNTP() {
  Serial.println(F("🕐 Syncing NTP..."));
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP1_SERVER, NTP2_SERVER, NTP3_SERVER);
  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry++ < 10) {
    delay(1000);
    Serial.print('.');
  }
  if (retry < 10) {
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.printf("\n✅ Time synced: %s\n", buf);

    if (sensorData.stat_rtc != 2) {
      rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
      Serial.println(F("✅ DS3231 RTC Updated"));
    }
  } else {
    Serial.println(F("\n⚠️  NTP sync failed"));
  }
}

// ═════════════════════════════════════════════════════════════════════════════
//  MQTT (Optional - Non-blocking)
// ═════════════════════════════════════════════════════════════════════════════
bool pingMQTTHost() {
  if (WiFi.status() != WL_CONNECTED) return false;
  
  WiFiClient pingClient;
  pingClient.setTimeout(2000);
  
  if (pingClient.connect(MQTT_SERVER, MQTT_PORT)) {
    pingClient.stop();
    return true;
  }
  return false;
}

void initMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  Serial.printf("🔌 Checking MQTT at %s:%d...", MQTT_SERVER, MQTT_PORT);
  
  // Non-blocking: skip if server unreachable
  if (!pingMQTTHost()) {
    Serial.println(F(" unreachable – MQTT disabled for now"));
    return;
  }
  
  Serial.println(F(" reachable, connecting..."));
  int attempts = 0;
  while (!mqttClient.connected() && attempts++ < 3) {  // Only 3 attempts, not 5
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
      Serial.println(F("✅ MQTT connected"));
      mqttClient.subscribe(MQTT_TOPIC_CMD);
      
      // Sync local DB to cloud on successful reconnect
      dbSyncToCloud();
    } else {
      Serial.printf(" failed (rc=%d), retry...\n", mqttClient.state());
      delay(2000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String msg = String((char*)payload);
  Serial.printf("📩 MQTT CMD: %s\n", msg.c_str());

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, msg)) return;

  const char* cmd = doc["cmd"];
  if (!cmd) return;

  if (strcmp(cmd, "water_start") == 0) {
    controlValve(true, true);
  } else if (strcmp(cmd, "water_stop") == 0) {
    controlValve(false);
  } else if (strcmp(cmd, "sync_rtc") == 0) {
    syncNTP();
  } else if (strcmp(cmd, "set_interval") == 0) {
    String val = doc["val"].as<String>();
    if (val == "auto") {
      config.isAutoInterval = true;
      preferences.putBool("auto_interval", true);
      Serial.println(F("⏱️  Interval set to AUTO"));
    } else {
      long ms = val.toInt();
      if (ms >= 120000) {
        config.isAutoInterval = false;
        config.targetCycleIntervalMs = ms;
        preferences.putBool("auto_interval", false);
        preferences.putULong("interval_ms", ms);
        Serial.printf("⏱️  Interval set to %ld ms\n", ms);
      }
    }
  } else if (strcmp(cmd, "weather_prediction") == 0) {
    sensorData.remoteRainPredicted = doc["raining"] | false;
    sensorData.lastRemoteRainMs = millis();
  } else if (strcmp(cmd, "db_stats") == 0) {
    // Return DB stats via MQTT
    StaticJsonDocument<512> resp;
    JsonObject obj = resp.as<JsonObject>();
    dbGetStats(obj);
    char buf[512];
    serializeJson(resp, buf);
    mqttClient.publish(MQTT_TOPIC_PUB, buf);
  }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Web Server
// ═════════════════════════════════════════════════════════════════════════════
void setupWebServer() {
  // CORS headers - must be added BEFORE other handlers
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
  
  // LittleFS file server - serve static files from /data folder
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // API: Latest reading
  server.on("/api/latest", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = buildJsonPayload();
    request->send(200, "application/json", json);
  });

  // API: Database stats (expanded for comprehensive storage info)
  server.on("/api/db/stats", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<2048> doc;
    JsonObject obj = doc.as<JsonObject>();
    dbGetStats(obj);
    doc["ip"] = WiFi.localIP().toString();
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["sketch_size"] = ESP.getSketchSize();
    doc["free_flash"] = ESP.getFreeSketchSpace();
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // API: Clear database
  server.on("/api/db/clear", HTTP_POST, [](AsyncWebServerRequest *request) {
    dbClear();
    request->send(200, "application/json", "{\"ok\":true,\"message\":\"Database cleared\"}");
  });

  // API: Command — reads JSON body from POST request
  // Uses onRequestBody callback to accumulate raw body
  server.on("/api/command", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Body was accumulated by onRequestBody callback below
    if (request->_tempObject == nullptr) {
      request->send(400, "application/json", "{\"error\":\"No body received\"}");
      return;
    }
    
    String body = *(String*)request->_tempObject;
    delete (String*)request->_tempObject;
    request->_tempObject = nullptr;

    Serial.printf("📥 Command body: %s\n", body.c_str());

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, body);
    if (error) {
      request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    const char* cmd = doc["cmd"] | "";
    String response = "{\"ok\":true,\"cmd\":\"" + String(cmd) + "\"}";

    if (strcmp(cmd, "water_start") == 0) {
      controlValve(true, true);
    } else if (strcmp(cmd, "water_stop") == 0) {
      controlValve(false);
    } else if (strcmp(cmd, "sync_rtc") == 0) {
      syncNTP();
    } else if (strcmp(cmd, "reboot") == 0) {
      request->send(200, "application/json", "{\"ok\":true,\"message\":\"Rebooting...\"}");
      delay(500);
      ESP.restart();
    } else if (strcmp(cmd, "ota_enable") == 0) {
      config.otaEnabled = true;
      preferences.putBool("ota_enabled", true);
      response = "{\"ok\":true,\"message\":\"OTA enabled on port 3232\"}";
    } else if (strcmp(cmd, "ota_disable") == 0) {
      config.otaEnabled = false;
      preferences.putBool("ota_enabled", false);
      response = "{\"ok\":true,\"message\":\"OTA disabled\"}";
    } else if (strcmp(cmd, "set_interval") == 0) {
      String val = doc["val"] | "";
      if (val == "auto") {
        config.isAutoInterval = true;
        preferences.putBool("auto_interval", true);
      } else {
        long ms = val.toInt();
        if (ms >= 120000) {
          config.isAutoInterval = false;
          config.targetCycleIntervalMs = ms;
          preferences.putBool("auto_interval", false);
          preferences.putULong("interval_ms", ms);
        }
      }
    } else if (strcmp(cmd, "mqtt_enable") == 0) {
      config.mqttEnabled = true;
      preferences.putBool("mqtt_enabled", true);
      initMQTT();
    } else if (strcmp(cmd, "mqtt_disable") == 0) {
      config.mqttEnabled = false;
      preferences.putBool("mqtt_enabled", false);
      mqttClient.disconnect();
    } else if (strcmp(cmd, "purge_db") == 0) {
      dbClear();
      response = "{\"ok\":true,\"message\":\"Database purged\"}";
    }

    request->send(200, "application/json", response);
  });

  // onRequestBody callback - runs BEFORE the handler
  // Must be registered BEFORE server.begin()
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Only accumulate for /api/command POST
    if (request->url() != "/api/command" || request->method() != HTTP_POST) return;
    
    // Allocate String on first chunk
    if (index == 0) {
      if (request->_tempObject != nullptr) {
        delete (String*)request->_tempObject;
      }
      request->_tempObject = new String();
    }
    
    // Append data to String
    if (request->_tempObject) {
      ((String*)request->_tempObject)->concat((char*)data, len);
    }
  });

  // API: History (reads from local DB)
  // Supports: /api/history?sensor=soil_moisture OR /api/history/soil_moisture
  server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest *request) {
    String sensor = "soil_moisture";
    if (request->hasParam("sensor")) {
      sensor = request->getParam("sensor")->value();
    }
    serveHistory(request, sensor);
  });

  // Additional route: /api/history/<sensor>
  server.on("/api/history/*", HTTP_GET, [](AsyncWebServerRequest *request) {
    String url = request->url();
    int slashIdx = url.lastIndexOf('/');
    String sensor = (slashIdx > 0) ? url.substring(slashIdx + 1) : "soil_moisture";
    int qIdx = sensor.indexOf('?');
    if (qIdx > 0) sensor = sensor.substring(0, qIdx);
    serveHistory(request, sensor);
  });

  // Fallback to index.html for SPA
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  // Start server
  server.begin();
  Serial.println(F("✅ Web server started on port 80"));

// WebSocket - AsyncWebSocket is built into ESPAsyncWebServer
  wsServer.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
      case 0: // WS_CONNECTED
        wsClientConnected = true;
        Serial.printf("WS [%u] Connected\n", client->id());
        sendWebSocketData();
        break;
      case 1: // WS_DISCONNECTED
        wsClientConnected = false;
        Serial.printf("WS [%u] Disconnected\n", client->id());
        break;
      case 2: // WS_EVT_DATA (text or binary)
        if (len > 0 && data) {
          data[len] = '\0';
          String msg = (char*)data;
          StaticJsonDocument<256> doc;
          if (!deserializeJson(doc, msg)) {
            const char* cmd = doc["cmd"];
            if (cmd) {
              if (strcmp(cmd, "water_start") == 0) controlValve(true, true);
              else if (strcmp(cmd, "water_stop") == 0) controlValve(false);
              else if (strcmp(cmd, "sync_rtc") == 0) syncNTP();
              else if (strcmp(cmd, "get_stats") == 0) sendWebSocketData();
            }
          }
        }
        break;
      default:
        break;
    }
  });
  server.addHandler(&wsServer);
  Serial.println(F("✅ WebSocket enabled at /ws"));
}

void sendWebSocketData() {
  if (!wsClientConnected) return;
  
  StaticJsonDocument<1024> doc;
  doc["ts"] = sensorData.timestamp;
  doc["soil_moisture"] = sensorData.soilMoistureAvgPct;
  doc["soil_temp"] = sensorData.soilTempC;
  doc["ambient_temp"] = sensorData.ambientTempC;
  doc["humidity"] = sensorData.humidityPct;
  doc["uv_index"] = sensorData.uvIndex;
  doc["battery_v"] = sensorData.busVoltageV;
  doc["current_ma"] = sensorData.currentMA;
  doc["is_charging"] = sensorData.isCharging;
  doc["valve_open"] = isWatering;
  doc["is_raining"] = sensorData.isRaining;
  doc["ph"] = sensorData.phValue;
  doc["nitrogen"] = sensorData.nitrogenPpm;
  doc["phosphorus"] = sensorData.phosphorusPpm;
  doc["potassium"] = sensorData.potassiumPpm;
  doc["env_temp"] = sensorData.envTempC;
  doc["env_hum"] = sensorData.envHumPct;
  doc["int_temp"] = sensorData.intTempC;
  doc["int_pres"] = sensorData.intPresHPa;
  doc["wifi_ip"] = WiFi.localIP().toString();
  doc["wifi_rssi"] = WiFi.RSSI();
  
  String json;
  serializeJson(doc, json);
  wsServer.textAll(json);
}

// ───── History API Handler ───────────────────────────────────────────────────
void serveHistory(AsyncWebServerRequest* request, const String& sensor) {
  StaticJsonDocument<2048> doc;
  JsonArray data = doc.createNestedArray("data");

  File f = LittleFS.open(DB_FILENAME, "r");
  if (f) {
    SensorRecord rec;
    int count = 0;
    // Read from newest to oldest (reverse order)
    f.seek(0, SeekEnd);
    long fileSize = f.size();
    long pos = fileSize - sizeof(rec);
    
    while (pos >= 0 && count < 100) {
      f.seek(pos);
      if (f.readBytes((char*)&rec, sizeof(rec)) == sizeof(rec)) {
        if (rec.magic == 0x47444253) {
          float val = 0;
          // Map sensor name to record field
          if (sensor == "soil_moisture") val = rec.soilMoisture;
          else if (sensor == "soil1_pct") {
            int dr = rec.soil1Raw;
            val = constrain(((3200 - dr) / (3200.0 - 1200.0)) * 100.0, 0, 100);
          }
          else if (sensor == "soil2_pct") {
            int dr = rec.soil2Raw;
            val = constrain(((3200 - dr) / (3200.0 - 1200.0)) * 100.0, 0, 100);
          }
          else if (sensor == "soil3_pct") {
            int dr = rec.soil3Raw;
            val = constrain(((3200 - dr) / (3200.0 - 1200.0)) * 100.0, 0, 100);
          }
          else if (sensor == "battery_v") val = rec.batteryV;
          else if (sensor == "current_ma") val = rec.currentMA;
          else if (sensor == "power_mw") val = rec.powerMW;
          else if (sensor == "soil_temp") val = rec.soilTemp;
          else if (sensor == "humidity") val = rec.humidity;
          else if (sensor == "ambient_temp") val = rec.ambientTemp;
          else if (sensor == "env_temp") val = rec.envTemp;
          else if (sensor == "env_hum") val = rec.envHum;
          else if (sensor == "int_temp") val = rec.intTemp;
          else if (sensor == "int_pres") val = rec.intPres;
          else if (sensor == "uv_index") val = rec.uvIndex;
          else if (sensor == "nitrogen") val = rec.nitrogen;
          else if (sensor == "phosphorus") val = rec.phosphorus;
          else if (sensor == "potassium") val = rec.potassium;
          else if (sensor == "ph") val = rec.ph;

          JsonObject row = data.createNestedObject();
          row["ts"] = rec.timestamp;
          row["value"] = round(val * 100.0) / 100.0;
          
          // Human-readable label for Chart.js
          char labelBuf[32];
          struct tm* ti = localtime((time_t*)&rec.timestamp);
          strftime(labelBuf, sizeof(labelBuf), "%m/%d %H:%M", ti);
          row["label"] = String(labelBuf);
          count++;
        }
      }
      pos -= sizeof(rec);
    }
    f.close();
  }

  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Local Database (LittleFS-based FIFO)
// ═════════════════════════════════════════════════════════════════════════════
bool initDatabase() {
  // First check if LittleFS is mounted
  if (!LittleFS.begin(true)) {
    Serial.println(F("❌ LittleFS mount failed!"));
    dbInitialized = false;
    return false;
  }
  
  Serial.printf("✅ LittleFS mounted. Total: %d bytes, Used: %d bytes\n", 
                LittleFS.totalBytes(), LittleFS.usedBytes());
  
  // Check if DB file exists
  File f = LittleFS.open(DB_FILENAME, "r");
  if (!f) {
    Serial.println(F("📝 DB file not found, creating new..."));
    // Create new DB file with write mode
    f = LittleFS.open(DB_FILENAME, "w");
    if (f) {
      Serial.printf("✅ Database file created: %s\n", DB_FILENAME);
      f.close();
      
      // Verify file was created
      f = LittleFS.open(DB_FILENAME, "r");
      if (f) {
        Serial.printf("✅ Database verified, size: %d bytes\n", f.size());
        f.close();
      }
      dbInitialized = true;
      return true;
    }
    Serial.println(F("❌ Database creation FAILED"));
    dbInitialized = false;
    return false;
  }
  
  // File exists, get its size
  uint32_t fileSize = f.size();
  uint32_t recordCount = fileSize / sizeof(SensorRecord);
  f.close();
  
  Serial.printf("✅ Database found: %d records (%d bytes)\n", recordCount, fileSize);
  dbInitialized = true;
  return true;
}

bool dbAddRecord() {
  SensorRecord rec;
  rec.magic = 0x47444253; // 'GDBS' in little-endian
  rec.timestamp = sensorData.timestamp;
  rec.soil1Raw = sensorData.soil1Raw;
  rec.soil2Raw = sensorData.soil2Raw;
  rec.soil3Raw = sensorData.soil3Raw;
  rec.soilMoisture = sensorData.soilMoistureAvgPct;
  rec.soilTemp = sensorData.soilTempC;
  rec.ambientTemp = sensorData.ambientTempC;
  rec.humidity = sensorData.humidityPct;
  rec.uvIndex = sensorData.uvIndex;
  rec.batteryV = sensorData.busVoltageV;
  rec.currentMA = sensorData.currentMA;
  rec.powerMW = sensorData.powerMW;
  rec.nitrogen = sensorData.nitrogenPpm;
  rec.phosphorus = sensorData.phosphorusPpm;
  rec.potassium = sensorData.potassiumPpm;
  rec.ph = sensorData.phValue;
  rec.envTemp = sensorData.envTempC;
  rec.envHum = sensorData.envHumPct;
  rec.intTemp = sensorData.intTempC;
  rec.intPres = sensorData.intPresHPa;
  rec.isRaining = sensorData.isRaining;
  rec.valveOpen = isWatering;
  rec.statusFlags = (sensorData.stat_rtc & 0x03) | ((sensorData.stat_npk & 0x03) << 2) |
                    ((sensorData.stat_aht & 0x03) << 4) | ((sensorData.stat_ds18 & 0x03) << 6);

  // Use "a+" mode: append + read, creates file if not exists
  File f = LittleFS.open(DB_FILENAME, "a+");
  if (!f) {
    Serial.println(F("⚠️  DB write failed - cannot open"));
    return false;
  }

  // Check current record count
  uint32_t count = f.size() / sizeof(SensorRecord);
  
  // FIFO: If at limit, remove oldest record
  if (count >= DB_MAX_RECORDS) {
    f.close();
    dbPruneOldest(DB_MAX_RECORDS - 1); // Keep DB_MAX_RECORDS - 1, then add new
    f = LittleFS.open(DB_FILENAME, "a+");
    if (!f) return false;
    count = f.size() / sizeof(SensorRecord);
  }

  // Append new record
  f.seek(0, SeekEnd);
  size_t written = f.write((const uint8_t*)&rec, sizeof(rec));
  f.close();

  if (written == sizeof(rec)) {
    Serial.printf("📝 DB: wrote record %d (size: %d bytes)\n", count + 1, sizeof(rec));
    return true;
  } else {
    Serial.printf("⚠️  DB write incomplete: %d/%d bytes\n", written, sizeof(rec));
    return false;
  }
}

uint32_t dbGetRecordCount() {
  File f = LittleFS.open(DB_FILENAME, "r");
  if (!f) return 0;
  uint32_t count = f.size() / sizeof(SensorRecord);
  f.close();
  return count;
}

void dbPruneOldest(uint32_t keepCount) {
  File f = LittleFS.open(DB_FILENAME, "r");
  if (!f) return;
  
  // Skip to keepCount records from end
  uint32_t total = f.size() / sizeof(SensorRecord);
  uint32_t skipCount = total - keepCount;
  
  if (skipCount == 0 || keepCount == 0) {
    f.close();
    return;
  }

  // Read remaining records into memory (max ~760KB for 10000 records)
  SensorRecord* records = new SensorRecord[keepCount];
  f.seek(skipCount * sizeof(SensorRecord));
  size_t read = f.readBytes((char*)records, keepCount * sizeof(SensorRecord));
  f.close();

  // Write back only kept records
  f = LittleFS.open(DB_FILENAME, "w");
  if (f) {
    f.write((uint8_t*)records, read);
    f.close();
  }
  delete[] records;
  
  Serial.printf("📦 DB pruned: removed %d old records\n", skipCount);
}

void dbClear() {
  LittleFS.remove(DB_FILENAME);
  initDatabase();
  Serial.println(F("📦 Database cleared"));
}

void dbGetStats(JsonObject &doc) {
  // Add initialization status
  doc["db_initialized"] = dbInitialized;
  
  // If database not initialized, return minimal safe stats
  if (!dbInitialized) {
    doc["record_count"] = 0;
    doc["max_records"] = DB_MAX_RECORDS;
    doc["db_size_kb"] = 0;
    doc["db_max_kb"] = (DB_MAX_RECORDS * sizeof(SensorRecord)) / 1024.0f;
    doc["days_remaining"] = 0;
    doc["hours_remaining"] = 0;
    doc["cycle_interval_ms"] = config.targetCycleIntervalMs;
    doc["lfs_free_kb"] = 0;
    doc["heap_free_bytes"] = ESP.getFreeHeap();
    doc["spiffs_free_kb"] = 0;
    return;
  }
  
  uint32_t count = dbGetRecordCount();
  
  // Calculate storage metrics
  const size_t RECORD_SIZE = sizeof(SensorRecord);
  const uint32_t LFS_SIZE = LittleFS.totalBytes();  // ~1MB for Huge APP partition
  const uint32_t LFS_USED = LittleFS.usedBytes();
  const uint32_t LFS_FREE = LFS_SIZE > LFS_USED ? LFS_SIZE - LFS_USED : 0;
  
  // DB storage calculations
  const uint32_t dbSizeBytes = count * RECORD_SIZE;
  const uint32_t dbMaxBytes = DB_MAX_RECORDS * RECORD_SIZE;
  const uint32_t dbFreeBytes = dbMaxBytes > dbSizeBytes ? dbMaxBytes - dbSizeBytes : 0;
  
  // Time calculations based on cycle interval (prevent division by zero)
  const unsigned long intervalMs = config.targetCycleIntervalMs > 0 ? config.targetCycleIntervalMs : 120000UL;
  const float recordsPerHour = 3600000.0f / intervalMs;
  const float recordsPerDay = recordsPerHour * 24.0f;
  
  // Days remaining calculations (prevent NaN)
  const uint32_t recordsRemaining = DB_MAX_RECORDS > count ? DB_MAX_RECORDS - count : 0;
  const float daysRemaining = (recordsPerDay > 0 && recordsRemaining > 0) ? (recordsRemaining / recordsPerDay) : 0.0f;
  const float hoursRemaining = (recordsPerHour > 0 && recordsRemaining > 0) ? (recordsRemaining / recordsPerHour) : 0.0f;
  
  // Calculate actual storage percentage used in LittleFS
  const float lfsUsagePct = (LFS_SIZE > 0) ? ((float)LFS_USED / LFS_SIZE) * 100.0f : 0.0f;
  
  // Calculate what DB percentage is of max allowed
  const float dbFillPct = (float)count / (float)DB_MAX_RECORDS * 100.0f;

  doc["record_count"] = count;
  doc["max_records"] = DB_MAX_RECORDS;
  doc["record_size_bytes"] = RECORD_SIZE;
  
  // Storage in bytes
  doc["db_size_bytes"] = dbSizeBytes;
  doc["db_max_bytes"] = dbMaxBytes;
  doc["db_free_bytes"] = dbFreeBytes;
  
  // LittleFS storage
  doc["lfs_total_bytes"] = LFS_SIZE;
  doc["lfs_used_bytes"] = LFS_USED;
  doc["lfs_free_bytes"] = LFS_FREE;
  doc["lfs_usage_percent"] = lfsUsagePct;
  
  // Human readable storage
  doc["db_size_kb"] = dbSizeBytes / 1024.0f;
  doc["db_max_kb"] = dbMaxBytes / 1024.0f;
  doc["db_free_kb"] = dbFreeBytes / 1024.0f;
  doc["lfs_free_kb"] = LFS_FREE / 1024.0f;
  
  // Time metrics
  doc["cycle_interval_ms"] = intervalMs;
  doc["records_per_hour"] = recordsPerHour;
  doc["records_per_day"] = recordsPerDay;
  doc["records_remaining"] = recordsRemaining;
  
  // Duration estimates
  doc["days_remaining"] = daysRemaining;
  doc["hours_remaining"] = hoursRemaining;
  doc["weeks_remaining"] = daysRemaining / 7.0f;
  
  // Alias for backward compatibility with frontend
  doc["spiffs_free_kb"] = doc["lfs_free_kb"];
  
  // Fill percentages
  doc["db_fill_percent"] = dbFillPct;
  doc["fifo_mode"] = true;
  
  // Flash memory info
  doc["flash_size_bytes"] = ESP.getFlashChipSize();
  doc["flash_free_sketch"] = ESP.getFreeSketchSpace();
  doc["heap_free_bytes"] = ESP.getFreeHeap();
  doc["heap_min_free"] = ESP.getMinFreeHeap();
  
  // Dynamic interval info
  doc["is_auto_interval"] = config.isAutoInterval;
  if (config.isAutoInterval) {
    doc["interval_mode"] = "auto";
    doc["battery_adjusted"] = (sensorData.busVoltageV > 1.0f && sensorData.busVoltageV < 3.7f);
  } else {
    doc["interval_mode"] = "fixed";
    doc["interval_seconds"] = intervalMs / 1000;
  }
  
  // Estimate when database will be full
  if (count > 0 && intervalMs > 0) {
    unsigned long msSinceOldest = 0;
    File f = LittleFS.open(DB_FILENAME, "r");
    if (f && f.available()) {
      SensorRecord rec;
      if (f.readBytes((char*)&rec, sizeof(rec)) == sizeof(rec)) {
        if (rec.magic == 0x47444253) {
          msSinceOldest = (sensorData.timestamp - rec.timestamp) * 1000;
        }
      }
      f.close();
    }
    
    if (msSinceOldest > 0 && recordsRemaining > 0) {
      float msPerRecord = (float)msSinceOldest / (float)count;
      unsigned long estMsToFull = (unsigned long)((float)recordsRemaining * msPerRecord);
      doc["estimated_hours_to_full"] = estMsToFull / 3600000.0f;
      doc["estimated_full_date"] = (sensorData.timestamp + (estMsToFull / 1000));
    }
  }
}

void dbSyncToCloud() {
  if (!mqttClient.connected()) return;
  
  File f = LittleFS.open(DB_FILENAME, "r");
  if (!f) return;

  StaticJsonDocument<1024> doc;
  SensorRecord rec;
  uint32_t syncedCount = 0;
  uint32_t startPos = preferences.getULong("db_sync_pos", 0);
  
  f.seek(startPos);
  
  while (f.available() && syncedCount < 10) { // Sync max 10 records per call
    if (f.readBytes((char*)&rec, sizeof(rec)) == sizeof(rec)) {
      if (rec.magic == 0x47444253) {
        // Build JSON payload
        doc.clear();
        doc["ts"] = rec.timestamp;
        doc["soil_moisture"] = rec.soilMoisture;
        doc["soil_temp"] = rec.soilTemp;
        doc["ambient_temp"] = rec.ambientTemp;
        doc["humidity"] = rec.humidity;
        doc["uv_index"] = rec.uvIndex;
        doc["battery_v"] = rec.batteryV;
        doc["current_ma"] = rec.currentMA;
        doc["power_mw"] = rec.powerMW;
        doc["nitrogen"] = rec.nitrogen;
        doc["phosphorus"] = rec.phosphorus;
        doc["potassium"] = rec.potassium;
        doc["ph"] = rec.ph;
        doc["env_temp"] = rec.envTemp;
        doc["env_hum"] = rec.envHum;
        doc["int_temp"] = rec.intTemp;
        doc["int_pres"] = rec.intPres;
        doc["is_raining"] = rec.isRaining;
        doc["valve_open"] = rec.valveOpen;

        String payload;
        serializeJson(doc, payload);
        mqttClient.publish(MQTT_TOPIC_PUB, payload.c_str());
        syncedCount++;
      }
    }
  }
  
  // Save sync position
  uint32_t newPos = f.position();
  preferences.putULong("db_sync_pos", newPos);
  f.close();
  
  if (syncedCount > 0) {
    Serial.printf("📤 Synced %d records to cloud\n", syncedCount);
    
    // If we've synced all records, reset position for next cycle
    if (newPos >= (dbGetRecordCount() * sizeof(SensorRecord))) {
      preferences.putULong("db_sync_pos", 0);
    }
  }
}

// ═════════════════════════════════════════════════════════════════════════════
//  Sensors
// ═════════════════════════════════════════════════════════════════════════════
void resetI2CBus() {
  pinMode(21, OUTPUT);
  pinMode(22, OUTPUT);
  for (int i = 0; i < 9; i++) {
    digitalWrite(22, HIGH);
    delayMicroseconds(5);
    digitalWrite(22, LOW);
    delayMicroseconds(5);
  }
  pinMode(21, INPUT);
  pinMode(22, INPUT);
}

void initSensors() {
  resetI2CBus();
  Wire.begin();

  // AHT10
  if (!aht10.begin()) {
    Serial.println(F("⚠️  AHT10 not found"));
    sensorData.stat_aht = 2;
  } else sensorData.stat_aht = 0;

  // BMP280
  if (!bmp280.begin(0x76) && !bmp280.begin(0x77)) {
    Serial.println(F("⚠️  BMP280 not found"));
    sensorData.stat_bmp = 2;
  } else {
    sensorData.stat_bmp = 0;
    bmp280.setSampling(Adafruit_BMP280::MODE_NORMAL, Adafruit_BMP280::SAMPLING_X2, Adafruit_BMP280::SAMPLING_X16, Adafruit_BMP280::FILTER_X16, Adafruit_BMP280::STANDBY_MS_500);
  }

  // HDC1080
  hdc1080.begin(0x40);
  sensorData.stat_hdc = 0;

  // DS18B20
  ds18b20.begin();
  if (ds18b20.getDeviceCount() > 0) sensorData.stat_ds18 = 0;
  else sensorData.stat_ds18 = 2;

  // INA226
  if (!ina226.begin()) {
    Serial.println(F("⚠️  INA226 not found"));
    sensorData.stat_ina = 2;
  } else {
    ina226.configure(INA226_SHUNT_OHM, INA226_Current_LSB_mA, INA226_Current_Zero_Offset, INA226_Bus_V_scaling);
    ina226.setAverage(2);
    sensorData.stat_ina = 0;
  }
}

void readSensors() {
  if (sensorData.stat_rtc != 2) {
    DateTime now = rtc.now();
    sensorData.timestamp = now.unixtime() - GMT_OFFSET_SEC;
  } else {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
      sensorData.timestamp = mktime(&timeinfo);
    } else {
      sensorData.timestamp = millis() / 1000;
    }
  }

  auto readADC = [](int pin, int n = 5) -> int {
    long sum = 0;
    for (int i = 0; i < n; i++) {
      sum += analogRead(pin);
      delay(5);
    }
    return sum / n;
  };

  sensorData.soil1Raw = readADC(PIN_SOIL1);
  sensorData.soil2Raw = readADC(PIN_SOIL2);
  sensorData.soil3Raw = readADC(PIN_SOIL3);

  float m1 = rawToMoisturePct(sensorData.soil1Raw);
  float m2 = rawToMoisturePct(sensorData.soil2Raw);
  float m3 = rawToMoisturePct(sensorData.soil3Raw);

  float m[3] = { m1, m2, m3 };
  if (m[0] > m[1]) { float t = m[0]; m[0] = m[1]; m[1] = t; }
  if (m[1] > m[2]) { float t = m[1]; m[1] = m[2]; m[2] = t; }
  if (m[0] > m[1]) { float t = m[0]; m[0] = m[1]; m[1] = t; }

  float avg = 0;
  if ((m[2] - m[1]) > 15.0f) avg = (m[0] + m[1]) / 2.0f;
  else if ((m[1] - m[0]) > 15.0f) avg = (m[1] + m[2]) / 2.0f;
  else avg = (m[0] + m[1] + m[2]) / 3.0f;

  acc.soilMoistureAvgPct += avg;
  acc.c_soilMoisture++;

  // DS18B20
  if (sensorData.stat_ds18 != 2) {
    ds18b20.requestTemperatures();
    float val = ds18b20.getTempCByIndex(0);
    if (val <= -100.0f) sensorData.stat_ds18 = 2;
    else {
      val = (val * MULT_SOIL_TEMP) + OFFSET_SOIL_TEMP;
      acc.soilTempC += val;
      acc.c_soilTemp++;
    }
  }

  // AHT10
  if (sensorData.stat_aht != 2) {
    sensors_event_t hum, temp;
    if (aht10.getEvent(&hum, &temp)) {
      acc.ambientTempC += (temp.temperature * MULT_AHT_TEMP) + OFFSET_AHT_TEMP;
      acc.c_ambientTemp++;
      acc.humidityPct += (hum.relative_humidity * MULT_AHT_HUMIDITY) + OFFSET_AHT_HUMIDITY;
      acc.c_humidity++;
    } else sensorData.stat_aht = 2;
  }

  // HDC1080
  float hdcT = (hdc1080.readTemperature() * MULT_HDC_TEMP) + OFFSET_HDC_TEMP;
  float hdcH = (hdc1080.readHumidity() * MULT_HDC_HUM) + OFFSET_HDC_HUM;
  if (hdcT < 120.0f) {
    acc.envTempC += hdcT;
    acc.c_envTemp++;
    acc.envHumPct += hdcH;
    acc.c_envHum++;
    sensorData.stat_hdc = 0;
  } else sensorData.stat_hdc = 2;

  // BMP280
  if (sensorData.stat_bmp != 2) {
    acc.intTempC += (bmp280.readTemperature() * MULT_BMP_TEMP) + OFFSET_BMP_TEMP;
    acc.c_intTemp++;
    acc.intPresHPa += (bmp280.readPressure() / 100.0F * MULT_BMP_PRES) + OFFSET_BMP_PRES;
    acc.c_intPres++;
  }

  // SoC Temp
  acc.socTempC += (temperatureRead() * MULT_SOC_TEMP) + OFFSET_SOC_TEMP;
  acc.c_socTemp++;

  // UV
  acc.uvIndex += rawToUVIndex(readADC(PIN_UV));
  acc.c_uv++;

  // INA226
  if (sensorData.stat_ina != 2) {
    float busV = ina226.getBusVoltage() + INA226_V_OFFSET_v;
    float shuntMV = ina226.getShuntVoltage_mV();
    float currMA = ina226.getCurrent_mA() + INA226_I_OFFSET_mA;
    float powMW = (busV - shuntMV / 1000) * currMA;
    acc.busVoltageV += busV;
    acc.c_busVoltage++;
    acc.currentMA += currMA;
    acc.c_current++;
    acc.powerMW += powMW;
    acc.c_power++;
    sensorData.busVoltageV = busV;
    sensorData.currentMA = currMA;
    sensorData.isCharging = (currMA > 0);
  }

  // Rain detection
  sensorData.isRaining = false;
  unsigned long nowMs = millis();
  if (sensorData.stat_bmp != 2 && (nowMs - sensorData.lastPresHistoryUpdate >= 900000UL || sensorData.lastPresHistoryUpdate == 0)) {
    sensorData.lastPresHistoryUpdate = nowMs;
    float currentPres = acc.intPresHPa / (acc.c_intPres > 0 ? acc.c_intPres : 1);
    sensorData.presHistory[sensorData.presIdx] = currentPres;
    sensorData.presIdx = (sensorData.presIdx + 1) % 12;
    if (sensorData.presIdx == 0) sensorData.presHistReady = true;

    if (sensorData.presHistReady) {
      float oldPres = sensorData.presHistory[sensorData.presIdx];
      float drop = oldPres - currentPres;
      if (drop > 3.0) sensorData.isRaining = true;
    }
  }

  float curHum = acc.humidityPct / (acc.c_humidity > 0 ? acc.c_humidity : 1);
  if (curHum > 96.0f) sensorData.isRaining = true;

  bool isDay = true;
  if (sensorData.stat_rtc != 2) {
    DateTime now = rtc.now();
    if (now.hour() < 7 || now.hour() > 17) isDay = false;
  }
  if (isDay) {
    float curUV = acc.uvIndex / (acc.c_uv > 0 ? acc.c_uv : 1);
    if (curUV < 0.5f && curHum > 85.0f && sensorData.currentMA < 10.0f) {
      sensorData.isRaining = true;
    }
  }

  if (nowMs - sensorData.lastRemoteRainMs < 900000UL) {
    sensorData.isRaining = sensorData.remoteRainPredicted;
  }

  readNPKandPH();
}

void readNPKandPH() {
  auto readNPKReg = [](uint8_t* query) -> int {
    while (rs485Serial.available()) rs485Serial.read();
    digitalWrite(PIN_RS485_DE_RE, HIGH);
    delay(2);
    rs485Serial.write(query, 8);
    rs485Serial.flush();
    delay(2);
    digitalWrite(PIN_RS485_DE_RE, LOW);

    uint8_t buf[20];
    int len = 0;
    unsigned long t = millis();
    while (millis() - t < 300) {
      yield();
      while (rs485Serial.available()) {
        if (len < 20) buf[len++] = rs485Serial.read();
        else rs485Serial.read();
      }
    }

    for (int i = 0; i < len - 2; i++) {
      if (buf[i] == 0x01 && buf[i + 1] == 0x03 && buf[i + 2] == 0x02) {
        if (i + 4 < len) return (buf[i + 3] << 8) | buf[i + 4];
      }
    }
    return -1;
  };

  uint8_t qN[] = { 0x01, 0x03, 0x00, 0x1E, 0x00, 0x01, 0xE4, 0x0C };
  uint8_t qP[] = { 0x01, 0x03, 0x00, 0x1F, 0x00, 0x01, 0xB5, 0xCC };
  uint8_t qK[] = { 0x01, 0x03, 0x00, 0x20, 0x00, 0x01, 0x85, 0xC0 };

  int n = readNPKReg(qN);
  delay(50);
  int p = readNPKReg(qP);
  delay(50);
  int k = readNPKReg(qK);

  if (n != -1 && p != -1 && k != -1) {
    acc.nitrogenPpm += n;
    acc.c_nitrogen++;
    acc.phosphorusPpm += p;
    acc.c_phosphorus++;
    acc.potassiumPpm += k;
    acc.c_potassium++;
    sensorData.npkValid = true;
    sensorData.stat_npk = 0;
  } else {
    sensorData.npkValid = false;
    sensorData.stat_npk = 2;
  }

  // PH
  long phSum = 0;
  for (int i = 0; i < 10; i++) {
    phSum += analogRead(PIN_PH);
    delay(5);
  }
  int phRaw12Bit = phSum / 10;
  float actualVoltage = (float)phRaw12Bit * 3.3f / 4095.0f;
  float phV = (-24.36 * actualVoltage) + 22.34;
  acc.phValue += phV;
  acc.c_ph++;
}

float rawToMoisturePct(int raw) {
  const int DRY_VAL = 3200;
  const int WET_VAL = 1200;
  float pct = ((float)(DRY_VAL - raw) / (float)(DRY_VAL - WET_VAL)) * 100.0f;
  return constrain(pct, 0.0f, 100.0f);
}

float rawToUVIndex(int raw) {
  float voltage = (float)raw * 3.3f / 4095.0f;
  return constrain(voltage / 0.1f, 0.0f, 16.0f);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Auto Watering
// ═════════════════════════════════════════════════════════════════════════════
void checkAutoWatering() {
  bool tooDry = (sensorData.soilMoistureAvgPct < 50.0f);

  if (!isWatering && tooDry) {
    Serial.println(F("💧 Auto-watering: soil too dry – opening valve"));
    controlValve(true);
  } else if (isWatering && sensorData.soilMoistureAvgPct >= 60.0f) {
    Serial.println(F("✅ Auto-watering: soil moist – closing valve"));
    controlValve(false);
  }
}

void controlValve(bool open, bool isManual) {
  if (isWatering == open) return;
  isWatering = open;

  if (open) {
    wateringStartMs = millis();
    wateringStartMoisture = sensorData.soilMoistureAvgPct;
    manualWateringActive = isManual;
  } else {
    unsigned long durationSec = (millis() - wateringStartMs) / 1000;
    float endMoist = sensorData.soilMoistureAvgPct;

    StaticJsonDocument<256> logDoc;
    logDoc["type"] = "watering_log";
    logDoc["duration_sec"] = durationSec;
    logDoc["start_moist"] = round(wateringStartMoisture * 10) / 10.0;
    logDoc["end_moist"] = round(endMoist * 10) / 10.0;
    logDoc["is_manual"] = manualWateringActive;

    char logBuffer[256];
    serializeJson(logDoc, logBuffer);
    if (mqttClient.connected()) {
      mqttClient.publish(MQTT_TOPIC_PUB, logBuffer);
    }
  }

  digitalWrite(PIN_MOSFET_VALVE, open ? HIGH : LOW);
  Serial.printf("💧 Valve: %s\n", open ? "OPEN" : "CLOSED");
  
  // Broadcast via WebSocket
  sendWebSocketData();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Process & Publish
// ═════════════════════════════════════════════════════════════════════════════
void processAndPublishData() {
  auto avg = [](float sum, int count) -> float {
    return count > 0 ? sum / count : 0.0f;
  };
  
  sensorData.soilMoistureAvgPct = avg(acc.soilMoistureAvgPct, acc.c_soilMoisture);
  sensorData.soilTempC = avg(acc.soilTempC, acc.c_soilTemp);
  sensorData.ambientTempC = avg(acc.ambientTempC, acc.c_ambientTemp);
  sensorData.humidityPct = avg(acc.humidityPct, acc.c_humidity);
  sensorData.uvIndex = avg(acc.uvIndex, acc.c_uv);
  sensorData.nitrogenPpm = avg(acc.nitrogenPpm, acc.c_nitrogen);
  sensorData.phosphorusPpm = avg(acc.phosphorusPpm, acc.c_phosphorus);
  sensorData.potassiumPpm = avg(acc.potassiumPpm, acc.c_potassium);
  sensorData.phValue = avg(acc.phValue, acc.c_ph);
  sensorData.busVoltageV = avg(acc.busVoltageV, acc.c_busVoltage);
  sensorData.currentMA = avg(acc.currentMA, acc.c_current);
  sensorData.powerMW = avg(acc.powerMW, acc.c_power);
  sensorData.envTempC = avg(acc.envTempC, acc.c_envTemp);
  sensorData.envHumPct = avg(acc.envHumPct, acc.c_envHum);
  sensorData.intTempC = avg(acc.intTempC, acc.c_intTemp);
  sensorData.intPresHPa = avg(acc.intPresHPa, acc.c_intPres);
  sensorData.socTempC = avg(acc.socTempC, acc.c_socTemp);

  acc.reset();

  // Publish via MQTT if connected
  if (mqttClient.connected()) {
    String payload = buildJsonPayload();
    mqttClient.publish(MQTT_TOPIC_PUB, payload.c_str(), false);
    Serial.printf("📤 Published %d bytes to MQTT\n", payload.length());
  }

  // Also broadcast via WebSocket for local clients
  sendWebSocketData();
  
  printSensorReadings();
}

void logSystemEvents() {
  StaticJsonDocument<512> doc;
  doc["type"] = "system_event";
  doc["ts"] = sensorData.timestamp;
  
  JsonArray events = doc.createNestedArray("events");
  
  if (sensorData.busVoltageV <= 2.85f && sensorData.busVoltageV > 1.0f && sensorData.stat_ina != 2) {
    events.add("Low Battery: " + String(sensorData.busVoltageV, 1) + "V");
  } else if (sensorData.busVoltageV >= 4.15f && sensorData.stat_ina != 2) {
    events.add("Battery Full: " + String(sensorData.busVoltageV, 1) + "V");
  }
  
  if (sensorData.stat_rtc == 2) events.add("RTC Error");
  if (sensorData.stat_aht == 2) events.add("AHT10 Error");
  if (sensorData.stat_bmp == 2) events.add("BMP280 Error");
  if (sensorData.stat_hdc == 2) events.add("HDC1080 Error");
  if (sensorData.stat_ina == 2) events.add("INA226 Error");
  if (sensorData.stat_npk == 2) events.add("NPK Sensor Error");
  
  if (events.size() > 0) {
    char buffer[512];
    serializeJson(doc, buffer);
    if (mqttClient.connected()) {
      mqttClient.publish(MQTT_TOPIC_PUB, buffer);
    }
  }
}

void printSensorReadings() {
  Serial.println();
  Serial.println(F("--- Sensor Readings ---"));
  Serial.printf("  Soil Moisture: %.1f %%\n", sensorData.soilMoistureAvgPct);
  Serial.printf("  Soil Temp   : %.1f C\n", sensorData.soilTempC);
  Serial.printf("  Ambient     : %.1f C / %.1f %%\n", sensorData.ambientTempC, sensorData.humidityPct);
  Serial.printf("  UV Index    : %.1f\n", sensorData.uvIndex);
  Serial.printf("  Battery     : %.2f V (%.1f mA)\n", sensorData.busVoltageV, sensorData.currentMA);
  Serial.printf("  NPK         : N=%.0f P=%.0f K=%.0f\n", sensorData.nitrogenPpm, sensorData.phosphorusPpm, sensorData.potassiumPpm);
  Serial.printf("  pH          : %.2f\n", sensorData.phValue);
  Serial.printf("  DB Records  : %d\n", dbGetRecordCount());
  Serial.println(F("------------------------"));
}

String buildJsonPayload() {
  StaticJsonDocument<1024> doc;

  doc["ts"] = sensorData.timestamp;
  doc["soil1_raw"] = sensorData.soil1Raw;
  doc["soil2_raw"] = sensorData.soil2Raw;
  doc["soil3_raw"] = sensorData.soil3Raw;
  doc["soil_moisture"] = round(sensorData.soilMoistureAvgPct * 10) / 10.0;
  doc["soil_temp"] = round(sensorData.soilTempC * 10) / 10.0;
  if (sensorData.stat_aht != 2) {
    doc["ambient_temp"] = round(sensorData.ambientTempC * 10) / 10.0;
    doc["humidity"] = round(sensorData.humidityPct * 10) / 10.0;
  }
  doc["uv_index"] = round(sensorData.uvIndex * 10) / 10.0;
  doc["is_raining"] = sensorData.isRaining;
  doc["battery_v"] = round(sensorData.busVoltageV * 100) / 100.0;
  doc["current_ma"] = round(sensorData.currentMA * 10) / 10.0;
  doc["power_mw"] = round(sensorData.powerMW * 10) / 10.0;
  doc["is_charging"] = sensorData.isCharging;
  doc["valve_open"] = isWatering;
  doc["wifi_ssid"] = WiFi.SSID();
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["wifi_ip"] = WiFi.localIP().toString();

  if (sensorData.stat_hdc != 2) {
    doc["env_temp"] = round(sensorData.envTempC * 10) / 10.0;
    doc["env_hum"] = round(sensorData.envHumPct * 10) / 10.0;
  }
  if (sensorData.stat_bmp != 2) {
    doc["int_temp"] = round(sensorData.intTempC * 10) / 10.0;
    doc["int_pres"] = round(sensorData.intPresHPa * 10) / 10.0;
  }
  doc["soc_temp"] = round(sensorData.socTempC * 10) / 10.0;
  doc["ph"] = round(sensorData.phValue * 100) / 100.0;

  if (sensorData.npkValid) {
    doc["nitrogen"] = round(sensorData.nitrogenPpm * 10) / 10.0;
    doc["phosphorus"] = round(sensorData.phosphorusPpm * 10) / 10.0;
    doc["potassium"] = round(sensorData.potassiumPpm * 10) / 10.0;
  }

  JsonObject sys = doc.createNestedObject("sys");
  auto sStr = [](uint8_t s) {
    return s == 0 ? "ok" : (s == 1 ? "warn" : "error");
  };
  sys["rtc"] = sStr(sensorData.stat_rtc);
  sys["npk"] = sStr(sensorData.stat_npk);
  sys["aht"] = sStr(sensorData.stat_aht);
  sys["ds18"] = sStr(sensorData.stat_ds18);
  sys["bmp"] = sStr(sensorData.stat_bmp);
  sys["hdc"] = sStr(sensorData.stat_hdc);
  sys["ina"] = sStr(sensorData.stat_ina);
  
  char timeStr[32];
  struct tm* ti = localtime(&sensorData.timestamp);
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", ti);
  sys["rtc_time"] = timeStr;
  sys["db_records"] = dbGetRecordCount();

  String out;
  serializeJson(doc, out);
  return out;
}
