# V3 Blueprint: Da Frame-Based a Tile-Based (Massimo Throughput)

> Questo documento descrive la trasformazione architetturale V3: un motore tile-first, con nodi procedurali specializzati, cache persistente per regione, e pipeline di output separata.
>
> **TL;DR:** Il motore attuale è frame-based. Ogni nodo opera su framebuffer full-frame. Con V3, ogni nodo produce lavoro per tile/regione, non per canvas intero. Il compositing diventa merge di tile attivi. I pattern procedurali diventano kernel dedicati. La cache è per-tile, non per-nodo.

---

## Regola Fondamentale V3 — Nessuna Duplicazione Permanente

**Policy:** Ogni componente V3 deve essere una **sostituzione**, non un'alternativa. La coesistenza V2/V3 è tollerata solo durante la migrazione, con un piano di eliminazione esplicito.

Ogni nuovo componente V3 deve dichiarare obbligatoriamente:

1. **Componente V2 sostituito** — file/percorso esatto del codice legacy
2. **Adapter temporaneo necessario** — wrapper che permette la coesistenza durante la transizione
3. **Criterio di rimozione** — condizione oggettiva e misurabile per eliminare il percorso legacy
4. **Test di equivalenza** — test che garantisce output identico V2 ↔ V3
5. **Milestone di eliminazione** — data o milestone entro cui il percorso legacy viene rimosso

**Template dichiarazione:**
```markdown
### Componente V3: <nome>
- **Sostituisce:** <percorso V2>
- **Adapter:** <percorso adapter temporaneo>
- **Criterio rimozione:** <es. "tutti i nodi usano il nuovo percorso">
- **Test equivalenza:** <percorso test>
- **Deadline eliminazione:** <milestone>
```

**Esempio — TileGrid (P1):**
```markdown
### Componente V3: TileGrid
- **Sostituisce:** `shared_ptr<Framebuffer>` full-frame in `RenderGraphNode::execute()`
- **Adapter:** `LegacyNodeAdapter` — wrapper che estrae un framebuffer da TileGrid per nodi legacy
- **Criterio rimozione:** tutti i nodi (`SourceNode`, `CompositeNode`, `EffectNode`, ...) sono tile-aware
- **Test equivalenza:** `tests/render_graph/test_tile_equivalence.cpp`
- **Deadline eliminazione:** Milestone P6 (Compositing per tile)
```

---

> **Grafico dipendenze tra pillar:**
> ```
> P1 (TileGrid) ─┬─→ P6 (Compositing per tile)
>                 ├─→ P10 (Tile cache)
> P3 (TileMask) ──┼─→ P6 (Compositing guidato da mask)
>                 └─→ P4 (Static classification)
> P5 (Procedural) ─→ P6 (Kernel dedicati per tile)
> P2 (DisplayList) ─→ P9 (Scheduler tile-aware)
> P7 (TileSurface) ─→ P8 (Encoder pipeline)
> P9 (Scheduler) ───→ P10 (Tile cache integration)
> ```

---

## Pillar 1 — Tile-First Architecture (non Node-First)

**Problema:** L'esecutore opera per livelli del DAG. Ogni nodo produce/consuma `shared_ptr<Framebuffer>` full-frame. Anche con dirty rect, il framebuffer è allocato per l'intera canvas.

**Soluzione:** Il motore ragiona per tile. Ogni `RenderGraphNode::execute()` riceve una griglia di tile. Ogni tile è un framebuffer piccolo (es. 256×256).

**Dove:** 
- `include/chronon3d/render_graph/render_graph.hpp` — Nuovo `TileGrid` runtime struct
- `include/chronon3d/render_graph/render_graph_node.hpp` — Aggiungere overload `execute()` con `TileGrid&`
- `src/render_graph/executor/graph_executor_phases.cpp` — `execute_single_node()` con tile scheduler

**Strategia di migrazione:**
- Aggiungere `LegacyNodeAdapter`: wrapper che estrae un singolo `shared_ptr<Framebuffer>` da `TileGrid` e lo passa al vecchio `execute()`. Permette nodi tile-aware e legacy di coesistere.
- Migrare ogni nodo individualmente: `SourceNode` primo, poi `CompositeNode`, poi `EffectNode`, ecc.
- Rimuovere `LegacyNodeAdapter` quando tutti i nodi sono tile-aware.

**Struttura:**
```cpp
struct TileId { int tx, ty; };
struct TileGrid {
    int tiles_x, tiles_y, tile_w, tile_h;  // 256×256 default
    std::unordered_map<TileId, shared_ptr<Framebuffer>> tiles;
    shared_ptr<Framebuffer> acquire_tile(TileId id, FramebufferPool& pool);
    void merge_to(Framebuffer& dst, const optional<BBox>& clip);
};
```

**Guadagno stimato:**
- Memoria: framebuffer intermedi 256×256 invece di 1920×1080 → -98% bandwidth per tile non toccati
- Parallelismo: worker pool processa tile indipendenti in parallelo
- Cache: tile statici non vengono mai ri-renderizzati

---

## Pillar 2 — Display List Compilation

**Problema:** La scena viene riconvertita ogni frame da `Scene → RenderGraph` (500+ chiamate `add_node()` e `connect()` per 100 layer).

**Soluzione:** Compilare UNA VOLTA in `CompiledDisplayList` — rappresentazione runtime compatta e immutabile. L'`Evaluator` produce solo parametri aggiornati.

**Dove:**
- `include/chronon3d/render_graph/display_list.hpp` (nuovo) — `CompiledDisplayList`
- `src/render_graph/display_list_compiler.cpp` (nuovo) — Compila `Scene` → `CompiledDisplayList`
- `src/runtime/scene_to_render_graph.cpp` — Punto di integrazione

**Come funziona:**
1. Al primo frame o quando la struttura della scena cambia: `Compiler::compile(scene) → CompiledDisplayList`
2. Ai frame successivi: `Executor::execute(compiled_list, params_delta)` — solo evaluate parametri cambiati
3. Se un layer viene aggiunto/rimosso: ricompila (raro)

**Guadagno stimato:** Build del grafo da 0.5-2ms a ~0ms.

---

## Pillar 3 — Tile Mask Invalidation (non dirty rect)

**Problema:** Il dirty rect è una bbox unica. Se due elementi si muovono in angoli opposti, l'unione copre quasi tutto il frame.

**Soluzione:** `TileMask` (evoluzione di `DirtyRectMask`) che propaga l'impatto dei nodi lungo il grafo. Ogni nodo produce quali tile ha modificato.

**Dove:**
- `include/chronon3d/core/dirty_rect_mask.hpp` — Estendere con `k_max_tiles` dinamico, metodi `propagate()`, `dilate()`, `intersect_with()`
- `src/render_graph/executor/graph_executor_dirty.cpp` — `propagate_tile_mask()`

**Propagazione per livello:**
1. Ogni nodo calcola la `output_tile_mask = propagate_from_inputs(input_masks)`
2. Compositing unisce mask degli input
3. Blur/Bloom: `mask.dilated(radius_in_tiles)` — l'effetto si propaga ai tile vicini
4. Skip completo del nodo se `tile_mask.is_empty()`

**Guadagno stimato:** Renderizza solo i tile effettivamente cambiati (es. 5-10% invece del 60%).

---

## Pillar 4 — Static/Dynamic Separation

**Problema:** La cache è per-nodo, non per-tile. Un nodo statico viene ricacheggiato interamente.

**Soluzione:** Static tile cache, pre-render per frame_invariant=true, persiste nel `PersistentFramebufferStore` per tile.

**Classificazione automatica:**
1. Ogni tile tiene un `frame_last_modified`
2. Se non modificato per 3 frame consecutivi → `is_static = true`
3. Se la camera si muove, tutti i tile diventano `is_static = false`
4. I tile statici salvati via `PersistentFramebufferStore::put()` (CFB3 magic, `.cfb3` files)

**Dove:** `src/cache/persistent_framebuffer_store.cpp` — estendere per tile-level caching

**Guadagno stimato:** Per scene con background statico → solo i tile dell'UI vengono renderizzati.

---

## Pillar 5 — Nodi Analitici / Procedural Kernels

**Problema:** Pattern semplici (griglia, gradiente) renderizzati via sampling bilineare blend2D — 40+ operazioni float per pixel invece di 8.

**Soluzione:** Kernel C++/SIMD dedicato per ogni pattern procedurale.

**Dove:**
- Nuova directory: `src/backends/software/procedural/`
- `src/backends/software/procedural/grid_background.cpp` — `render_grid_tile()` kernel SIMD
- `src/backends/software/procedural/gradient.cpp` — gradiente lineare/radiale
- `include/chronon3d/render_graph/nodes/procedural_source_node.hpp` — nuovo tipo di nodo

**Kernel griglia esempio:**
```cpp
void render_grid_tile(Color* __restrict__ dst, int tile_x, int tile_y,
                       int tile_w, int tile_h, const GridParams& p) {
    for (int y = 0; y < tile_h; ++y) {
        for (int x = 0; x < tile_w; ++x) {
            float gx = (tile_x * tile_w + x + p.offset_x) / p.cell_w;
            float gy = (tile_y * tile_h + y + p.offset_y) / p.cell_h;
            float gx_frac = gx - floorf(gx);
            float gy_frac = gy - floorf(gy);
            bool is_line = (gx_frac < p.line_thick || gy_frac < p.line_thick);
            dst[y * tile_w + x] = is_line ? p.line_color : p.bg_color;
        }
    }
}
```

**Guadagno stimato:** Griglia semplice da ~500μs a ~20μs — **25× speedup**.

---

## Pillar 6 — Compositing Solo Dove Serve

**Problema:** Compositing full-frame o su dirty rect.

**Soluzione:** Compositing per tile attivo — merge di layer che toccano quel tile.

**Dove:** `src/backends/software/software_compositor.cpp` — nuova funzione `composite_tile()`

**Struttura:**
```cpp
void composite_tile(
    Framebuffer& dst_tile,                         // tile 256×256
    std::span<const Framebuffer*> src_tiles,         // tile dallo stesso rect
    std::span<const BlendMode> modes,
    const TileMask& active_mask);

if (!active_mask.is_tile_affected(tx, ty)) continue;  // zero lavoro
```

**Guadagno stimato:** Da O(W×H×layers) a O(tile²×active_tiles). Per scena con UI piccola: ~1-5% dei pixel.

---

## Pillar 7 — Buffer Persistenti e Tile Surfaces

**Problema:** FramebufferPool alloca/dealloca dinamicamente.

**Soluzione:** `TileSurfaceCache` — superfici tile persistenti con copy-on-write. Tile statici condivisi tra layer (write-once-read-many). Solo tile dirty hanno superfici dedicate.

**⚠️ Budget memoria:** 4096 tile × 256×256 × 16 bytes = 4 GB per UNA superficie. Con condivisione read-only, 4 GB bastano per composizioni complesse.

**Dove:**
- `include/chronon3d/cache/tile_surface_cache.hpp` (nuovo) — `TileSurfaceCache`
- `src/cache/tile_surface_cache.cpp` (nuovo)

**Guadagno stimato:** Zero `framebuffer_acquire_ms` per tile surfaces, zero `framebuffer_clear_ms` per tile non dirty.

---

## Pillar 8 — Encoder Pipeline Separata

**Problema:** Conversione YUV nel path di rendering bloccante — il render thread aspetta che la conversione finisca.

**Soluzione:** Output pipeline asincrona. Coda thread-safe (`RenderFrameQueue<TileEncodeJob>`, std::queue + std::mutex). Il render thread fa `enqueue()`, l'encoder thread fa `try_dequeue()`.

**Dove:**
- `include/chronon3d/cli/video/output_pipeline.hpp` (nuovo) — `OutputPipeline`
- `apps/chronon3d_cli/utils/video/output_pipeline.cpp` (nuovo)
- `apps/chronon3d_cli/commands/video/exporters/` — riscrittura su `PipeExportSession`

**Guadagno stimato:** Zero attesa nel render thread, pipeline parallela con encoding del frame successivo.

---

## Pillar 9 — Tile Scheduler

**Problema:** Esecutore parallelizza per livello DAG, non per tile.

**Soluzione:** `tbb::parallel_for` nidificato per tile. Work-stealing automatico di TBB.

**Dove:** `src/render_graph/executor/graph_executor_phases.cpp`

**Approccio V1 (TBB-first):**
```cpp
tbb::parallel_for(
    tbb::blocked_range<int>(0, level.size()),
    [&](auto& range) {
        for (int i = range.begin(); i < range.end(); ++i) {
            auto tile_fn = node.get_tile_executor(ctx);
            tbb::parallel_for(
                tbb::blocked_range<int>(0, active_tiles.size()),
                [&](auto& tr) {
                    for (int ti = tr.begin(); ti < tr.end(); ++ti)
                        tile_fn(active_tiles[ti]);
                }
            );
        }
    }
);
```

**Benchmark:** Misurare overhead TBB a granularità tile. Se > 5% → implementare `TileScheduler` custom. Altrimenti, TBB basta.

**Guadagno stimato:** Da parallelismo per-livello (10-20 job) a parallelismo per-tile (64-4096 job).

---

## Pillar 10 — Per-Tile Cache

**Problema:** Cache per-nodo invalida tutto quando un tile cambia.

**Soluzione:** Cache per-tile con `TileCacheKey` (nodo + tile_id + params_hash).

**Dove:**
- `include/chronon3d/cache/tile_cache.hpp` (nuovo) — `TileCache`
- `src/cache/tile_cache.cpp` (nuovo)
- `src/render_graph/executor/graph_executor_cache.cpp` — `evaluate_cache()` per tile

**Struttura:**
```cpp
struct TileCacheKey {
    u64 node_digest;    // hash del nodo
    TileId tile_id;     // (tx, ty)
    u64 input_hash;     // hash degli input per questo tile
    u64 params_hash;    // hash dei parametri correnti
    Frame frame;        // per frame-dependent tiles
    u64 digest() const; // XXH3 dell'intera key
};
```

**Guadagno stimato:** Hit rate da ~60% (node-level) a ~95% (tile-level). Tile invariato copiato dalla cache in ~5μs vs 500μs per renderizzarlo.

---

## V3 Deletion Map — Tracciamento eliminazione legacy

Questa sezione traccia lo stato di ogni componente V3 e il corrispondente percorso legacy da eliminare.

| Componente V3 | Sostituisce V2 | Adapter | Criterio rimozione | Test equivalenza | Deadline | Stato |
|---|---|---|---|---|---|---|
| TileGrid (P1) | FB full-frame in execute() | LegacyNodeAdapter | Tutti i nodi tile-aware | `test_tile_equivalence.cpp` | P6 | 🔵 Planned |
| DisplayList (P2) | Compilazione per-frame | Scene→RenderGraph translator | Ricompilazione solo su structure change | `test_display_list_equivalence.cpp` | P6 | 🔵 Planned |
| TileMask (P3) | DirtyRect bbox singola | DirtyRectMask wrapper | Tutti i nodi propagano TileMask | `test_tile_mask_equivalence.cpp` | P6 | 🔵 Planned |
| StaticSeparation (P4) | Cache per-nodo | NodeCache wrapper | Classificazione automatica attiva | `test_static_tile_equivalence.cpp` | P10 | 🔵 Planned |
| ProceduralKernels (P5) | Sampling bilineare blend2D | ProceduralSourceNode | Tutti i pattern semplici usano kernel dedicati | `test_procedural_equivalence.cpp` | P6 | 🔵 Planned |
| TileCompositing (P6) | Compositing full-frame | CompositeNode tile-aware | Merge per tile attivo | `test_tile_composite_equivalence.cpp` | P6 | 🔵 Planned |
| TileSurfaceCache (P7) | FramebufferPool dinamico | Pool wrapper | Tile statici condivisi via copy-on-write | `test_tile_surface_equivalence.cpp` | P10 | 🔵 Planned |
| OutputPipeline (P8) | Encoding bloccante | Async encoder queue | Render thread non aspetta encoding | `test_output_pipeline_equivalence.cpp` | P8 | 🔵 Planned |
| TileScheduler (P9) | Parallelismo per-livello | TBB parallel_for nidificato | Work-stealing per tile | `test_tile_scheduler_equivalence.cpp` | P10 | 🔵 Planned |
| TileCache (P10) | Cache per-nodo | NodeCache wrapper | Hit rate ≥ 90% in benchmark | `test_tile_cache_equivalence.cpp` | P10 | 🔵 Planned |
