# Telemetry Dashboard — Full Pipeline Guide

> **Goal:** This document explains how rendering → telemetry → web preview works end-to-end.
> A new developer should be able to read this and understand how to render, where data goes,
> how the web server serves it, and what to do when something breaks.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Rendering — CLI Commands](#2-rendering--cli-commands)
3. [Telemetry Database](#3-telemetry-database)
4. [Flask Backend Server](#4-flask-backend-server)
5. [React Frontend](#5-react-frontend)
6. [Artifact Serving Flow](#6-artifact-serving-flow)
7. [Auth Mechanism](#7-auth-mechanism)
8. [How to Start Everything](#8-how-to-start-everything)
9. [Troubleshooting Checklist](#9-troubleshooting-checklist)
10. [File Reference](#10-file-reference)

---

## 1. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                      Developer Machine                          │
│                                                                  │
│  chronon3d_cli (C++)                                            │
│    render/video ───→ output/*.png, output/*.mp4                  │
│         │                                                        │
│         └──→ Telemetry SQLite DB (~/.chronon3d/telemetry/)       │
│                                                                  │
│  Flask Server (Python)                                           │
│    Port 5005                                                     │
│    ├── serves React SPA (from frontend/dist/)                    │
│    ├── /api/*       ← JSON API (runs, run detail)                │
│    ├── /artifact    ← serves output/*.png, output/*.mp4          │
│    └── /socket.io   ← WebSocket real-time updates                │
│                                                                  │
│  Vite Dev Server (Node, optional)                                │
│    Port 5173                                                     │
│    ├── hot-reloads React during development                      │
│    ├── proxies /api, /artifact, /socket.io → Flask:5005          │
│    └── Use for dev, Flask built dist/ for production             │
└─────────────────────────────────────────────────────────────────┘
```

### Data Flow: Render → Browser

```
1. CLI renders composition → saves PNG/MP4 to output/
2. CLI writes run metadata to SQLite DB + JSONL
3. User opens browser dashboard
4. React fetches /api/runs → list of completed renders
5. User selects a run → React fetches /api/run/<id> → full detail
6. React displays:
   - Video mode: <video src="/artifact?path=output/video.mp4&token=...">
   - Frame mode: <img src="/artifact?path=output/video_frame.png&token=...">
   - Metrics, charts, node/table data from API response
```

---

## 2. Rendering — CLI Commands

### Render a Single Frame to PNG

```bash
./build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli render \
    MinimalistFocusQuote \
    --frames 0 \
    -o output/focus_quote_frame.png
```

- `--frames N` — single frame number
- `-o path` — output file path
- `--report` — **required** for the run to appear in the telemetry dashboard

### Render a Video to MP4

```bash
./build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli video \
    MinimalistFocusQuote \
    -o output/focus_quote.mp4
```

- `--start N` — start frame (default 0)
- `--end N` — end frame (exclusive, default = composition duration)
- `--fps N` — frame rate (default 30)
- `--keep-frames` — keep individual frame PNGs in temp directory
- `--frames-dir PATH` — override temp frames directory

> **Important:** The `video` command always writes telemetry data to the DB automatically.
> The `render` command needs `--report` to write telemetry data.

### The Frame PNG Convention

When the user clicks "Frame" in the dashboard preview panel, the frontend looks for a PNG
file named after the video output:

```
output/focus_quote.mp4      → output/focus_quote_frame.png
output/MyComposition.mp4    → output/MyComposition_frame.png
```

**You must generate this PNG manually** by rendering a single frame:

```bash
# 1. Render video
./build/.../chronon3d_cli video MinimalistFocusQuote -o output/focus_quote.mp4

# 2. Render frame PNG for preview
./build/.../chronon3d_cli render MinimalistFocusQuote --frames 0 \
    -o output/focus_quote_frame.png
```

Or use the `--keep-frames` + `--frames-dir` approach and copy the frame manually.

---

## 3. Telemetry Database

### Location

```
~/.chronon3d/telemetry/
├── chronon3d_render_history.sqlite    ← Primary DB (SQLite)
├── render_history.jsonl               ← Append-only JSON backup
└── chronon3d_render_history.sqlite.old ← Automatic backup
```

Override with `CHRONON3D_TELEMETRY_PATH` environment variable.

### Tables

| Table | Content |
|---|---|
| `render_runs` | One row per render (composition, timing, file counts) |
| `render_frames` | Per-frame timing, cache status, dirty area |
| `render_phase_events` | Phase-level timings (Clear, Composite, Transform...) |
| `render_counters` | Named counters (framebuffer stats, IO queue, etc.) |
| `render_node_events` | Per-node render graph execution stats |
| `render_layer_events` | Per-layer stats (visibility, culling, glyphs) |
| `render_cache_events` | Cache hit/miss per node per frame |
| `render_culling_events` | Layer culling decisions |
| `render_text_events` | Text shaping/rasterization metrics |
| `render_image_events` | Image decode/sample metrics |
| `render_tile_events` | Tile-based execution stats |

### Key Column: `output_path`

This is what the dashboard uses to load the preview. It's stored as a **relative path**
from the project root:

```
output/focus_quote.mp4
```

The server resolves this relative to `PROJECT_ROOT/output/` first, and falls back
to `PROJECT_ROOT/` if not found. See [Artifact Serving Flow](#6-artifact-serving-flow).

### JSONL Backup

Every write to SQLite also appends a JSON line to `render_history.jsonl`.
On server startup, the JSONL is merged into the in-memory SQLite cache, so even
if the .sqlite file is corrupted, data is recoverable.

---

## 4. Flask Backend Server

### Entry Point

```
tools/telemetry_dashboard/server.py
```

Usage: `python3 server.py [PORT]` — default 8000, we use **5005**.

### Routes

| Method | Path | Auth | Description |
|---|---|---|---|
| POST | `/api/login` | No | Login with password, returns token |
| POST | `/api/logout` | Header | Invalidate token |
| GET | `/api/runs` | Yes | List all render runs |
| GET | `/api/run/<id>` | Yes | Full detail for one run (frames, phases, counters, nodes, layers...) |
| GET | `/artifact?path=...` | Yes | Serve output file (PNG, MP4, etc.) |
| GET | `/output` | Yes | Gallery of all output PNGs |
| GET | `/output/<file>` | Yes | Single output file |
| GET | `/api/graph/<comp>` | Yes | Generate and serve render graph DOT |
| GET | `/videos` | No | Gallery page of all output MP4s |
| GET | `/*` | No | Serve React SPA (from `frontend/dist/`) |

### Auth Check (`require_auth`)

> **Note:** Authentication is currently **disabled** (no-op). All API routes are open.
> The `require_auth` decorator passes through without checking tokens.
> `POST /api/login` always returns `{"token": "no-auth", "success": true}`.

### Path Resolution (`resolve_artifact_path`)

When the frontend requests `/artifact?path=output/focus_quote.mp4`, the server:

1. Rejects absolute paths (security)
2. Tries resolving relative to `ALLOWED_ARTIFACT_ROOTS` in order:
   - `PROJECT_ROOT/output/` → `project/output/output/focus_quote.mp4` (likely doesn't exist)
   - `PROJECT_ROOT/` → `project/output/focus_quote.mp4` (found! ✓)
3. Returns the file with the correct MIME type (`video/mp4`, `image/png`, etc.)

### Database Watcher (Background Thread)

The server watches for file changes on the SQLite DB and JSONL file every 2 seconds.
When a new run is detected, it emits a `new_run` event via WebSocket so the
dashboard auto-updates.

### Server Startup

```bash
python3 tools/telemetry_dashboard/server.py 5005
```

The default port is **8000**; pass an argument to override (convention is **5005**).

---

## 5. React Frontend

### Tech Stack

- **React 19** + Vite 5
- **apexcharts** — performance charts
- **reactflow** — render graph visualization
- **socket.io-client** — real-time updates
- CSS custom properties for theming (dark theme)

### Directory Structure

```
tools/telemetry_dashboard/frontend/
├── src/
│   ├── App.jsx                         ← Main app (routing, auth, polling)
│   ├── App.css                         ← Global styles
│   ├── main.jsx                        ← Entry point
│   ├── api/
│   │   └── telemetryApi.js             ← API client (fetch, login, artifact URLs)
│   ├── components/
│   │   ├── PreviewPanel.jsx            ← Video/Frame/Heatmap preview
│   │   ├── Sidebar.jsx                 ← Run list sidebar
│   │   ├── TabNavigation.jsx           ← Tab bar (Overview/Layers/Nodes/Graph/Comparison)
│   │   ├── MetricsGrid.jsx             ← Summary metric cards
│   │   ├── FrameChart.jsx              ← Frame timeline bar chart
│   │   ├── FrameBreakdownPanel.jsx     ← Per-frame detail
│   │   ├── PerformanceCharts.jsx       ← Performance over time
│   │   ├── ProfilePanels.jsx           ← Profiling detail
│   │   ├── LayersTable.jsx             ← Layer events table
│   │   ├── NodesTable.jsx              ← Node events table
│   │   ├── RenderGraph.jsx             ← Graph visualization
│   │   └── ComparisonMetrics.jsx       ← Side-by-side comparison
│   ├── data/
│   │   └── constants.js                ← API_BASE, WS_BASE
│   ├── styles/
│   │   └── preview.css                 ← Preview panel styles
│   └── utils/
│       ├── format.jsx                  ← Metric formatting
│       ├── aggregate.js                ← Data aggregation helpers
│       └── clipboard.js               ← Clipboard helper
├── dist/                               ← Built production bundle (served by Flask)
├── vite.config.js                      ← Vite config + dev proxy to Flask:5005
└── package.json
```

### Key State Flow

```
App.jsx
├── isAuthenticated → Login form or Dashboard
├── runs[] → Sidebar (list)
├── selectedRunId → loadRunDetail() → runDetail
├── runDetail.run → PreviewPanel, MetricsGrid, etc.
├── runDetail.frames[] → FrameChart, PerformanceCharts
├── selectedFrame → FrameBreakdownPanel, PreviewPanel (frame mode)
└── comparisonRunId → ComparisonMetrics
```

### Polling & WebSocket

- Polls `/api/runs` every **3 seconds** for new runs
- WebSocket `new_run` event triggers immediate refresh
- Both detect `AuthError` → redirect to login

### PreviewPanel Modes

| Mode | Element | Source | Error Message |
|---|---|---|---|
| `video` | `<video>` | `/artifact?path=output/video.mp4&token=...` | "Video preview unavailable" |
| `frame` | `<img>` | `/artifact?path=output/video_frame.png&token=...` | "Frame preview unavailable" |
| `heatmap` | `<img>` + overlay divs | Same as frame + dirty rect overlays | Same as frame |

The frame path is automatically derived from the video path:
- `output/focus_quote.mp4` → `output/focus_quote_frame.png`
- `output/Video_####.png` → `output/Video_0000.png` (with frame number substitution)

---

## 6. Artifact Serving Flow

### Complete Request Trace

When the browser loads a frame preview:

```
1. Browser: <img src="/artifact?path=output/focus_quote_frame.png&token=xyz&v=run_id:0:iso">

2. Vite proxy (port 5173, dev only):
   → forwards to http://127.0.0.1:5005/artifact?path=output/...&token=xyz

3. Flask @require_auth:
   → Checks ?token=xyz → found in auth_tokens set → OK

4. Flask get_artifact():
   → resolve_artifact_path("output/focus_quote_frame.png")
   → Tries PROJECT_ROOT/output/ + "output/focus_quote_frame.png" = doesn't exist
   → Tries PROJECT_ROOT/ + "output/focus_quote_frame.png" = /home/pierone/Pyt/Chronon3d/output/focus_quote_frame.png
   → Exists! Returns file with Content-Type: image/png

5. Browser renders the image ✓
```

### Why the Token Is in the URL

HTML `<img>` and `<video>` elements cannot set custom HTTP headers like
`Authorization: Bearer <token>`. The only way to pass auth for these elements is:
- URL query parameter (`?token=...`)
- Cookies

The current implementation uses query parameters. This is a known security limitation
(token appears in server logs, browser history, Referer headers). For production,
consider switching to signed URLs with expiration (`?expires=...&sig=...`).

---

## 7. Auth Mechanism

> **Authentication is currently disabled.** The server does not validate passwords or tokens.
> `POST /api/login` returns a dummy token (`no-auth`) and the frontend stores it, but
> `require_auth` is a no-op decorator that lets all requests through.
>
> To re-enable auth, restore token-checking logic in `telemetry_server/flask_app.py` and
> require `CHRONON3D_DASHBOARD_PASSWORD` in the login route.

---

## 8. How to Start Everything

> **Port convention:** Use **5005** for production (Flask serves built frontend + API).
> Use **5173** for development (Vite dev server with hot-reload proxies to Flask:5005).

### Quick Start (Production — port 5005)

```bash
# 1. Build the CLI (if not already built)
cmake --preset linux-fast-dev
cmake --build build/chronon/linux-fast-dev -j$(nproc)

# 2. Build the frontend (required after every React change!)
cd tools/telemetry_dashboard/frontend
npx vite build
cd ../../..

# 3. Render something
./build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli video \
    MinimalistFocusQuote -o output/focus_quote.mp4
./build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli render \
    MinimalistFocusQuote --frames 0 -o output/focus_quote_frame.png

# 4. Start Flask server
python3 tools/telemetry_dashboard/server.py 5005 &

# 5. Open browser → http://<server-ip>:5005/
```

> **Important:** After any change to the React source files, you must rebuild the frontend
> (`npx vite build`) for the Flask-served version (port 5005) to reflect the updates.
> The Vite dev server (port 5173) auto-reloads changes immediately.

### Development Mode (with hot-reload — port 5173)

```bash
# Terminal 1: Flask server
python3 tools/telemetry_dashboard/server.py 5005

# Terminal 2: Vite dev server (auto-proxies /api, /artifact, /socket.io to Flask:5005)
cd tools/telemetry_dashboard/frontend
npx vite --port 5173 --host 0.0.0.0

# Open browser → http://<server-ip>:5173/
```

> **Tip:** During development, edit React files and see changes instantly on :5173
> without rebuilding. Only rebuild the `dist/` when deploying to production (:5005).

### Restarting After Changes

```bash
# Kill Flask
lsof -ti:5005 | xargs kill -9

# Rebuild frontend (only needed if React source changed)
cd tools/telemetry_dashboard/frontend && npx vite build && cd ../..

# Restart Flask
nohup python3 tools/telemetry_dashboard/server.py 5005 \
    > /tmp/telemetry_server.log 2>&1 &
```

---

## 9. Troubleshooting Checklist

### "Frame preview unavailable"

1. **Does the frame PNG exist?**
   ```bash
   ls -la output/<composition>_frame.png
   ```
   If not, render it: `./chronon3d_cli render <Comp> --frames 0 -o output/<name>_frame.png`

2. **Does the artifact endpoint work?**
   ```bash
   curl -o /dev/null -w '%{http_code}' \
     'http://localhost:5005/artifact?path=output/<name>_frame.png&token=<TOKEN>'
   ```
   Should return **200**. If **401**, the token in the URL is wrong.
   If **404**, the path resolution is failing.

3. **Is the Flask server running?**
   ```bash
   lsof -i:5005
   ```

4. **Is the frontend looking for the right path?**
   Check PreviewPanel.jsx `resolvedFramePath` logic — it derives the frame PNG
   from the video basename by replacing `.mp4` → `_frame.png`.

5. **Is it a path resolution issue?**
   The DB stores `output_path` as `output/focus_quote.mp4`. The server must be
   able to resolve this relative to `PROJECT_ROOT`. Check `ALLOWED_ARTIFACT_ROOTS`
   in `flask_app.py`.

### "Video preview unavailable"

1. **Does the MP4 file exist?**
   ```bash
   ls -la output/<name>.mp4
   ```

2. **Is the MIME type correct?**
   The server should return `Content-Type: video/mp4`. Check `ARTIFACT_MIME_TYPES`
   in `config.py`.

3. **Is the MP4 file valid?**
   ```bash
   ffprobe output/<name>.mp4
   ```

### "Site unreachable"

1. **Is the server running?**
   ```bash
   ps aux | grep server.py
   ```

2. **Is the port open?**
   ```bash
   lsof -i:5173  # Vite dev
   lsof -i:5005  # Flask
   ```

3. **Restart both if needed.** See [section 8](#8-how-to-start-everything).

### Common Pitfalls

| Symptom | Cause | Fix |
|---|---|---|
| 404 on artifact | Double path nesting (`output/output/`) | `ALLOWED_ARTIFACT_ROOTS` must include `PROJECT_ROOT` |
| "Frame preview unavailable" for video runs | Frontend loads MP4 as image | Frame path derivation logic must replace `.mp4` → `_frame.png` |
| Run not appearing in dashboard | `render` command without `--report` | Add `--report` flag |
| Server won't start on port 5005 | Old process still running | `lsof -ti:5005 \| xargs kill -9` |

---

## 10. File Reference

### Backend (Python)

| File | Purpose |
|---|---|
| `tools/telemetry_dashboard/server.py` | Entry point, parses port arg |
| `telemetry_server/flask_app.py` | Flask app, routes, auth, artifact serving |
| `telemetry_server/handler.py` | Legacy HTTP handler (not used if Flask is running) |
| `telemetry_server/config.py` | Paths, MIME types, DB column definitions |
| `telemetry_server/database.py` | In-memory SQLite cache, JSONL merge |
| `telemetry_server/jsonl_loader.py` | JSONL file reader |

### Frontend (React)

| File | Purpose |
|---|---|
| `frontend/src/App.jsx` | Main app: auth, polling, routing |
| `frontend/src/components/PreviewPanel.jsx` | Video/Frame/Heatmap preview |
| `frontend/src/api/telemetryApi.js` | API client, artifact URL builder |
| `frontend/src/data/constants.js` | API_BASE, WS_BASE |
| `frontend/vite.config.js` | Dev proxy to Flask:5005 |

### Config & Schema

| File | Purpose |
|---|---|
| `frontend/package.json` | Node dependencies (React, apexcharts, reactflow, socket.io) |
| `src/runtime/telemetry/sqlite/telemetry_schema.sql` | Canonical SQLite schema (shared C++/Python) |

---

## Appendix: Complete End-to-End Flow Diagram

```
CLI Render/Video
  │
  ├── 1. Renders frames → saves to output/*.png or pipes to ffmpeg
  │
  ├── 2. Writes to SQLite:
  │      render_runs (1 row)
  │      render_frames (N rows)
  │      render_phase_events (M rows)
  │      render_counters (K rows)
  │      render_node_events (P rows)
  │      render_layer_events (Q rows)
  │      render_cache_events (R rows)
  │
  └── 3. Appends to JSONL:
         render_history.jsonl (append-only log)

Flask Server (running on :5005)
  │
  ├── 4. Database watcher detects file change (poll every 2s)
  ├── 5. Rebuilds in-memory SQLite cache from .sqlite + .jsonl
  ├── 6. Emits WebSocket 'new_run' event to connected browsers
  │
  ├── 7. Serves React SPA (index.html + JS bundle) from dist/
  │
  └── 8. API endpoints:
        GET /api/runs       → JSON array of runs
        GET /api/run/<id>   → Full run detail
        GET /artifact       → Binary file (PNG/MP4)
        GET /output/<file>  → Binary file via gallery

Browser (React SPA)
  │
  ├── 9. Loads App.jsx
  ├── 10. Login → POST /api/login → gets token
  ├── 11. Polls GET /api/runs every 3s
  ├── 12. Listens for WebSocket 'new_run'
  ├── 13. On run select: GET /api/run/<id>
  │
  ├── 14. PreviewPanel:
  │       mode='video': <video src="/artifact?path=output/x.mp4&token=...">
  │       mode='frame': <img src="/artifact?path=output/x_frame.png&token=...">
  │
  └── 15. Charts, tables, metrics rendered from API data
```
