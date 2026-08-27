#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ==================== PIN DEFINITIONS ====================
#define ONE_WIRE_BUS      22  // GPIO 22: DS18B20 1-Wire Data Bus
#define MOSFET_GATE_PIN   23  // GPIO 23: MOSFET Gate Control Line
#define HOT_PUMP_PWM_PIN  21  // GPIO 21: Hot Exhaust Loop Pump PWM
#define COLD_PUMP_PWM_PIN 18  // GPIO 18: Cold Bed Loop Pump PWM

// ==================== WI-FI AP CREDENTIALS ====================
const char* apSSID = "WaterBed-Controller";
const char* apPASS = "coolingsystem"; // Minimum 8 characters

// ==================== SYSTEM & THERMAL SETTINGS ====================
float targetBedTemp    = 20.0;  // Target Cold Loop Temperature (°C)
float hysteresis       = 0.5;   // +/- 0.5°C Hysteresis Window
float maxHotLoopTemp   = 55.0;  // Emergency Overheat Threshold (°C)

int hotPumpSpeedPercent  = 85;  // Default Radiator Pump Duty Cycle (%)
int coldPumpSpeedPercent = 60;  // Default Bed Pump Duty Cycle (%)

// ==================== GLOBAL OBJECTS & STATE ====================
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
WebServer server(80);

const int pwmFreq = 25000; // 25 kHz PC Fan Standard
const int pwmRes  = 8;     // 8-bit (0-255)

float currentColdTemp = 25.0;
float currentHotTemp  = 25.0;
bool isCoolingActive   = false;
bool emergencyShutdown = false;
int deviceCount = 0;

const unsigned long SENSOR_INTERVAL = 1500;
unsigned long lastSensorReadTime    = 0;

// ==================== EMBEDDED DASHBOARD HTML ====================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Smart Bed Thermal Hub</title>
  <style>
    :root { --bg: #0f172a; --card: #1e293b; --accent: #38bdf8; --text: #f8fafc; --warn: #ef4444; --green: #22c55e; }
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 16px; display: flex; justify-content: center; }
    .container { width: 100%; max-width: 480px; }
    h1 { font-size: 1.4rem; text-align: center; margin-bottom: 20px; color: var(--accent); }
    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-bottom: 12px; }
    .card { background: var(--card); border-radius: 12px; padding: 16px; text-align: center; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.3); }
    .card.full { grid-column: span 2; }
    .label { font-size: 0.8rem; text-transform: uppercase; color: #94a3b8; margin-bottom: 6px; }
    .val { font-size: 2rem; font-weight: bold; color: var(--text); }
    .unit { font-size: 1rem; color: #94a3b8; }
    .badge { display: inline-block; padding: 4px 12px; border-radius: 9999px; font-weight: 600; font-size: 0.85rem; }
    .badge.on { background: rgba(34, 197, 94, 0.2); color: var(--green); border: 1px solid var(--green); }
    .badge.off { background: rgba(148, 163, 184, 0.2); color: #94a3b8; }
    .badge.trip { background: rgba(239, 68, 68, 0.2); color: var(--warn); border: 1px solid var(--warn); }
    .controls { display: flex; align-items: center; justify-content: space-between; gap: 12px; margin-top: 10px; }
    .btn { background: var(--accent); color: #0f172a; border: none; border-radius: 8px; font-size: 1.5rem; font-weight: bold; width: 48px; height: 48px; cursor: pointer; }
    .slider-row { margin-top: 10px; text-align: left; }
    .slider-row label { display: flex; justify-content: space-between; font-size: 0.85rem; color: #94a3b8; }
    input[type=range] { width: 100%; accent-color: var(--accent); margin-top: 6px; }
  </style>
</head>
<body>
  <div class="container">
    <h1>Smart Bed Thermal Hub</h1>
    <div class="grid">
      <div class="card">
        <div class="label">Cold Loop (Bed)</div>
        <div class="val" id="coldTemp">--<span class="unit">°C</span></div>
      </div>
      <div class="card">
        <div class="label">Hot Loop (Radiator)</div>
        <div class="val" id="hotTemp">--<span class="unit">°C</span></div>
      </div>
      <div class="card full">
        <div class="label">Peltier State</div>
        <div id="peltierBadge" class="badge off">STANDBY</div>
      </div>
      <div class="card full">
        <div class="label">Target Bed Temperature</div>
        <div class="controls">
          <button class="btn" onclick="adjustTarget(-0.5)">-</button>
          <div class="val"><span id="targetTemp">20.0</span><span class="unit">°C</span></div>
          <button class="btn" onclick="adjustTarget(0.5)">+</button>
        </div>
      </div>
      <div class="card full">
        <div class="label">Pump Flow Controls</div>
        <div class="slider-row">
          <label>Cold Pump Speed: <span id="coldPumpVal">60%</span></label>
          <input type="range" min="30" max="100" value="60" onchange="updatePumps()" id="coldSlider">
        </div>
        <div class="slider-row" style="margin-top:14px;">
          <label>Hot Pump Speed: <span id="hotPumpVal">85%</span></label>
          <input type="range" min="40" max="100" value="85" onchange="updatePumps()" id="hotSlider">
        </div>
      </div>
    </div>
  </div>
  <script>
    let currentTarget = 20.0;
    function fetchData() {
      fetch('/api/data')
        .then(res => res.json())
        .then(d => {
          document.getElementById('coldTemp').innerHTML = d.cold.toFixed(1) + '<span class="unit">°C</span>';
          document.getElementById('hotTemp').innerHTML = d.hot.toFixed(1) + '<span class="unit">°C</span>';
          document.getElementById('targetTemp').innerText = d.target.toFixed(1);
          currentTarget = d.target;
          const badge = document.getElementById('peltierBadge');
          if (d.emergency) {
            badge.className = 'badge trip';
            badge.innerText = 'OVERHEAT SAFETY TRIP';
          } else if (d.cooling) {
            badge.className = 'badge on';
            badge.innerText = 'COOLING ACTIVE (ON)';
          } else {
            badge.className = 'badge off';
            badge.innerText = 'STANDBY (OFF)';
          }
        });
    }
    function adjustTarget(delta) {
      currentTarget = Math.round((currentTarget + delta) * 10) / 10;
      fetch('/api/setTarget?val=' + currentTarget).then(() => fetchData());
    }
    function updatePumps() {
      const cold = document.getElementById('coldSlider').value;
      const hot = document.getElementById('hotSlider').value;
      document.getElementById('coldPumpVal').innerText = cold + '%';
      document.getElementById('hotPumpVal').innerText = hot + '%';
      fetch(`/api/setPumps?cold=${cold}&hot=${hot}`);
    }
    setInterval(fetchData, 1500);
    fetchData();
  </script>
</body>
</html>
)rawliteral";

// ==================== HELPER FUNCTIONS ====================
void applyPumpSpeeds() {
  uint8_t coldDuty = (uint8_t)((coldPumpSpeedPercent / 100.0) * 255.0);
  uint8_t hotDuty  = (uint8_t)((hotPumpSpeedPercent / 100.0) * 255.0);
  ledcWrite(COLD_PUMP_PWM_PIN, coldDuty);
  ledcWrite(HOT_PUMP_PWM_PIN, hotDuty);
}

void handleRoot() { server.send(200, "text/html", INDEX_HTML); }

void handleData() {
  String json = "{";
  json += "\"cold\":" + String(currentColdTemp, 2) + ",";
  json += "\"hot\":" + String(currentHotTemp, 2) + ",";
  json += "\"target\":" + String(targetBedTemp, 1) + ",";
  json += "\"cooling\":" + String(isCoolingActive ? "true" : "false") + ",";
  json += "\"emergency\":" + String(emergencyShutdown ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetTarget() {
  if (server.hasArg("val")) { targetBedTemp = server.arg("val").toFloat(); }
  server.send(200, "text/plain", "OK");
}

void handleSetPumps() {
  if (server.hasArg("cold")) coldPumpSpeedPercent = server.arg("cold").toInt();
  if (server.hasArg("hot"))  hotPumpSpeedPercent  = server.arg("hot").toInt();
  applyPumpSpeeds();
  server.send(200, "text/plain", "OK");
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(MOSFET_GATE_PIN, OUTPUT);
  digitalWrite(MOSFET_GATE_PIN, LOW);

  ledcAttach(HOT_PUMP_PWM_PIN, pwmFreq, pwmRes);
  ledcAttach(COLD_PUMP_PWM_PIN, pwmFreq, pwmRes);
  applyPumpSpeeds();

  sensors.begin();
  deviceCount = sensors.getDeviceCount();

  WiFi.softAP(apSSID, apPASS);
  Serial.println("\n[INFO] Access Point Started: http://192.168.4.1");

  server.on("/", handleRoot);
  server.on("/api/data", handleData);
  server.on("/api/setTarget", handleSetTarget);
  server.on("/api/setPumps", handleSetPumps);
  server.begin();
}

// ==================== MAIN LOOP ====================
void loop() {
  server.handleClient();

  unsigned long currentMillis = millis();
  if (currentMillis - lastSensorReadTime >= SENSOR_INTERVAL) {
    lastSensorReadTime = currentMillis;

    sensors.requestTemperatures();
    currentColdTemp = sensors.getTempCByIndex(0);
    currentHotTemp  = (deviceCount >= 2) ? sensors.getTempCByIndex(1) : 25.0;

    // 1. Sensor Disconnect Watchdog
    if (currentColdTemp == DEVICE_DISCONNECTED_C || currentColdTemp <= -100.0) {
      isCoolingActive = false;
      digitalWrite(MOSFET_GATE_PIN, LOW);
      return;
    }

    // 2. Overheat Emergency Interlock
    if (currentHotTemp >= maxHotLoopTemp) {
      emergencyShutdown = true;
      isCoolingActive = false;
      digitalWrite(MOSFET_GATE_PIN, LOW);
    } else if (emergencyShutdown && currentHotTemp < (maxHotLoopTemp - 5.0)) {
      emergencyShutdown = false;
    }

    // 3. Hysteresis Switching Engine
    if (!emergencyShutdown) {
      if (!isCoolingActive && currentColdTemp >= (targetBedTemp + hysteresis)) {
        isCoolingActive = true;
        digitalWrite(MOSFET_GATE_PIN, HIGH);
      } else if (isCoolingActive && currentColdTemp <= (targetBedTemp - hysteresis)) {
        isCoolingActive = false;
        digitalWrite(MOSFET_GATE_PIN, LOW);
      }
    }
  }
}
