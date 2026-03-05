import threading
import time
import csv
import io
from flask import Flask, jsonify, request, render_template, Response
import serial
import re
import math
import os


# Serial / Fake config
USE_FAKE_SERIAL = True
FAKE_FILE = "fake_serial.txt"
FAKE_PERIOD_S = 0.10  # seconds/sample

SERIAL_PORT = "COM4"
SERIAL_BAUD = 115200
SERIAL_TIMEOUT_S = 1

# Expected serial line (Lab 5)
# Example:
# Vrms_ref= 1.170 V  Vrms_test= 1.160 V  phase=  +0.5 deg  f= 60.00 Hz
lab5_re = re.compile(
    r"^\s*Vrms_ref\s*=\s*([+-]?[0-9]*\.?[0-9]+)\s*V\s+"
    r"Vrms_test\s*=\s*([+-]?[0-9]*\.?[0-9]+)\s*V\s+"
    r"phase\s*=\s*([+-]?[0-9]*\.?[0-9]+)\s*deg\s+"
    r"f\s*=\s*([+-]?[0-9]*\.?[0-9]+)\s*Hz\s*$",
    re.IGNORECASE,
)

PHASE_UNITS = ("deg", "rad")
SQRT2 = math.sqrt(2.0)

app = Flask(__name__)
lock = threading.Lock()

t0 = time.time()
def _now_s() -> float:
    return time.time() - t0

state = {
    "t": 0.0,
    "connected": False,
    "status": "DISCONNECTED",   # DISCONNECTED / CONNECTING / CONNECTED
    "running": False,           # START/STOP gating
    "last_line": "",
    "seq": 0,

    # Lab 5 measurements
    "vrms_ref": None,
    "vrms_test": None,
    "vpeak_ref": None,
    "vpeak_test": None,

    # phase (always computed from MCU deg)
    "phase_deg": None,
    "phase_rad": None,

    "freq_hz": None,

    # display preference 
    "phase_unit": "deg",        # "deg" or "rad"
    "phase": None,             
}

_ser = None
_ser_lock = threading.Lock()

# Run log (for CSV export)
# Logs ONLY after START. Includes both deg and rad.
run_log = []  # list of (t_s, vrms_ref, vrms_test, vpeak_ref, vpeak_test, phase_deg, phase_rad, freq_hz)
run_log_lock = threading.Lock()

def parse_line(line: str):
    s = line.strip()
    if not s:
        return None

    m = lab5_re.match(s)
    if not m:
        return None

    vr = float(m.group(1))
    vt = float(m.group(2))
    ph_deg = float(m.group(3))
    f_hz = float(m.group(4))
    return (vr, vt, ph_deg, f_hz)

def _apply_sample(vrms_ref: float, vrms_test: float, phase_deg: float, freq_hz: float, raw_line: str):
    # Update debug fields always (so you can see last_line even when stopped)
    with lock:
        state["t"] = _now_s()
        state["last_line"] = raw_line

        if not state["running"]:
            return

        # store RMS
        state["vrms_ref"] = float(vrms_ref)
        state["vrms_test"] = float(vrms_test)

        # compute peak (sine assumption)
        state["vpeak_ref"] = float(vrms_ref) * SQRT2
        state["vpeak_test"] = float(vrms_test) * SQRT2

        # phase in both units
        state["phase_deg"] = float(phase_deg)
        state["phase_rad"] = float(phase_deg) * (math.pi / 180.0)

        state["freq_hz"] = float(freq_hz)

        # UI-facing phase based on preference
        unit = str(state.get("phase_unit", "deg")).lower()
        if unit == "rad":
            state["phase"] = state["phase_rad"]
        else:
            state["phase"] = state["phase_deg"]

        state["seq"] += 1

        # snapshot for logging
        t_s = float(state["t"])
        vr = state["vrms_ref"]
        vt = state["vrms_test"]
        vpr = state["vpeak_ref"]
        vpt = state["vpeak_test"]
        ph_d = state["phase_deg"]
        ph_r = state["phase_rad"]
        fhz = state["freq_hz"]

    with run_log_lock:
        run_log.append((t_s, vr, vt, vpr, vpt, ph_d, ph_r, fhz))

def serial_send(line: str) -> bool:
    global _ser
    with _ser_lock:
        if _ser is None:
            return False
        try:
            _ser.write((line.strip() + "\n").encode("ascii", errors="ignore"))
            return True
        except:
            return False

def serial_reader():
    global _ser

    while True:
        if USE_FAKE_SERIAL:
            time.sleep(0.25)
            continue

        try:
            with lock:
                state["status"] = "CONNECTING"
                state["connected"] = False

            ser = serial.Serial(
                SERIAL_PORT,
                SERIAL_BAUD,
                timeout=SERIAL_TIMEOUT_S
            )

            with _ser_lock:
                _ser = ser

            with lock:
                state["connected"] = True
                state["status"] = "CONNECTED"

            try:
                ser.reset_input_buffer()
            except:
                pass

            while not USE_FAKE_SERIAL:
                raw = ser.readline()
                if not raw:
                    continue

                line = raw.decode("ascii", errors="ignore").strip()
                if not line:
                    continue

                parsed = parse_line(line)
                if parsed is None:
                    with lock:
                        state["last_line"] = line
                        state["t"] = _now_s()
                    continue

                vr, vt, ph_deg, f_hz = parsed
                _apply_sample(vr, vt, ph_deg, f_hz, line)

        except Exception as e:
            with _ser_lock:
                try:
                    if _ser is not None:
                        _ser.close()
                except:
                    pass
                _ser = None

            with lock:
                state["connected"] = False
                state["status"] = "DISCONNECTED"
                state["last_line"] = f"(serial error: {e})"

            time.sleep(1)

def fake_serial_reader():
    while True:
        if not USE_FAKE_SERIAL:
            time.sleep(0.25)
            continue

        # Make connected if FAKE_FILE exists and can be opened.
        file_path = FAKE_FILE
        period = FAKE_PERIOD_S

        if not os.path.exists(file_path):
            with lock:
                state["connected"] = False
                state["status"] = "DISCONNECTED"
                state["last_line"] = f"(fake error: file not found: {file_path})"
            time.sleep(0.5)
            continue

        with lock:
            state["connected"] = True
            state["status"] = "CONNECTED"

        try:
            with open(file_path, "r", encoding="utf-8") as f:
                for line in f:
                    if not USE_FAKE_SERIAL:
                        break

                    s = line.strip()
                    if not s:
                        time.sleep(period)
                        continue

                    parsed = parse_line(s)
                    if parsed is None:
                        with lock:
                            state["t"] = _now_s()
                            state["last_line"] = s
                        time.sleep(period)
                        continue

                    vr, vt, ph_deg, f_hz = parsed
                    _apply_sample(vr, vt, ph_deg, f_hz, s)
                    time.sleep(period)
            # loop forever (re-open file)

        except Exception as e:
            with lock:
                state["connected"] = False
                state["status"] = "DISCONNECTED"
                state["last_line"] = f"(fake error: {e})"
            time.sleep(1)

# Routes
@app.route("/")
def index():
    return render_template("volt_phase.html")

@app.get("/api/latest")
def api_latest():
    with lock:
        state["t"] = _now_s()
        return jsonify(state)

@app.post("/api/config")
def api_config():
    global USE_FAKE_SERIAL, FAKE_FILE, FAKE_PERIOD_S

    data = request.get_json(force=True) or {}

    # phase_unit is always allowed
    phase_unit = str(data.get("phase_unit", state.get("phase_unit", "deg"))).lower()
    if phase_unit not in PHASE_UNITS:
        phase_unit = "deg"

    with lock:
        state["phase_unit"] = phase_unit
        if state["phase_deg"] is not None:
            state["phase"] = state["phase_rad"] if phase_unit == "rad" else state["phase_deg"]

    if "fake_file" in data:
        fake_file_in = str(data.get("fake_file", "")).strip()
        if fake_file_in:
            FAKE_FILE = fake_file_in

    if "fake_period_ms" in data:
        try:
            fake_period_ms = int(data.get("fake_period_ms", int(FAKE_PERIOD_S * 1000)))
            fake_period_ms = max(20, min(2000, fake_period_ms))
            FAKE_PERIOD_S = fake_period_ms / 1000.0
        except Exception:
            pass

    if "source" in data:
        src = str(data.get("source", "")).lower()
        if src == "serial":
            USE_FAKE_SERIAL = False
        elif src == "file":
            USE_FAKE_SERIAL = True
        # else ignore bad value

    return jsonify(
        ok=True,
        use_fake=USE_FAKE_SERIAL,
        fake_file=FAKE_FILE,
        fake_period_s=FAKE_PERIOD_S,
        phase_unit=phase_unit
    ), 200

@app.post("/api/start")
def api_start():
    with lock:
        if not state["connected"]:
            return jsonify(ok=False, reason="DISCONNECTED"), 200

        global t0
        t0 = time.time()

        state["seq"] = 0
        state["vrms_ref"] = None
        state["vrms_test"] = None
        state["vpeak_ref"] = None
        state["vpeak_test"] = None
        state["phase_deg"] = None
        state["phase_rad"] = None
        state["freq_hz"] = None
        state["phase"] = None
        state["running"] = True

    # CSV starts at START (history = this run only)
    with run_log_lock:
        run_log.clear()

    if not USE_FAKE_SERIAL:
        serial_send("CMD=START")

    return jsonify(ok=True), 200

@app.post("/api/stop")
def api_stop():
    with lock:
        state["running"] = False
        state["vrms_ref"] = None
        state["vrms_test"] = None
        state["vpeak_ref"] = None
        state["vpeak_test"] = None
        state["phase_deg"] = None
        state["phase_rad"] = None
        state["freq_hz"] = None
        state["phase"] = None

    if not USE_FAKE_SERIAL:
        serial_send("CMD=STOP")

    return jsonify(ok=True), 200

@app.get("/api/export")
def api_export():
    with run_log_lock:
        rows = list(run_log)

    buf = io.StringIO()
    w = csv.writer(buf)

    w.writerow([
        "t_s",
        "vrms_ref_V",
        "vrms_test_V",
        "vpeak_ref_V",
        "vpeak_test_V",
        "phase_deg",
        "phase_rad",
        "freq_hz"
    ])

    for (t_s, vr, vt, vpr, vpt, ph_deg, ph_rad, fhz) in rows:
        w.writerow([
            f"{t_s:.6f}",
            "" if vr is None else f"{float(vr):.6f}",
            "" if vt is None else f"{float(vt):.6f}",
            "" if vpr is None else f"{float(vpr):.6f}",
            "" if vpt is None else f"{float(vpt):.6f}",
            "" if ph_deg is None else f"{float(ph_deg):.6f}",
            "" if ph_rad is None else f"{float(ph_rad):.6f}",
            "" if fhz is None else f"{float(fhz):.6f}",
        ])

    data = buf.getvalue().encode("utf-8")
    return Response(
        data,
        mimetype="text/csv",
        headers={"Content-Disposition": "attachment; filename=volt_phase_run.csv"},
    )

if __name__ == "__main__":
    threading.Thread(target=fake_serial_reader, daemon=True).start()
    threading.Thread(target=serial_reader, daemon=True).start()
    app.run(host="127.0.0.1", port=5000, debug=True, use_reloader=False)