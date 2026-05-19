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
from flask import Flask, jsonify, render_template_string
from flask_cors import CORS

app = Flask(__name__)
CORS(app) # Enable CORS for all routes

PROJECT_ROOT = Path(__file__).resolve().parent.parent
REPORT_GLOB = "chronon3d-*.log"

REPORT_PATTERN = re.compile(
    r"Git Commit:\s*(?P<git_commit>.*?)\n"
    r"OS:\s*(?P<os>.*?)\n"
    r"CPU Model:\s*(?P<cpu_model>.*?)\n"
    r"Cores:\s*(?P<cores>.*?)\n"
    r"Compiler:\s*(?P<compiler>.*?)\n"
    r"Build Type:\s*(?P<build_type>.*?)\n"
    r".*?"
    r"Run ID:\s*(?P<run_id>\S+)\n"
    r"Composition ID:\s*(?P<comp_id>.*?)\n"
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
        "counters": counters,
        "frames": frames
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
    # Frontend expects run_id as primary key
    return jsonify(reports)


@app.route("/api/run/<run_id>")
def api_run_detail(run_id):
    # Find report by run_id
    reports = get_all_reports()
    for r in reports:
        if r.get("run_id") == run_id:
            return jsonify(r)
    return jsonify({"error": "run not found"}), 404


@app.route("/api/report/<filename>")
def api_report(filename):
    filepath = PROJECT_ROOT / filename
    if not filepath.exists() or not filepath.name.startswith("chronon3d-") or not filepath.name.endswith(".log"):
        return jsonify({"error": "not found"}), 404
    parsed = parse_report(str(filepath))
    if not parsed:
        return jsonify({"error": "parse failed"}), 500
    return jsonify(parsed)


if __name__ == "__main__":
    import sys
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print(f"Chronon3D Dashboard: http://localhost:{port}")
    print(f"Watching: {PROJECT_ROOT / REPORT_GLOB}")
    app.run(host="0.0.0.0", port=port, debug=False)
