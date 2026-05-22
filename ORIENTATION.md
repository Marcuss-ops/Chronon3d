# ORIENTATION — Chronon3d Codebase Map

> How to navigate the repo in 5 minutes.

---

## 🗺️ Mappa Rapida

| Directory | Cosa contiene |
|---|---|
| `include/chronon3d/` | **API pubblica**: tutti gli header stabili (scene, math, animation, rendering, core, registry, effects…) |
| `src/` | **Implementazioni**: runtime, render graph, backends (software, text, image, video), composizioni |
| `src/backends/software/` | **Software renderer**: rasterizzatori, compositor, processori effetti, SIMD blending |
| `src/render_graph/` | **Render graph**: DAG builder, executor, caching, profiling, pipeline |
| `src/runtime/` | **Orchestrazione**: pipeline, timeline evaluator, bench runner, warmup, telemetry |
| `src/compositions/` | **Composizioni registrate**: tutte le scene demo, proof, preset (BackgroundGrid, GridTitleMotion, LilDirkClean, ecc.) |
| `apps/chronon3d_cli/` | **CLI**: entry point (`main.cpp`), comandi (`commands/`), utilities (`utils/`) |
| `tests/` | **Test**: unità, integrazione, golden test, performance, CLI |
| `tools/` | **Strumenti**: build scripts, telemetry dashboard (Python + React), compare PNG |
| `docs/` | **Documentazione tecnica**: architettura, render graph, 3D, governance, roadmap |
| `assets/` | **Asset statici**: font (Inter), immagini di riferimento |
| `output/` | **Output generati** — gitignored (frame, video, telemetry.db) |
| `deps/` | **Header-only**: stb libraries (image, truetype, etc.) |
| `build/chronon/` | **Build artifacts** — gitignored (binari compilati) |

---

## ⚡ Quick Start

```bash
# Primo build (vcpkg bootstrap + configure + build — tutto automatico)
bash tools/chronon-linux.sh

# Build incrementale (già configurato)
cmake --preset linux-release
cmake --build build/chronon/linux-release -j$(nproc)

# Build CLI-only (senza test né telemetry SQLite, più veloce per iterare)
cmake --preset linux-debug-render
cmake --build build/chronon/linux-debug-render -j$(nproc)
```

### Eseguire la CLI

```bash
# Lista composizioni disponibili
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli list

# Info su una composizione
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli info GridTitleMotion

# Render singolo frame
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli render BackgroundGrid --frame 0 -o output/test.png

# Render sequenza
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli render BackgroundGrid --start 0 --end 10 -o output/frame_####.png
```

---

## 🧠 Concetti Fondamentali

### Flusso di Rendering

```
Composition (codice C++)     ← l'utente scrive la scena
        │
        ▼
SceneBuilder → Scene         ← risoluzione timeline, animazioni, layout
        │
        ▼
SceneToRenderGraph           ← converte la Scene in un DAG di nodi
        │
        ▼
RenderGraph (DAG)            ← nodi: SourceNode, EffectNode, CompositeNode, ecc.
        │
        ▼
GraphExecutor                ← esegue i nodi, caching per content-hash
        │
        ▼
SoftwareRenderer             ← rasterizzazione CPU con SIMD (Highway)
        │
        ▼
Framebuffer → PNG/EXR/MP4   ← output finale
```

### Componenti Chiave

| Componente | File Principali | Ruolo |
|---|---|---|
| **Composition** | `include/chronon3d/timeline/composition.hpp` | Definisce una scena animabile via codice |
| **Scene** | `include/chronon3d/scene/scene.hpp` | Descrizione statica di cosa disegnare per un frame |
| **SceneBuilder** | `src/scene/scene_builder.cpp` | Builder API fluente per costruire la Scene |
| **LayerBuilder** | `src/scene/layer_builder.cpp` | Builder per layer individuali (forme, testo, immagini, maschere, effetti) |
| **RenderGraph** | `src/render_graph/render_graph.cpp` | DAG di nodi di rendering per un frame |
| **GraphExecutor** | `src/render_graph/graph_executor.cpp` | Esegue il DAG con caching per hash |
| **NodeCache** | `src/cache/node_cache.cpp` | Cache per nodo: evita di ri-renderizzare nodi invariati |
| **FrameCache** | `src/cache/frame_cache.cpp` | Cache cross-frame per precomposizioni e video |
| **SoftwareCompositor** | `src/backends/software/software_compositor.cpp` | Blend di framebuffer con SIMD |
| **Camera 2.5D** | `include/chronon3d/math/camera_2_5d_projection.hpp` | Proiezione prospettica AE-style |
| **Telemetry** | `src/runtime/telemetry/` | Raccolta dati di performance (SQLite) |

---

## 🏗️ Come Registrare una Composizione

```cpp
// my_comp.cpp
#include <chronon3d/chronon3d.hpp>
using namespace chronon3d;

static Composition MyComp() {
    return composition(
        { .name = "MyComp", .width = 1920, .height = 1080, .duration = 60 },
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("bg", { .size = {1920, 1080}, .color = Color::black() });
            return s.build();
        }
    );
}
CHRONON_REGISTER_COMPOSITION("MyComp", MyComp)
```

Dopo il rebuild, `MyComp` appare in `chronon3d_cli list`.

---

## 🧪 Test

```bash
# Eseguire tutti i test
ctest --preset linux-test --output-on-failure

# Eseguire un test specifico
./build/chronon/linux-release/tests/chronon3d_core_tests -tc="GraphExecutor*"

# Suite disponibili:
#   chronon3d_core_tests       — math, geometry, animation, timeline
#   chronon3d_scene_tests      — scene builder, layer hierarchy
#   chronon3d_renderer_tests   — rendering, effects, render graph
#   chronon3d_io_tests         — PNG/EXR output
#   chronon3d_cli_tests        — CLI parsing, job plan
```

### Test helpers condivisi

| File | Uso |
|---|---|
| `tests/helpers/render_fixtures.hpp` | Setup renderer, framebuffer, scene minime |
| `tests/helpers/scene_fixtures.hpp` | Builder di scene di test |
| `tests/helpers/pixel_assertions.hpp` | Assert su pixel specifici |
| `tests/helpers/render_regression.hpp` | Test di regressione visuale |

---

## 📊 Benchmarking

```bash
# Benchmark con report dettagliato
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli bench GridTitleMotion \
    --frames 30 --warmup 5 --dirty-rects --warmup-renderer --json /tmp/bench.json

# Il JSON contiene: FPS, frame times (avg/median/P95), cache hit rate,
# category breakdown (Composite/Transform/Clear/Text), counters, dirty rect stats
```

---

## 📈 Telemetry Dashboard

Il dashboard è composto da due parti:

### Server API (Flask)
```bash
python3 tools/telemetry_dashboard/server.py 5005
```
Legge `output/telemetry.db` (SQLite) ed espone API REST su porta 5005.

### Frontend (React + Vite)
```bash
cd tools/telemetry_dashboard/frontend
npm install
npm run dev    # http://localhost:5173
```
Visualizza run storici, grafici interattivi (frame timeline, category breakdown, node bottlenecks), e modalità compare.

---

## 📁 Dove Trovare le Cose

### "Voglio aggiungere una forma primitiva"
- Header: `include/chronon3d/registry/shape_ids.hpp`, `shape_params.hpp`
- Registro: `src/registry/shape_registry.cpp`
- Rasterizer: `src/backends/software/rasterizers/`
- Processor: `src/backends/software/processors/`
- Test: `tests/renderer/basics/`

### "Voglio aggiungere un effetto"
- Header: `include/chronon3d/effects/`
- Registro: `src/effects/effect_registry.cpp`
- Processor: `src/backends/software/processors/software_effect_processors.cpp`
- Test: `tests/renderer/effects/`, `tests/effects/`

### "Voglio aggiungere una composizione/proof"
- Composizioni: `src/compositions/`
- Registrazione: `CHRONON_REGISTER_COMPOSITION("Nome", funzione)`
- Test golden: `tests/golden/golden_render_tests.cpp`
- Test 2.5D: `tests/renderer/camera/25d/`

### "Voglio capire il sistema di caching"
- `include/chronon3d/cache/` — interfacce pubbliche
- `src/cache/` — implementazioni
- `src/render_graph/graph_executor.cpp` — dove il cache viene usato
- `tests/cache/` — test

### "Voglio debuggare una pipeline di rendering"
- `src/render_graph/render_pipeline.cpp` — orchestratore
- `src/render_graph/graph_profiler.cpp` — profiling nodi
- `src/backends/software/software_renderer.cpp` — entry point backend
- CLI: `chronon3d_cli graph <comp> --frame 0` stampa il DAG

---

## 🔑 Regole Architetturali

### Cosa NON fare

| ❌ | ✅ Invece |
|---|---|
| Mettere logica CLI nel renderer | `apps/chronon3d_cli/` solo per interfaccia utente |
| Includere header video nel core | Forward declaration o header separati |
| Test che ricreano l'engine da zero | Usare `tests/helpers/` e API pubbliche |
| Duplicare logica di proiezione | Usare `project_layer_2_5d()` condiviso |
| Aggiungere proof solo estetiche | Ogni proof deve validare una feature del core |

### Mantra

> **core only builds, adapters only call, data only flows in.**
>
> *Core definisce il dominio. Runtime orchestra il rendering.*
> *Backend rasterizzano, decodificano, codificano.*
> *CLI traduce comandi umani. Tools non diventano mai architettura.*

---

## 📚 Documentazione di Riferimento

| Documento | Contenuto |
|---|---|
| `ARCHITECTURE.md` | Design ad alto livello, principi, decisioni architetturali |
| `BUILDING.md` | Setup build, troubleshooting, dipendenze |
| `README.md` | Panoramica progetto, API, esempi codice |
| `docs/RENDER_GRAPH.md` | Specifica del render graph (DAG, nodi, caching) |
| `docs/3d_subsystem.md` | Sistema 3D/2.5D: coordinate, camera, proiezione, luci |
| `docs/GOVERNANCE.md` | Standard di codice, checklist pre-merge, filosofia test |
| `docs/ROADMAP.md` | Stato attuale, feature completate, piani futuri |
| `docs/UNIFIED_ORCHESTRATION.md` | Regola: ogni capability può consumare ogni altra capability |
| `docs/PROOFS_POLICY.md` | Cosa può (e non può) diventare una proof |
| `CHANGELOG.md` | Cronologia modifiche |

---

## 🔄 Workflow Comuni

### Iterare su una composizione
```bash
# 1. Modifica src/compositions/...
# 2. Build rapido (solo CLI)
cmake --build build/chronon/linux-debug-render -j$(nproc)
# 3. Render di prova
./build/chronon/linux-debug-render/apps/chronon3d_cli/chronon3d_cli render MyComp --frame 0 -o /tmp/test.png
# 4. Controlla /tmp/test.png
```

### Eseguire benchmark prima/dopo una modifica
```bash
# Prima
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli bench GridTitleMotion --frames 30 --json /tmp/before.json

# Dopo la modifica
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli bench GridTitleMotion --frames 30 --json /tmp/after.json

# Confronta i JSON
python3 -c "
import json
for f in ['/tmp/before.json','/tmp/after.json']:
    d=json.load(open(f))
    print(f'{f}: FPS={d.get(\"effective_fps\",\"?\")}, avg={d.get(\"avg_frame_ms\",\"?\")}ms')
"
```

### Avviare il telemetry dashboard
```bash
# Terminale 1: API server
python3 tools/telemetry_dashboard/server.py 5005

# Terminale 2: Frontend
cd tools/telemetry_dashboard/frontend && npm run dev

# Apri http://localhost:5173
```

### Eseguire test con un pattern specifico
```bash
# Solo test della cache
./build/chronon/linux-release/tests/chronon3d_core_tests -tc="*cache*"

# Solo test 2.5D
./build/chronon/linux-release/tests/chronon3d_renderer_tests -tc="*2.5d*"
```
