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
11. [Live Infrastructure & Dual-Mode Verification](#11-live-infrastructure--dual-mode-verification-2026-07-05)

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
| GET | `/*` | No | Serve React SPA (from `frontend/dist/`) |

### Auth Check (`require_auth`)

The decorator checks **two places** for the auth token:

1. `Authorization: Bearer <token>` header — used by `fetch()` API calls
2. `?token=<token>` query parameter — used by `<img>` and `<video>` tags
   (these HTML elements cannot set custom headers)

If neither has a valid token, returns **401 Unauthorized**.

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

```python
CHRONON3D_DASHBOARD_PASSWORD="yourpassword" python3 server.py 5005
```

The password is read from the `CHRONON3D_DASHBOARD_PASSWORD` env variable.
If not set, login returns 500 with "Dashboard password not configured".

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
   → Tries PROJECT_ROOT/ + "output/focus_quote_frame.png" = <repo-root>/output/focus_quote_frame.png
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

### Login Flow

1. User enters password → `POST /api/login` with `{ password: "..." }`
2. Server validates against `CHRONON3D_DASHBOARD_PASSWORD` env var
3. On success: returns `{ token: "<random-32-bytes>" }`
4. Frontend stores token in `localStorage` as `chronon_auth_token`
5. Token is sent with every request:
   - `fetch()` calls: `Authorization: Bearer <token>` header
   - `<img>/<video>` tags: `?token=<token>` query parameter

### Session Model

- Tokens are stored **in-memory** on the server (`auth_tokens` set)
- Restarting the server invalidates all tokens
- No token expiry currently (tokens live until server restart)
- On 401 response, frontend clears token and shows login form again

### Setting the Password

```bash
export CHRONON3D_DASHBOARD_PASSWORD="my_password"
python3 tools/telemetry_dashboard/server.py 5005
```

---

## 8. How to Start Everything

> **Port convention:** Use **8000** for Flask backend (API), **5173** for Vite dev server (React frontend).
> The Vite dev server proxies `/api`, `/artifact`, and `/socket.io` to Flask on port 8000.

### Preliminary: Check Dependencies

```bash
# 1. Check that npm packages are installed
cd tools/telemetry_dashboard/frontend
ls node_modules/.package-lock.json && echo 'NPM OK' || npm install
cd ../../..

# 2. (Optional) Build frontend dist/ — only needed if you plan to use Flask standalone
cd tools/telemetry_dashboard/frontend && npx vite build && cd ../../..
```

### Quick Start — Remote Access (Proven Working Recipe)

This is the exact recipe used to start the dashboard on `149.56.131.97` (2026-07-05):

```bash
# ─── Terminal 1: Flask backend (API + WebSocket) ───
cd /home/pierone/Pyt/Chronon3d
CHRONON3D_DASHBOARD_PASSWORD="chronon3d_admin" \
    setsid python3 tools/start_dashboard_shim.py 8000 \
    </dev/null >/tmp/flask_backend.log 2>&1 &

# Verify: curl -s -o /dev/null -w '%{http_code}' http://localhost:8000/api/runs
# Expected: 200 (returns JSON array of runs)

# ─── Terminal 2: Vite dev server (React with hot-reload) ───
cd /home/pierone/Pyt/Chronon3d/tools/telemetry_dashboard/frontend
setsid npm run dev </dev/null >/tmp/vite_dev.log 2>&1 &

# Verify: curl -s -o /dev/null -w '%{http_code}' http://localhost:5173/
# Expected: 200
```

> **Why `start_dashboard_shim.py` instead of `server.py` directly?**
> 
> `server.py` binds to `127.0.0.1` (loopback only) by default. The shim auto-patches
> every `host='127.0.0.1'` / `host='localhost'` pattern to `host='0.0.0.0'`, making the
> dashboard reachable from any machine on the network. This is essential for remote access.
> The shim requires `CHRONON3D_DASHBOARD_PASSWORD` to be set — it refuses to bind `0.0.0.0`
> without authentication configured.

### URL di accesso

| Servizio | Porta | URL |
|---|---|---|
| Dashboard (Vite) | 5173 | `http://149.56.131.97:5173/` |
| Flask API | 8000 | `http://149.56.131.97:8000/api/runs` |

> **Password:** `chronon3d_admin`

### Sostituire il tuo IP

```bash
# Trova l'IP della macchina
hostname -I | awk '{print $1}'

# Poi sostituisci 149.56.131.97 con il tuo IP negli URL sopra
```

### Development Mode (with hot-reload)

```bash
# Terminal 1: Flask backend
CHRONON3D_DASHBOARD_PASSWORD="chronon3d_admin" \
    python3 tools/start_dashboard_shim.py 8000

# Terminal 2: Vite dev server (auto-proxies /api, /artifact, /socket.io to Flask:8000)
cd tools/telemetry_dashboard/frontend
npm run dev

# Vite avvia su http://0.0.0.0:5173/
# Apri browser → http://<tuo-ip>:5173/
```

> **Tip:** During development, edit React files and see changes instantly on :5173
> without rebuilding. Only rebuild the `dist/` when deploying to production.

### Setting the Password Permanently

Add to `~/.bashrc` or `~/.profile`:

```bash
export CHRONON3D_DASHBOARD_PASSWORD="chronon3d_admin"
```

Then restart the server. This avoids having to pass it inline every time.

### Fermare e riavviare

```bash
# Kill tutti i processi dashboard
fuser -k 5173/tcp 2>/dev/null   # Vite
fuser -k 8000/tcp 2>/dev/null   # Flask

# Riavviare (dai comandi nel Quick Start sopra)
```

### Verifica stato

```bash
# Controlla che i server siano attivi
curl -s -o /dev/null -w '%{http_code}' http://localhost:5173/ && echo ' VITE OK'
curl -s -o /dev/null -w '%{http_code}' http://localhost:8000/api/runs && echo ' FLASK OK'

# Controlla i processi
ps aux | grep -E 'vite|start_dashboard' | grep -v grep

# Controlla le porte
fuser 5173/tcp
fuser 8000/tcp
```

### Dove sono i dati telemetria

Il DB telemetria si trova in `~/.chronon3d/telemetry/chronon3d_render_history.sqlite`.

```bash
# Controlla il contenuto del DB
sqlite3 ~/.chronon3d/telemetry/chronon3d_render_history.sqlite \
  "SELECT COUNT(*) FROM render_runs;"

# Esempio di output: 1225

# Dimensione del DB
ls -lh ~/.chronon3d/telemetry/chronon3d_render_history.sqlite
# Esempio: 318M
```

La dashboard legge automaticamente da questo path. Se il DB è vuoto (0 run),
la dashboard mostrerà "No runs match your filter". Per popolare il DB:

```bash
# Usa la CLI Chronon3D per renderizzare e popolare la telemetria
./build/.../chronon3d_cli render <CompositionName> --frames 0 -o output/frame.png --report
./build/.../chronon3d_cli video <CompositionName> -o output/video.mp4
```

> **Importante:** Il comando `render` richiede `--report` per scrivere nel DB.
> Il comando `video` scrive sempre automaticamente.

### Golden PNGs Server (separato)

Per servire i golden PNG AE parity (o qualsiasi cartella di immagini):

```bash
cd /home/pierone/Pyt/Chronon3d/tests/golden/ae_parity
setsid python3 -m http.server 8888 --bind 0.0.0.0 \
    </dev/null >/dev/null 2>&1 &

# Accedi: http://149.56.131.97:8888/
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

### "No runs match your filter" / "Failed to load runs"

1. **Il backend Flask sta puntando al DB giusto?**
   ```bash
   curl -s http://localhost:8000/api/runs | python3 -c "import sys,json; d=json.load(sys.stdin); print(f'{len(d)} runs')"
   ```
   Se restituisce `0 runs`, il DB telemetria è vuoto o il backend non lo trova.

2. **Il DB telemetria esiste e ha dati?**
   ```bash
   sqlite3 ~/.chronon3d/telemetry/chronon3d_render_history.sqlite \
     "SELECT COUNT(*) FROM render_runs;"
   ```
   Se restituisce `0`, nessun render è mai stato eseguito con telemetria attiva.

3. **Il Flask backend è effettivamente in esecuzione?**
   ```bash
   curl -s -o /dev/null -w '%{http_code}' http://localhost:8000/api/runs
   ```
   Se diverso da `200`, il backend è spento — riavvialo con `start_dashboard_shim.py`.

4. **La dashboard frontend sta chiamando l'API giusta?**
   La Vite dev server (5173) proxy `/api/*` a Flask (8000). Se Flask è su una porta
   diversa, modifica `vite.config.js`:
   ```js
   proxy: { '/api': 'http://127.0.0.1:8000' }
   ```

### Common Pitfalls

| Symptom | Cause | Fix |
|---|---|---|
| "No runs match your filter" | DB telemetria vuoto o Flask spento | Vedi checklist sopra |
| 401 on artifact load but logged in | Token in query param not recognized | Ensure `require_auth` checks `request.args.get('token')` |
| 404 on artifact | Double path nesting (`output/output/`) | `ALLOWED_ARTIFACT_ROOTS` must include `PROJECT_ROOT` |
| "Frame preview unavailable" for video runs | Frontend loads MP4 as image | Frame path derivation logic must replace `.mp4` → `_frame.png` |
| Run not appearing in dashboard | `render` command without `--report` | Add `--report` flag |
| Server won't start on port | Old process still running | `fuser -k <PORT>/tcp` |
| Backend si spegne dopo qualche minuto | Avviato senza `setsid` o `nohup` | Usa `setsid ... </dev/null >/tmp/log 2>&1 &` |

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

## 11. Live Infrastructure & Dual-Mode Verification (2026-07-05)

> **Status (post-3-commit fix chain):** Dashboard is reachable on **both** Vite dev mode (`:5173`) AND Flask prod mode (`:8000`) React 19 trees. Chromium headless DOM dump with `virtual-time-budget=15000` confirms `<div id="root">` populated with `dashboard-container`/`sidebar`/`CHRONON3D`/`TELEMETRY` markers on both endpoints. Zero JS console errors in chromium stderr.

### 3-Commit Fix Lineage

| SHA | Subject | Scope |
|-----|---------|-------|
| `aad79a47` | `fix(dashboard): /api/* JSON 404 - React SPA fetch unbreaks (cat-1 defensive)` | Flask catch-all wrapper was swallowing unregistered `/api/*` paths with HTML instead of JSON. Added 4-line guard returning JSON 404 with `error.path` info. Surfaced by initial 500-error debugging. |
| `82425250` | `chore(gitignore): exclude dashboard frontend dist/ + node_modules/ (AGENTS anti-artefact)` | After `npm install` + `npm run build`, both `dist/` and `node_modules/` were unregistered in git. AGENTS.md § anti-artefact invariant violation waiting to happen. Belt-and-suspenders (scoped + global). |
| `ba8f6ed2` | `fix(frontend): React ErrorBoundary - surface mount errors instead of empty root` | User-reported `<div id="root">` empty on both Vite dev + Flask prod. New `ErrorBoundary` class component wraps `<App />`; any runtime render error now surfaces as styled monospace dark fallback UI showing `error.message` + `error.stack` + `componentStack`. |

### Live URLs

| URL | Server | Mode | Status |
|-----|--------|------|--------|
| `http://51.222.204.158:5173/` | Vite dev server | Hot-reload React | ✅ `dashboard-container` present, root 852-byte content |
| `http://127.0.0.1:5173/`     | Vite dev server | Same           | ✅ loopback OK |
| `http://51.222.204.158:8000/` | Flask (Gunicorn/stocketio) | Production static dist/ | ✅ `dashboard-container` present, root populated |
| `http://127.0.0.1:8000/`      | Flask                  | Same                 | ✅ loopback OK |

### Verification Pattern (re-runnable)

```bash
# Single chromium headless probe — works for both :5173 and :8000.
mkdir -p $HOME/chromium-recheck
chromium-browser \
  --headless=new --no-sandbox --disable-gpu \
  --user-data-dir=$HOME/chromium-recheck \
  --hide-scrollbars --window-size=1280,800 \
  --virtual-time-budget=15000 \
  --dump-dom \
  http://51.222.204.158:8000/ 2>/tmp/chromium_recheck.log > /tmp/dom_recheck.html

# Smoking-gun metrics:
awk '/<div[^>]*id="root"/,/<\/div>/' /tmp/dom_recheck.html | wc -c
# Expected: > 100 (vs pre-fix = 0)
grep -c 'dashboard-container' /tmp/dom_recheck.html
# Expected: 1 (vs pre-fix = 0)
grep -oE 'CHRONON3D|TELEMETRY|sidebar' /tmp/dom_recheck.html | sort -u
# Expected: all 3 markers visible
```

### `/api/*` JSON contract (preserved by `aad79a47`)

```
GET /              → 200 + index.html (SPA root)
GET /api/runs      → 200 + JSON []  (registered; no DB rows expected on fresh deploy)
GET /api/run/<id>  → 404 + JSON {"error":"Run not found"}  (registered)
GET /api/health    → 404 + JSON {"error":"API endpoint not found","path":"api/health"}  (defensive guard)
GET /api/projects  → 404 + JSON  (defensive guard — bundle never calls it; legacy probe)
POST /api/login    → 200 + JSON {"token":"no-auth","success":true}  (no-op auth)
```

The catch-all `serve_static` returns JSON 404 (NOT HTML) for any unregistered `/api/*` path, so React's `fetch().json()` never throws `SyntaxError` on an unexpected HTML body.

### Why both modes share identical React source

Both endpoints mount the same `App.jsx` tree:
- Vite dev (`:5173`) — source via `/src/main.jsx` + `/src/App.jsx` (HMR). Vite proxies `/api`, `/artifact`, `/socket.io` requests to the Flask backend process.
- Flask prod (`:8000`) — built via `cd tools/telemetry_dashboard/frontend && npm run build`. Serves `dist/index.html` + `dist/assets/index-*.{js,css}`. **Flask IS the API server itself** (no proxy needed — `/api/*` routes are handled by the same process serving the SPA).

The dist/ rebuild is the SYNC mechanism: `aad79a47` source change → rebuild → same React tree on both server types.

### Caveats & forward-only tickets

- **Hard refresh required after first deploy**: Browser may cache a stale empty `<div id="root">` from before the fix chain. `Ctrl+Shift+R` (or DevTools → Network → "Disable cache" + refresh) clears it.
- **ErrorBoundary fallback UI shown if App.jsx throws**: future render-time errors are explicitly visible (not silent). Stop iterating on root cause if needed.
- **Operational follow-ups** (informational, not blocking):
  - `/api/*` bundle-call audit — `grep -oE '/api/[a-z_/-]+' tools/telemetry_dashboard/frontend/dist/assets/*.js` confirms the production bundle only actively calls 3 paths: `/api/runs`, `/api/run/<run_id>`, `/api/graph/<composition_id>`. All 3 are registered handlers in `flask_app.py`. The defensive JSON 404 guard from `aad79a47` is kept as a latent safety net for future bundle additions or direct non-browser probes (not exercised by current SPA traffic but prevents `fetch().json()` SyntaxError regressions).
  - Verified dual-mode parity: Vite dev `:5173` and Flask prod `:8000` mount the same React tree (verified 2026-07-05 via chromium headless DOM dump). Future operators can rely on this for HMR parity during local development.

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
