'use strict';

// ── WebSocket ──────────────────────────────────────────────
const wsProto = location.protocol === 'https:' ? 'wss' : 'ws';
let ws, wsRetry = 0;
let latestData = {};

function connectWS() {
  ws = new WebSocket(`${wsProto}://${location.host}/ws`);
  ws.onopen = () => { wsRetry = 0; };
  ws.onclose = () => { setTimeout(connectWS, Math.min(5000 * ++wsRetry, 30000)); };
  ws.onerror = () => ws.close();
  ws.onmessage = e => {
    try {
      const msg = JSON.parse(e.data);
      // Support both direct sensor data and wrapped format
      if (msg.type === 'sensor_update') {
        latestData = msg.data;
        updateDashboard(msg.data);
      } else if (msg.ts || msg.soil_moisture) {
        // Direct sensor data from ESP32
        latestData = msg;
        updateDashboard(msg);
      } else if (msg.type === 'watering_log' || msg.type === 'system_event') {
        if (document.getElementById('page-watering')?.classList.contains('active')) {
          fetchCalendarEvents();
        }
      }
    } catch { }
  };
}

function toggleWsTooltip() {
  const t = document.getElementById('ws-tooltip');
  if (t) t.classList.toggle('show');
  // close on next outside click
  if (t && t.classList.contains('show')) {
    setTimeout(() => {
      document.addEventListener('click', function handler(e) {
        if (!t.contains(e.target)) { t.classList.remove('show'); }
        document.removeEventListener('click', handler);
      });
    }, 10);
  }
}

// ── Fetch latest from REST on load ────────────────────────
async function fetchLatest() {
  try {
    const r = await fetch('/api/latest');
    if (r.ok) {
      latestData = await r.json();
      updateDashboard(latestData);
    }
  } catch { }
}

// ── Dashboard update ──────────────────────────────────────
function updateDashboard(d) {
  lastDataTime = d.recorded_at
    ? new Date(d.recorded_at)
    : d.ts
      ? new Date(d.ts * 1000)
      : new Date();

  updateTimeAgo();

  const sysData = d.sys || (typeof d.raw_json === 'string' ? (JSON.parse(d.raw_json).sys || null) : (d.raw_json?.sys || null));
  if (sysData) {
    if (d.wifi_ssid) {
      sysData.wifi = (d.wifi_rssi < -85) ? 'warn' : 'ok';
    }
    updateSystemHealth(sysData);
  }

  d.soil1_pct = parseFloat(rawToMoistPct(d.soil1_raw));
  d.soil2_pct = parseFloat(rawToMoistPct(d.soil2_raw));
  d.soil3_pct = parseFloat(rawToMoistPct(d.soil3_raw));

  // Compute Averages for Temp and Humidity from both sensors
  const atVal = num(d.ambient_temp), etVal = num(d.env_temp);
  const ahVal = num(d.humidity), ehVal = num(d.env_hum);

  if (d.ambient_temp != null && d.env_temp != null) { d.avg_temp = (atVal + etVal) / 2; }
  else { d.avg_temp = d.ambient_temp != null ? atVal : etVal; }

  if (d.humidity != null && d.env_hum != null) { d.avg_hum = (ahVal + ehVal) / 2; }
  else { d.avg_hum = d.humidity != null ? ahVal : ehVal; }

  // ── Soil Health ──
  const moist = num(d.soil_moisture);
  const soilLabel = moist < 30 ? 'Too Dry' : moist < 60 ? 'Adequate' : 'Well Moist';
  setText('val-soil', soilLabel);

  // ── Soil Temp ──
  const st = num(d.soil_temp);
  setText('val-soiltemp-dash', isNaN(st) || d.soil_temp == null ? '—' : `${st.toFixed(1)} °C`);

  // ── Fertilization ──
  const n = num(d.nitrogen), p = num(d.phosphorus), k = num(d.potassium), ph = num(d.ph);
  let fertLabel = 'No Data';
  if (n || p || k) {
    const avg = (n + p + k) / 3;
    if (avg < 50) { fertLabel = 'Low'; }
    else if (avg > 200) { fertLabel = 'High'; }
    else { fertLabel = 'Balanced'; }
    if (ph > 0 && (ph < 5.5 || ph > 7.5)) { fertLabel += ' (PH Off)'; }
  }
  setText('val-fertilize', fertLabel);

  // ── UV Index ──
  const uv = num(d.uv_index);
  const uvLabel = uv < 3 ? 'Minimal' : uv < 6 ? 'Good' : uv < 8 ? 'High' : uv < 11 ? 'Very High' : 'Extreme';
  setText('val-uv', uvLabel);

  // ── Temperature ──
  const at = num(d.avg_temp);
  const tLabel = at < 18 ? 'Cold' : at < 28 ? 'Good' : at < 34 ? 'Warm' : 'Too Hot';
  setText('val-temp', tLabel);

  // ── Weather & Animations ──
  const raining = !!d.is_raining;
  const wLabel = raining ? 'Rainy' : uv > 6 ? 'Sunny' : at > 28 ? 'Hot' : 'Cloudy';
  setText('val-weather', wLabel);
  updateWeatherEffects(uv, raining);

  // ── Battery ──
  const charging = !!d.is_charging;
  const bLabel = charging ? 'Charging' : 'Discharging';
  setText('val-battery', bLabel);

  // ── List View Page ──
  setText('dv-moisture', `${num(d.soil_moisture).toFixed(2)} %`);
  setText('dv-soil1', rawToMoistPct(d.soil1_raw));
  setText('dv-soil2', rawToMoistPct(d.soil2_raw));
  setText('dv-soil3', rawToMoistPct(d.soil3_raw));
  setText('dv-soiltemp', `${num(d.soil_temp).toFixed(2)} °C`);
  setText('dv-ambtemp', `${num(d.ambient_temp).toFixed(2)} °C`);
  setText('dv-hum', `${num(d.humidity).toFixed(2)} %`);
  setText('dv-envtemp', d.env_temp != null ? `${num(d.env_temp).toFixed(2)} °C` : '—');
  setText('dv-envhum', d.env_hum != null ? `${num(d.env_hum).toFixed(2)} %` : '—');
  setText('dv-inttemp', d.int_temp != null ? `${num(d.int_temp).toFixed(2)} °C` : '—');
  setText('dv-intpres', d.int_pres != null ? `${num(d.int_pres).toFixed(2)} hPa` : '—');
  setText('dv-soctemp', d.soc_temp != null ? `${num(d.soc_temp).toFixed(2)} °C` : '—');
  setText('dv-uv', num(d.uv_index).toFixed(2));
  setText('dv-rain', d.is_raining ? '🌧️ Likely/Raining' : '☀️ Dry');
  setText('dv-n', d.nitrogen != null ? `${num(d.nitrogen).toFixed(0)} ppm` : '—');
  setText('dv-p', d.phosphorus != null ? `${num(d.phosphorus).toFixed(0)} ppm` : '—');
  setText('dv-k', d.potassium != null ? `${num(d.potassium).toFixed(0)} ppm` : '—');
  setText('dv-ph', d.ph != null ? num(d.ph).toFixed(2) : '—');
  setText('dv-bv', `${num(d.battery_v).toFixed(2)} V`);
  setText('dv-ma', `${num(d.current_ma).toFixed(2)} mA`);
  setText('dv-mw', `${num(d.power_mw).toFixed(2)} mW`);
  setText('val-valve', d.valve_open ? 'Open' : 'Closed');

  // refresh open modal gauges only (chart is debounced separately)
  if (currentModal) {
    refreshGauges(currentModal, d);
  }

  // refresh analytics chart if it is visible
  if (document.getElementById('page-history').classList.contains('active')) {
    if (histPeriod === 'hourly') loadHistoryPage();
  }
}

// ── Dynamic Weather Effects ──
function updateWeatherEffects(uv, raining) {
  const layer = document.getElementById('weather-layer');
  const sky = document.getElementById('sky-bg');
  if (!layer || !sky) return;

  layer.innerHTML = '';
  sky.className = 'sky-background';

  if (raining) {
    sky.classList.add('is-rainy');
    for (let i = 0; i < 40; i++) {
      const drop = document.createElement('div');
      drop.className = 'raindrop';
      drop.style.left = Math.random() * 100 + '%';
      drop.style.animationDuration = (0.4 + Math.random() * 0.3) + 's';
      drop.style.animationDelay = Math.random() + 's';
      layer.appendChild(drop);
    }
  } else if (uv <= 6) {
    // Cloudy
    for (let i = 0; i < 4; i++) {
      const cloud = document.createElement('div');
      cloud.className = 'cloud';
      cloud.style.top = (5 + Math.random() * 20) + '%';
      cloud.style.animationDuration = (30 + Math.random() * 30) + 's';
      cloud.style.animationDelay = '-' + (Math.random() * 20) + 's';
      layer.appendChild(cloud);
    }
  }
}


function rawToMoistPct(raw) {
  if (raw == null) return '—';
  const DRY = 3200, WET = 1200;
  const pct = ((DRY - raw) / (DRY - WET)) * 100;
  return `${Math.max(0, Math.min(100, pct)).toFixed(0)} %`;
}

function setText(id, val) {
  const el = document.getElementById(id);
  if (el) el.textContent = val;
}
function num(v) { return parseFloat(v) || 0; }

// ── Page Navigation ───────────────────────────────────────
function setPage(page) {
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));
  document.getElementById(`page-${page}`)?.classList.add('active');
  document.getElementById(`nav-${page}`)?.classList.add('active');
  if (page === 'dashboard') loadHistoryPage();
  if (page === 'watering') loadWateringLogs();
  document.querySelector('.sidebar')?.classList.remove('open');
}

function toggleMenu() {
  document.querySelector('.sidebar')?.classList.toggle('open');
}

// ── Toast Notifications ───────────────────────────────────
function showToast(message, type = 'success', durationMs = 3500) {
  const container = document.getElementById('toast-container');
  if (!container) return;
  const colors = {
    success: { bg: 'rgba(34,197,94,0.15)', border: 'rgba(34,197,94,0.4)', icon: '✅' },
    error: { bg: 'rgba(239,68,68,0.15)', border: 'rgba(239,68,68,0.4)', icon: '❌' },
    info: { bg: 'rgba(14,165,233,0.15)', border: 'rgba(14,165,233,0.4)', icon: 'ℹ️' },
    warn: { bg: 'rgba(250,204,21,0.15)', border: 'rgba(250,204,21,0.4)', icon: '⚠️' },
  };
  const c = colors[type] || colors.info;
  const el = document.createElement('div');
  el.style.cssText = `
    background: rgba(255,255,255,0.55); backdrop-filter: blur(20px) saturate(180%);
    -webkit-backdrop-filter: blur(20px) saturate(180%);
    border: 1px solid ${c.border}; border-radius: 14px;
    padding: 12px 18px; display: flex; align-items: center; gap: 10px;
    box-shadow: 0 8px 32px rgba(0,0,0,0.15); pointer-events: auto;
    font-weight: 600; font-size: 0.9rem; color: #333;
    opacity: 0; transform: translateX(20px);
    transition: opacity 0.25s, transform 0.25s;
    max-width: 320px;
  `;
  el.innerHTML = `<span>${c.icon}</span><span>${message}</span>`;
  container.appendChild(el);
  requestAnimationFrame(() => { el.style.opacity = '1'; el.style.transform = 'translateX(0)'; });
  setTimeout(() => {
    el.style.opacity = '0'; el.style.transform = 'translateX(20px)';
    setTimeout(() => el.remove(), 300);
  }, durationMs);
}

// ── Evo-Calendar (Logs) ───────────────────────────────────
function loadWateringLogs() {
  fetchCalendarEvents();
}

async function fetchCalendarEvents() {
  try {
    const res = await fetch('/api/calendar-events');
    const allEvents = await res.json();
    
    if (!Array.isArray(allEvents)) {
        console.error("Failed to load events. Backend returned:", allEvents);
        return;
    }
    
    // Store globally for the custom renderer
    window.fullCalendarEvents = allEvents;
    
    // Deduplicate so Evo-Calendar only draws ONE dot per category per day
    const dotsMap = {};
    const summaryEvents = [];
    allEvents.forEach(e => {
        const key = `${e.date}_${e.type}`;
        if (!dotsMap[key]) {
            dotsMap[key] = true;
            summaryEvents.push(e);
        }
    });
    
    // Destroy previous instance if it exists to refresh events safely
    if ($("#calendar").hasClass("calendar-initialized")) {
        try { $("#calendar").evoCalendar('destroy'); } catch (e) {}
        $("#calendar").removeClass("calendar-initialized");
    }
    
    $("#calendar").evoCalendar({
      theme: 'Midnight Blue',
      format: 'mm/dd/yyyy',
      titleFormat: 'MM yyyy',
      eventHeaderFormat: 'MM d, yyyy',
      calendarEvents: summaryEvents
    });
    $("#calendar").addClass("calendar-initialized");
    
    // Bind to Evo-Calendar events to override the UI
    $("#calendar").on('selectDate', function() {
        renderCustomEventList();
    });
    $("#calendar").on('selectMonth', function() {
        renderCustomEventList();
    });
    
    renderCustomEventList();
  } catch(e) { console.error("Evo-Calendar fetch error:", e); }
}

function renderCustomEventList() {
    const selectedDate = $('#calendar').evoCalendar('getActiveDate'); // e.g. "05/27/2026"
    const container = $('.calendar-events'); // the default evo-calendar container
    
    // Group events
    const groups = {
        error: { title: 'Error Logs', color: '#ef4444', events: [] },
        warning: { title: 'Warning Logs', color: '#facc15', events: [] },
        info: { title: 'Info Logs', color: '#9333ea', events: [] },
        watering: { title: 'Watering Logs', color: '#0ea5e9', events: [] }
    };
    
    const fullEvents = window.fullCalendarEvents || [];
    let hasEvents = false;
    
    fullEvents.forEach(e => {
        if (e.date === selectedDate && groups[e.type]) {
            groups[e.type].events.push(e);
            hasEvents = true;
        }
    });

    let html = '<div class="calendar-events-custom">';
    
    Object.keys(groups).forEach(key => {
        const group = groups[key];
        if (group.events.length === 0) return;
        hasEvents = true;
        
        // Build Accordion
        html += `
        <div class="event-accordion expanded">
          <div class="event-accordion-header" onclick="this.parentElement.classList.toggle('expanded')">
            <div class="dot" style="background-color: ${group.color}"></div>
            <div class="title">${group.title}</div>
            <div class="chevron">
               <svg viewBox="0 0 24 24" width="16" height="16" fill="currentColor"><path d="M8.59,16.59L13.17,12,8.59,7.41,10,6l6,6-6,6Z"/></svg>
            </div>
          </div>
          <div class="event-accordion-body">
        `;
        
        group.events.forEach(e => {
            html += `
            <div class="timeline-item">
              <div class="timeline-time">${e.time || ''}</div>
              <div class="timeline-divider"><div class="timeline-dot"></div></div>
              <div class="timeline-content">
                <div class="timeline-title">${e.name}</div>
                <div class="timeline-desc">${e.description}</div>
              </div>
            </div>
            `;
        });
        
        html += `
          </div>
        </div>
        `;
    });
    
    if (!hasEvents) {
        html += `<div style="text-align:center; padding:20px; color:#999; font-size:14px;">No events for this date.</div>`;
    }
    
    html += '</div>';
    
    // Evo calendar re-renders .calendar-events natively so we overwrite it shortly after
    setTimeout(() => {
        container.html(html);
    }, 50);
}

// ── Watering Control ──────────────────────────────────────
let waterTimer = null;
const WATER_DURATION_MS = 30000; // 30s visible timer (adjust as needed)

async function cmdWater() {
  const btn = document.getElementById('btn-water');
  if (btn) { btn.classList.add('active'); btn.textContent = 'Watering...'; }
  try {
    await sendCmd({ cmd: 'water_start' });
    // Show overlay
    const overlay = document.getElementById('water-overlay');
    const bar = document.getElementById('water-progress-bar');
    if (overlay) { overlay.style.display = 'flex'; }
    if (bar) { bar.style.width = '0%'; }
    let elapsed = 0;
    clearInterval(waterTimer);
    waterTimer = setInterval(() => {
      elapsed += 500;
      const pct = Math.min((elapsed / WATER_DURATION_MS) * 100, 100);
      if (bar) bar.style.width = pct + '%';
      if (elapsed >= WATER_DURATION_MS) {
        clearInterval(waterTimer);
        cmdWaterStop(true);
      }
    }, 500);
  } catch (e) {
    showToast('Failed to send water command', 'error');
    if (btn) { btn.classList.remove('active'); btn.textContent = 'Water Plant'; }
  }
}

async function cmdWaterStop(auto = false) {
  clearInterval(waterTimer);
  const overlay = document.getElementById('water-overlay');
  const bar = document.getElementById('water-progress-bar');
  const btn = document.getElementById('btn-water');
  if (overlay) overlay.style.display = 'none';
  if (bar) bar.style.width = '0%';
  if (btn) { btn.classList.remove('active'); btn.textContent = 'Water Plant'; }
  await sendCmd({ cmd: 'water_stop' });
  if (auto) showToast('💧 Watering complete! Valve closed.', 'success', 4000);
  else showToast('Valve closed manually.', 'info');
}

async function cmdSyncRTC() {
  const btn = document.getElementById('btn-sync-rtc');
  if (btn) { btn.classList.add('active'); btn.textContent = 'Syncing...'; }
  try {
    await sendCmd({ cmd: 'sync_rtc' });
    showToast('🕒 RTC sync command sent! Check serial monitor.', 'success');
  } catch (e) {
    showToast('Failed to send RTC sync command', 'error');
  } finally {
    setTimeout(() => {
      if (btn) { btn.classList.remove('active'); btn.textContent = 'Sync RTC'; }
    }, 2000);
  }
}

async function sendCmd(payload) {
  const r = await fetch('/api/command', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  if (!r.ok) {
    const err = await r.text();
    throw new Error(`HTTP ${r.status}: ${err}`);
  }
  return r.json();
}

async function confirmPurgeDB() {
  if (confirm('⚠️ WARNING: This will PERMANENTLY delete all sensor history from the database. Are you sure?')) {
    try {
      const r = await fetch('/api/purge-db', { method: 'POST' });
      if (r.ok) {
        showToast('Database purged successfully', 'success');
        if (histChart) loadHistoryPage();
      } else {
        showToast('Purge failed', 'error');
      }
    } catch (e) {
      showToast('Error purging database', 'error');
    }
  }
}

async function analyzeRainLikelihood() {
  try {
    const presRes = await fetch(`/api/history/int_pres?period=hourly`);
    const presRows = await presRes.json();
    if (!Array.isArray(presRows)) {
        console.error("Failed to fetch pressure data:", presRows);
        return;
    }
    const pressures = presRows.map(r => parseFloat(r.value));
    const curRes = await fetch('/api/history/current_ma?period=hourly');
    const curRows = await curRes.json();

    if (!presRows || presRows.length < 5) return;

    const presVals = presRows.map(r => r.value);
    const presDrop = presVals[0] - presVals[presVals.length - 1]; 
    
    const curVals = (curRows && Array.isArray(curRows)) ? curRows.map(r => r.value) : [];
    
    const latestHum = num(latestData.avg_hum || latestData.humidity);
    const latestUV = num(latestData.uv_index);
    const latestCur = num(latestData.current_ma);

    const now = new Date();
    const isDaylight = now.getHours() >= 7 && now.getHours() <= 17;
    
    // Detect sudden cloud cover: was charging > 100mA recently, but now < 20mA
    const maxRecentCur = curVals.length > 0 ? Math.max(...curVals) : 0;
    const solarDrop = isDaylight && latestCur < 20 && maxRecentCur > 100;
    const lowUV = isDaylight && latestUV < 1.0;

    let predictedRain = false;
    
    // 1. Barometric Trend (Day or Night)
    if (presDrop > 1.2) predictedRain = true;
    
    // 2. High Humidity + Pressure Drop (Day or Night)
    if (latestHum > 93 && presDrop > 0.4) predictedRain = true;

    // 3. Solar/UV Sudden Drop (Daylight only)
    if (isDaylight && (solarDrop || lowUV) && latestHum > 82) {
      predictedRain = true;
    }

    sendCmd({ cmd: 'weather_prediction', raining: predictedRain });
  } catch (e) { console.error('Rain analysis failed:', e); }
}

// ── Modal ─────────────────────────────────────────────────
let currentModal = null;
let modalChart = null;
let modalChart2 = null;
let lastModalChartLoad = 0;  // timestamp ms — debounce auto-refresh

const MODAL_CONFIG = {
  soil: { title: 'Soil Health', sensors: ['soil1_pct', 'soil2_pct', 'soil3_pct', 'soil_moisture'], colors: ['#4ade80', '#22c55e', '#16a34a', '#0ea5e9'] },
  soiltemp: { title: 'Soil Temperature', sensors: ['soil_temp'], colors: ['#f59e0b'] },
  fertilize: { title: 'Fertilization & PH', sensors: ['nitrogen', 'phosphorus', 'potassium', 'ph'], colors: ['#0ea5e9', '#55c21b', '#f59e0b', '#d946ef'] },
  uv: { title: 'UV Index', sensors: ['uv_index'], colors: ['#f59e0b'] },
  temp: { title: 'Temperature And Humidity', sensors: ['avg_temp', 'avg_hum', 'ambient_temp', 'humidity', 'env_temp', 'env_hum'], colors: ['#0ea5e9', '#3b82f6', '#f59e0b', '#d946ef', '#10b981', '#6366f1'] },
  weather: { title: 'Weather / Rain', sensors: ['uv_index', 'humidity'], colors: ['#f59e0b', '#0ea5e9'] },
  battery: { title: 'Battery Usage', sensors: ['battery_v'], colors: ['#55c21b'] },
};

const GAUGE_KEYS = {
  soil: [
    { key: 'soil1_pct', label: 'Moisture - Left', unit: '%', max: 100 },
    { key: 'soil2_pct', label: 'Moisture - Middle', unit: '%', max: 100 },
    { key: 'soil3_pct', label: 'Moisture - Right', unit: '%', max: 100 },
    { key: 'soil_moisture', label: 'Avg Moisture', unit: '%', max: 100 }
  ],
  soiltemp: [{ key: 'soil_temp', label: 'Soil Temp', unit: '°C', max: 50 }],
  fertilize: [
    { key: 'nitrogen', label: 'Nitrogen (N)', unit: 'ppm', max: 500 },
    { key: 'phosphorus', label: 'Phosphorus (P)', unit: 'ppm', max: 500 },
    { key: 'potassium', label: 'Potassium (K)', unit: 'ppm', max: 500 },
    { key: 'ph', label: 'Soil PH', unit: '', max: 14 }
  ],
  uv: [{ key: 'uv_index', label: 'UV Index', unit: '', max: 16 }],
  temp: [
    {
      group: 'Real-Time Environment 1 :',
      rows: [
        [{ key: 'env_temp', label: 'Env1 Temp (HDC)', unit: '°C', max: 50, color: '#0ea5e9' },
        { key: 'env_hum', label: 'Env1 Hum (HDC)', unit: '%', max: 100, color: '#6366f1' }],
        [{ key: 'avg_temp_1', dataKey: 'avg_temp', label: 'Avg Temp', unit: '°C', max: 50, color: '#10b981' },
        { key: 'avg_hum_1', dataKey: 'avg_hum', label: 'Avg Hum', unit: '%', max: 100, color: '#3b82f6' }]
      ]
    },
    {
      group: 'Real-Time Environment 2 :',
      rows: [
        [{ key: 'ambient_temp', label: 'Env2 Temp (AHT)', unit: '°C', max: 50, color: '#f59e0b' },
        { key: 'humidity', label: 'Env2 Hum (AHT)', unit: '%', max: 100, color: '#d946ef' }],
        [{ key: 'avg_temp_2', dataKey: 'avg_temp', label: 'Avg Temp', unit: '°C', max: 50, color: '#10b981' },
        { key: 'avg_hum_2', dataKey: 'avg_hum', label: 'Avg Hum', unit: '%', max: 100, color: '#3b82f6' }]
      ]
    }
  ],
  weather: [{ key: 'uv_index', label: 'UV Index', unit: '', max: 16 }, { key: 'humidity', label: 'Humidity', unit: '%', max: 100 }],
  battery: [{ key: 'battery_v', label: 'Voltage', unit: 'V', max: 15 }, { key: 'current_ma', label: 'Current', unit: 'mA', max: 3000 }],
};

function openModal(type) {
  currentModal = type;
  const cfg = MODAL_CONFIG[type];
  if (!cfg) return;

  document.getElementById('modal-title').textContent = cfg.title;

  buildGauges(type);
  document.getElementById('modal-overlay').classList.add('open');
  lastModalChartLoad = 0;  // force a fresh load on open
  loadModalChart(type);
}

function closeModal(e) {
  if (e && e.target !== document.getElementById('modal-overlay') && e.type === 'click') return;
  document.getElementById('modal-overlay').classList.remove('open');
  currentModal = null;
  if (modalChart) { modalChart.destroy(); modalChart = null; }
  if (modalChart2) { modalChart2.destroy(); modalChart2 = null; }
}

async function loadModalChart(type) {
  const cfg = MODAL_CONFIG[type];
  if (!cfg) return;

  // Debounce: don't reload if loaded within last 2 minutes
  // (reset to 0 by openModal/tab switch to force fresh load)
  const now = Date.now();
  if (lastModalChartLoad > 0 && (now - lastModalChartLoad) < 120_000) return;
  lastModalChartLoad = now;

  // Fetch all sensor rows concurrently
  const rawResults = await Promise.all(
    cfg.sensors.map(async (s) => {
      try {
        let url = `/api/history/${s}`;
        if (globalStart && globalEnd) {
            url += `?start=${Math.floor(globalStart.getTime()/1000)}&end=${Math.floor(globalEnd.getTime()/1000)}`;
        } else {
            url += `?period=hourly`;
        }
        const r = await fetch(url);
        const rows = await r.json();
        return Array.isArray(rows) ? rows : [];
      } catch { return []; }
    })
  );

  // Build a unified ordered labels list from all datasets combined
  const labelSet = new Map(); // label -> min recorded_at order key
  rawResults.forEach(rows => rows.forEach((row, idx) => {
    if (!labelSet.has(row.label)) labelSet.set(row.label, idx);
  }));
  const allLabels = [...labelSet.keys()]; // already insertion-ordered (oldest first)

  // Build flat numeric data arrays aligned to allLabels
  const datasets = rawResults.map((rows, i) => {
    const rowMap = new Map(rows.map(r => [r.label, parseFloat(r.value) || 0]));
    const color = cfg.colors[i];
    return {
      label: cfg.sensors[i].replace(/_/g, ' '),
      data: allLabels.map(lbl => rowMap.has(lbl) ? rowMap.get(lbl) : null),
      borderColor: color,
      backgroundColor: color + '33',  // ~20% opacity fill
      fill: true,
      tension: 0.4,
      pointRadius: allLabels.length > 30 ? 0 : 3,
      borderWidth: 2.5,
      spanGaps: true,  // connect across null gaps
    };
  });

  const ctx = document.getElementById('modal-chart').getContext('2d');
  if (modalChart) modalChart.destroy();
  if (modalChart2) modalChart2.destroy();

  Chart.defaults.color = '#777';
  Chart.defaults.borderColor = 'rgba(0,0,0,0.05)';

  const showLegend = cfg.sensors.length > 1;

  const chartOpts = {
    responsive: true,
    maintainAspectRatio: false,
    interaction: { mode: 'index', intersect: false },
    scales: {
      x: { display: true, grid: { display: false }, ticks: { maxTicksLimit: 8, maxRotation: 0 } },
      y: {
        display: true,
        beginAtZero: false,
        ticks: { callback: v => Number(v).toFixed(2) }
      }
    },
    plugins: {
      legend: { display: showLegend, position: 'top', labels: { boxWidth: 12, font: { size: 11 } } },
      tooltip: {
        backgroundColor: 'rgba(255,255,255,0.95)', titleColor: '#333', bodyColor: '#555',
        borderColor: 'rgba(0,0,0,0.1)', borderWidth: 1,
        callbacks: { label: ctx => ` ${ctx.dataset.label}: ${Number(ctx.parsed.y).toFixed(2)}` }
      }
    }
  };

  const box2 = document.getElementById('modal-chart-box-2');
  if (box2) box2.style.display = 'none';

  if (type === 'temp') {
    // Chart 1: HDC (indices 4,5) and Averages (indices 0,1)
    const ds1 = [datasets[0], datasets[1], datasets[4], datasets[5]];
    // Chart 2: AHT (indices 2,3) and Averages (indices 0,1)
    const ds2 = [datasets[0], datasets[1], datasets[2], datasets[3]];

    modalChart = new Chart(ctx, { type: 'line', data: { labels: allLabels, datasets: ds1 }, options: chartOpts });

    if (box2) box2.style.display = 'block';
    const ctx2 = document.getElementById('modal-chart-2').getContext('2d');
    modalChart2 = new Chart(ctx2, { type: 'line', data: { labels: allLabels, datasets: ds2 }, options: chartOpts });
  } else {
    modalChart = new Chart(ctx, { type: 'line', data: { labels: allLabels, datasets }, options: chartOpts });
  }
}

// ── Gauges ────────────────────────────────────────────────
const gaugeInstances = {};

function _makeGaugeEl(gk, color) {
  const wrap = document.createElement('div');
  wrap.style.cssText = 'display:flex; flex-direction:column; align-items:center; gap:8px;';
  const label = document.createElement('div');
  label.style.cssText = 'font-weight:600; color:#555; font-size:0.9rem; text-align:center;';
  label.textContent = gk.label || gk.key;
  const canvasWrap = document.createElement('div');
  canvasWrap.className = 'gauge-wrapper';
  const canvas = document.createElement('canvas');
  canvas.id = `gauge-${gk.key}`;
  canvas.width = 240; canvas.height = 240;
  canvasWrap.appendChild(canvas);
  wrap.appendChild(label);
  wrap.appendChild(canvasWrap);
  const dataField = gk.dataKey || gk.key;
  const val = num(latestData[dataField]);
  drawGauge(canvas, val, gk.max, gk.unit, color);
  gaugeInstances[gk.key] = { canvas, max: gk.max, unit: gk.unit, color, dataField };
  return wrap;
}

function buildGauges(type) {
  const container = document.getElementById('modal-gauges');
  container.innerHTML = '';
  const cfg = MODAL_CONFIG[type];
  const configArr = GAUGE_KEYS[type] || [];

  // ── Temp modal: grouped cards with row-by-row 2x2 layout ──────────────────
  if (configArr.length > 0 && configArr[0].rows) {
    container.style.cssText = 'display:flex; flex-direction:column; gap:0px; width:100%;';
    configArr.forEach(g => {
      const card = document.createElement('div');
      card.style.cssText = 'background:rgba(255,255,255,0); border-radius:14px; padding:5px 45px;';
      const title = document.createElement('h4');
      title.textContent = g.group;
      title.style.cssText = 'margin:0 0 12px 0; font-size:0.9rem; font-weight:600; color:#444;';
      card.appendChild(title);
      g.rows.forEach(row => {
        const rowEl = document.createElement('div');
        rowEl.style.cssText = 'display:flex; flex-direction:row; gap:12px; justify-content:center; margin-bottom:8px;';
        row.forEach(gk => rowEl.appendChild(_makeGaugeEl(gk, gk.color)));
        card.appendChild(rowEl);
      });
      container.appendChild(card);
    });
    return;
  }

  // ── All other modals: original horizontal flex layout ─────────────────────
  container.style.cssText = 'display:flex; flex-wrap:wrap; gap:16px; justify-content:center;';
  configArr.forEach((gk, i) => {
    const color = cfg.colors[i % cfg.colors.length];
    container.appendChild(_makeGaugeEl(gk, color));
  });
}

function refreshGauges(type, data) {
  // Walk all registered gauge instances regardless of structure
  for (const key in gaugeInstances) {
    const inst = gaugeInstances[key];
    if (!inst) continue;
    drawGauge(inst.canvas, num(data[inst.dataField || key]), inst.max, inst.unit, inst.color);
  }
}

function drawGauge(canvas, value, max, unit, color) {
  const ctx = canvas.getContext('2d');
  const W = canvas.width, H = canvas.height;
  const cx = W / 2, cy = H / 2;
  const r = W * 0.35;
  const startAngle = Math.PI * 0.5;
  const endAngle = Math.PI * 2.5;
  const ratio = Math.min(Math.max(value / max, 0), 1);

  ctx.clearRect(0, 0, W, H);
  ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2);
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.4)'; ctx.lineWidth = 12; ctx.stroke();

  if (ratio > 0) {
    const valAngle = startAngle + (Math.PI * 2) * ratio;
    ctx.beginPath(); ctx.arc(cx, cy, r, startAngle, valAngle);
    ctx.strokeStyle = color || '#38bdf8';
    ctx.lineWidth = 12; ctx.lineCap = 'round'; ctx.stroke();
  }

  // Use 2 decimal points globally for gauges (except NPK which is 0 decimals)
  let display = '—';
  if (!isNaN(value)) {
    if (unit === 'ppm') display = `${value.toFixed(0)}${unit}`;
    else display = `${value.toFixed(2)}${unit}`;
  }
  
  ctx.fillStyle = '#333'; ctx.font = `600 ${W * 0.18}px Inter, sans-serif`;
  ctx.textAlign = 'center'; ctx.textBaseline = 'middle'; ctx.fillText(display, cx, cy);
}

// ── Global Date & Refresh Controls ─────────────────────────
let globalStart = null;
let globalEnd = null;
let globalAutoRefreshTimer = null;

function updateGlobalAutoRefresh() {
  const select = document.getElementById('global-auto-refresh-select');
  const ms = parseInt(select.value, 10);
  if (globalAutoRefreshTimer) clearInterval(globalAutoRefreshTimer);
  
  // Post interval configuration to backend API (which sends MQTT to ESP32)
  let val = select.value === "0" ? "auto" : parseInt(select.value, 10);
  fetch('/api/command', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ cmd: 'set_interval', val: val })
  }).catch(e => console.error("Auto refresh update error", e));

  if (ms > 0) {
      globalAutoRefreshTimer = setInterval(() => {
          forceRefreshCharts();
      }, ms);
  }
}

function forceRefreshCharts() {
  if (document.getElementById('page-dashboard').classList.contains('active')) {
      loadHistoryPage();
  }
  if (currentModal && document.getElementById('modal-overlay').classList.contains('open')) {
      lastModalChartLoad = 0;
      loadModalChart(currentModal);
  }
}

function shiftGlobalTime(dir) {
  if (!globalStart || !globalEnd) {
      globalEnd = new Date();
      globalStart = new Date(globalEnd.getTime() - 24 * 3600 * 1000);
  }
  const diff = globalEnd.getTime() - globalStart.getTime();
  globalStart = new Date(globalStart.getTime() + dir * diff);
  globalEnd = new Date(globalEnd.getTime() + dir * diff);
  updateGlobalDateLabel();
  forceRefreshCharts();
}

function zoomOutGlobalTime() {
  if (!globalStart || !globalEnd) {
      globalEnd = new Date();
      globalStart = new Date(globalEnd.getTime() - 24 * 3600 * 1000);
  }
  const diff = globalEnd.getTime() - globalStart.getTime();
  globalStart = new Date(globalStart.getTime() - diff/2);
  globalEnd = new Date(globalEnd.getTime() + diff/2);
  updateGlobalDateLabel();
  forceRefreshCharts();
}

function updateGlobalDateLabel() {
  const lbl = document.getElementById('global-date-label');
  if (!globalStart || !globalEnd) {
    lbl.textContent = 'Last 24 hours';
    sessionStorage.removeItem('globalStart');
    sessionStorage.removeItem('globalEnd');
    return;
  }
  const opts = { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' };
  lbl.textContent = `${globalStart.toLocaleString(undefined, opts)} - ${globalEnd.toLocaleString(undefined, opts)}`;
  
  // Save to sessionStorage so it persists across page navigations
  sessionStorage.setItem('globalStart', globalStart.getTime());
  sessionStorage.setItem('globalEnd', globalEnd.getTime());
}

// ── Grafana Popover Logic ─────────────────────────
function parseGrafanaTime(str) {
  if (str === 'now') return new Date();
  const match = str.match(/^now-(\d+)([mhdMy])$/);
  if (!match) {
    // fallback
    const d = new Date(str);
    return isNaN(d.getTime()) ? new Date() : d;
  }
  const val = parseInt(match[1], 10);
  const unit = match[2];
  const d = new Date();
  if (unit === 'm') d.setMinutes(d.getMinutes() - val);
  if (unit === 'h') d.setHours(d.getHours() - val);
  if (unit === 'd') d.setDate(d.getDate() - val);
  if (unit === 'M') d.setMonth(d.getMonth() - val);
  if (unit === 'y') d.setFullYear(d.getFullYear() - val);
  return d;
}

function toggleGrafanaPopover() {
  const popover = document.getElementById('grafana-time-picker-popover');
  popover.style.display = popover.style.display === 'none' ? 'block' : 'none';
}

function applyGrafanaQuickRange(val, label) {
  document.getElementById('grafana-from-input').value = `now-${val}`;
  document.getElementById('grafana-to-input').value = 'now';
  
  // Update active state in list
  document.querySelectorAll('.grafana-quick-ranges li').forEach(el => el.classList.remove('active'));
  if (event && event.target.tagName === 'LI') {
    event.target.classList.add('active');
  }

  globalStart = parseGrafanaTime(`now-${val}`);
  globalEnd = new Date();
  
  updateGlobalDateLabel();
  document.getElementById('grafana-time-picker-popover').style.display = 'none';
  forceRefreshCharts();
}

function applyGrafanaAbsoluteTime() {
  const fromStr = document.getElementById('grafana-from-input').value;
  const toStr = document.getElementById('grafana-to-input').value;
  
  globalStart = parseGrafanaTime(fromStr);
  globalEnd = parseGrafanaTime(toStr);
  
  updateGlobalDateLabel();
  document.getElementById('grafana-time-picker-popover').style.display = 'none';
  forceRefreshCharts();
}

// ── History Page Chart ────────────────────────────────────
let histChart = null;

async function loadHistoryPage() {
  const sensor = document.getElementById('hist-sensor').value;
  let url = `/api/history/${sensor}`;
  
  if (globalStart && globalEnd) {
      url += `?start=${Math.floor(globalStart.getTime()/1000)}&end=${Math.floor(globalEnd.getTime()/1000)}`;
  } else {
      url += `?period=daily`; // fallback
  }
  
  try {
    const r = await fetch(url);
    const rows = await r.json();
    if (!Array.isArray(rows)) return;

    const ctx = document.getElementById('hist-chart').getContext('2d');
    if (histChart) histChart.destroy();

    Chart.defaults.color = '#777'; Chart.defaults.borderColor = 'rgba(0,0,0,0.05)';

    histChart = new Chart(ctx, {
      type: 'line',
      data: {
        labels: rows.map(r => r.label),
        datasets: [{
          label: sensor.replace(/_/g, ' '),
          data: rows.map(r => parseFloat(r.value) || 0),
          borderColor: '#0ea5e9', backgroundColor: '#0ea5e940',
          fill: true, tension: 0.45, pointRadius: 3, borderWidth: 2,
        }]
      },
      options: {
        responsive: true, maintainAspectRatio: false,
        plugins: {
          legend: { display: false },
          tooltip: { callbacks: { label: ctx => ` ${Number(ctx.parsed.y).toFixed(2)}` } }
        },
        scales: {
          y: { ticks: { callback: v => Number(v).toFixed(2) } }
        }
      }
    });
  } catch (e) { console.error('History load error:', e); }
}

// ── Init ──────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {
  // Restore global date from sessionStorage if exists
  const savedStart = sessionStorage.getItem('globalStart');
  const savedEnd = sessionStorage.getItem('globalEnd');
  if (savedStart && savedEnd) {
      globalStart = new Date(parseInt(savedStart, 10));
      globalEnd = new Date(parseInt(savedEnd, 10));
      updateGlobalDateLabel();
  }

  fetchLatest();
  connectWS();
  
  // Init Flatpickr for Absolute Time inputs inside Popover
  flatpickr("#grafana-from-input", {
    enableTime: true,
    dateFormat: "Y-m-d H:i",
    allowInput: true
  });
  flatpickr("#grafana-to-input", {
    enableTime: true,
    dateFormat: "Y-m-d H:i",
    allowInput: true
  });
  
  // Weather analysis every 5 minutes
  setTimeout(analyzeRainLikelihood, 10000); // initial run after 10s
  setInterval(analyzeRainLikelihood, 300000); 

  document.addEventListener('keydown', e => {
    if (e.key === 'Escape') {
      if (currentModal) closeModal();
      closeSysModal();
      document.getElementById('grafana-time-picker-popover').style.display = 'none';
    }
  });

  document.addEventListener('click', e => {
    const popover = document.getElementById('grafana-time-picker-popover');
    const btn = document.getElementById('global-date-picker-btn');
    if (popover && popover.style.display === 'block') {
      // Close popover if clicked outside of popover and not on the toggle button
      if (!popover.contains(e.target) && !btn.contains(e.target) && !e.target.closest('.flatpickr-calendar')) {
        popover.style.display = 'none';
      }
    }
  });
});

// ── Time Ago Formatting ───────────────────────────────────
let lastDataTime = null;
function updateTimeAgo() {
  if (!lastDataTime) return;
  const now = new Date();
  const diffSec = Math.floor((now - lastDataTime) / 1000);
  let agoStr = '';
  if (diffSec < 60) agoStr = `${diffSec} sec ago`;
  else if (diffSec < 3600) agoStr = `${Math.floor(diffSec / 60)} min ago`;
  else if (diffSec < 86400) agoStr = `${Math.floor(diffSec / 3600)} hour ago`;
  else agoStr = `${Math.floor(diffSec / 86400)} day ago`;

  const options = { day: 'numeric', month: 'short', year: 'numeric', hour: '2-digit', minute: '2-digit', second: '2-digit' };
  const txt = `Updated on ${lastDataTime.toLocaleString('en-GB', options)}\n(${agoStr})`;

  const tooltipText = document.getElementById('ws-tooltip-text');
  if (tooltipText) tooltipText.innerText = txt;

  const dot = document.getElementById('ws-dot');
  if (dot) {
    // Dynamically calculate the "green" threshold based on the selected interval
    const select = document.getElementById('global-auto-refresh-select');
    const valMs = parseInt(select.value, 10);
    
    let expectedMaxDelaySec = 150; // default 2m + 30s buffer
    
    if (valMs === 0) {
        // Auto mode can be up to 10 mins (low battery)
        expectedMaxDelaySec = 660; // 10m + 1m buffer
    } else if (valMs >= 120000) {
        expectedMaxDelaySec = (valMs / 1000) + 60; // Selected interval + 1m buffer
    }

    if (diffSec <= expectedMaxDelaySec) { dot.style.background = '#22c55e'; dot.style.boxShadow = '0 0 8px #22c55e'; } // Green
    else if (diffSec < 3600) { dot.style.background = '#facc15'; dot.style.boxShadow = '0 0 8px #facc15'; } // Yellow (<1 hr)
    else if (diffSec < 86400) { dot.style.background = '#ef4444'; dot.style.boxShadow = '0 0 8px #ef4444'; } // Red (<1 day)
    else { dot.style.background = '#000'; dot.style.boxShadow = '0 0 8px #000'; } // Black (>1 day)
  }
}
setInterval(updateTimeAgo, 1000);

// ── System Health ─────────────────────────────────────────
let currentSysStatus = null;
const SYS_MAP = {
  rtc: { name: 'RTC DS3231', ok: 'Time synchronized', warn: 'Lost power/NTP failed', error: 'Disconnected' },
  npk: { name: 'NPK & PH', ok: 'Reading successfully', warn: 'Values out of bounds', error: 'RS485 failure' },
  aht: { name: 'AHT10 Temp/Hum (Env2)', ok: 'Reading successfully', warn: '-', error: 'I2C Disconnected' },
  hdc: { name: 'HDC1080 Temp/Hum (Env1)', ok: 'Reading successfully', warn: '-', error: 'I2C Disconnected' },
  bmp: { name: 'BMP280 Enclosure (T/P)', ok: 'Reading successfully', warn: '-', error: 'I2C Disconnected' },
  ds18: { name: 'DS18B20 Soil Temp', ok: 'Reading successfully', warn: '-', error: 'Disconnected / -127' },
  ina: { name: 'INA226 Power', ok: 'Reading successfully', warn: '-', error: 'I2C Disconnected' },
  wifi: { name: 'Wi-Fi Connection', ok: 'Connected', warn: 'Weak Signal', error: 'Disconnected' }
};

function updateSystemHealth(sys) {
  currentSysStatus = sys;
  let global = 'ok';
  for (const k in sys) {
    if (sys[k] === 'error') global = 'error';
    else if (sys[k] === 'warn' && global !== 'error') global = 'warn';
  }
  const dot = document.getElementById('global-health-dot');
  if (dot) dot.className = `health-dot ${global}`;
  if (document.getElementById('sys-modal').classList.contains('open')) renderSysHealth();
}

function openSysModal() {
  document.getElementById('sys-modal').classList.add('open');
  renderSysHealth();
}

function closeSysModal(e) {
  if (e && e.target !== document.getElementById('sys-modal') && e.type === 'click') return;
  document.getElementById('sys-modal').classList.remove('open');
}

function renderSysHealth() {
  const c = document.getElementById('sys-health-list');
  c.innerHTML = '';
  if (!currentSysStatus) {
    c.innerHTML = '<p style="text-align:center; padding:20px;">No diagnostics data yet.</p>';
    return;
  }
  for (const k in SYS_MAP) {
    const status = currentSysStatus[k] || 'error';
    let desc = SYS_MAP[k][status] || 'Unknown status';

    // Inject dynamic RTC time
    if (k === 'rtc' && status === 'ok' && currentSysStatus.rtc_time) {
      desc = `Time synchronized (${currentSysStatus.rtc_time})`;
    }
    
    // Inject dynamic Wi-Fi stats
    if (k === 'wifi' && status !== 'error' && latestData.wifi_ssid) {
      desc = `Connected to ${latestData.wifi_ssid} (${latestData.wifi_rssi} dBm)`;
      
      // Also update the global header Wi-Fi indicator!
      const wSSID = document.getElementById('wifi-hover-ssid');
      const wIP   = document.getElementById('wifi-hover-ip');
      const wRSSI = document.getElementById('wifi-hover-rssi');
      const wIcon = document.getElementById('wifi-icon-svg');
      if (wSSID) wSSID.innerText = latestData.wifi_ssid;
      if (wIP) wIP.innerText = latestData.wifi_ip || '--';
      if (wRSSI) wRSSI.innerText = latestData.wifi_rssi;
      
      if (wIcon) {
        if (latestData.wifi_rssi > -65) wIcon.style.color = '#22c55e'; // Green
        else if (latestData.wifi_rssi > -85) wIcon.style.color = '#eab308'; // Yellow
        else wIcon.style.color = '#ef4444'; // Red
      }
    }

    const info = SYS_MAP[k];
    const div = document.createElement('div');
    div.className = 'sys-item';
    div.innerHTML = `
      <div class="sys-icon ${status}"></div>
      <div class="sys-text">
        <div class="sys-title">${info.name} <span style="font-size:0.8rem; text-transform:uppercase; color:var(--text-muted)">(${status})</span></div>
        <div class="sys-desc">${desc}</div>
      </div>
    `;
    c.appendChild(div);
  }
}
