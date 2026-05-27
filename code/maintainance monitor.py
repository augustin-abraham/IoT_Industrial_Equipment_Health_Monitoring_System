from flask import Flask, jsonify, request
import requests
import time
import threading

app = Flask(__name__)

# ===== CONFIG =====
ESP_IP = "http://192.168.137.50/data"

TEMP_THRESHOLD    = 40
VIB_THRESHOLD     = 1.2
CURRENT_THRESHOLD = 0.25
FAULT_COOLDOWN    = 10   # seconds between fault counts (works even if fault stays active)

# ===== STATE =====
fault_count          = 0
maintenance_required = False
latest_data          = {"temp": 0, "vib": 0, "current": 0}
last_fault_time      = 0
fault_history        = []


# ===== FAULT DETECTION =====
def check_faults(data):
    global fault_count, maintenance_required, fault_history, last_fault_time

    temp    = data["temp"]
    vib     = data["vib"]
    current = data["current"]

    temp_fault    = temp    > TEMP_THRESHOLD
    vib_fault     = vib     > VIB_THRESHOLD
    current_fault = current > CURRENT_THRESHOLD

    fault = temp_fault or vib_fault or current_fault

    # Build fault source list
    fault_source = []
    if temp_fault:    fault_source.append("TEMP")
    if vib_fault:     fault_source.append("VIB")
    if current_fault: fault_source.append("CURRENT")

    now = time.time()

    # Count fault if:
    #   - any sensor is over threshold
    #   - maintenance not yet triggered
    #   - cooldown has elapsed since last count
    if fault and not maintenance_required and (now - last_fault_time >= FAULT_COOLDOWN):
        fault_count    += 1
        last_fault_time = now

        fault_history.append({
            "fault_no": fault_count,
            "cause":    fault_source,
            "temp":     round(temp,    2),
            "vib":      round(vib,     2),
            "current":  round(current, 3),
            "time":     time.strftime("%H:%M:%S")
        })

    if fault_count >= 3:
        maintenance_required = True


# ===== BACKGROUND POLLING LOOP =====
def fetch_loop():
    global latest_data
    while True:
        try:
            r    = requests.get(ESP_IP, timeout=2)
            data = r.json()
            latest_data = data
            check_faults(data)
        except Exception as e:
            print(f"Error fetching data: {e}")
        time.sleep(1)


# ===== API ROUTES =====

@app.route("/status")
def status():
    return jsonify({
        "data":          latest_data,
        "fault_count":   fault_count,
        "maintenance":   maintenance_required,
        "fault_history": fault_history
    })


@app.route("/reset", methods=["POST"])
def reset():
    global fault_count, maintenance_required, fault_history, last_fault_time
    fault_count          = 0
    maintenance_required = False
    last_fault_time      = 0
    fault_history        = []
    return jsonify({"message": "Reset Done"})


# ===== DASHBOARD =====

@app.route("/")
def home():
    return """
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ML Dashboard</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Exo+2:wght@400;700;900&display=swap');

  :root {
    --bg:       #060d1a;
    --surface:  #0d1b2e;
    --border:   #1a3050;
    --text:     #c8d8e8;
    --muted:    #4a6080;
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
    padding-bottom: 40px;
  }

  header {
    display: flex;
    align-items: center;
    gap: 14px;
    padding: 18px 24px 14px;
    border-bottom: 1px solid var(--border);
  }

  .pulse-dot {
    width: 11px; height: 11px;
    border-radius: 50%;
    background: var(--ok);
    box-shadow: 0 0 0 0 rgba(22,163,74,.7);
    animation: pulse 1.8s infinite;
  }
  @keyframes pulse {
    0%   { box-shadow: 0 0 0 0   rgba(22,163,74,.7); }
    70%  { box-shadow: 0 0 0 9px rgba(22,163,74,0);  }
    100% { box-shadow: 0 0 0 0   rgba(22,163,74,0);  }
  }

  header h1 {
    font-size: 1.2rem;
    font-weight: 900;
    letter-spacing: .1em;
    text-transform: uppercase;
    color: #e2f0ff;
  }

  #hdr-time {
    margin-left: auto;
    font-family: 'Share Tech Mono', monospace;
    font-size: .8rem;
    color: var(--muted);
  }

  /* ---- STATUS BAR ---- */
  #status-bar {
    margin: 16px 20px 0;
    padding: 16px 24px;
    border-radius: 10px;
    text-align: center;
    font-weight: 900;
    font-size: 1.1rem;
    letter-spacing: .12em;
    text-transform: uppercase;
    border: 1px solid var(--border);
    background: var(--surface);
    transition: background .3s, border-color .3s, color .3s;
  }
  #status-bar.ok    { background:#052e16; border-color:var(--ok);    color:#86efac; }
  #status-bar.alarm { background:var(--alert-dk); border-color:var(--alert); color:#fca5a5;
                      animation: flashBorder .6s infinite alternate; }
  @keyframes flashBorder {
    from { border-color: var(--alert);   }
    to   { border-color: transparent;    }
  }

  /* ---- SENSOR CARDS ---- */
  .cards {
    display: flex;
    gap: 14px;
    padding: 18px 20px 6px;
    flex-wrap: wrap;
  }

  .card {
    flex: 1;
    min-width: 140px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 16px 20px;
    display: flex;
    flex-direction: column;
    gap: 6px;
    transition: border-color .3s;
  }
  .card.alert { border-color: var(--alert); }

  .card-label {
    font-size: .7rem;
    font-weight: 700;
    letter-spacing: .1em;
    text-transform: uppercase;
    color: var(--muted);
  }

  .card-value {
    font-family: 'Share Tech Mono', monospace;
    font-size: 1.8rem;
    font-weight: 700;
    transition: color .3s;
  }

  .card-unit {
    font-size: .75rem;
    color: var(--muted);
  }

  /* ---- FAULT COUNTER ---- */
  #fault-wrap {
    margin: 6px 20px 0;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 14px 20px;
    display: flex;
    align-items: center;
    justify-content: space-between;
  }

  /* ---- COOLDOWN PROGRESS BAR ---- */
  #cooldown-wrap {
    margin: 6px 20px 0;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 12px 20px;
  }
  #cooldown-label {
    font-size: .7rem;
    font-weight: 700;
    letter-spacing: .1em;
    text-transform: uppercase;
    color: var(--muted);
    margin-bottom: 8px;
  }
  #cooldown-bar-bg {
    width: 100%;
    height: 8px;
    background: var(--border);
    border-radius: 4px;
    overflow: hidden;
  }
  #cooldown-bar-fill {
    height: 100%;
    width: 0%;
    background: #facc15;
    border-radius: 4px;
    transition: width .4s linear, background .3s;
  }
  #cooldown-bar-fill.ready { background: #22c55e; }
  #cooldown-timer {
    font-family: 'Share Tech Mono', monospace;
    font-size: .75rem;
    color: var(--muted);
    margin-top: 5px;
    text-align: right;
  }

  #fault-label {
    font-size: .75rem;
    font-weight: 700;
    letter-spacing: .1em;
    text-transform: uppercase;
    color: var(--muted);
  }

  #fault-count {
    font-family: 'Share Tech Mono', monospace;
    font-size: 2rem;
    font-weight: 700;
    color: #facc15;
    transition: color .3s;
  }
  #fault-count.danger { color: var(--alert); }

  #fault-sub {
    font-size: .75rem;
    color: var(--muted);
    margin-top: 2px;
  }

  /* ---- FAULT HISTORY ---- */
  .section-title {
    padding: 18px 20px 8px;
    font-size: .75rem;
    font-weight: 700;
    letter-spacing: .12em;
    text-transform: uppercase;
    color: var(--muted);
  }

  #history-wrap {
    margin: 0 20px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 12px;
    overflow: hidden;
  }

  table {
    width: 100%;
    border-collapse: collapse;
    font-family: 'Share Tech Mono', monospace;
    font-size: .85rem;
  }

  th {
    background: #0a1525;
    padding: 10px 14px;
    text-align: left;
    font-size: .7rem;
    letter-spacing: .08em;
    text-transform: uppercase;
    color: var(--muted);
    border-bottom: 1px solid var(--border);
  }

  td {
    padding: 10px 14px;
    border-bottom: 1px solid var(--border);
    color: var(--text);
  }

  tr:last-child td { border-bottom: none; }
  tr:hover td      { background: #0d1b2e; }

  .cause-badge {
    display: inline-block;
    padding: 2px 8px;
    border-radius: 4px;
    font-size: .75rem;
    font-weight: 700;
    margin: 1px 2px;
  }
  .cause-TEMP    { background:#431407; color:#fb923c; }
  .cause-VIB     { background:#2e1065; color:#c084fc; }
  .cause-CURRENT { background:#052e16; color:#4ade80; }

  #no-faults {
    padding: 24px;
    text-align: center;
    color: var(--muted);
    font-size: .85rem;
  }

  /* ---- RESET BUTTON ---- */
  .btn-wrap { text-align: center; padding: 22px 20px 0; }

  button {
    padding: 12px 36px;
    font-family: 'Exo 2', sans-serif;
    font-size: 1rem;
    font-weight: 700;
    letter-spacing: .08em;
    text-transform: uppercase;
    background: #1a3050;
    color: var(--text);
    border: 1px solid var(--border);
    border-radius: 8px;
    cursor: pointer;
    transition: background .2s, border-color .2s;
  }
  button:hover { background:#1e3d60; border-color:#2a5080; }
</style>
</head>

<body>

<header>
  <div class="pulse-dot" id="dot"></div>
  <h1>&#9881; Maintenance Monitoring Dashboard</h1>
  <span id="hdr-time">--:--:--</span>
</header>

<div id="status-bar">Connecting...</div>

<div class="cards">
  <div class="card" id="card-temp">
    <div class="card-label">Temperature</div>
    <div class="card-value" id="val-temp" style="color:#f97316">--</div>
    <div class="card-unit">&#176;C &nbsp;|&nbsp; Threshold: 35 &#176;C</div>
  </div>
  <div class="card" id="card-vib">
    <div class="card-label">Vibration</div>
    <div class="card-value" id="val-vib" style="color:#a855f7">--</div>
    <div class="card-unit">m/s&#178; &nbsp;|&nbsp; Threshold: 1.2 m/s&#178;</div>
  </div>
  <div class="card" id="card-cur">
    <div class="card-label">Current</div>
    <div class="card-value" id="val-cur" style="color:#22c55e">--</div>
    <div class="card-unit">A &nbsp;|&nbsp; Threshold: 0.25 A</div>
  </div>
</div>

<div id="fault-wrap">
  <div>
    <div class="card-label">Fault Count</div>
    <div id="fault-sub">3 faults &rarr; maintenance required</div>
  </div>
  <div style="text-align:right">
    <div id="fault-count">0</div>
    <div id="fault-sub2" style="font-size:.7rem;color:var(--muted);">/ 3</div>
  </div>
</div>

<div id="cooldown-wrap">
  <div id="cooldown-label">Next Fault Window</div>
  <div id="cooldown-bar-bg">
    <div id="cooldown-bar-fill"></div>
  </div>
  <div id="cooldown-timer">--</div>
</div>

<div class="section-title">Fault History</div>
<div id="history-wrap">
  <div id="no-faults">No faults recorded yet.</div>
  <table id="fault-table" style="display:none">
    <thead>
      <tr>
        <th>#</th>
        <th>Cause</th>
        <th>Temp (&#176;C)</th>
        <th>Vib (m/s&#178;)</th>
        <th>Current (A)</th>
        <th>Time</th>
      </tr>
    </thead>
    <tbody id="fault-body"></tbody>
  </table>
</div>

<div class="btn-wrap">
  <button onclick="doReset()">&#8635; Reset Maintenance</button>
</div>

<script>
const CAUSE_COLORS  = { TEMP: 'cause-TEMP', VIB: 'cause-VIB', CURRENT: 'cause-CURRENT' };
const FAULT_COOLDOWN = 10; // must match server value

let lastFaultTime = 0;  // epoch ms of last fault (estimated client-side)

function load() {
  fetch('/status')
    .then(r => r.json())
    .then(data => {

      document.getElementById('hdr-time').innerText = new Date().toLocaleTimeString();

      // ---- sensor values ----
      const d = data.data;
      document.getElementById('val-temp').innerText = d.temp.toFixed(2);
      document.getElementById('val-vib').innerText  = d.vib.toFixed(2);
      document.getElementById('val-cur').innerText  = d.current.toFixed(3);

      const tempFault = d.temp    > 40;
      const vibFault  = d.vib     > 1.2;
      const curFault  = d.current > 0.25;

      document.getElementById('val-temp').style.color = tempFault ? '#ef4444' : '#f97316';
      document.getElementById('val-vib').style.color  = vibFault  ? '#ef4444' : '#a855f7';
      document.getElementById('val-cur').style.color  = curFault  ? '#ef4444' : '#22c55e';

      document.getElementById('card-temp').classList.toggle('alert', tempFault);
      document.getElementById('card-vib').classList.toggle('alert',  vibFault);
      document.getElementById('card-cur').classList.toggle('alert',  curFault);

      // ---- fault count ----
      const fc   = data.fault_count;
      const fcEl = document.getElementById('fault-count');
      const prev = parseInt(fcEl.innerText) || 0;
      if (fc > prev) lastFaultTime = Date.now();   // new fault just counted
      fcEl.innerText = fc;
      fcEl.classList.toggle('danger', fc >= 3);

      // ---- status bar ----
      const bar = document.getElementById('status-bar');
      const dot = document.getElementById('dot');

      if (data.maintenance) {
        bar.className = 'alarm';
        bar.innerText = '\u26a0  MAINTENANCE REQUIRED';
        dot.style.background = '#ef4444';
      } else {
        bar.className = 'ok';
        bar.innerText = '\u2713  SYSTEM NORMAL';
        dot.style.background = '#22c55e';
      }

      // ---- fault history table ----
      const history = data.fault_history;
      const noFaults = document.getElementById('no-faults');
      const table    = document.getElementById('fault-table');
      const tbody    = document.getElementById('fault-body');

      if (history.length === 0) {
        noFaults.style.display = 'block';
        table.style.display    = 'none';
      } else {
        noFaults.style.display = 'none';
        table.style.display    = 'table';

        tbody.innerHTML = history.map(f => {
          const causes = f.cause.map(c =>
            `<span class="cause-badge ${CAUSE_COLORS[c] || ''}">${c}</span>`
          ).join('');
          return `<tr>
            <td>${f.fault_no}</td>
            <td>${causes}</td>
            <td>${f.temp.toFixed(2)}</td>
            <td>${f.vib.toFixed(2)}</td>
            <td>${f.current.toFixed(3)}</td>
            <td>${f.time}</td>
          </tr>`;
        }).join('');
      }
    })
    .catch(() => {
      const bar = document.getElementById('status-bar');
      bar.className = 'alarm';
      bar.innerText = '\u2715  CONNECTION LOST';
    });
}

// ---- cooldown bar (runs every 200ms for smooth animation) ----
function updateCooldown() {
  const fill  = document.getElementById('cooldown-bar-fill');
  const timer = document.getElementById('cooldown-timer');
  const elapsed = (Date.now() - lastFaultTime) / 1000;
  const pct     = Math.min(elapsed / FAULT_COOLDOWN * 100, 100);
  const remaining = Math.max(FAULT_COOLDOWN - elapsed, 0);

  fill.style.width = pct + '%';

  if (remaining <= 0) {
    fill.classList.add('ready');
    timer.innerText = 'Ready for next fault';
  } else {
    fill.classList.remove('ready');
    timer.innerText = remaining.toFixed(1) + 's until next fault window';
  }
}

function doReset() {
  fetch('/reset', { method: 'POST' })
    .then(() => {
      lastFaultTime = 0;
      load();
    });
}

load();
setInterval(load, 1000);
setInterval(updateCooldown, 200);
</script>

</body>
</html>
"""


# ===== RUN =====
if __name__ == "__main__":
    t = threading.Thread(target=fetch_loop)
    t.daemon = True
    t.start()

    app.run(host="0.0.0.0", port=5000)
