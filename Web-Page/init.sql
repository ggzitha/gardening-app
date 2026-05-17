-- ============================================================
--  Garden Monitoring Database Schema
--  DB: 002_garden_monitoring
-- ============================================================

USE `002_garden_monitoring`;

-- ── Sensor Readings (time-series) ─────────────────────────
CREATE TABLE IF NOT EXISTS sensor_readings (
    id              BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    recorded_at     DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    -- Soil Moisture
    soil1_raw       SMALLINT UNSIGNED,
    soil2_raw       SMALLINT UNSIGNED,
    soil3_raw       SMALLINT UNSIGNED,
    soil_moisture   FLOAT COMMENT 'Average moisture %',
    -- Soil Temperature
    soil_temp       FLOAT COMMENT 'DS18B20 °C',
    -- Ambient
    ambient_temp    FLOAT COMMENT 'AHT10 °C',
    humidity        FLOAT COMMENT 'AHT10 %',
    -- UV
    uv_index        FLOAT,
    -- Rain
    rain_raw        SMALLINT UNSIGNED,
    is_raining      TINYINT(1) DEFAULT 0,
    -- NPK + PH (nullable – only when panel active)
    nitrogen        FLOAT,
    phosphorus      FLOAT,
    potassium       FLOAT,
    ph              FLOAT,
    -- Power
    battery_v       FLOAT,
    current_ma      FLOAT,
    power_mw        FLOAT,
    is_charging     TINYINT(1) DEFAULT 0,
    -- New Sensors
    env_temp        FLOAT COMMENT 'HDC1080 °C',
    env_hum         FLOAT COMMENT 'HDC1080 %',
    int_temp        FLOAT COMMENT 'BMP280 °C',
    int_pres        FLOAT COMMENT 'BMP280 hPa',
    soc_temp        FLOAT COMMENT 'ESP32 Internal °C',
    -- Actuators
    valve_open      TINYINT(1) DEFAULT 0,
    -- Raw MQTT JSON
    raw_json        JSON,
    INDEX idx_recorded_at (recorded_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ── Events / Commands Log ──────────────────────────────────
CREATE TABLE IF NOT EXISTS events (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    event_at    DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    event_type  VARCHAR(64),
    detail      TEXT,
    INDEX idx_event_at (event_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ── Watering Logs ────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS watering_logs (
    id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    event_at      DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    duration_sec  INT UNSIGNED,
    start_moist   FLOAT,
    end_moist     FLOAT,
    is_manual     TINYINT(1) DEFAULT 0,
    INDEX idx_event_at (event_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ── Seed demo row (so dashboard shows something on first boot) ──
INSERT IGNORE INTO sensor_readings
  (recorded_at, soil1_raw, soil2_raw, soil3_raw, soil_moisture,
   soil_temp, ambient_temp, humidity, uv_index, rain_raw, is_raining,
   battery_v, current_ma, power_mw, is_charging)
VALUES
  (NOW(), 2800, 2750, 2900, 22.0, 28.5, 32.1, 55.3, 3.2, 3100, 0, 12.6, 250.0, 3150.0, 1);
