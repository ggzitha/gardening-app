# 🌿 Garden Monitoring System — Project Reference

## Required Arduino Libraries (Library Manager)

| Library | Install Name | Purpose |
|---|---|---|
| WiFiMulti | Built-in ESP32 | Dual WiFi with failover |
| PubSubClient | `PubSubClient` by Nick O'Leary | MQTT |
| ArduinoJson | `ArduinoJson` by Benoit Blanchon | JSON |
| OneWire | `OneWire` | DS18B20 bus |
| DallasTemperature | `DallasTemperature` | DS18B20 readings |
| Adafruit AHTX0 | `Adafruit AHTX0` | AHT10 temp/humidity |
| Adafruit ADS1X15 | `Adafruit ADS1X15` | ADS1015 extra ADC |
| INA226 | `INA226` by Korneliusz Jarzebski | Power monitor |
| WireGuard-ESP32 | `WireGuard-ESP32-Arduino` (GitHub) | VPN tunnel |

> WireGuard: https://github.com/ciniml/WireGuard-ESP32-Arduino
> Install via: Sketch → Include Library → Add .ZIP Library

---

## ESP32 Pin Wiring Summary

| Sensor / Module | Pin | Protocol |
|---|---|---|
| Soil Moisture #1 | GPIO 34 (ADC1_CH6) | ADC |
| Soil Moisture #2 | GPIO 35 (ADC1_CH7) | ADC |
| Soil Moisture #3 | GPIO 32 (ADC1_CH4) | ADC |
| Soil PH Sensor | GPIO 36 (ADC1_CH0) | ADC |
| UV Sensor GUVA-S12SD | GPIO 33 (ADC1_CH5) | ADC |
| Raindrop Sensor | GPIO 39 (ADC1_CH3) | ADC |
| DS18B20 Soil Temp | GPIO 4 | 1-Wire (4.7kΩ pull-up to 3.3V) |
| I2C SDA (Common) | GPIO 21 | I2C |
| I2C SCL (Common) | GPIO 22 | I2C |
| Valve MOSFET Gate | GPIO 26 | Digital OUT |
| MAX485 RO (RX) | GPIO 16 (UART2 RX) | Serial / Modbus |
| MAX485 DI (TX) | GPIO 17 (UART2 TX) | Serial / Modbus |
| MAX485 DE & RE | GPIO 5 | Digital OUT |

### I2C Address Summary
| Device | Address |
|---|---|
| AHT10 Temp/Hum | 0x38 |
| INA226 Power | 0x40 |
| DS3231 RTC | 0x68 |

### MOSFET Wiring (D472A N-Channel)
- Gate → ESP32 GPIO (via 100Ω resistor)
- Source → GND
- Drain → Sensor/Valve GND leg
- Gate pull-down resistor (10kΩ) to GND recommended

### INA226 Shunt
- Use 100mΩ (0.1Ω) shunt resistor on the battery negative line
- `INA226_SHUNT_OHM = 0.1` in firmware
- Adjust `INA226_MAX_A` to your expected max current

---

## MQTT Topics

| Topic | Direction | Content |
|---|---|---|
| `garden/sensors` | ESP32 → Server | JSON sensor payload (every 60s) |
| `garden/commands` | Server → ESP32 | `{"cmd":"water_start"}` etc. |

### Command Payloads
```json
{"cmd": "water_start"}
{"cmd": "water_stop"}
{"cmd": "npk_read"}
```

---

## Docker Deployment

```bash
cd Web-Page
docker compose up -d --build
```

Dashboard: http://<host-ip>:9876

### Environment Variables (docker-compose.yml)
| Variable | Default | Description |
|---|---|---|
| MQTT_HOST | 192.168.88.88 | Eclipse Mosquitto broker IP |
| MQTT_PORT | 1883 | MQTT port |
| DB_NAME | 002_garden_monitoring | MariaDB database |
| PORT | 9876 | Web server port |

---

## Soil Moisture Calibration
Capacitive sensors: **higher ADC = drier**
Edit in `Plantations.ino`:
```cpp
const int DRY_VAL = 3200;  // ADC when completely dry (in air)
const int WET_VAL = 1200;  // ADC when fully submerged in water
```
To calibrate: read ADC value in dry air → set DRY_VAL; submerge in water → set WET_VAL.

---

## Auto-Watering Logic
Valve opens when:
- `is_raining == false` AND
- (`soil_moisture < 30%` OR `ambient_temp > 35°C`)

Valve closes when:
- `soil_moisture >= 60%` → auto-close
- 60 seconds elapsed → watchdog close
- Manual `water_stop` command

---

## WireGuard VPN
Config from `Client_plant.conf` is embedded directly in firmware constants.
The VPN is best-effort — the system works without it.
Endpoint: `vpn-2.balai5.my.id:51822`
Local IP: `100.90.80.28`

NEWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW




# 🌿 Garden Monitor ESP32 Enhanced

Enhanced Garden Monitoring System with ESP32 featuring embedded web server, local database with FIFO storage, and optional MQTT cloud sync.

## Features

- ✅ **Embedded Web Server** - Access via browser at ESP32's IP address
- ✅ **Local Database** - Store up to 10,000 readings (~14 days at 2-min cycles)
- ✅ **FIFO Storage** - Automatically removes oldest records when full
- ✅ **Optional MQTT** - Syncs to cloud when server is available
- ✅ **MQTT Non-blocking** - ESP32 boots even if MQTT server is down
- ✅ **WebSocket** - Real-time sensor updates
- ✅ **Web Controls** - Reboot, Clear DB, Water, Sync RTC

## Hardware

- **Board**: ESP32 WROOM-32 (4MB Flash)
- **Sensors**: Soil moisture, temperature, humidity, UV, NPK, pH
- **RTC**: DS3231
- **Power Monitor**: INA226

## Arduino IDE Settings

### Board Selection
```
Tools > Board > ESP32 Arduino > ESP32 Dev Module
```

### Partition Scheme (REQUIRED!)
```
Tools > Partition Scheme > Huge APP (3MB APP / 1MB SPIFFS)
```

| Partition | Size | Purpose |
|-----------|------|---------|
| App | 3MB | Firmware code |
| SPIFFS | 1MB | Web files + Database |

### Other Settings
```
Upload Speed: 115200
CPU Frequency: 240MHz
Flash Frequency: 80MHz
```

## Required Libraries

Install via Arduino IDE Library Manager or GitHub:

| Library | Source |
|---------|--------|
| `ESP Async Web Server` | https://github.com/ESP32Async/ESPAsyncWebServer |
| `AsyncTCP` | https://github.com/ESP32Async/AsyncTCP |
| `DallasTemperature` | Library Manager |
| `Adafruit AHTX0` | Library Manager |
| `Adafruit BMP280` | Library Manager |
| `ClosedCube HDC1080` | Library Manager |
| `PubSubClient` | Library Manager |
| `ArduinoJson` | Library Manager |
| `INA226` | Library Manager |
| `RTClib` | Library Manager |

> ⚠️ **Important**: Use `ESP32Async` libraries (NOT me-no-dev). The me-no-dev versions are deprecated.

## Upload Instructions

### Step 1: Upload Sketch
1. Connect ESP32 via USB
2. Select correct Port
3. Click Upload (Ctrl+U)

### Step 2: Upload Web Files (SPIFFS)
1. Copy web files to `data` folder:
   - `data/index.html`
   - `data/app.js`
   - `data/style.css`
2. Go to: `Tools > ESP32 Sketch Data Upload`
3. Wait for upload to complete

### Step 3: Find ESP32 IP
1. Open Serial Monitor (115200 baud)
2. Look for: `Web server started on port 80`
3. Note the IP address shown

### Step 4: Access Web Interface
Open browser: `http://<ESP32_IP>`

## Web Interface Features

### Dashboard
- Live sensor readings with animated garden view
- Real-time updates via WebSocket
- Click pills for detailed sensor data

### Raw Data Page
- Complete sensor readings list
- Historical charts
- Data export capability

### System Controls
- **Reboot** - Restart ESP32
- **Clear DB** - Delete all local records
- **Water Plant** - Manual watering
- **Sync RTC** - Synchronize clock

### Database Statistics
View storage info including:
- Record count
- Storage used
- Days remaining (based on current cycle interval)

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/latest` | GET | Latest sensor readings |
| `/api/db/stats` | GET | Database statistics |
| `/api/db/clear` | POST | Clear database |
| `/api/command` | POST | Send commands |
| `/api/history` | GET | Historical data |
| `/ws` | WebSocket | Real-time updates |

### Command Examples

```bash
# Water plant
curl -X POST http://192.168.1.100/api/command \
  -H "Content-Type: application/json" \
  -d '{"cmd":"water_start"}'

# Reboot
curl -X POST http://192.168.1.100/api/command \
  -H "Content-Type: application/json" \
  -d '{"cmd":"reboot"}'

# Clear database
curl -X POST http://192.168.1.100/api/db/clear

# Get DB stats
curl http://192.168.1.100/api/db/stats
```

## MQTT Commands

When MQTT is connected, you can also send commands via MQTT:

```json
{"cmd": "water_start"}
{"cmd": "water_stop"}
{"cmd": "sync_rtc"}
{"cmd": "reboot"}
{"cmd": "db_clear"}
{"cmd": "set_interval", "val": 120000}
{"cmd": "set_interval", "val": "auto"}
```

## Storage Capacity

| Cycle Interval | Max Records | Days of Storage |
|---------------|-------------|------------------|
| 2 minutes | 10,000 | ~14 days |
| 5 minutes | 10,000 | ~35 days |
| 10 minutes | 10,000 | ~70 days |

When storage is full, oldest records are automatically deleted (FIFO).

## Troubleshooting

### ESP32 won't boot
- Check power supply (should be 5V 2A minimum)
- Verify serial connection at 115200 baud
- Check for boot loop in serial output

### Web page not loading
- Verify SPIFFS upload completed
- Check partition scheme is "Huge APP"
- Try accessing via IP address directly

### MQTT not connecting
- This is normal if MQTT server is unreachable
- ESP32 will work without MQTT
- It will automatically connect when server becomes available

### Database full
- Old records are automatically deleted (FIFO)
- Or manually clear via web interface: Clear DB button

## MQTT Cloud Sync

When MQTT server is available:
1. ESP32 automatically connects
2. Buffered records are synced (10 per cycle)
3. After sync, records remain in local DB as backup

This ensures data is never lost even if cloud goes down.

## File Structure

```
Plantations/
├── Plantations.ino        # Main firmware
├── README.md              # This file
├── data/                  # SPIFFS upload folder
│   ├── index.html         # Web interface
│   ├── app.js             # Web app logic
│   └── style.css          # Styles
└── backup/                # Keep original backup
```

## License

Auto-generated for Garden Monitoring Project
