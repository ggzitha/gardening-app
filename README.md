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
