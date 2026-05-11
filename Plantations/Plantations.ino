// =============================================================================
//  🌿 Garden Monitoring System - ESP32 Firmware
//  Board  : ESP32 WROOM-32 (4MB)
//  Author : Auto-generated
//  Date   : 2026-05-06
// =============================================================================

// ───── Library Includes ──────────────────────────────────────────────────────
#include <WiFi.h>
#include <WiFiMulti.h>
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
// WireGuard – uses WireGuard-ESP32 library (https://github.com/ciniml/WireGuard-ESP32-Arduino)
#include <WireGuard-ESP32.h>

// ───── Pin Definitions ───────────────────────────────────────────────────────
// Soil Moisture (capacitive) – ADC1 channels (safe with WiFi)
#define PIN_SOIL1       34   // ADC1_CH6
#define PIN_SOIL2       35   // ADC1_CH7
#define PIN_SOIL3       32   // ADC1_CH4
// Soil PH Sensor
#define PIN_PH          36   // ADC1_CH0

// UV Sensor (GUVA-S12SD)
#define PIN_UV          33   // ADC1_CH5

// PIN 39 was Raindrop Sensor (REMOVED)

// DS18B20 Soil Temperature
#define PIN_DS18B20     4    // 1-Wire data

// MOSFET Gates
#define PIN_MOSFET_VALVE 26  // Controls solenoid water valve

// RS485 for NPK Sensor
#define PIN_RS485_RO    16   // Connect to RO (Receiver Output) on MAX485
#define PIN_RS485_DI    17   // Connect to DI (Data In) on MAX485
#define PIN_RS485_DE_RE 5    // Connect to both DE and RE pins (Drive Enable)

// I2C (SDA=21, SCL=22 default) → AHT10 + INA226 + ADS1015
// ADS1015 is used if extra ADC channels are needed (declared below)

// ───── Calibration Configuration ─────────────────────────────────────────────
// Formula: (reading * MULT_SENSOR_X) + OFFSET_SENSOR_X
// DS18B20 Soil Temperature → calibrated to MH170 Temp
#define MULT_SOIL_TEMP 1.3494f
#define OFFSET_SOIL_TEMP -9.2477f
// AHT10 → calibrated to MH170
#define MULT_AHT_TEMP 1.1817f
#define OFFSET_AHT_TEMP -5.9871f
#define MULT_AHT_HUMIDITY 1.0562f
#define OFFSET_AHT_HUMIDITY -14.9680f
// HDC1080 → calibrated to MH170
#define MULT_HDC_TEMP 1.0199f
#define OFFSET_HDC_TEMP -1.1547f
#define MULT_HDC_HUM 1.0190f
#define OFFSET_HDC_HUM -5.1205f
// BMP280 → calibrated to MH170
#define MULT_BMP_TEMP 1.0239f
#define OFFSET_BMP_TEMP -3.9511f

// Not Calibrated
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
#define MULT_PH 1.0f
#define OFFSET_PH 0.0f
#define MULT_UV 1.0f
#define OFFSET_UV 0.0f

// ───── Configuration ─────────────────────────────────────────────────────────
// WiFi credentials (tries both)
const char* WIFI_SSID1     = "DCLXVI";
const char* WIFI_PASS1     = "1029384756";
const char* WIFI_SSID2     = "BBWV_Oprasional";
const char* WIFI_PASS2     = "Balai5-OPRA123!";

// NTP
const char* NTP_SERVER     = "pool.ntp.org";
const long  GMT_OFFSET_SEC = 9 * 3600;   // UTC+9 (adjust if needed)
const int   DAYLIGHT_OFFSET_SEC = 0;

// MQTT
const char* MQTT_SERVER    = "192.168.88.88";
const int   MQTT_PORT      = 1883;
const char* MQTT_CLIENT_ID = "garden_esp32";
const char* MQTT_TOPIC_PUB = "garden/sensors";
const char* MQTT_TOPIC_CMD = "garden/commands";

// Timing
const unsigned long PUBLISH_INTERVAL_MS = 60000UL;  // 1 minute
const unsigned long SAMPLE_INTERVAL_MS  = 5000UL;   // 5 seconds

// Thresholds
const int   SOIL_DRY_THRESHOLD  = 2800;  // ADC raw (0-4095, dry = high value for cap. sensor)
const int   SOIL_WET_THRESHOLD  = 1800;
// Raindrop sensor threshold removed (using pressure trend)
const float TEMP_HOT_THRESHOLD  = 35.0f; // °C

// Watering
const unsigned long WATER_MAX_MS = 60000UL; // 1 minute max

// WireGuard
const char* WG_PRIVATE_KEY    = "IHrHmcFWJ7CiEJ58Ptfg4g5luPYvy6wK6fvohog8u38=";
const char* WG_PUBLIC_KEY     = "RmHRBgognQb7b9ft/gmNxhmnx1Q/KpFEWJCI8SlCMDg=";
const char* WG_PRESHARED_KEY  = "j+PWGeEMaRanljKbjM6sfOiNMkDacVehl9/NqBJkzcc=";
const char* WG_ENDPOINT_IP    = "vpn-2.balai5.my.id";
const int   WG_ENDPOINT_PORT  = 51822;
const char* WG_LOCAL_IP       = "100.90.80.28";
const char* WG_LOCAL_NETMASK  = "255.255.255.255";

// INA226 config (I2C address 0x45: A1=VS, A0=VS)
// Shunt confirmed = 5mΩ (0.005Ω) based on working test sketch values
// Proof: 0.962mV ÷ 192.5mA = 0.005Ω
// NOTE: "R100" marking on the board is NOT the shunt — the shunt is the
// tiny resistor between IN+ and IN- (usually unlabeled or marked separately)
#define INA226_SHUNT_OHM  0.005f  // 5mΩ confirmed from test bench
#define INA226_MAX_A      1.0f    // max expected current — matches test sketch

// ───── Global Objects ────────────────────────────────────────────────────────
WiFiMulti     wifiMulti;
WiFiClient    wifiClient;
PubSubClient  mqttClient(wifiClient);

OneWire           oneWire(PIN_DS18B20);
DallasTemperature ds18b20(&oneWire);

Adafruit_AHTX0    aht10;
Adafruit_BMP280   bmp280;
ClosedCube_HDC1080 hdc1080;
INA226            ina226(0x45);
RTC_DS3231        rtc;

static WireGuard wg;
bool wgConnected = false;

// RS485 Serial
HardwareSerial rs485Serial(2);  // UART2

// ───── State Variables ───────────────────────────────────────────────────────
unsigned long lastPublishMs   = 0;
unsigned long lastSampleMs    = 0;
unsigned long wateringStartMs = 0;
bool          isWatering      = false;

struct SensorData {
  // Soil moisture (raw ADC, average of 3 sensors)
  int   soil1Raw, soil2Raw, soil3Raw;
  float soilMoistureAvgPct;   // 0–100 %
  float soilTempC;            // DS18B20
  // Ambient
  float ambientTempC;
  float humidityPct;
  // UV
  float uvIndex;
  // Rain
  int   rainRaw;  // Now unused or for compatibility
  bool  isRaining; // Based on sensors or remote prediction
  // NPK + PH (read continuously)
  float nitrogenPpm;
  float phosphorusPpm;
  float potassiumPpm;
  float phValue;
  bool  npkValid;
  // Power
  float busVoltageV;
  float currentMA;
  float powerMW;
  bool  isCharging;
  // New Sensors
  float envTempC;
  float envHumPct;
  float intTempC;
  float intPresHPa;
  float socTempC;
  // Timestamp
  time_t timestamp;
  // Status Flags (0=OK, 1=WARN, 2=ERR)
  uint8_t stat_rtc, stat_npk, stat_aht, stat_ds18, stat_ina, stat_bmp, stat_hdc;
  // Pressure history for offline rain detection (last 3 hours, every 15 mins)
  float presHistory[12]; 
  int   presIdx = 0;
  bool  presHistReady = false;
  unsigned long lastPresHistoryUpdate = 0;
  bool  remoteRainPredicted = false;
  unsigned long lastRemoteRainMs = 0;
} sensorData;

// ───── Accumulator for 1-minute averaging ────────────────────────────────────
struct SensorAccumulator {
  float soilMoistureAvgPct; int c_soilMoisture;
  float soilTempC; int c_soilTemp;
  float ambientTempC; int c_ambientTemp;
  float humidityPct; int c_humidity;
  float uvIndex; int c_uv;
  float nitrogenPpm; int c_nitrogen;
  float phosphorusPpm; int c_phosphorus;
  float potassiumPpm; int c_potassium;
  float phValue; int c_ph;
  float busVoltageV; int c_busVoltage;
  float currentMA; int c_current;
  float powerMW; int c_power;
  float envTempC; int c_envTemp;
  float envHumPct; int c_envHum;
  float intTempC; int c_intTemp;
  float intPresHPa; int c_intPres;
  float socTempC; int c_socTemp;
  void reset() { memset(this, 0, sizeof(SensorAccumulator)); }
} acc;

// ───── Forward Declarations ──────────────────────────────────────────────────
void connectWiFi();
void syncNTP();
void connectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void readSensors();
void readNPKandPH();
void publishData();
void checkAutoWatering();
void controlValve(bool open);
String buildJsonPayload();
float rawToMoisturePct(int raw);
float rawToUVIndex(int raw);
bool  tryWireGuard();

#ifdef __cplusplus
extern "C" float temperatureRead();
#endif

// =============================================================================
//  SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println(F("\n🌿 Garden Monitor Booting..."));

  // Pin modes
  pinMode(PIN_MOSFET_VALVE, OUTPUT); digitalWrite(PIN_MOSFET_VALVE, LOW);
  pinMode(PIN_RS485_DE_RE, OUTPUT); digitalWrite(PIN_RS485_DE_RE, LOW);

  // ADC attenuation for 0-3.3V range
  analogSetAttenuation(ADC_11db);

  // RS485 Serial
  rs485Serial.begin(4800, SERIAL_8N1, PIN_RS485_RO, PIN_RS485_DI);

  // I2C
  Wire.begin();

  // AHT10
  if (!aht10.begin()) {
    Serial.println(F("⚠️  AHT10 not found – check wiring!"));
    sensorData.stat_aht = 2;
  } else sensorData.stat_aht = 0;

  // BMP280
  if (!bmp280.begin(0x76) && !bmp280.begin(0x77)) {
    Serial.println(F("⚠️  BMP280 not found!"));
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
    Serial.println(F("⚠️  INA226 not found!"));
    sensorData.stat_ina = 2;
  } else {
    // Match exactly the verified test sketch: setMaxCurrentShunt(maxA, shuntOhm)
    ina226.setMaxCurrentShunt(INA226_MAX_A, INA226_SHUNT_OHM);
    ina226.setAverage(2);  // 16-sample averaging for stable readings
    sensorData.stat_ina = 0;
  }



  // RTC DS3231
  if (!rtc.begin()) {
    Serial.println(F("⚠️  Couldn't find RTC"));
    sensorData.stat_rtc = 2;
  } else {
    sensorData.stat_rtc = rtc.lostPower() ? 1 : 0;
  }

  // WiFi
  connectWiFi();

  // NTP
  syncNTP();

  // WireGuard (best-effort)
  wgConnected = tryWireGuard();

  // MQTT
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);
  connectMQTT();

  Serial.println(F("✅ Setup complete!"));
  acc.reset();
  // Initialize both timers to now so:
  //   - samples start immediately (lastSampleMs trick removed, handled by ==0 check)
  //   - first publish waits a FULL minute of real sensor samples (not zeros)
  lastSampleMs  = millis() - SAMPLE_INTERVAL_MS;  // triggers first sample immediately
  lastPublishMs = millis();                         // first publish after 60s of samples
}

// =============================================================================
//  LOOP
// =============================================================================
void loop() {
  // Keep MQTT alive
  if (!mqttClient.connected()) connectMQTT();
  mqttClient.loop();

  unsigned long now = millis();

  // Sample every 5 seconds
  if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = now;
    readSensors();
  }

  // Publish every minute — only after accumulator has real data
  if (now - lastPublishMs >= PUBLISH_INTERVAL_MS) {
    lastPublishMs = now;
    // Average out the accumulated values
    auto avg = [](float sum, int count) -> float { return count > 0 ? sum / count : 0.0f; };
    sensorData.soilMoistureAvgPct = avg(acc.soilMoistureAvgPct, acc.c_soilMoisture);
    sensorData.soilTempC          = avg(acc.soilTempC, acc.c_soilTemp);
    sensorData.ambientTempC       = avg(acc.ambientTempC, acc.c_ambientTemp);
    sensorData.humidityPct        = avg(acc.humidityPct, acc.c_humidity);
    sensorData.uvIndex            = avg(acc.uvIndex, acc.c_uv);
    sensorData.nitrogenPpm        = avg(acc.nitrogenPpm, acc.c_nitrogen);
    sensorData.phosphorusPpm      = avg(acc.phosphorusPpm, acc.c_phosphorus);
    sensorData.potassiumPpm       = avg(acc.potassiumPpm, acc.c_potassium);
    sensorData.phValue            = avg(acc.phValue, acc.c_ph);
    sensorData.busVoltageV        = avg(acc.busVoltageV, acc.c_busVoltage);
    sensorData.currentMA          = avg(acc.currentMA, acc.c_current);
    sensorData.powerMW            = avg(acc.powerMW, acc.c_power);
    sensorData.envTempC           = avg(acc.envTempC, acc.c_envTemp);
    sensorData.envHumPct          = avg(acc.envHumPct, acc.c_envHum);
    sensorData.intTempC           = avg(acc.intTempC, acc.c_intTemp);
    sensorData.intPresHPa         = avg(acc.intPresHPa, acc.c_intPres);
    sensorData.socTempC           = avg(acc.socTempC, acc.c_socTemp);

    // Reset accumulators after average
    acc.reset();
    
    checkAutoWatering();
    publishData();
    printSensorReadings();  // always print every minute to Serial
  }

  // Watering watchdog – cut off after max time
  if (isWatering && (now - wateringStartMs >= WATER_MAX_MS)) {
    Serial.println(F("⏱️  Watering timeout – closing valve"));
    controlValve(false);
  }

  delay(100);
}

// =============================================================================
//  WiFi
// =============================================================================
void connectWiFi() {
  Serial.println(F("📶 Connecting to WiFi..."));
  wifiMulti.addAP(WIFI_SSID1, WIFI_PASS1);
  wifiMulti.addAP(WIFI_SSID2, WIFI_PASS2);

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

// =============================================================================
//  NTP Sync
// =============================================================================
void syncNTP() {
  Serial.println(F("🕐 Syncing NTP..."));
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER,
             "time.google.com", "time.cloudflare.com");
  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry++ < 10) {
    delay(1000); Serial.print('.');
  }
  if (retry < 10) {
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.printf("\n✅ Time synced via NTP: %s\n", buf);
    
    // Sync RTC if it's connected
    if (sensorData.stat_rtc != 2) {
      rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
      Serial.println(F("✅ DS3231 RTC Updated"));
    }
  } else {
    Serial.println(F("\n⚠️  NTP sync failed"));
  }
}

// =============================================================================
//  WireGuard
// =============================================================================
bool tryWireGuard() {
  Serial.println(F("🔒 Starting WireGuard..."));
  IPAddress localIP;
  if (!localIP.fromString(WG_LOCAL_IP)) return false;
  IPAddress gatewayIP(100, 90, 80, 1);
  IPAddress netmask(255, 255, 255, 255);

  // Note: WireGuard-ESP32 library uses static config
  wg.begin(
    localIP,
    WG_PRIVATE_KEY,
    WG_ENDPOINT_IP,
    WG_PUBLIC_KEY,
    WG_ENDPOINT_PORT
  );
  delay(2000);
  Serial.println(F("✅ WireGuard tunnel up (best-effort)"));
  return true;
}

// =============================================================================
//  MQTT
// =============================================================================
void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  Serial.print(F("🔌 Connecting MQTT..."));
  int attempts = 0;
  while (!mqttClient.connected() && attempts++ < 5) {
    if (mqttClient.connect(MQTT_CLIENT_ID)) {
      Serial.println(F(" OK"));
      mqttClient.subscribe(MQTT_TOPIC_CMD);
    } else {
      Serial.printf(" failed (rc=%d), retry...\n", mqttClient.state());
      delay(3000);
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
    controlValve(true);
  } else if (strcmp(cmd, "water_stop") == 0) {
    controlValve(false);
  } else if (strcmp(cmd, "sync_rtc") == 0) {
    syncNTP();
    publishData(); // publish updated time
  } else if (strcmp(cmd, "weather_prediction") == 0) {
    sensorData.remoteRainPredicted = doc["raining"] | false;
    sensorData.lastRemoteRainMs = millis();
    Serial.printf("🌧️  Remote Prediction: %s\n", sensorData.remoteRainPredicted ? "RAIN" : "CLEAR");
  }
}

// =============================================================================
//  Sensor Reading
// =============================================================================
void readSensors() {
  if (sensorData.stat_rtc != 2) {
    DateTime now = rtc.now();
    // now.unixtime() assumes the DateTime is UTC. Since we stored local time, we must subtract the offset.
    sensorData.timestamp = now.unixtime() - GMT_OFFSET_SEC;
  } else {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
      sensorData.timestamp = mktime(&timeinfo);
    } else {
      sensorData.timestamp = millis() / 1000; // ultimate fallback
    }
  }

  // ── Soil Moisture (ADC1, averaged 5 samples each) ──
  auto readADC = [](int pin, int n = 5) -> int {
    long sum = 0;
    for (int i = 0; i < n; i++) { sum += analogRead(pin); delay(5); }
    return sum / n;
  };

  sensorData.soil1Raw = readADC(PIN_SOIL1);
  sensorData.soil2Raw = readADC(PIN_SOIL2);
  sensorData.soil3Raw = readADC(PIN_SOIL3);

  float m1 = rawToMoisturePct(sensorData.soil1Raw);
  float m2 = rawToMoisturePct(sensorData.soil2Raw);
  float m3 = rawToMoisturePct(sensorData.soil3Raw);
  acc.soilMoistureAvgPct += (m1 + m2 + m3) / 3.0f; acc.c_soilMoisture++;

  // ── DS18B20 Soil Temperature ──
  if (sensorData.stat_ds18 != 2) {
    ds18b20.requestTemperatures();
    float val = ds18b20.getTempCByIndex(0);
    if (val <= -100.0f) sensorData.stat_ds18 = 2;
    else {
      val = (val * MULT_SOIL_TEMP) + OFFSET_SOIL_TEMP;
      acc.soilTempC += val; acc.c_soilTemp++;
    }
  }

  // ── AHT10 Ambient Temp + Humidity ──
  if (sensorData.stat_aht != 2) {
    sensors_event_t hum, temp;
    if (aht10.getEvent(&hum, &temp)) {
      float aTemp = (temp.temperature * MULT_AHT_TEMP) + OFFSET_AHT_TEMP;
      float aHum = (hum.relative_humidity * MULT_AHT_HUMIDITY) + OFFSET_AHT_HUMIDITY;
      acc.ambientTempC += aTemp; acc.c_ambientTemp++;
      acc.humidityPct += aHum; acc.c_humidity++;
    } else {
      sensorData.stat_aht = 2;
    }
  }

  // ── HDC1080 Environment 1 ──
  float hdcT = (hdc1080.readTemperature() * MULT_HDC_TEMP) + OFFSET_HDC_TEMP;
  float hdcH = (hdc1080.readHumidity() * MULT_HDC_HUM) + OFFSET_HDC_HUM;
  if (hdcT < 120.0f) { // Arbitrary validity check
    acc.envTempC += hdcT; acc.c_envTemp++;
    acc.envHumPct += hdcH; acc.c_envHum++;
    sensorData.stat_hdc = 0;
  } else {
    sensorData.stat_hdc = 2;
  }

  // ── BMP280 Internal Enclosure ──
  if (sensorData.stat_bmp != 2) {
    float bmpT = (bmp280.readTemperature() * MULT_BMP_TEMP) + OFFSET_BMP_TEMP;
    float bmpP = (bmp280.readPressure() / 100.0F * MULT_BMP_PRES) + OFFSET_BMP_PRES;
    acc.intTempC += bmpT; acc.c_intTemp++;
    acc.intPresHPa += bmpP; acc.c_intPres++;
  }

  // ── ESP32 SoC Temperature ──
  float socT = (temperatureRead() * MULT_SOC_TEMP) + OFFSET_SOC_TEMP;
  acc.socTempC += socT; acc.c_socTemp++;

  // ── UV Index (GUVA-S12SD) ──
  int uvRaw = readADC(PIN_UV);
  float uvI = (rawToUVIndex(uvRaw) * MULT_UV) + OFFSET_UV;
  acc.uvIndex += uvI; acc.c_uv++;

  // ── Raindrop (PHYSICAL MODULE REMOVED) ──
  // Now using BMP280, AHT10, HDC1080 and UV for detection
  // This is handled at the end of readSensors()
  sensorData.rainRaw = 0; 

  // ── INA226 Power Monitor ──
  if (sensorData.stat_ina != 2) {
    float busV   = ina226.getBusVoltage();
    // Negate: with Battery→IN+, Load→IN- wiring, discharge reads positive on INA226.
    // We flip the sign so: negative = discharging, positive = charging (charger connected).
    float currMA = -ina226.getCurrent_mA();
    float powMW  = ina226.getPower_mW();
    acc.busVoltageV += busV;   acc.c_busVoltage++;
    acc.currentMA   += currMA; acc.c_current++;
    acc.powerMW     += powMW;  acc.c_power++;
    // Keep live values for auto-watering/charging detection
    sensorData.busVoltageV = busV;
    sensorData.currentMA   = currMA;
    sensorData.isCharging  = (currMA > 0);  // positive = charger pushing current in
  }

  // ── Fallback Rain Detection (Offline) ──
  sensorData.isRaining = false; // Reset each cycle so it doesn't get stuck

  // Update pressure history every 15 minutes
  unsigned long nowMs = millis();
  if (sensorData.stat_bmp != 2 && (nowMs - sensorData.lastPresHistoryUpdate >= 900000UL || sensorData.lastPresHistoryUpdate == 0)) {
    sensorData.lastPresHistoryUpdate = nowMs;
    float currentPres = acc.intPresHPa / (acc.c_intPres > 0 ? acc.c_intPres : 1);
    sensorData.presHistory[sensorData.presIdx] = currentPres;
    sensorData.presIdx = (sensorData.presIdx + 1) % 12;
    if (sensorData.presIdx == 0) sensorData.presHistReady = true;

    if (sensorData.presHistReady) {
      // Calculate drop over 3 hours
      float oldPres = sensorData.presHistory[sensorData.presIdx]; 
      float drop = oldPres - currentPres;
      
      if (drop > 3.0) sensorData.isRaining = true;
      Serial.printf("📊 Pressure Trend: %.2f hPa drop over 3h\n", drop);
    }
  }

  // High Humidity Fallback
  float curHum = acc.humidityPct / (acc.c_humidity > 0 ? acc.c_humidity : 1);
  if (curHum > 96.0f) sensorData.isRaining = true;

  // Daylight Smart Detection (Solar + UV)
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

  // Use Remote Prediction if it's fresh (last 15 mins)
  if (nowMs - sensorData.lastRemoteRainMs < 900000UL) {
    sensorData.isRaining = sensorData.remoteRainPredicted;
  }

  // Read NPK and PH automatically every cycle
  readNPKandPH();
}

// ── Capacitive moisture: higher raw = drier ──
float rawToMoisturePct(int raw) {
  // Calibrate: dry≈3200 (0%), wet≈1200 (100%)
  const int DRY_VAL = 3200;
  const int WET_VAL = 1200;
  float pct = ((float)(DRY_VAL - raw) / (float)(DRY_VAL - WET_VAL)) * 100.0f;
  return constrain(pct, 0.0f, 100.0f);
}

// ── GUVA-S12SD UV Index ──
float rawToUVIndex(int raw) {
  // Vout = raw * 3.3 / 4095, UV index ≈ Vout / 0.1 (approx formula)
  float voltage = (float)raw * 3.3f / 4095.0f;
  float uvIndex = voltage / 0.1f;  // rough approximation
  return constrain(uvIndex, 0.0f, 16.0f);
}

// =============================================================================
//  NPK + PH (RS485 Modbus RTU)
// =============================================================================
void readNPKandPH() {
  // Some NPK sensors strictly reject multi-register reads (Count=0x03).
  // We will read them sequentially just like the debug script!
  auto readNPKReg = [](uint8_t* query) -> int {
    while(rs485Serial.available()) rs485Serial.read();
    digitalWrite(PIN_RS485_DE_RE, HIGH); delay(2);
    rs485Serial.write(query, 8); rs485Serial.flush();
    delay(2); digitalWrite(PIN_RS485_DE_RE, LOW);
    
    uint8_t buf[20]; int len = 0;
    unsigned long t = millis();
    while (millis() - t < 300) {
      while (rs485Serial.available()) {
        if (len < 20) buf[len++] = rs485Serial.read();
        else rs485Serial.read();
      }
    }
    
    // Scan dynamically for Modbus frame start (ignoring leading noise)
    for (int i = 0; i < len - 2; i++) {
      if (buf[i] == 0x01 && buf[i+1] == 0x03 && buf[i+2] == 0x02) {
        if (i + 4 < len) return (buf[i+3] << 8) | buf[i+4];
      }
    }
    return -1;
  };

  // Pre-calculated CRCs for registers 0x001E, 0x001F, 0x0020 (Length=1)
  uint8_t qN[] = {0x01, 0x03, 0x00, 0x1E, 0x00, 0x01, 0xE4, 0x0C};
  uint8_t qP[] = {0x01, 0x03, 0x00, 0x1F, 0x00, 0x01, 0xB5, 0xCC};
  uint8_t qK[] = {0x01, 0x03, 0x00, 0x20, 0x00, 0x01, 0x85, 0xC0};

  int n = readNPKReg(qN); delay(50);
  int p = readNPKReg(qP); delay(50);
  int k = readNPKReg(qK);

  if (n != -1 && p != -1 && k != -1) {
    acc.nitrogenPpm += (n * MULT_N) + OFFSET_N; acc.c_nitrogen++;
    acc.phosphorusPpm += (p * MULT_P) + OFFSET_P; acc.c_phosphorus++;
    acc.potassiumPpm += (k * MULT_K) + OFFSET_K; acc.c_potassium++;
    sensorData.npkValid = true;
    sensorData.stat_npk = 0;
  } else {
    sensorData.npkValid = false;
    sensorData.stat_npk = 2;
    Serial.println(F("⚠️  NPK RS485 read failed"));
  }

  // PH Sensor via direct ADC (Pin 36)
  long phSum = 0;
  for (int i = 0; i < 10; i++) {
    phSum += analogRead(PIN_PH);
    delay(5);
  }
  int phRaw12Bit = phSum / 10;
  // User's formula was calibrated on an Arduino Uno (5V Reference, 10-bit ADC: 0-1023).
  // The ESP32 is a 3.3V Reference, 12-bit ADC (0-4095).
  // We must translate the ESP32's raw reading exactly back to what an Uno would have read for the same voltage:
  // Uno_ADC = ESP_ADC * (3.3V / 4095) * (1023 / 5.0V) = ESP_ADC * 0.16489
  float sensorValue10Bit = (float)phRaw12Bit * 0.16489f;
  
  // The user's formula was completely misaligned with the hardware's 1.6V output.
  // Standard analog PH sensors output a median voltage (around 1.65V when powered by 3.3V) at PH 7.0.
  // We use the standard generic PH curve: pH = -5.70 * Voltage + calibration_offset
  // For a 1.65V center: offset = 16.4
  float actualVoltage = (float)phRaw12Bit * 3.3f / 4095.0f;
  float phV = (-5.70f * actualVoltage) + 16.4f;
  phV = (phV * MULT_PH) + OFFSET_PH;
  
  acc.phValue += phV; acc.c_ph++;
}

// =============================================================================
//  Auto Watering Logic
// =============================================================================
void checkAutoWatering() {
  bool tooDry = (sensorData.soilMoistureAvgPct < 30.0f);
  
  float tSum = 0; int tCount = 0;
  if (sensorData.stat_aht != 2) { tSum += sensorData.ambientTempC; tCount++; }
  if (sensorData.stat_hdc != 2) { tSum += sensorData.envTempC; tCount++; }
  float validAvgTemp = tCount > 0 ? tSum / (float)tCount : 0.0f;
  
  bool tooHot = (validAvgTemp > TEMP_HOT_THRESHOLD);
  bool noRain = !sensorData.isRaining;

  if (!isWatering && noRain && (tooDry || tooHot)) {
    Serial.println(F("💧 Auto-watering: conditions met – opening valve"));
    controlValve(true);
  } else if (isWatering && !tooDry && sensorData.soilMoistureAvgPct >= 60.0f) {
    Serial.println(F("✅ Auto-watering: soil moist – closing valve"));
    controlValve(false);
  }
}

void controlValve(bool open) {
  isWatering = open;
  if (open) wateringStartMs = millis();
  digitalWrite(PIN_MOSFET_VALVE, open ? HIGH : LOW);
  Serial.printf("💧 Valve: %s\n", open ? "OPEN" : "CLOSED");
}

// =============================================================================
//  MQTT Publish
// =============================================================================
void publishData() {
  if (!mqttClient.connected()) {
    connectMQTT();
    if (!mqttClient.connected()) return;
  }

  String payload = buildJsonPayload();
  mqttClient.publish(MQTT_TOPIC_PUB, payload.c_str(), false);
  Serial.printf("📤 Published %d bytes to %s\n", payload.length(), MQTT_TOPIC_PUB);
}

void printSensorReadings() {
  Serial.println();
  Serial.println(F("--- Sensor Readings (Calibrated 1-min Avg) ---"));
  Serial.printf("  Soil Moisture    : %.1f %%\n",   sensorData.soilMoistureAvgPct);
  Serial.printf("  Soil Temp        : %.1f C\n",    sensorData.soilTempC);
  if (sensorData.stat_aht != 2) {
    Serial.printf("  Env2 Temp (AHT)  : %.1f C\n",  sensorData.ambientTempC);
    Serial.printf("  Env2 Hum  (AHT)  : %.1f %%\n", sensorData.humidityPct);
  } else Serial.println(F("  Env2 (AHT10)     : [ERROR]"));
  if (sensorData.stat_hdc != 2) {
    Serial.printf("  Env1 Temp (HDC)  : %.1f C\n",  sensorData.envTempC);
    Serial.printf("  Env1 Hum  (HDC)  : %.1f %%\n", sensorData.envHumPct);
  } else Serial.println(F("  Env1 (HDC1080)   : [ERROR]"));
  if (sensorData.stat_bmp != 2) {
    Serial.printf("  Enclosure Temp   : %.1f C\n",  sensorData.intTempC);
    Serial.printf("  Enclosure Pres   : %.1f hPa\n",sensorData.intPresHPa);
  } else Serial.println(F("  Enclosure (BMP)  : [ERROR]"));
  Serial.printf("  SoC Temp         : %.1f C\n",    sensorData.socTempC);
  Serial.printf("  UV Index         : %.1f\n",       sensorData.uvIndex);
  Serial.printf("  Rain Raw         : %d (%s)\n",    sensorData.rainRaw, sensorData.isRaining ? "RAIN" : "DRY");
  Serial.printf("  Bus Voltage      : %.2f V\n",     sensorData.busVoltageV);
  Serial.printf("  Current          : %.1f mA\n",    sensorData.currentMA);
  Serial.printf("  Power            : %.1f mW\n",    sensorData.powerMW);
  if (sensorData.npkValid) {
    Serial.printf("  Nitrogen  (N)    : %.1f ppm\n", sensorData.nitrogenPpm);
    Serial.printf("  Phosphorus(P)    : %.1f ppm\n", sensorData.phosphorusPpm);
    Serial.printf("  Potassium (K)    : %.1f ppm\n", sensorData.potassiumPpm);
  } else Serial.println(F("  NPK              : [NO DATA]"));
  Serial.printf("  PH               : %.2f\n",       sensorData.phValue);
  Serial.println(F("----------------------------------------------"));
}

String buildJsonPayload() {
  StaticJsonDocument<1024> doc;

  doc["ts"]              = sensorData.timestamp;
  doc["soil1_raw"]       = sensorData.soil1Raw;
  doc["soil2_raw"]       = sensorData.soil2Raw;
  doc["soil3_raw"]       = sensorData.soil3Raw;
  doc["soil_moisture"]   = round(sensorData.soilMoistureAvgPct * 10) / 10.0;
  doc["soil_temp"]       = round(sensorData.soilTempC          * 10) / 10.0;
  if (sensorData.stat_aht != 2) {
    doc["ambient_temp"]    = round(sensorData.ambientTempC       * 10) / 10.0;
    doc["humidity"]        = round(sensorData.humidityPct        * 10) / 10.0;
  }
  
  doc["uv_index"]        = round(sensorData.uvIndex      * 10) / 10.0;
  doc["rain_raw"]        = sensorData.rainRaw;
  doc["is_raining"]      = sensorData.isRaining;
  doc["battery_v"]       = round(sensorData.busVoltageV  * 100) / 100.0;
  doc["current_ma"]      = round(sensorData.currentMA    * 10) / 10.0;
  doc["power_mw"]        = round(sensorData.powerMW      * 10) / 10.0;
  doc["is_charging"]     = sensorData.isCharging;
  doc["valve_open"]      = isWatering;

  if (sensorData.stat_hdc != 2) {
    doc["env_temp"]        = round(sensorData.envTempC           * 10) / 10.0;
    doc["env_hum"]         = round(sensorData.envHumPct          * 10) / 10.0;
  }
  if (sensorData.stat_bmp != 2) {
    doc["int_temp"]        = round(sensorData.intTempC           * 10) / 10.0;
    doc["int_pres"]        = round(sensorData.intPresHPa         * 10) / 10.0;
  }
  doc["soc_temp"]        = round(sensorData.socTempC           * 10) / 10.0;

  doc["ph"]              = round(sensorData.phValue * 100) / 100.0;

  if (sensorData.npkValid) {
    doc["nitrogen"]   = round(sensorData.nitrogenPpm   * 10) / 10.0;
    doc["phosphorus"] = round(sensorData.phosphorusPpm * 10) / 10.0;
    doc["potassium"]  = round(sensorData.potassiumPpm  * 10) / 10.0;
  }

  JsonObject sys = doc.createNestedObject("sys");
  auto sStr = [](uint8_t s) { return s == 0 ? "ok" : (s == 1 ? "warn" : "error"); };
  sys["rtc"] = sStr(sensorData.stat_rtc);
  sys["npk"] = sStr(sensorData.stat_npk);
  sys["aht"] = sStr(sensorData.stat_aht);
  sys["ds18"] = sStr(sensorData.stat_ds18);
  sys["bmp"] = sStr(sensorData.stat_bmp);
  sys["hdc"] = sStr(sensorData.stat_hdc);
  sys["ina"] = sStr(sensorData.stat_ina);
  char timeStr[32];
  struct tm *ti = localtime(&sensorData.timestamp);
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", ti);
  sys["rtc_time"] = timeStr;

  String out;
  serializeJson(doc, out);
  return out;
}
