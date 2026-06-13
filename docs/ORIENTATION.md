# ORIENTATION — Chronon3d (IT)

**Chronon3d** è un motore di motion graphics **code-first**, headless, CPU-only, scritto in C++20.

Componi video scrivendo codice C++ — niente GUI, niente JSON, niente editor di keyframe.

> **Language status / Stato lingua:** Questo documento è in italiano.
> Una guida rapida in inglese per i contributor non-italofoni è in
> `README.md` (root) e `CONTRIBUTING.md` (root).  Per la roadmap
> tecnica, vedi `docs/IMPROVEMENTS.md` e `docs/improvements/00_INDEX.md`.
>
> Nuove sezioni in questa pagina vanno aggiunte in inglese per favorire
> la collaborazione internazionale; le sezioni preesistenti restano
> in italiano come changelog storico.

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

### Windows

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
& "C:\vcpkg\bootstrap-vcpkg.bat" -disableMetrics
$env:VCPKG_ROOT="C:\vcpkg"

cmake --preset win-release
cmake --build --preset win
```

Build output: `build\chronon\win-release\apps\chronon3d_cli\chronon3d_cli.exe`

For debug: use `win-debug` preset. Run commands from the repo root so relative asset paths work.

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
| **`ROADMAP.md`** | Roadmap attiva: item prioritari da implementare |
| **`CHANGELOG.md`** | Cronologia item completati |
| **`V3_BLUEPRINT.md`** | Architettura tile-based: da frame-based a tile-first |
| **`Future Implementations.md`** | Idee speculative, brainstorming, architetture estreme |

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

### Render presets nuovi, senza attrito

Per i preset premium e i test camera usa lo script unico:

```bash
bash tools/render_premium_artifacts.sh
```

Lo script renderizza:

```bash
PremiumThumbnailSaaSBlue
PremiumThumbnailButterySmooth
TextPremiumHeroSaaSBlue
```

e passa già `--report`, quindi i run finiscono nel DB telemetria e compaiono nella dashboard.

Se vuoi fare un singolo render manuale, ricorda sempre:

```bash
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli render PremiumThumbnailSaaSBlue --frame 0 --report -o output/premium_thumbnail_saas_blue.png
```

Senza `--report` il file PNG viene creato ma il run non entra nella telemetry SQLite.

### Render camere, senza attrito

Per i shot camera già registrati usa:

```bash
bash tools/render_camera_artifacts.sh
```

Lo script renderizza e valida:

```bash
OrbitCameraTest
ExtremePerspectiveTest
HeroTextFrontTest
ZStackParallaxTest
```

e genera anche gli overlay diagnostici in:

```bash
output/camera_smoke_overlay/
```

I render finiscono in:

```bash
output/camera_smoke/
```

Il comando usa `--report`, quindi i run entrano nel DB e nella dashboard.

### Validazione camera

Per controllare clipping, safe area e integrità del testo:

```bash
python3 tools/visual_quality_suite.py \
  --executable ./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli \
  --skip-pipeline \
  --camera-template OrbitCameraTest ExtremePerspectiveTest HeroTextFrontTest ZStackParallaxTest \
  --camera-output-dir output/camera_smoke \
  --camera-overlay-dir output/camera_smoke_overlay
```

Il validator produce:

- `text_bbox`
- `content_bbox`
- `safe_area`
- lista `failures` con casi come `title_clipped_left`, `content_outside_safe_area`, `missing_words:*`

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

## V3 Blueprint (V3_BLUEPRINT.md)

L'evoluzione V3 del motore è documentata in [`V3_BLUEPRINT.md`](V3_BLUEPRINT.md) — 10 pillar per passare da frame-based a tile-based rendering:

1. **P5** — Procedural kernels (grid SIMD, ~25× speedup)
2. **P3** — TileMask invalidation (solo tile cambiati)
3. **P1** — TileGrid execution (non più framebuffer full-frame)
4. **P2** — Display list compilation (scene compiled once)
5. ... e altri fino a P10 (per-tile cache, output pipeline separata)

---

## API Changes (June 2026 Refactoring)

### Renderer State Refactoring

The `SoftwareRenderer` internal state has been decomposed into dedicated aggregates. These types live in the new headers under `include/chronon3d/backends/software/`:

| Type | Header | Purpose |
|---|---|---|
| `RendererFrameHistory` | `renderer_types.hpp` | Per-frame camera + fingerprint history for fast-path reuse checks |
| `RendererDirtyTelemetry` | `renderer_types.hpp` | Dirty-rect / tile-execution telemetry counters |
| `RendererLayerHistory` | `renderer_types.hpp` | Per-layer bbox state for frame-to-frame dirty-rect diffing |
| `LayerBBoxState` | `renderer_types.hpp` | Per-layer bounding box + diff state |
| `RendererBufferRing` | `buffer_ring.hpp` | managed ping-pong framebuffer ring (replaces raw `m_ping_fb[]`) |
| `TransformScratchBuffer` | `scratch_buffer.hpp` | managed transform scratch buffer (replaces raw `m_transform_scratch`) |
| `CompiledGraphCache` | `graph_cache.hpp` | managed cached compiled render graph (replaces `m_cached_compiled_graph`) |

**SoftwareRenderer accessors** — `SoftwareRenderer` now exposes `.frame_history()`, `.dirty_telemetry()`, `.layer_history()`, `.buffer_ring()`, `.scratch_buffer()`, and `.graph_cache()` instead of the old direct public members.

**Deleted headers:**
- `include/chronon3d/backends/software/software_renderer_internal.hpp` — removed; migrate includes to the four new headers above.

### Breaking Changes

- **`TextAnchor` is now an `enum class : u8`** (was a struct). Remove `.align` / `.padding` accessors — use `TextStyle::align` and `TextStyle::vertical_align` directly.
- **`SceneBuilder` / `LayerBuilder` includes are no longer transitive.** Add `#include <chronon3d/scene/builders/scene_builder.hpp>` explicitly in your code.
- **`project_layer_2_5d()`** Mat4 overload gains `bool diagnostics_enabled = false` default parameter.

---

## API Overview

### Composition
```cpp
composition({ .name, .width, .height, .duration }, [](const FrameContext& ctx) {
    SceneBuilder s(ctx);
    // ...
    return s.build();
});
```

### Shapes (root level)
```cpp
s.rect("id",         { .size, .color, .pos });
s.rounded_rect("id", { .size, .radius, .color, .pos });
s.circle("id",       { .radius, .color, .pos });
s.line("id",         { .from, .to, .thickness, .color });
s.text("id",         { .content, .style, .pos });
s.image("id",        { .path, .size, .pos, .opacity });
```

### Layers
```cpp
s.layer("name", [](LayerBuilder& l) {
    l.position({x, y, z})
     .scale({sx, sy, 1})
     .rotate({0, 0, deg})
     .opacity(alpha)
     .enable_3d(true)                           // Camera 2.5D
     .mask_rounded_rect({ .size, .radius })     // Mask
     .blur(radius)                              // Effects
     .tint(Color{r, g, b, strength})
     .brightness(value)
     .contrast(value)
     .blend(BlendMode::Screen);

    l.rect(...);
    l.image(...);
    l.text(...);
});
```

### Camera 2.5D
```cpp
s.camera_2_5d({
    .enabled          = true,
    .position         = {cam_x, cam_y, -1000},
    .point_of_interest = {0, 0, 0},
    .zoom             = 1000.0f
});
```
Convention: Z negative = closer (larger), Z positive = farther (smaller).

### Animation
```cpp
auto x  = interpolate(ctx.frame, 0, 60, 0.0f, 800.0f, Easing::InOutCubic);
auto [v, vel] = spring(ctx.frame, target, { .stiffness = 120, .damping = 14 });
```

---

## Coordinate System

- Origin (0, 0) — top-left
- X increases right, Y increases down
- Z negative — closer to camera (Camera 2.5D)
- **Hybrid Coordinates**:
    - **2D Layers**: Use standard top-left screen coordinates.
    - **3D / Projected Layers**: Use centered coordinates (e.g., 0,0 is screen center) to simplify perspective transformations and parallax.
- Shapes are **center-anchored** by default
- Draw order — painter's algorithm (insertion order for 2D, depth-sorted for 3D layers)

---

## CLI Reference

### `render`
```
chronon3d_cli render <Comp> [--frame N] [--start N] [--end N] [-o path]
```
- `--frame N` — render single frame N
- `--start / --end` — render frame range [start, end)
- `-o path` — output path; use `####` for zero-padded frame number
- `--diagnostic` — overlay debug info
- `--report` — save execution report to telemetry database (appears in web dashboard)
- `--warmup-renderer` — preallocate framebuffers and prime caches

### `video`
```
chronon3d_cli video <Comp> --end N -o output.mp4 [options]
```
| Option | Default | Description |
|---|---|---|
| `--start` | 0 | Start frame (inclusive) |
| `--end` | required | End frame (exclusive) |
| `--fps` | 30 | Output frame rate |
| `--crf` | 18 | x264 quality (0=best, 51=worst) |
| `--codec` | auto | Encoder selection |
| `--encode-preset` | medium | x264 preset |
| `--hardware` | none | Hardware encoder |
| `--keep-frames` | off | Keep temporary PNG frames |
| `--frames-dir` | auto | Override temp frames directory |
| `--chunks` | 1 | Render in N parallel chunks |
| `--ffmpeg-mode` | pipe | `pipe` (raw YUV to stdin) or `png` |
| `--dry-run` | off | Validate without rendering |

### `video camera`
```
chronon3d_cli video camera [--axis Tilt|Pan|Roll] [options]
```
| Option | Default | Description |
|---|---|---|
| `--axis` | Tilt | Camera motion axis |
| `--start` | 0 | Start frame |
| `--end` | 60 | End frame |
| `--fps` | 30 | Output frame rate |
| `--ssaa` | 1.0 | Super sampling factor |

### Other commands
```
chronon3d_cli verify           # quick smoke test
chronon3d_cli doctor           # environment diagnostics
chronon3d_cli bench <Comp>     # performance benchmark
chronon3d_cli graph <Comp>     # print frame DAG
```

---

## Dependencies

| Library | Purpose |
|---|---|
| glm | Math (Vec, Mat, Quat) |
| stb_truetype | Font rasterization |
| stb_image | PNG/JPG loading |
| spdlog + fmt | Logging |
| Google Highway | SIMD blending |
| Taskflow | Parallel frame pipeline |
| CLI11 | CLI argument parsing |
| doctest | Tests |
| FreeType + HarfBuzz | Text shaping |
| **ffmpeg** (external) | MP4 encoding — must be in PATH, not linked |
