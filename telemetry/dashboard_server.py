#!/usr/bin/env python3
"""Chronon3D Telemetry Dashboard Server

Serves a real-time dashboard that parses chronon3d-*.log report files.
Usage:
    cd /home/pierone/Pyt/Chronon3d
    python3 telemetry/dashboard_server.py [port]
"""

import json
import os
import re
import glob
import time
from pathlib import Path
from flask import Flask, jsonify, render_template_string, request, send_file, Response
from flask_cors import CORS

app = Flask(__name__)
CORS(app) # Enable CORS for all routes

PROJECT_ROOT = Path(__file__).resolve().parent.parent
REPORT_GLOB = "chronon3d-*.log"

REPORT_PATTERN = re.compile(
    r"Command Line:\s*(?P<command_line>.*?)\n"
    r"Timestamp:\s*(?P<timestamp_iso>.*?)\n"
    r"Git Commit:\s*(?P<git_commit>.*?)\n"
    r"OS:\s*(?P<os>.*?)\n"
    r"CPU Model:\s*(?P<cpu_model>.*?)\n"
    r"Cores:\s*(?P<cores>.*?)\n"
    r"Compiler:\s*(?P<compiler>.*?)\n"
    r"Build Type:\s*(?P<build_type>.*?)\n"
    r".*?"
    r"Run ID:\s*(?P<run_id>\S+)\n"
    r"Composition ID:\s*(?P<comp_id>.*?)\n"
    r"(?:Output Path:\s*(?P<output_path_log>.*?)\n)?"
    r"Frames Total:\s*(?P<frames_total>\d+)\n"
    r"Wall Time:\s*(?P<wall_time>[\d.]+) ms\n"
    r"Render Time:\s*(?P<render_time>[\d.]+) ms\n"
    r"Effective FPS:\s*(?P<fps>[\d.]+)",
    re.DOTALL
)

COUNTER_SECTION = re.compile(
    r"--- PERFORMANCE COUNTERS ---\n(?P<counters>.*?)\n--- PHASE DURATIONS ---",
    re.DOTALL
)

FRAME_SECTION = re.compile(
    r"--- FRAME BREAKDOWN ---\n(?P<frames>.*)",
    re.DOTALL
)

COUNTER_LINE = re.compile(
    r"\s*(?P<name>\w+)\s*:\s*(?P<value>\d+)"
)

FRAME_LINE = re.compile(
    r"Frame\s+(?P<num>\d+)\s*\|\s*Dur:\s*(?P<dur>[\d.]+)\s*ms\s*\|\s*Cache Hit:\s*(?P<cache_hit>YES|NO)\s*\|\s*Dirty Ratio:\s*(?P<dirty>[\d.]+)"
)


def parse_report(filepath):
    text = Path(filepath).read_text()
    m = REPORT_PATTERN.search(text)
    if not m:
        return None
    info = m.groupdict()
    
    counters = {}
    cm = COUNTER_SECTION.search(text)
    if cm:
        for line in cm.group("counters").split("\n"):
            cl = COUNTER_LINE.match(line)
            if cl:
                counters[cl.group("name")] = int(cl.group("value"))
    
    frames = []
    fm = FRAME_SECTION.search(text)
    if fm:
        for line in fm.group("frames").split("\n"):
            fl = FRAME_LINE.match(line)
            if fl:
                frames.append({
                    "frame": int(fl.group("num")),
                    "duration_ms": float(fl.group("dur")),
                    "cache_hit": fl.group("cache_hit") == "YES",
                    "dirty_ratio": float(fl.group("dirty"))
                })
    
    # Determine output_path: prefer from log (new format), then command line, then default
    output_path = (info.get("output_path_log") or "").strip()
    if not output_path:
        cmd = info.get("command_line", "").strip()
        m_out = re.search(r'(?:-o|--output)\s+(\S+)', cmd)
        if m_out:
            output_path = m_out.group(1)
        elif "render" in cmd:
            output_path = "render_####.png"
        else:
            output_path = "output.mp4"

    return {
        "file": Path(filepath).name,
        "timestamp": Path(filepath).stat().st_mtime,
        "git_commit": info.get("git_commit", "").strip(),
        "os": info.get("os", "").strip(),
        "cpu": info.get("cpu_model", "").strip(),
        "cores": int(info.get("cores", 0)),
        "compiler": info.get("compiler", "").strip(),
        "build_type": info.get("build_type", "").strip(),
        "run_id": info.get("run_id", "").strip(),
        "composition": info.get("comp_id", "").strip(),
        "frames_total": int(info.get("frames_total", 0)),
        "wall_time_ms": float(info.get("wall_time", 0)),
        "render_time_ms": float(info.get("render_time", 0)),
        "fps": float(info.get("fps", 0)),
        "output_path": output_path,
        "counters": counters,
        "frames": frames
    }


def _report_to_react(report):
    """Convert a flat report dict to React frontend nested format."""
    c = report.get("counters", {})
    frames = report.get("frames", [])
    # Rename frame keys to match React expectations
    react_frames = []
    for f in frames:
        rf = {
            "frame_number": f.get("frame"),
            "duration_ms": f.get("duration_ms"),
            "cache_hit": f.get("cache_hit"),
            "dirty_area_ratio": f.get("dirty_ratio")
        }
        react_frames.append(rf)

    counters_array = [{"counter_name": k, "counter_value": v} for k, v in c.items()]

    ts = report.get("timestamp", 0)
    import datetime
    finished_at_iso = datetime.datetime.fromtimestamp(ts, tz=datetime.timezone.utc).isoformat()

    git = report.get("git_commit", "")
    run_dict = {
        "run_id": report.get("run_id", ""),
        "composition_id": report.get("composition", ""),
        "success": True,
        "finished_at_iso": finished_at_iso,
        "git_commit_short": git[:8] if git else "",
        "build_type": report.get("build_type", ""),
        "compiler_info": report.get("compiler", ""),
        "os": report.get("os", ""),
        "cpu_model": report.get("cpu", ""),
        "cores": int(report.get("cores", 0)),
        "effective_fps": float(report.get("fps", 0)),
        "wall_time_ms": float(report.get("wall_time_ms", 0)),
        "render_ms": float(report.get("render_time_ms", 0)),
        "frames_total": int(report.get("frames_total", 0)),
        "output_path": report.get("output_path", ""),
        "bytes_allocated_peak": c.get("bytes_allocated_peak", 0),
        "cache_hits": c.get("cache_hits", 0),
        "cache_misses": c.get("cache_misses", 0),
    }
    return {
        "file": report.get("file", ""),
        "timestamp": ts,
        "run": run_dict,
        "frames": react_frames,
        "phases": [],
        "counters": counters_array,
        "node_events": [],
        "layer_events": []
    }


def get_all_reports():
    reports = []
    for f in sorted(glob.glob(str(PROJECT_ROOT / REPORT_GLOB)), key=os.path.getmtime, reverse=True):
        parsed = parse_report(f)
        if parsed:
            reports.append(parsed)
    return reports


DASHBOARD_HTML = """
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Chronon3D Telemetry Dashboard</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #0d1117; color: #c9d1d9; padding: 20px; }
h1 { color: #58a6ff; margin-bottom: 10px; }
h2 { color: #f0f6fc; margin: 20px 0 10px; border-bottom: 1px solid #30363d; padding-bottom: 5px; }
.report-card { background: #161b22; border: 1px solid #30363d; border-radius: 8px; padding: 16px; margin-bottom: 16px; }
.report-card:hover { border-color: #58a6ff; }
.meta { display: grid; grid-template-columns: repeat(auto-fill, minmax(200px, 1fr)); gap: 8px; margin-bottom: 12px; font-size: 13px; }
.meta span { background: #21262d; padding: 4px 8px; border-radius: 4px; }
.meta .label { color: #8b949e; }
.meta .value { color: #f0f6fc; font-weight: 600; }
.counters { display: grid; grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); gap: 6px; margin-bottom: 12px; }
.counter { background: #21262d; padding: 6px 10px; border-radius: 4px; font-size: 12px; }
.counter .name { color: #8b949e; }
.counter .val { color: #d2a8ff; font-weight: 700; float: right; }
.counter .val.good { color: #3fb950; }
.counter .val.warn { color: #d29922; }
.counter .val.bad { color: #f85149; }
.frame-table { width: 100%; border-collapse: collapse; font-size: 12px; }
.frame-table th { text-align: left; padding: 4px 8px; background: #21262d; color: #8b949e; position: sticky; top: 0; }
.frame-table td { padding: 3px 8px; border-bottom: 1px solid #21262d; }
.frame-table tr:hover { background: #1c2128; }
.cache-hit { color: #3fb950; }
.cache-miss { color: #f85149; }
.summary-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 8px; margin-bottom: 16px; }
.summary-card { background: #161b22; border: 1px solid #30363d; border-radius: 8px; padding: 12px; text-align: center; }
.summary-card .big { font-size: 28px; font-weight: 700; }
.summary-card .label { font-size: 11px; color: #8b949e; text-transform: uppercase; }
.status-bar { display: flex; gap: 16px; margin-bottom: 16px; padding: 8px 12px; background: #161b22; border-radius: 6px; font-size: 13px; align-items: center; }
.status-dot { width: 8px; height: 8px; border-radius: 50%; display: inline-block; }
.status-dot.live { background: #3fb950; }
.status-dot.stale { background: #d29922; }
.refresh-btn { background: #238636; color: #fff; border: none; padding: 6px 14px; border-radius: 6px; cursor: pointer; font-size: 13px; margin-left: auto; }
.refresh-btn:hover { background: #2ea043; }
</style>
</head>
<body>
<h1>🔷 Chronon3D Telemetry Dashboard</h1>
<div class="status-bar">
  <span class="status-dot live" id="statusDot"></span>
  <span id="statusText">Live</span>
  <span id="reportCount">0 reports</span>
  <span id="lastUpdate"></span>
  <button class="refresh-btn" onclick="location.reload()">Refresh</button>
</div>

<div class="summary-grid" id="summaryGrid"></div>
<div id="reports"></div>

<script>
let reports = [];

function valClass(name, value) {
  if (name.includes('reuses') || name.includes('hits') || name.includes('simd')) return value > 0 ? 'good' : 'bad';
  if (name.includes('misses') || name.includes('allocations') || name.includes('bytes')) return value > 1000 ? 'warn' : 'good';
  return '';
}

function render() {
  const container = document.getElementById('reports');
  const grid = document.getElementById('summaryGrid');
  container.innerHTML = '';
  document.getElementById('reportCount').textContent = reports.length + ' reports';

  if (reports.length === 0) {
    container.innerHTML = '<p style="color:#8b949e;text-align:center;padding:40px;">No reports found. Run a render with --report first.</p>';
    return;
  }

  // Summary grid
  const last = reports[0];
  const counters = last.counters || {};
  grid.innerHTML = `
    <div class="summary-card"><div class="big">${last.fps.toFixed(1)}</div><div class="label">FPS</div></div>
    <div class="summary-card"><div class="big">${last.frames_total}</div><div class="label">Frames</div></div>
    <div class="summary-card"><div class="big">${(last.wall_time_ms/1000).toFixed(1)}s</div><div class="label">Wall Time</div></div>
    <div class="summary-card"><div class="big">${(last.render_time_ms/1000).toFixed(1)}s</div><div class="label">Render Time</div></div>
  `;

  // Each report
  for (const r of reports) {
    const c = r.counters || {};
    const card = document.createElement('div');
    card.className = 'report-card';

    const time = new Date(r.timestamp * 1000).toLocaleString();
    const hitRate = (c.cache_hits + c.cache_misses) > 0
      ? (c.cache_hits / (c.cache_hits + c.cache_misses) * 100).toFixed(1)
      : 'N/A';

    card.innerHTML = `
      <div class="meta">
        <span><span class="label">Comp:</span> <span class="value">${r.composition}</span></span>
        <span><span class="label">FPS:</span> <span class="value">${r.fps.toFixed(2)}</span></span>
        <span><span class="label">Git:</span> <span class="value">${r.git_commit}</span></span>
        <span><span class="label">Time:</span> <span class="value">${time}</span></span>
        <span><span class="label">Wall:</span> <span class="value">${r.wall_time_ms.toFixed(1)} ms</span></span>
        <span><span class="label">Render:</span> <span class="value">${r.render_time_ms.toFixed(1)} ms</span></span>
        <span><span class="label">Cache Hit Rate:</span> <span class="value">${hitRate}%</span></span>
        <span><span class="label">Dirty Ratio:</span> <span class="value">${r.frames.length > 0 ? r.frames.reduce((a,f) => a+f.dirty_ratio,0)/r.frames.length : 0}</span></span>
      </div>
      <div class="counters">
        ${Object.entries(c).map(([k,v]) =>
          `<div class="counter"><span class="name">${k}:</span> <span class="val ${valClass(k,v)}">${v.toLocaleString()}</span></div>`
        ).join('')}
      </div>
      <table class="frame-table">
        <thead><tr><th>Frame</th><th>Duration (ms)</th><th>Cache</th><th>Dirty</th></tr></thead>
        <tbody>
          ${r.frames.slice(0, 20).map(f =>
            `<tr>
              <td>${f.frame}</td>
              <td>${f.duration_ms.toFixed(2)}</td>
              <td class="${f.cache_hit ? 'cache-hit' : 'cache-miss'}">${f.cache_hit ? 'HIT' : 'MISS'}</td>
              <td>${f.dirty_ratio}</td>
            </tr>`
          ).join('')}
          ${r.frames.length > 20 ? `<tr><td colspan="4" style="text-align:center;color:#8b949e;">... ${r.frames.length - 20} more frames</td></tr>` : ''}
        </tbody>
      </table>
    `;
    container.appendChild(card);
  }
}

async function fetchReports() {
  try {
    const resp = await fetch('/api/reports');
    reports = await resp.json();
    render();
    document.getElementById('statusText').textContent = 'Live';
    document.getElementById('lastUpdate').textContent = new Date().toLocaleTimeString();
  } catch(e) {
    document.getElementById('statusText').textContent = 'Error fetching';
  }
}

fetchReports();
setInterval(fetchReports, 5000);
</script>
</body>
</html>
"""


@app.route("/")
def index():
    return render_template_string(DASHBOARD_HTML)


@app.route("/api/reports")
def api_reports():
    reports = get_all_reports()
    return jsonify(reports)


@app.route("/api/runs")
def api_runs():
    reports = get_all_reports()
    # React sidebar needs flat run fields
    runs = [_report_to_react(r).get("run", r) for r in reports]
    return jsonify(runs)


@app.route("/api/run/<run_id>")
def api_run_detail(run_id):
    reports = get_all_reports()
    for r in reports:
        if r.get("run_id") == run_id:
            return jsonify(_report_to_react(r))
    return jsonify({"error": "run not found"}), 404


def _serve_file_with_range(filepath, req):
    """Serve a file with proper Range (206 Partial Content) support."""
    file_size = os.path.getsize(str(filepath))
    range_header = req.headers.get("Range")

    if range_header:
        byte_range = range_header.strip().removeprefix("bytes=")
        parts = byte_range.split("-")
        start = int(parts[0]) if parts[0] else 0
        end = int(parts[1]) if len(parts) > 1 and parts[1] else file_size - 1
        end = min(end, file_size - 1)
        length = end - start + 1

        with open(str(filepath), "rb") as f:
            f.seek(start)
            data = f.read(length)

        resp = Response(data, 206, mimetype="video/mp4", direct_passthrough=True)
        resp.headers["Content-Range"] = f"bytes {start}-{end}/{file_size}"
        resp.headers["Accept-Ranges"] = "bytes"
        resp.headers["Content-Length"] = str(length)
        return resp

    return send_file(str(filepath), mimetype="video/mp4")


@app.route("/artifact")
def artifact():
    path = request.args.get("path", "")
    if not path:
        return jsonify({"error": "missing path"}), 400

    safe = Path(path).name
    filepath = PROJECT_ROOT / path
    if not filepath.exists():
        filepath = PROJECT_ROOT / safe
    if not filepath.exists():
        return jsonify({"error": "not found"}), 404

    return _serve_file_with_range(filepath, request)


@app.route("/api/report/<filename>")
def api_report(filename):
    filepath = PROJECT_ROOT / filename
    if not filepath.exists() or not filepath.name.startswith("chronon3d-") or not filepath.name.endswith(".log"):
        return jsonify({"error": "not found"}), 404
    parsed = parse_report(str(filepath))
    if not parsed:
        return jsonify({"error": "parse failed"}), 500
    return jsonify(parsed)


@app.route("/video/<path:filename>")
def serve_video(filename):
    filepath = PROJECT_ROOT / filename
    if not filepath.exists():
        return jsonify({"error": "not found"}), 404
    return _serve_file_with_range(filepath, request)


WATCH_HTML = """
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Chronon3D - Watch Render</title>
<style>
body { margin:0; background:#000; display:flex; justify-content:center; align-items:center; height:100vh; }
video { max-width:100vw; max-height:100vh; }
</style>
</head>
<body>
<video controls autoplay muted>
  <source src="%s" type="video/mp4">
</video>
</body>
</html>
"""


WATCH_LIST_HTML = """
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Chronon3D - Renders</title>
<style>
body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #0d1117; color: #c9d1d9; padding: 40px; }
a { color: #58a6ff; text-decoration: none; display: block; margin: 8px 0; font-size: 18px; }
a:hover { text-decoration: underline; }
h1 { color: #f0f6fc; }
</style>
</head>
<body>
<h1>Rendered Videos</h1>
%s
</body>
</html>
"""


@app.route("/renders")
def renders():
    import glob as gglob
    mp4s = sorted(gglob.glob(str(PROJECT_ROOT / "*.mp4")), key=os.path.getmtime, reverse=True)
    links = "".join(f'<a href="/watch?f={os.path.basename(m)}">{os.path.basename(m)}</a>'
                    for m in mp4s)
    if not links:
        links = "<p>No MP4 files yet. Run a render first.</p>"
    return render_template_string(WATCH_LIST_HTML % links)


@app.route("/watch")
def watch_with_query():
    from flask import request
    fname = request.args.get("f")
    if fname and (PROJECT_ROOT / fname).exists():
        return render_template_string(WATCH_HTML % f"/video/{fname}")
    # fallback: first mp4 found
    import glob as gglob
    mp4s = gglob.glob(str(PROJECT_ROOT / "*.mp4"))
    if mp4s:
        return render_template_string(WATCH_HTML % f"/video/{os.path.basename(mp4s[0])}")
    return "No MP4 files found", 404


if __name__ == "__main__":
    import sys
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print(f"Chronon3D Dashboard: http://localhost:{port}")
    print(f"Watching: {PROJECT_ROOT / REPORT_GLOB}")
    app.run(host="0.0.0.0", port=port, debug=False)
