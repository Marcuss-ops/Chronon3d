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

- **Core Zone** — file protetti (render graph, scene model, executor)
- **Feature Zone** — lavoro di default (content, effects, nodes, CLI)
- **Integration Zone** — extension points (extension system, registries, validation)
- **Regole per agenti** — come toccare il core, checklist PR

---

## Roadmap Attiva

Gli item prioritari correnti sono in **[ROADMAP.md](ROADMAP.md)**.

Priorità suggerite dall'analisi architetturale:

### P0 — Stabilizzazione
1. ✅ CI su `main` — GitHub Actions: fast tests on PR, full suite on push
2. ✅ Path documentali sincronizzati
3. Confine formale fra V2 e V3
4. Benchmark baseline per 1080p e 4K
5. Conclusione migrazione `VideoSink`

### P1 — Pulizia architetturale
1. `RenderSession` obbligatoria per frame isolation
2. `GraphExecutor` stateless / session-based
3. Ridurre `SoftwareRenderer` a facciata sottile
4. Dipendenze opzionali (`CHRONON3D_ENABLE_TEXT`, `ENABLE_EXR`, `ENABLE_VIDEO`)

### P2 — Evoluzione tile-first
Migrazione incrementale: `TileLayout + TileMask → Tile-aware pool → 
CompositeNode tile-aware → SourceNode tile-aware → effetti tile-aware → 
per-tile cache → rimozione LegacyNodeAdapter`

---

## Riferimenti

| Documento | Contenuto |
|---|---|
| **[V3_BLUEPRINT.md](V3_BLUEPRINT.md)** | Architettura tile-based: 10 pillar |
| **[CORE_OWNERSHIP.md](CORE_OWNERSHIP.md)** | File protetti, regole agenti, ownership |
| **[ROADMAP.md](ROADMAP.md)** | Roadmap attiva: item prioritari |
| **[ORIENTATION.md](ORIENTATION.md)** | Panoramica architettura, build guide, API reference |
| **[CHANGELOG.md](CHANGELOG.md)** | Cronologia item completati |
| **`docs/archive/`** | Documentazione storica (pre-riorganizzazione) |
