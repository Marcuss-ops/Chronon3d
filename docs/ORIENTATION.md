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

Build presets: `linux-release`, `linux-debug`, `linux-debug-render`.

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

## Telemetry Dashboard & Web Gallery

Il telemetry dashboard registra ogni render eseguito con `--report` e li mostra via web.

### 1. Avviare il server

```bash
# Porta 8000 di default
python3 tools/telemetry_dashboard/server.py
```

### 2. Renderizzare con --report

```bash
# Il flag --report salva il run nel database di telemetria
./build/.../chronon3d_cli render MyComp --frame 0 -o output/test.png --report

# Senza --report il render NON appare nella dashboard!
```

### 3. Visualizzare i risultati

| URL | Cosa mostra |
|---|---|
| `http://localhost:8000/` | Dashboard telemetria (runs dal database SQLite) |
| `http://localhost:8000/output` | Galleria di tutti i PNG renderizzati in `output/` |
| `http://localhost:8000/output/nome.png` | Singolo file PNG |
| `http://localhost:8000/artifact?path=output/nome.png` | Alternativa per scaricare un file |

Nota: la galleria `/output` mostra solo i file PNG nella cartella `output/`, indipendentemente dal flag `--report`.

### 4. Esempio completo

```bash
# 1. Avvia server (in un terminale separato)
python3 tools/telemetry_dashboard/server.py

# 2. Render con report
./build/.../chronon3d_cli render GlowPremiumSuite -o output/glow.png --frames 0 --report

# 3. Apri nel browser
#    http://localhost:8000/        → dashboard
#    http://localhost:8000/output   → galleria immagini
```

---

## Text Rendering: Pixel-Ink Centering

**File**: `src/backends/text/text_rasterizer_render.cpp`

Il testo centrato (`TextAlign::Center`) viene allineato misurando l'inchiostro effettivamente renderizzato, non le metriche del font.

### Problema originale

`FontEngine::measure_text()` (FreeType + HarfBuzz) restituiva larghezze diverse da ciò che Blend2D effettivamente rasterizzava.
Esempio: "CIAO MONDO" misurato ~886px ma renderizzato ~301px — il testo appariva spostato di ~289px.

### Soluzione

Dopo il rendering del testo, viene eseguita una scansione dei pixel per trovare i bound effettivi dell'inchiostro (`ink_left`/`ink_right`/`ink_top`/`ink_bottom`).
La `x_offset` viene poi spostata in modo che il centro dell'inchiostro coincida con il centro del box/frame.

```cpp
// Pseudo-codice del fix:
// 1. Scansiona i pixel renderizzati, trova ink_left e ink_right
// 2. Calcola ink_center = (ink_left + ink_right) * 0.5f
// 3. Calcola shift = box_center - ink_center
// 4. x_offset += shift  // ora l'inchiostro è centrato
```

### Importante per futuri sviluppatori

- Questo fix si applica **sia** al centraggio in box **sia** al centraggio senza box
- I golden test delle immagini vanno rigenerati dopo modifiche a questo codice
- Il fix è nel ramo `t.box.enabled == true` (centraggio box) e nel ramo `else` (centraggio senza box)
- L'offset calcolato viene cacheato come parte del `TextRasterization`, quindi la scansione pixel viene eseguita una sola volta per combinazione stile+testo

---

## V3 Blueprint (IMPROVEMENTS.md)

L'evoluzione V3 del motore è documentata in `IMPROVEMENTS.md` — 10 pillar per passare da frame-based a tile-based rendering:

1. **P5** — Procedural kernels (grid SIMD, ~25× speedup)
2. **P3** — TileMask invalidation (solo tile cambiati)
3. **P1** — TileGrid execution (non più framebuffer full-frame)
4. **P2** — Display list compilation (scene compiled once)
5. ... e altri fino a P10 (per-tile cache, output pipeline separata)
