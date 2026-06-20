# Architecture Evolution Plan — Chronon3d

> **Status:** Questo documento è il punto di ingresso per l'architettura del motore.
> Sostituisce i vecchi piani di modularizzazione pre-riorganizzazione e
> consolida la struttura corrente.
>
> **TL;DR:** Il motore è passato da `SoftwareRenderer` monolitico a una
> pipeline modulare: `Composition → Scene → RenderGraph (DAG) → 
> GraphExecutor → SoftwareRenderer → Framebuffer → output`.
> La V3 introdurrà rendering tile-based.

---

## Architettura Corrente

Il flusso di rendering principale:

```
Composition (C++) → Scene → RenderGraph (DAG) 
  → GraphBuildPipeline → GraphExecutor → SoftwareRenderer → Framebuffer → PNG/MP4
```

### Componenti chiave

| Layer | Ruolo | Collocazione |
|---|---|---|
| **Composition** | Definisce scena animabile in C++ | `content/` |
| **SceneBuilder** | API fluente per layer, shapes, camera, effetti | `include/chronon3d/scene/builders/` |
| **RenderGraph** | DAG di nodi di rendering per frame | `include/chronon3d/render_graph/` |
| **GraphBuildPipeline** | Pass di costruzione (resolve, source, layer, lighting, output) | `src/render_graph/builder/passes/` |
| **GraphExecutor** | Esecuzione DAG con caching + dirty rect | `include/chronon3d/render_graph/executor/` |
| **SoftwareRenderer** | Rasterizzazione CPU con SIMD (Highway) | `include/chronon3d/backends/software/` |

### Fasi di Modularizzazione Completate

Il motore è stato riorganizzato in fasi successive:

| Fase | Cosa | Documentazione |
|---|---|---|
| **Fase 1-2** | Scomposizione di `SoftwareRenderer`: `RendererFrameHistory`, `RendererDirtyTelemetry`, `RendererLayerHistory`, `RendererBufferRing`, `TransformScratchBuffer`, `CompiledGraphCache` | `include/chronon3d/backends/software/renderer_types.hpp`, `buffer_ring.hpp`, `scratch_buffer.hpp`, `graph_cache.hpp` |
| **Fase 3-4** | Pipeline builder: `GraphBuildPipeline` con pass indipendenti (resolve, source, layer, lighting, output, validation) | `src/render_graph/builder/passes/` |
| **Fase 5** | Sistema di estensione: `ExtensionModule` e `GraphNodeRegistry` | `include/chronon3d/extension/` |
| **Fase 6** | LayerCommand system per motion presets | `include/chronon3d/scene/builders/layer_command.hpp` |
| **Fase 7** | Scene validation con `SceneValidator` | `include/chronon3d/scene/validation/` |
| **Fase 8** | Content Module registration (Minimalist, Text, 2D5) | `content/register_content_modules.cpp` |

---

## Evoluzione Futura: V3 Tile-Based

L'evoluzione V3 è documentata separatamente in **[V3_BLUEPRINT.md](V3_BLUEPRINT.md)**.

Dieci pillar per passare da frame-based a tile-based rendering:

1. **P1** — TileGrid execution (tile 256×256 invece di framebuffer full-frame)
2. **P2** — Display list compilation (scene compiled once, params delta only)
3. **P3** — TileMask invalidation (solo tile cambiati)
4. **P4** — Static/Dynamic separation (tile cache per frame-invariant)
5. **P5** — Procedural kernels (kernel C++/SIMD dedicati per pattern)
6. **P6** — Compositing per tile (solo tile attivi)
7. **P7** — TileSurface cache (tile persistenti copy-on-write)
8. **P8** — Encoder pipeline separata (output asincrono)
9. **P9** — Tile scheduler (parallelismo per-tile)
10. **P10** — Per-tile cache (cache key per nodo + tile + params hash)

**Raccomandazione:** Prima di implementare P1–P10, completare la stabilizzazione
P0 (CI obbligatoria, path documentali corretti, confine V2↔V3 formale).

---

## Ownership & Regole

La struttura di ownership è documentata in **[CORE_OWNERSHIP.md](CORE_OWNERSHIP.md)**:

- **Core Zone** — contratti e invarianti fondamentali (Composition, FrameContext, Scene, Layer, TransformResolver, Camera base, CameraProjection, RenderGraph, RenderGraphNode, GraphExecutor public contract, Framebuffer, CachePolicy, registri canonici)
- **Feature Zone** — lavoro di default (content, effects, render nodes, preset, exporter, media feature, camera preset)
- **Integration Zone** — extension points (extension system, registries, resolvers, samplers, validation)
- **Diagnostics Zone** — telemetria, debug overlay, validator, benchmark, profiling, visual test, calibration e dump
- **Experimental Zone** — V3 tile-first (`experimental/**` o feature branch dedicato)
- **Regole anti-crescita** — anti-duplicazione, regole V3, regole Content, budget di crescita per nuovi moduli
- **Regole per agenti** — come toccare il core, checklist PR

---

## Roadmap Attiva

Gli item prioritari correnti sono in **[ROADMAP.md](ROADMAP.md)**.

Priorità suggerite dall'analisi architetturale:

### P0 — Stabilizzazione
1. ✅ CI su `main` — GitHub Actions: fast tests on PR, full suite on push
2. ✅ Path documentali sincronizzati
3. 🔵 Confine formale fra V2 e V3 — policy documentata in CORE_OWNERSHIP.md, da attuare nel codice
4. Benchmark baseline per 1080p e 4K
5. Conclusione migrazione `VideoSink`
6. 🔵 Core Zone reduction — completata a livello documentale; da verificare nel codice
7. 🔵 Diagnostics isolation — feature flag `CHRONON3D_ENABLE_DIAGNOSTICS` documentato; da implementare nel build system

### P1 — Pulizia architetturale
1. `RenderSession` obbligatoria per frame isolation
2. `GraphExecutor` stateless / session-based
3. Ridurre `SoftwareRenderer` a facciata sottile
4. Dipendenze opzionali (`CHRONON3D_ENABLE_TEXT`, `ENABLE_EXR`, `ENABLE_VIDEO`)
5. 🔵 Cache primitive unification — usare `LruCache` comune per tutte le cache
6. 🔵 Content growth policy — attuare budget di crescita per ogni nuovo modulo

### P2 — Evoluzione tile-first
Migrazione incrementale: `TileLayout + TileMask → Tile-aware pool → 
CompositeNode tile-aware → SourceNode tile-aware → effetti tile-aware → 
per-tile cache → rimozione LegacyNodeAdapter`

**V3 Deletion Map** — ogni componente V3 deve dichiarare il componente V2 che sostituisce, con criterio e milestone di eliminazione (vedi CORE_OWNERSHIP.md §2.2). Nessuna duplicazione V2/V3 permanente.

---

## Riferimenti

| Documento | Contenuto |
|---|---|
| **[V3_BLUEPRINT.md](V3_BLUEPRINT.md)** | Architettura tile-based: 10 pillar |
| **[CORE_OWNERSHIP.md](CORE_OWNERSHIP.md)** | File protetti, regole agenti, ownership |
| **[ROADMAP.md](ROADMAP.md)** | Roadmap attiva: item prioritari |
| **[ORIENTATION.md](ORIENTATION.md)** | Panoramica architettura, build guide, API reference |
| **[CHANGELOG.md](CHANGELOG.md)** | Cronologia item completati |

---

## Experimental Zone — Promozioni recenti (2026-06-20)

Componenti entrate nella **Experimental Zone** (vedi `CORE_OWNERSHIP.md` §1D)
con la corrispondente traccia di lifecycle, registrata qui il 2026-06-20
per evitare che i reviewer perdano tempo riaprendo TICKET già documentati.

### `expressions/v2` — attualmente su `main`

| Campo | Valore |
|---|---|
| **Stato** | 🧪 Sperimentale — merged su `main`, non ancora promosso a feature stabile |
| **Dove su main** | `include/chronon3d/expressions/v2/` (header pubblici) + `src/expressions/v2/` (sorgenti + target `chronon3d_expressions_v2`) + `tests/expressions/` (fixture di test, alcune disabilitate) |
| **Perché su main nonostante Experimental** | Merged via PR #23 come pull-request sperimentale; non ancora passato al vaglio della fase di promozione |
| **Provenance** | `CHANGELOG.md` → "Expression System v2 — Lifecycle" |
| **Promotion blockers** | TICKET-003 (typo `<chrono3d/...>` nel lexer v2), TICKET-004 (regression `PUBLIC ${CMAKE_SOURCE_DIR}` sul target `chronon3d_expressions_v2`) |
| **Storia del flag CMake** | `CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2` ritirato in questa sessione; `option(... OFF)` mantenuto a `CMakeLists.txt:233-237` come no-op deprecato per compatibilità della cache-key |

La promozione alla **Feature Zone** richiede i quattro gate elencati in
[`FEATURES.md` → "Expression System v2 — Experimental" → Promotion gates](FEATURES.md#expression-system-v2--experimental).

> Audit di copertura (2026-06-20): nessun riferimento stale al flag in CI,
> preset, script, `.cfg`, `.ini`, `.env`, `Dockerfile*`, `.github/workflows`,
> `vcpkg.json`, `tools/*.sh`, o documenti MD. Uniche menzioni residue del
> flag sono: (a) il blocco di retirement comment in `CMakeLists.txt`,
> intenzionale, e (b) i riferimenti storici in `docs/FOLLOWUP_TICKETS.md`
> (TICKET-003/-004/-005).
