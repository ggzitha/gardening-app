// =============================================================================
//  Garden Monitoring - Node.js Server
//  - Subscribes to MQTT and stores data in MariaDB
//  - Serves REST API + WebSocket for real-time updates
//  - Serves static HTML dashboard on port 9876
// =============================================================================

'use strict';
require('dotenv').config();

const express = require('express');
const http = require('http');
const path = require('path');
const mysql = require('mysql2/promise');
const mqtt = require('mqtt');
const { WebSocketServer } = require('ws');

const app = express();
const server = http.createServer(app);
const wss = new WebSocketServer({ server });

// ── Config from ENV ──────────────────────────────────────────────────────────
const PORT = parseInt(process.env.PORT || '9876');
const DB_HOST = process.env.DB_HOST || 'mariadb';
const DB_PORT = parseInt(process.env.DB_PORT || '3306');
const DB_NAME = process.env.DB_NAME || '002_garden_monitoring';
const DB_USER = process.env.DB_USER || 'garden_user';
const DB_PASS = process.env.DB_PASS || 'garden_pass_2026!';
const MQTT_HOST = process.env.MQTT_HOST || '192.168.88.88';
const MQTT_PORT = parseInt(process.env.MQTT_PORT || '1883');
const MQTT_USER = process.env.MQTT_USER || '';
const MQTT_PASS = process.env.MQTT_PASS || '';
const MQTT_TOPIC = process.env.MQTT_TOPIC_SUB || 'garden/sensors';
const MQTT_CMD = process.env.MQTT_TOPIC_CMD || 'garden/commands';

// ── State ─────────────────────────────────────────────────────────────────────
let dbPool = null;
let mqttClient = null;
let latestData = null;   // last MQTT payload

// ── DB Pool ──────────────────────────────────────────────────────────────────
async function initDB() {
  dbPool = mysql.createPool({
    host: DB_HOST,
    port: DB_PORT,
    database: DB_NAME,
    user: DB_USER,
    password: DB_PASS,
    timezone: '+09:00', // Jayapura UTC+9
    waitForConnections: true,
    connectionLimit: 10,
    queueLimit: 0,
  });
  // Test connection
  const conn = await dbPool.getConnection();
  console.log('✅ MariaDB connected');
  
  // Auto-create tables in case init.sql fails due to Docker permission issues
  try {
    await conn.query(`
      CREATE TABLE IF NOT EXISTS sensor_readings (
        id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
        recorded_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
        soil1_raw SMALLINT UNSIGNED,
        soil2_raw SMALLINT UNSIGNED,
        soil3_raw SMALLINT UNSIGNED,
        soil_moisture FLOAT,
        soil_temp FLOAT,
        ambient_temp FLOAT,
        humidity FLOAT,
        uv_index FLOAT,
        rain_raw SMALLINT UNSIGNED,
        is_raining TINYINT(1) DEFAULT 0,
        nitrogen FLOAT,
        phosphorus FLOAT,
        potassium FLOAT,
        ph FLOAT,
        battery_v FLOAT,
        current_ma FLOAT,
        power_mw FLOAT,
        is_charging TINYINT(1) DEFAULT 0,
        env_temp FLOAT,
        env_hum FLOAT,
        int_temp FLOAT,
        int_pres FLOAT,
        soc_temp FLOAT,
        valve_open TINYINT(1) DEFAULT 0,
        raw_json JSON,
        INDEX idx_recorded_at (recorded_at)
      ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
    `);
    
    await conn.query(`
      CREATE TABLE IF NOT EXISTS events (
        id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
        event_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
        event_type VARCHAR(64),
        detail TEXT,
        INDEX idx_event_at (event_at)
      ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
    `);
    
    await conn.query(`
      CREATE TABLE IF NOT EXISTS watering_logs (
        id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
        event_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
        duration_sec INT UNSIGNED,
        start_moist FLOAT,
        end_moist FLOAT,
        is_manual TINYINT(1) DEFAULT 0,
        INDEX idx_event_at (event_at)
      ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
    `);
    console.log('✅ DB Tables verified');
  } catch (err) {
    console.error('Table creation error:', err.message);
  }
  
  conn.release();
}

// ── MQTT ─────────────────────────────────────────────────────────────────────
function initMQTT() {
  mqttClient = mqtt.connect(`mqtt://${MQTT_HOST}:${MQTT_PORT}`, {
    clientId: `garden_web_${Math.random().toString(16).slice(2, 8)}`,
    clean: true,
    reconnectPeriod: 5000,
    username: `${MQTT_USER}`, // Add your username here
    password: `${MQTT_PASS}`, // Add your password here
  });

  mqttClient.on('connect', () => {
    console.log('✅ MQTT connected');
    mqttClient.subscribe(MQTT_TOPIC, { qos: 0 });
  });

  mqttClient.on('message', async (topic, payload) => {
    let data;
    try { data = JSON.parse(payload.toString()); } catch { return; }
    
    if (data.type === 'watering_log') {
      try {
        await dbPool.execute(
          'INSERT INTO watering_logs (duration_sec, start_moist, end_moist, is_manual) VALUES (?, ?, ?, ?)',
          [data.duration_sec, data.start_moist, data.end_moist, data.is_manual ? 1 : 0]
        );
        const msg = JSON.stringify({ type: 'watering_log', data });
        wss.clients.forEach(c => { if (c.readyState === 1) c.send(msg); });
      } catch (e) { console.error('DB log error:', e.message); }
      return;
    }
    
    if (data.type === 'system_event') {
      try {
        for (const eventMsg of data.events) {
            await dbPool.execute(
              'INSERT INTO events (event_type, detail) VALUES (?, ?)',
              ['SYSTEM_ALERT', eventMsg]
            );
        }
        const msg = JSON.stringify({ type: 'system_event', data });
        wss.clients.forEach(c => { if (c.readyState === 1) c.send(msg); });
      } catch (e) { console.error('DB event log error:', e.message); }
      return;
    }

    latestData = data;

    // Store in DB
    try { await storeReading(data); } catch (e) { console.error('DB store error:', e.message); }

    // Broadcast to all WebSocket clients
    const msg = JSON.stringify({ type: 'sensor_update', data });
    wss.clients.forEach(c => { if (c.readyState === 1) c.send(msg); });
  });

  mqttClient.on('error', e => console.error('MQTT error:', e.message));
}

async function storeReading(d) {
  if (!dbPool) return;
  const sql = `
    INSERT INTO sensor_readings
      (recorded_at, soil1_raw, soil2_raw, soil3_raw, soil_moisture,
       soil_temp, ambient_temp, humidity, uv_index, rain_raw, is_raining,
       nitrogen, phosphorus, potassium, ph,
       battery_v, current_ma, power_mw, is_charging, valve_open,
       env_temp, env_hum, int_temp, int_pres, soc_temp, raw_json)
    VALUES
      (FROM_UNIXTIME(?), ?, ?, ?, ?,  ?, ?, ?, ?, ?, ?,  ?, ?, ?, ?,  ?, ?, ?, ?, ?,  ?, ?, ?, ?, ?, ?)
  `;
  const ts = d.ts || Math.floor(Date.now() / 1000);
  await dbPool.execute(sql, [
    ts,
    d.soil1_raw ?? null, d.soil2_raw ?? null, d.soil3_raw ?? null,
    d.soil_moisture ?? null,
    d.soil_temp ?? null,
    d.ambient_temp ?? null, d.humidity ?? null,
    d.uv_index ?? null,
    d.rain_raw ?? null, d.is_raining ? 1 : 0,
    d.nitrogen ?? null, d.phosphorus ?? null,
    d.potassium ?? null, d.ph ?? null,
    d.battery_v ?? null, d.current_ma ?? null,
    d.power_mw ?? null, d.is_charging ? 1 : 0,
    d.valve_open ? 1 : 0,
    d.env_temp ?? null, d.env_hum ?? null, d.int_temp ?? null, d.int_pres ?? null, d.soc_temp ?? null,
    JSON.stringify(d),
  ]);
}

// ── WebSocket ─────────────────────────────────────────────────────────────────
wss.on('connection', ws => {
  if (latestData) ws.send(JSON.stringify({ type: 'sensor_update', data: latestData }));
});

// ── Middleware ────────────────────────────────────────────────────────────────
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// ── REST API ──────────────────────────────────────────────────────────────────

// Latest reading
app.get('/api/latest', async (req, res) => {
  try {
    const [rows] = await dbPool.query(
      'SELECT * FROM sensor_readings ORDER BY recorded_at DESC LIMIT 1'
    );
    res.json(rows[0] || latestData || {});
  } catch (e) { res.status(500).json({ error: e.message }); }
});

// History: period = daily | weekly | monthly or start/end
app.get('/api/history/:sensor', async (req, res) => {
  const { sensor } = req.params;
  const { period = 'daily', start, end } = req.query;

  const allowed = [
    'soil_moisture', 'soil1_pct', 'soil2_pct', 'soil3_pct', 'soil_temp', 'ambient_temp', 'humidity',
    'uv_index', 'battery_v', 'current_ma', 'power_mw',
    'nitrogen', 'phosphorus', 'potassium', 'ph',
    'env_temp', 'env_hum', 'int_temp', 'int_pres', 'soc_temp',
    'avg_temp', 'avg_hum'
  ];
  if (!allowed.includes(sensor)) return res.status(400).json({ error: 'Invalid sensor' });

  let groupBy, limitClause, timeFilter;
  
  if (start && end) {
      // Dynamic date range
      timeFilter = `recorded_at >= FROM_UNIXTIME(${dbPool.escape(start)}) AND recorded_at <= FROM_UNIXTIME(${dbPool.escape(end)})`;
      limitClause = 'LIMIT 1000'; // Prevent huge payloads
      
      const diffMs = (end - start) * 1000;
      if (diffMs > 30 * 24 * 60 * 60 * 1000) { // > 30 days
          groupBy = `DATE_FORMAT(recorded_at, '%Y-%b-%d')`;
      } else if (diffMs > 7 * 24 * 60 * 60 * 1000) { // > 7 days
          groupBy = `DATE_FORMAT(recorded_at, '%b-%d %H:00')`;
      } else if (diffMs > 24 * 60 * 60 * 1000) { // > 1 day
          groupBy = `DATE_FORMAT(recorded_at, '%b-%d %H:00')`;
      } else { // < 1 day
          groupBy = `DATE_FORMAT(recorded_at, '%H:%i')`;
      }
  } else {
      switch (period) {
        case 'monthly':
          timeFilter = 'recorded_at >= NOW() - INTERVAL 30 DAY AND recorded_at <= NOW()';
          groupBy = `DATE_FORMAT(recorded_at, '%b-%d')`;
          limitClause = 'LIMIT 30';
          break;
        case 'weekly':
          timeFilter = 'recorded_at >= NOW() - INTERVAL 7 DAY AND recorded_at <= NOW()';
          groupBy = `DATE_FORMAT(recorded_at, '%b-%d')`;
          limitClause = 'LIMIT 7';
          break;
        case 'daily':
          timeFilter = 'recorded_at >= NOW() - INTERVAL 24 HOUR AND recorded_at <= NOW()';
          groupBy = `DATE_FORMAT(recorded_at, '%b-%d %H:00')`;
          limitClause = 'LIMIT 24';
          break;
        case 'hourly':
        default:
          timeFilter = 'recorded_at >= NOW() - INTERVAL 60 MINUTE AND recorded_at <= NOW()';
          groupBy = `DATE_FORMAT(recorded_at, '%H:%i')`;
          limitClause = 'LIMIT 60';
          break;
      }
  }

  let sensorCol = `\`${sensor}\``;
  let notNullCheck = `\`${sensor}\` IS NOT NULL`;
  
  if (sensor === 'soil1_pct') {
    sensorCol = `GREATEST(0, LEAST(100, ((3200 - soil1_raw) / 2000) * 100))`;
    notNullCheck = `soil1_raw IS NOT NULL`;
  } else if (sensor === 'soil2_pct') {
    sensorCol = `GREATEST(0, LEAST(100, ((3200 - soil2_raw) / 2000) * 100))`;
    notNullCheck = `soil2_raw IS NOT NULL`;
  } else if (sensor === 'soil3_pct') {
    sensorCol = `GREATEST(0, LEAST(100, ((3200 - soil3_raw) / 2000) * 100))`;
    notNullCheck = `soil3_raw IS NOT NULL`;
  } else if (sensor === 'avg_temp') {
    sensorCol = `(IFNULL(ambient_temp, env_temp) + IFNULL(env_temp, ambient_temp)) / 2`;
    notNullCheck = `(ambient_temp IS NOT NULL OR env_temp IS NOT NULL)`;
  } else if (sensor === 'avg_hum') {
    sensorCol = `(IFNULL(humidity, env_hum) + IFNULL(env_hum, humidity)) / 2`;
    notNullCheck = `(humidity IS NOT NULL OR env_hum IS NOT NULL)`;
  }

  try {
    const sql = `
      SELECT
        ${groupBy} AS label,
        ROUND(AVG(${sensorCol}), 2) AS value,
        ROUND(MIN(${sensorCol}), 2) AS min_val,
        ROUND(MAX(${sensorCol}), 2) AS max_val
      FROM sensor_readings
      WHERE ${timeFilter}
        AND ${notNullCheck}
      GROUP BY ${groupBy}
      ORDER BY MIN(recorded_at) ASC
      ${limitClause}
    `;
    const [rows] = await dbPool.query(sql);
    res.json(rows);
  } catch (e) { res.status(500).json({ error: e.message }); }
});

// Send command to ESP32 via MQTT
app.post('/api/command', async (req, res) => {
  const { cmd, ...args } = req.body;
  if (!cmd) return res.status(400).json({ error: 'Missing cmd' });
  const payload = JSON.stringify({ cmd, ...args });
  mqttClient.publish(MQTT_CMD, payload, { qos: 0 });
  // Log event
  try {
    await dbPool.execute(
      'INSERT INTO events (event_type, detail) VALUES (?, ?)',
      [cmd, payload]
    );
  } catch { }
  res.json({ ok: true, sent: payload });
});

// Purge database
app.post('/api/purge-db', async (req, res) => {
  try {
    await dbPool.execute('DELETE FROM sensor_readings');
    await dbPool.execute('DELETE FROM watering_logs');
    res.json({ ok: true, message: 'Sensor data purged successfully' });
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// Fetch Watering Logs
app.get('/api/watering-events', async (req, res) => {
  try {
    const [rows] = await dbPool.query('SELECT * FROM watering_logs ORDER BY event_at DESC LIMIT 200');
    res.json(rows);
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// Fetch all events for Calendar
app.get('/api/calendar-events', async (req, res) => {
  try {
    // Get watering logs
    const [waterRows] = await dbPool.query('SELECT * FROM watering_logs ORDER BY event_at DESC LIMIT 1000');
    // Get system events
    const [sysRows] = await dbPool.query('SELECT * FROM events ORDER BY event_at DESC LIMIT 1000');
    
    // Format for Evo-Calendar
    const calendarEvents = [];
    
    waterRows.forEach(row => {
        const d = new Date(row.event_at);
        const mm = String(d.getMonth() + 1).padStart(2, '0');
        const dd = String(d.getDate()).padStart(2, '0');
        const yyyy = d.getFullYear();

        const hh24 = d.getHours();
        const min = String(d.getMinutes()).padStart(2, '0');
        const ampm = hh24 >= 12 ? 'PM' : 'AM';
        const hh12 = hh24 % 12 || 12;
        const timeStr = `${String(hh12).padStart(2, '0')}:${min} ${ampm}`;

        calendarEvents.push({
            id: `w_${row.id}`,
            name: "Watering Plant",
            date: `${mm}/${dd}/${yyyy}`,
            time: timeStr,
            type: "watering",
            description: `${row.is_manual ? "Manual" : "Auto"} Trigger From ${row.start_moist}% -> ${row.end_moist}%`,
            color: "#0ea5e9" // Sky blue
        });
    });
    
    sysRows.forEach(row => {
        let type = "error";
        let color = "#ef4444"; // Red for errors
        let name = "Error Logs";
        let detail = row.detail || row.event_type;
        
        // Try parsing detail to see if it's a JSON command (Info)
        try {
            if (detail.startsWith('{') && detail.includes('cmd')) {
                const jsonObj = JSON.parse(detail);
                type = "info";
                color = "#9333ea"; // Purple
                name = "Info Logs";
                // Pretty print the JSON for the log description
                detail = "Command Received: " + Object.keys(jsonObj).map(k => `${k}=${jsonObj[k]}`).join(', ');
            }
        } catch(e) {}

        const lowerDetail = detail.toLowerCase();
        
        if (type !== "info") {
            if (lowerDetail.includes("warning") || lowerDetail.includes("full batt") || lowerDetail.includes("low bat") || lowerDetail.includes("bad signal") || lowerDetail.includes("too hot")) {
                type = "warning";
                color = "#facc15"; // Yellow
                name = "Warning Logs";
            } else if (lowerDetail.includes("error") || lowerDetail.includes("failed") || lowerDetail.includes("not sending data") || lowerDetail.includes("sensor")) {
                type = "error";
                color = "#ef4444"; // Red
                name = "Error Logs";
            } else {
                // Default fallback if it doesn't clearly match error/warning
                type = "info";
                color = "#9333ea"; // Purple
                name = "Info Logs";
            }
        }
        
        const d = new Date(row.event_at);
        const mm = String(d.getMonth() + 1).padStart(2, '0');
        const dd = String(d.getDate()).padStart(2, '0');
        const yyyy = d.getFullYear();
        
        const hh24 = d.getHours();
        const min = String(d.getMinutes()).padStart(2, '0');
        const ampm = hh24 >= 12 ? 'PM' : 'AM';
        const hh12 = hh24 % 12 || 12;
        const timeStr = `${String(hh12).padStart(2, '0')}:${min} ${ampm}`;

        calendarEvents.push({
            id: `s_${row.id}`,
            name: name,
            date: `${mm}/${dd}/${yyyy}`,
            time: timeStr,
            type: type,
            description: detail,
            color: color
        });
    });
    
    res.json(calendarEvents);
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// Fallback → index.html
app.get('*', (req, res) => {
  res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

// ── Start ─────────────────────────────────────────────────────────────────────
(async () => {
  try {
    await initDB();
  } catch (e) {
    console.error('⚠️  DB not ready yet, will retry on each request:', e.message);
  }
  initMQTT();
  server.listen(PORT, '0.0.0.0', () => {
    console.log(`🌿 Garden Monitor listening on http://0.0.0.0:${PORT}`);
  });
})();
