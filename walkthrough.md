# Dynamic ESP32 Polling & Date Persistence - Walkthrough

Both features have been fully implemented across the ESP32 firmware, Node.js backend, and the frontend Dashboard!

## What was Changed

### 1. Persistent Time Picker (Frontend)
- The web dashboard now uses `sessionStorage` in `app.js` to remember your selected time frame (e.g., "Last 24 hours" or "Yesterday").
- When you navigate between pages (like from the Dashboard to Analytics, or reloading the page), your browser instantly recalls the time range and applies it to your charts automatically.

### 2. ESP32 Dynamic Sleep (Hardware)
- **Persistent Settings:** Your ESP32 now uses the `Preferences` library (Non-Volatile Memory) to permanently memorize the polling interval. It will survive power outages and reboots.
- **Smart Battery Mode:** When you select `Auto` in the web dropdown, the website fires an MQTT command (`{"cmd": "set_interval", "val": "auto"}`) down to the ESP32.
  - While running in `Auto`, if the battery is healthy (> 3.7V / 50%), the ESP32 checks sensors and publishes data **every 2 minutes**.
  - If the battery falls below 3.7V, it automatically enters Power Saving Mode and stretches the sleep cycle out to **10 minutes** to conserve power.
- **Fixed Intervals:** If you select `5m` or `15m` in the dashboard, the ESP32 adopts that exactly.
- **Real-Time Watering Priority:** The hardware is clamped to a maximum sensor-update rate of 2 minutes to prevent battery drain. However, the ESP32's MQTT listener runs non-stop in the background. If you hit the **Water Plant** button, the valve will open instantly, regardless of the polling sleep state!

## How to Test
1. Make sure to do a **Hard Refresh** (Ctrl+F5) in your web browser to load the latest `app.js`.
2. Upload the new `Plantations.ino` code to your ESP32.
3. Use the Auto-Refresh dropdown. Select "5m" - watch the Arduino Serial Monitor log `⏱️ Interval set to 300000 ms`.
4. Change a date in the time picker, then refresh the page. The date will still be selected!
