// =====================================================
// INDUSTRIAL MONITOR — FINAL
//
// ALERT LOGIC:
// - Any parameter above threshold → alarm latches
// - All three combined alert scenarios on LCD + web
//
// BUTTON:
// - Short press → works ONLY if ALL params are below
//                threshold; clears alarm fully +
//                buzzer + LCD + dashboard = NORMAL
// - Long press 2s → force resets alarm even if any
//                   fault is still active
// =====================================================

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <UIPEthernet.h>

// =====================================================
// LCD
// =====================================================

LiquidCrystal_I2C lcd(0x3F, 16, 4);

// =====================================================
// ETHERNET
// =====================================================

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 137, 50);
IPAddress gateway(192, 168, 137, 1);
IPAddress subnet(255, 255, 255, 0);
EthernetServer server(80);

// =====================================================
// SENSORS
// =====================================================

Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

#define ONE_WIRE_BUS 2
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// =====================================================
// ACS712 CURRENT SENSOR
// =====================================================

#define ACS_PIN      0
float current        = 0;
float offsetVoltage  = 0;
float sensitivity    = 0.185;   // 5A version

int   rawADC         = 0;
float acsVoltage     = 0;

// =====================================================
// BUZZER & BUTTON
// =====================================================

#define BUZZER_PIN 19
#define BUTTON_PIN 18

// =====================================================
// THRESHOLDS
// =====================================================

#define TEMP_THRESHOLD    40
#define VIB_CRITICAL      1.2  // raised: well above noise floor
#define CURRENT_THRESHOLD 0.25

// =====================================================
// TEMPERATURE
// =====================================================

float         tempC        = 0;
unsigned long lastTempTime = 0;

// =====================================================
// VIBRATION — DYNAMIC FILTER + BASELINE CALIBRATION
// =====================================================

float smoothedVib           = 0;
float vibBuffer[3]          = {0, 0, 0};
float vibBaseline           = 0;

#define VIB_NOISE_FLOOR  0.5    // raised: filters ADXL345 electrical noise
#define VIB_DELAY        3000   // raised: noise spikes rarely sustain 3s

unsigned long vibViolationStart = 0;

// =====================================================
// ALARM STATE
// =====================================================

bool alarmLatched = false;

// =====================================================
// ACK DISPLAY
// =====================================================

unsigned long ackDisplayTime = 0;
bool          showAckMessage = false;
String        ackMessage     = "";

// =====================================================
// LCD
// =====================================================

unsigned long lastLcdUpdate = 0;
String        lastLine3     = "";

// =====================================================
// ACS712 CALIBRATION — 300 samples
// =====================================================

float calibrateACS712() {

  long sum = 0;

  for (int i = 0; i < 300; i++) {
    sum += analogRead(ACS_PIN);
    delay(2);
  }

  return ((sum / 300.0) / 4095.0) * 3.3;
}

// =====================================================
// CURRENT READING
// 20-sample multisample, 0.03A deadband, EMA filter
// =====================================================

float readCurrent() {

  static float avg = 0;

  long sum = 0;

  for (int i = 0; i < 20; i++) {
    sum += analogRead(ACS_PIN);
    delayMicroseconds(200);
  }

  rawADC     = sum / 20;
  acsVoltage = (rawADC / 4095.0) * 3.3;

  float inst = (offsetVoltage - acsVoltage) / sensitivity;

  if (abs(inst) < 0.03)
    inst = 0;

  avg = (0.6 * avg) + (0.4 * inst);

  if (avg < 0)
    avg = 0;

  return avg;
}

// =====================================================
// VIBRATION BASELINE CALIBRATION
// 50 samples over ~2 seconds of idle
// =====================================================

float calibrateVibBaseline() {

  float total = 0;
  int   count = 200;  // increased: ~8s at startup, more accurate baseline

  for (int i = 0; i < count; i++) {

    sensors_event_t e;
    accel.getEvent(&e);

    float mag = sqrt(
      e.acceleration.x * e.acceleration.x +
      e.acceleration.y * e.acceleration.y +
      e.acceleration.z * e.acceleration.z
    );

    total += abs(mag - 9.81);
    delay(40);
  }

  return total / count;
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  delay(2000);

  Wire.begin(5, 4);

  lcd.begin();
  lcd.backlight();
  lcd.print("Init Hardware...");

  analogReadResolution(12);

  accel.begin();
  accel.setRange(ADXL345_RANGE_2_G);

  sensors.begin();
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();

  // ===== CURRENT CALIBRATION =====
  lcd.clear();
  lcd.print("Calib. Current..");
  offsetVoltage = calibrateACS712();

  // ===== VIBRATION BASELINE =====
  lcd.clear();
  lcd.print("Calib. Vibration");
  vibBaseline = calibrateVibBaseline();

  Serial.print("Vib Baseline : ");
  Serial.println(vibBaseline, 4);

  Serial.print("ACS Offset V : ");
  Serial.println(offsetVoltage, 3);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  SPI.begin(6, 7, 8, 10);
  Ethernet.init(10);
  Ethernet.begin(mac, ip, gateway, gateway, subnet);

  delay(1500);

  lcd.clear();
  lcd.print("System Ready");

  server.begin();

  Serial.println("================================");
  Serial.println("INDUSTRIAL MONITOR STARTED");
  Serial.println("================================");
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  // ===== LCD RATE LIMITER =====
  if (millis() - lastLcdUpdate < 250)
    return;

  lastLcdUpdate = millis();

  // =====================================================
  // TEMPERATURE
  // =====================================================

  if (millis() - lastTempTime >= 500) {
    tempC = sensors.getTempCByIndex(0);
    sensors.requestTemperatures();
    lastTempTime = millis();
  }

  // =====================================================
  // CURRENT
  // =====================================================

  current = readCurrent();

  // =====================================================
  // VIBRATION — DYNAMIC FILTER WITH BASELINE OFFSET
  // =====================================================

  sensors_event_t event;
  accel.getEvent(&event);

  float rawMag = sqrt(
    event.acceleration.x * event.acceleration.x +
    event.acceleration.y * event.acceleration.y +
    event.acceleration.z * event.acceleration.z
  );

  float rawVib = abs(rawMag - 9.81) - vibBaseline;

  if (rawVib < VIB_NOISE_FLOOR)
    rawVib = 0;

  // 3-point moving average pre-filter
  vibBuffer[2] = vibBuffer[1];
  vibBuffer[1] = vibBuffer[0];
  vibBuffer[0] = rawVib;

  float midVib = (vibBuffer[0] + vibBuffer[1] + vibBuffer[2]) / 3.0;

  // Dynamic alpha
  float diff        = abs(midVib - smoothedVib);
  float filterAlpha = (diff > 1.5) ? 0.2 : 0.05;  // reduced: less reactive to spikes

  smoothedVib = ((1.0 - filterAlpha) * smoothedVib) + (filterAlpha * midVib);

  // Vibration sustain timer
  if (smoothedVib <= VIB_CRITICAL) {
    vibViolationStart = 0;
  } else {
    if (vibViolationStart == 0)
      vibViolationStart = millis();
  }

  // =====================================================
  // SERIAL MONITOR
  // =====================================================

  Serial.print("ADC:");    Serial.print(rawADC);
  Serial.print(" ACSv:");  Serial.print(acsVoltage, 3);
  Serial.print("V Curr:"); Serial.print(current, 3);
  Serial.print("A Vib:");  Serial.print(smoothedVib, 3);
  Serial.print(" Temp:");  Serial.print(tempC, 1);
  Serial.println("C");

  // =====================================================
  // FAULT FLAGS — evaluated fresh every loop
  // =====================================================

  bool faultTemp = (tempC    > TEMP_THRESHOLD);

  bool faultVib  = (smoothedVib > VIB_CRITICAL &&
                    vibViolationStart > 0 &&
                    (millis() - vibViolationStart > VIB_DELAY));

  bool faultCur  = (current  > CURRENT_THRESHOLD);

  // ALL parameters currently safe?
  bool allNormal = (!faultTemp && !faultVib && !faultCur);

  // =====================================================
  // LATCH ALARM
  // Any fault → latch. Stays latched until cleared
  // by button press.
  // =====================================================

  if (faultTemp || faultVib || faultCur) {
    alarmLatched = true;
  }

  // =====================================================
  // BUTTON
  // =====================================================

  static bool          buttonHeld     = false;
  static unsigned long pressStartTime = 0;

  bool buttonState = digitalRead(BUTTON_PIN);

  // --- detect press start ---
  if (buttonState == LOW && !buttonHeld) {
    buttonHeld     = true;
    pressStartTime = millis();
  }

  // --- long press: force reset regardless of faults ---
  if (buttonState == LOW && buttonHeld) {
    if (millis() - pressStartTime >= 2000) {

      alarmLatched   = false;
      showAckMessage = true;
      ackMessage     = "ALARM RESET";
      ackDisplayTime = millis();
      buttonHeld     = false;
    }
  }

  // --- short press release ---
  if (buttonState == HIGH && buttonHeld) {

    if (millis() - pressStartTime < 2000) {

      // SHORT PRESS — only acts if ALL parameters are normal
      if (allNormal) {
        alarmLatched   = false;
        showAckMessage = true;
        ackMessage     = "ALARM RESET";
        ackDisplayTime = millis();
      }
      // if any fault still active — do nothing at all
    }

    buttonHeld = false;
  }

  // =====================================================
  // BUZZER — beeps while alarm is latched
  // =====================================================

  static unsigned long lastBeep = 0;
  static bool          bState   = false;

  if (alarmLatched) {
    if (millis() - lastBeep > 300) {
      bState = !bState;
      digitalWrite(BUZZER_PIN, bState);
      lastBeep = millis();
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }

  // =====================================================
  // LCD ROWS 0–2
  // =====================================================

  lcd.setCursor(0, 0);
  lcd.printf("Vib:%5.2f m/s2 ", smoothedVib);

  lcd.setCursor(0, 1);
  lcd.printf("Curr:%5.2f A   ", current);

  lcd.setCursor(0, 2);
  lcd.printf("Temp:%5.2f ", tempC);
  lcd.write(0xDF);
  lcd.print("C  ");

  // =====================================================
  // LCD ROW 3 — STATUS
  // =====================================================

  String line3 = "";

  if (showAckMessage) {

    line3 = ackMessage;

    if (millis() - ackDisplayTime > 2000)
      showAckMessage = false;
  }

  else if (alarmLatched) {

    if      (faultTemp && faultVib && faultCur) line3 = "ALL ALERTS!";
    else if (faultTemp && faultVib)             line3 = "TEMP+VIB ALT";
    else if (faultTemp && faultCur)             line3 = "TEMP+CUR ALT";
    else if (faultVib  && faultCur)             line3 = "VIB+CUR ALT";
    else if (faultTemp)                         line3 = "HIGH TEMP!";
    else if (faultVib)                          line3 = "HIGH VIB!";
    else if (faultCur)                          line3 = "HIGH CURRENT!";
    else                                        line3 = "ALERT ACTIVE";
  }

  else {
    line3 = "SYSTM NORML";
  }

  if (line3 != lastLine3) {
    lcd.setCursor(0, 3);
    lcd.print("                ");
    lcd.setCursor(0, 3);
    lcd.print(line3);
    lastLine3 = line3;
  }

  // =====================================================
  // ETHERNET SERVER
  // =====================================================

  EthernetClient client = server.available();

  if (client) {

    String req = "";

    while (client.connected()) {
      if (client.available()) {

        char c = client.read();
        req += c;

        if (c == '\n' && req.endsWith("\r\n\r\n")) {

          // =====================================================
          // JSON — /data
          // =====================================================

          if (req.indexOf("GET /data") >= 0) {

            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: application/json");
            client.println();

            client.print("{");
            client.print("\"temp\":");       client.print(tempC);
            client.print(",\"vib\":");       client.print(smoothedVib);
            client.print(",\"current\":");   client.print(current);
            client.print(",\"alarm\":");     client.print(alarmLatched ? 1 : 0);
            client.print(",\"tempAlarm\":"); client.print(faultTemp    ? 1 : 0);
            client.print(",\"vibAlarm\":");  client.print(faultVib     ? 1 : 0);
            client.print(",\"curAlarm\":");  client.print(faultCur     ? 1 : 0);
            client.println("}");
          }

          // =====================================================
          // HTML DASHBOARD
          // =====================================================

          else {

client.println(R"rawliteral(
HTTP/1.1 200 OK
Content-Type: text/html

<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Industrial Monitor</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Exo+2:wght@400;700;900&display=swap');

  :root {
    --bg:       #060d1a;
    --surface:  #0d1b2e;
    --border:   #1a3050;
    --text:     #c8d8e8;
    --muted:    #4a6080;
    --vib:      #a855f7;
    --temp:     #f97316;
    --cur:      #22c55e;
    --alert:    #ef4444;
    --alert-dk: #7f1d1d;
    --ok:       #16a34a;
  }

  * { box-sizing: border-box; margin: 0; padding: 0; }

  body {
    background: var(--bg);
    font-family: 'Exo 2', sans-serif;
    color: var(--text);
    min-height: 100vh;
  }

  header {
    display: flex;
    align-items: center;
    gap: 14px;
    padding: 16px 20px 12px;
    border-bottom: 1px solid var(--border);
  }

  .pulse-dot {
    width: 10px; height: 10px;
    border-radius: 50%;
    background: var(--ok);
    box-shadow: 0 0 0 0 rgba(22,163,74,0.7);
    animation: pulse 1.8s infinite;
  }

  @keyframes pulse {
    0%   { box-shadow: 0 0 0 0   rgba(22,163,74,0.7); }
    70%  { box-shadow: 0 0 0 8px rgba(22,163,74,0);   }
    100% { box-shadow: 0 0 0 0   rgba(22,163,74,0);   }
  }

  header h1 {
    font-size: 1.25rem;
    font-weight: 900;
    letter-spacing: 0.08em;
    text-transform: uppercase;
    color: #e2f0ff;
  }

  #hdr-status {
    margin-left: auto;
    font-family: 'Share Tech Mono', monospace;
    font-size: 0.8rem;
    color: var(--muted);
  }

  #status-bar {
    margin: 14px 16px 0;
    padding: 14px 20px;
    border-radius: 10px;
    text-align: center;
    font-weight: 900;
    font-size: 1.05rem;
    letter-spacing: 0.12em;
    text-transform: uppercase;
    background: var(--surface);
    border: 1px solid var(--border);
    transition: background 0.35s, border-color 0.35s, color 0.35s;
  }

  #status-bar.alarm {
    background: var(--alert-dk);
    border-color: var(--alert);
    color: #ff9999;
    animation: flashBorder 0.6s infinite alternate;
  }

  @keyframes flashBorder {
    from { border-color: var(--alert);  }
    to   { border-color: transparent;   }
  }

  #status-bar.ok {
    background: #052e16;
    border-color: var(--ok);
    color: #86efac;
  }

  .grid {
    display: flex;
    gap: 12px;
    padding: 14px 16px 16px;
  }

  .card {
    flex: 1;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 14px;
    display: flex;
    flex-direction: column;
    gap: 10px;
  }

  .card-header {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
  }

  .card-title {
    font-size: 0.75rem;
    font-weight: 700;
    letter-spacing: 0.1em;
    text-transform: uppercase;
    color: var(--muted);
  }

  .card-live {
    font-family: 'Share Tech Mono', monospace;
    font-size: 1.4rem;
    font-weight: 700;
    transition: color 0.3s;
  }

  .card-unit {
    font-size: 0.75rem;
    color: var(--muted);
    margin-left: 4px;
  }

  canvas {
    height: 200px !important;
    width: 100% !important;
  }

  .badge {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 8px 12px;
    border-radius: 8px;
    font-family: 'Share Tech Mono', monospace;
    font-size: 0.85rem;
    font-weight: 700;
    transition: background 0.3s, color 0.3s;
  }

  .badge-vib  { background: #2e1065; color: var(--vib);  }
  .badge-temp { background: #431407; color: var(--temp); }
  .badge-cur  { background: #052e16; color: var(--cur);  }

  .badge.alert { background: var(--alert-dk) !important; color: #fca5a5 !important; }

  .threshold-label {
    font-size: 0.7rem;
    color: var(--muted);
  }

  @media (max-width: 640px) {
    .grid { flex-direction: column; }
  }
</style>
</head>
<body>

<header>
  <div class="pulse-dot" id="dot"></div>
  <h1>Industrial Equipment Monitor</h1>
  <span id="hdr-status">--:--:--</span>
</header>

<div id="status-bar">Connecting...</div>

<div class="grid">

  <div class="card">
    <div class="card-header">
      <span class="card-title">Vibration</span>
      <span>
        <span class="card-live" id="vib-val" style="color:var(--vib)">--</span>
        <span class="card-unit">m/s²</span>
      </span>
    </div>
    <canvas id="vib-chart"></canvas>
    <div class="badge badge-vib" id="badge-vib">
      <span>Acceleration Magnitude</span>
      <span class="threshold-label">Threshold: 1.2 m/s²</span>
    </div>
  </div>

  <div class="card">
    <div class="card-header">
      <span class="card-title">Temperature</span>
      <span>
        <span class="card-live" id="temp-val" style="color:var(--temp)">--</span>
        <span class="card-unit">°C</span>
      </span>
    </div>
    <canvas id="temp-chart"></canvas>
    <div class="badge badge-temp" id="badge-temp">
      <span>DS18B20 Sensor</span>
      <span class="threshold-label">Threshold: 33 °C</span>
    </div>
  </div>

  <div class="card">
    <div class="card-header">
      <span class="card-title">Current</span>
      <span>
        <span class="card-live" id="cur-val" style="color:var(--cur)">--</span>
        <span class="card-unit">A</span>
      </span>
    </div>
    <canvas id="cur-chart"></canvas>
    <div class="badge badge-cur" id="badge-cur">
      <span>ACS712 5A Sensor</span>
      <span class="threshold-label">Threshold: 0.25 A</span>
    </div>
  </div>

</div>

<script>
const CHART_OPTS = (color) => ({
  type: 'line',
  data: {
    labels: [],
    datasets: [{
      data: [],
      borderColor: color,
      borderWidth: 2,
      pointRadius: 0,
      tension: 0.35,
      fill: true,
      backgroundColor: color + '18'
    }]
  },
  options: {
    animation: false,
    plugins: { legend: { display: false } },
    scales: {
      x: {
        ticks: { color: '#4a6080', maxTicksLimit: 6,
                 font: { family: 'Share Tech Mono', size: 10 } },
        grid:  { color: '#0d1b2e' }
      },
      y: {
        ticks: { color: '#4a6080',
                 font: { family: 'Share Tech Mono', size: 10 } },
        grid:  { color: '#1a3050' }
      }
    }
  }
});

const vibChart  = new Chart(document.getElementById('vib-chart'),  CHART_OPTS('#a855f7'));
const tempChart = new Chart(document.getElementById('temp-chart'), CHART_OPTS('#f97316'));
const curChart  = new Chart(document.getElementById('cur-chart'),  CHART_OPTS('#22c55e'));

function push(chart, label, value) {
  chart.data.labels.push(label);
  chart.data.datasets[0].data.push(value);
  if (chart.data.labels.length > 30) {
    chart.data.labels.shift();
    chart.data.datasets[0].data.shift();
  }
  chart.update();
}

function setAlert(chartObj, color, isAlert) {
  chartObj.data.datasets[0].borderColor     = isAlert ? '#ef4444' : color;
  chartObj.data.datasets[0].backgroundColor = isAlert ? '#ef444418' : color + '18';
  chartObj.update('none');
}

setInterval(async () => {
  try {
    const d   = await (await fetch('/data')).json();
    const now = new Date().toLocaleTimeString();

    document.getElementById('hdr-status').innerText = now;

    push(vibChart,  now, d.vib);
    push(tempChart, now, d.temp);
    push(curChart,  now, d.current);

    document.getElementById('vib-val').innerText  = d.vib.toFixed(2);
    document.getElementById('temp-val').innerText = d.temp.toFixed(2);
    document.getElementById('cur-val').innerText  = d.current.toFixed(3);

    // Per-parameter live value colour
    document.getElementById('vib-val').style.color  = d.vibAlarm  ? '#ef4444' : 'var(--vib)';
    document.getElementById('temp-val').style.color = d.tempAlarm ? '#ef4444' : 'var(--temp)';
    document.getElementById('cur-val').style.color  = d.curAlarm  ? '#ef4444' : 'var(--cur)';

    // Chart line colour
    setAlert(vibChart,  '#a855f7', d.vibAlarm);
    setAlert(tempChart, '#f97316', d.tempAlarm);
    setAlert(curChart,  '#22c55e', d.curAlarm);

    // Badge colour
    document.getElementById('badge-vib').classList.toggle('alert',  !!d.vibAlarm);
    document.getElementById('badge-temp').classList.toggle('alert', !!d.tempAlarm);
    document.getElementById('badge-cur').classList.toggle('alert',  !!d.curAlarm);

    // Status bar
    const bar = document.getElementById('status-bar');
    const dot = document.getElementById('dot');

    if (d.alarm == 1) {

      bar.classList.add('alarm');
      bar.classList.remove('ok');
      dot.style.background = '#ef4444';

      const parts = [];
      if (d.tempAlarm) parts.push('HIGH TEMPERATURE');
      if (d.vibAlarm)  parts.push('HIGH VIBRATION');
      if (d.curAlarm)  parts.push('HIGH CURRENT');
      bar.innerText = '⚠  ' + parts.join('  +  ') + '  ALERT';

    } else {

      bar.classList.remove('alarm');
      bar.classList.add('ok');
      bar.innerText        = '✓  SYSTEM NORMAL';
      dot.style.background = '#22c55e';
    }

  } catch(e) {
    document.getElementById('status-bar').innerText   = 'Connection lost...';
    document.getElementById('status-bar').className   = 'alarm';
  }
}, 700);
</script>
</body>
</html>
)rawliteral");

          }

          break;
        }
      }
    }

    client.stop();
  }
}
