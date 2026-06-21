================================================================================
🌿 ESP32 SPIFFS Web Files Upload Instructions
================================================================================

> ⚠️ IMPORTANT: Install ESP32Async libraries (NOT me-no-dev):
> - https://github.com/ESP32Async/ESPAsyncWebServer
> - https://github.com/ESP32Async/AsyncTCP

This folder contains the web files (index.html, app.js, style.css) that will 
be uploaded to the ESP32's SPIFFS partition.

--------------------------------------------------------------------------------
STEP 1: Install Libraries
--------------------------------------------------------------------------------

1. Open Arduino IDE
2. Go to: Tools > Manage Libraries...
3. Install these libraries:
   - ESP Async Web Server (from ESP32Async)
   - AsyncTCP (from ESP32Async)
   - DallasTemperature
   - Adafruit AHTX0
   - Adafruit BMP280
   - ClosedCube HDC1080
   - PubSubClient
   - ArduinoJson
   - INA226
   - RTClib

4. Install ESP32 Sketch Data Upload plugin:
   - Go to: https://github.com/me-no-dev/arduino-esp32fs-plugin
   - Download the JAR file
   - Place in: Arduino/tools/ESP32FS/tool/esp32fs.jar

--------------------------------------------------------------------------------
STEP 2: Copy Web Files
--------------------------------------------------------------------------------

Copy these files from the Web-Page/public folder to this "data" folder:

  - index.html  (from: Web-Page/public/index.html)
  - app.js      (from: Web-Page/public/app.js)  
  - style.css   (from: Web-Page/public/style.css)

Your data folder should look like:
  Plantations/
  ├── Plantations.ino
  └── data/
      ├── README.txt          (this file)
      ├── index.html
      ├── app.js
      └── style.css

--------------------------------------------------------------------------------
STEP 3: Configure Partition Scheme
--------------------------------------------------------------------------------

Before uploading, you MUST set the partition scheme:

1. In Arduino IDE, go to: Tools > Partition Scheme
2. Select: "Huge APP (3MB APP / 1MB SPIFFS)"

   This gives:
   - 3MB for the application code (plenty of space)
   - 1MB SPIFFS for web files (~95KB) and database

--------------------------------------------------------------------------------
STEP 4: Upload
--------------------------------------------------------------------------------

UPLOAD ORDER IS IMPORTANT:

1. FIRST: Upload the sketch
   - Connect ESP32 via USB
   - Select the correct Port
   - Click Upload (Ctrl+U)

2. SECOND: Upload the web files
   - Go to: Tools > ESP32 Sketch Data Upload
   - Wait for upload to complete

NOTE: If you modify web files, repeat Step 2 only.

--------------------------------------------------------------------------------
STEP 5: Verify
--------------------------------------------------------------------------------

After upload:
1. Open Serial Monitor (115200 baud)
2. Reset ESP32 or press EN button
3. You should see:
   "✅ SPIFFS initialized"
   "✅ Web server started on port 80"

4. Access the web interface:
   - Find ESP32's IP address (shown in Serial Monitor)
   - Open browser: http://<ESP32_IP>

--------------------------------------------------------------------------------
Troubleshooting
--------------------------------------------------------------------------------

Problem: "SPIFFS upload failed"
Solution: 
  - Make sure you selected "Huge APP (3MB APP / 1MB SPIFFS)" partition
  - Hold BOOT button while pressing EN to enter download mode
  - Or use esptool.py directly

Problem: "Web page not loading"
Solution:
  - Check serial monitor for IP address
  - Make sure index.html is in the data folder
  - Re-upload SPIFFS data

Problem: "Sketch too big"
Solution:
  - Verify partition scheme is set to "Huge APP"
  - The 3MB APP partition has plenty of space for this sketch

================================================================================
Web Interface Features
================================================================================

- Live sensor data via WebSocket
- Historical charts
- Water control
- RTC sync
- System diagnostics
- Database statistics (shows storage capacity and days remaining)
- Reboot ESP32 button
- Clear database button
- MQTT enable/disable

================================================================================
Database Capacity (FIFO)
================================================================================

With "Huge APP (3MB APP / 1MB SPIFFS)" partition:

- Maximum records: 10,000
- Record size: ~76 bytes each
- Storage used: ~760KB max

At 2-minute cycles: ~14 days of local storage
At 5-minute cycles: ~35 days of local storage
At 10-minute cycles: ~70 days of local storage

When full, oldest records are automatically deleted (FIFO).

================================================================================
