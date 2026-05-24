# ORIENTATION — Chronon3d

**Chronon3d** è un motore di motion graphics **code-first**, headless, CPU-only, scritto in C++20.

Componi video scrivendo codice C++ — niente GUI, niente JSON, niente editor di keyframe.

---

## Quick Start

```bash
# Primo build (vcpkg + cmake + build — tutto automatico)
bash tools/chronon-linux.sh

# Build incrementale
cmake --preset linux-release
cmake --build build/chronon/linux-release -j$(nproc)

# Lista composizioni disponibili
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli list

# Render di un frame
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli render BackgroundGrid --frame 0 -o output/test.png

# Video export
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli video MyComp --start 0 --end 90 --fps 30 -o output/video.mp4
```

Build presets: `linux-release`, `linux-debug`, `linux-debug-render`, `win-release`, `win-debug`.

---

## Architettura in 30 secondi

```
Composition (C++) → Scene → RenderGraph (DAG) → GraphExecutor → SoftwareRenderer → Framebuffer → PNG/MP4
```

| Layer | Ruolo |
|---|---|
| **Composition** | Definisce la scena animabile via codice C++ |
| **SceneBuilder** | API fluente per costruire la scena (forme, layer, camera, effetti) |
| **RenderGraph** | DAG di nodi di rendering per un frame (source, effect, composite) |
| **GraphExecutor** | Esegue il DAG con caching per content-hash + dirty rect |
| **SoftwareRenderer** | Rasterizzazione CPU con SIMD (Highway), pooling framebuffer |

---

## Struttura del Repository

| Directory | Cosa contiene |
|---|---|
| `apps/chronon3d_cli/` | CLI entry point e comandi |
| `include/chronon3d/` | API pubblica stabile (scene, math, rendering, core) |
| `src/` | Implementazioni: runtime, render graph, backends, cache, scene, layout |
| `src/backends/software/` | Software renderer, compositor, effetti, SIMD blending |
| `src/render_graph/` | DAG builder, executor, caching, profiling, pipeline |
| `src/runtime/` | Pipeline evaluation, telemetry, bench runner |
| `tests/` | Test unitari, integrazione, golden, performance |
| `tools/` | Build scripts, telemetry dashboard, utility script |
| `assets/` | Font (Inter), immagini di riferimento |
| `docs/` | Documentazione tecnica |

---

## Documentazione

| Documento | Contenuto |
|---|---|
| **`IMPROVEMENTS.md`** | Roadmap dettagliata: ottimizzazioni, V3 Blueprint tile-based, priorità implementazione |
| **`Future Implementations.md`** | Idee speculative, brainstorming, architetture estreme |
| **`CHANGELOG.md`** | Cronologia modifiche del progetto |

---

## Concetti Chiave

- **Code-first**: tutto è codice C++ compilato — niente file di scena JSON/XML
- **Deterministico**: stessi input → stessi pixel (fondamentale per render batch/video)
- **Headless**: nessuna dipendenza GUI — core engine puro
- **CPU-only**: software rendering con SIMD (Google Highway) — nessuna GPU
- **Render Graph**: DAG con caching per content-hash, invalidazione dirty rect
- **Camera 2.5D**: AE-style Classic 3D con Z depth, parallax, prospettiva
- **Framebuffer Pool**: preallocazione con Huge Pages per minimizzare allocazioni runtime

---

## Comandi CLI principali

```bash
chronon3d_cli list                          # composizioni disponibili
chronon3d_cli info <comp>                   # metadata composizione
chronon3d_cli doctor                        # diagnostica ambiente
chronon3d_cli verify                        # smoke test rapido
chronon3d_cli render <comp> --frame N -o p  # render frame singolo
chronon3d_cli video <comp> --end N -o p     # export video via ffmpeg
chronon3d_cli bench <comp> --frames 30      # benchmark performance
chronon3d_cli graph <comp> --frame 0        # stampa DAG del frame
```

---

## Test

```bash
ctest --preset linux-test --output-on-failure
# Oppure direttamente:
./build/chronon/linux-release/tests/chronon3d_core_tests -tc="GraphExecutor*"
```

---

## Telemetry Dashboard

```bash
python3 tools/telemetry_dashboard/server.py 5005      # API server
cd tools/telemetry_dashboard/frontend && npm run dev   # React frontend (http://localhost:5173)
```

---

## V3 Blueprint (IMPROVEMENTS.md)

L'evoluzione V3 del motore è documentata in `IMPROVEMENTS.md` — 10 pillar per passare da frame-based a tile-based rendering:

1. **P5** — Procedural kernels (grid SIMD, ~25× speedup)
2. **P3** — TileMask invalidation (solo tile cambiati)
3. **P1** — TileGrid execution (non più framebuffer full-frame)
4. **P2** — Display list compilation (scene compiled once)
5. ... e altri fino a P10 (per-tile cache, output pipeline separata)
