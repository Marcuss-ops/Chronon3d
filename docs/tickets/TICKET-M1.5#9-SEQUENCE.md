# TICKET-M1.5#9-SEQUENCE — progressive emptying of legacy text pipeline

> Stato: **PLANNED** (5 commit atomic series; Step 1 done in working tree, pending wrap_push).
>
> Scope per AGENTS.md v0.1 §"Area minima": **each step = 1 atomic commit on main**, `tools/wrap_push.sh origin main` + doc sync (CURRENT_STATUS + FOLLOWUP_TICKETS + CHANGELOG) nello stesso commit.
>
> Schema pattern precedent: [`docs/tickets/TICKET-M1.5#7-FULL-SPLIT.md`](TICKET-M1.5#7-FULL-SPLIT.md), [`docs/tickets/TICKET-M1.5#8-PRE-EXISTING-ROT.md`](TICKET-M1.5#8-PRE-EXISTING-ROT.md), [`docs/tickets/TICKET-M1.5#6-TIGHTENING.md`](TICKET-M1.5#6-TIGHTENING.md).

## Sintesi del problema

Pre-M1.5#9, due pipeline text coesistono nel codebase:

1. **Legacy** — `TextShape` (struct in `include/chronon3d/scene/model/shape/shape.hpp:238`, variant<11> case 6 / `ShapeType::Text = 6`) prodotto da `RenderNodeFactory::text()` e consumato da `SoftwareTextProcessor` (`src/backends/software/processors/text/software_text_processor.cpp:308-LOC`) → `rasterize_text_to_bl_image()` → `BLImage`.
2. **Modern canonico** (M1.5#1..#6 cluster) — `TextRunShape` (`include/chronon3d/text/text_run_shape.hpp`, variant case 14 / `ShapeType::TextRun = 14`) prodotto da `RenderNodeFactory::text_run()` (via `materialize_text_run_shape`) e consumato da `SoftwareBackend::draw_text_run` via `multi_source_node`/`TextRunNode` (orchestrator 4-stage + 5 single-responsibility sub-cpps).

Il pipeline legacy è **obsoleto** ma ancora attivo:
- `builtin_processors.cpp` registra `create_text_processor()` su `ShapeType::Text` (orphan dispatch ma compilando).
- `software_text_processor.cpp` chiama `rasterize_text_to_bl_image()` su `node.shape.text()` (variante 6 → 14 non usate).
- 2 test file `tests/text/test_text_material.cpp` + `tests/text/test_text_cache_key.cpp` continuano a usare `rasterize_text_to_bl_image()` direttamente.
- 4 production callers generano / consumano `TextShape`.

L'obiettivo della sequenza M1.5#9 è **svuotare progressivamente** questa pipeline in **5 commit atomic consecutivi su main**, senza fretta di "ripulire" ma eliminandola passo-passo.

## I 5 step (sequenza obbligatoria)

### Step 1 (1/5) — DONE in working tree (commit pending)

- `tests/install_consumer/main.cpp` line 115-130: migration `c3d::TextShape ts; ts.text = ...` → `c3d::ShapeType::TextRun` + `c3d::TextRunShapeHandle` payload (`modern_shape->layout = nullptr` perché il consumer non possiede un FontEngine al SDK boundary; il `SoftwareRenderer` sorgenta il suo engine interno; se l'asset manca, il renderer logga errore e il TextRun è silently skipped — la `GridBackgroundShape` layer sopra soddisfa la pixel-hash ≥ 5/255).
- `src/backends/software/processors/text/software_text_processor.cpp` line 21-65: inventory comment aggiunto sotto il P1-LEGACY-TEXT-PIPELINE header (preservato verbatim) elencando gli steps 2-5 callsites.

### Step 2 (2/5) — PLANNED (next commit)

- `src/scene/model/render_node_factory.cpp::RenderNodeFactory::text()` line 144-200: la firma pubblica (`RenderNode RenderNodeFactory::text(std::pmr::memory_resource*, std::string, TextSpec)`) resta invariata; internamente delega a `materialize_text_run_shape(p, engine, sample_time)` e popola `node.shape.text_run_shape_handle().value` invece di `node.shape.text()`. `ShapeType::Text` setter cambia a `ShapeType::TextRun`. Engine resolution: aggiungere param esplicito `FontEngine* engine = nullptr` alla signature (default-nullptr → fallback al renderer-side; quando `engine == nullptr`, factory ritorna `RenderNode` con `text_run_shape_handle().value = nullptr` invece di costruire un TextShape).
- 2 test callers aggiornati:
  - `tests/scene/rendering/test_render_node_factory.cpp` line 104, 116: asserzioni `ShapeType::Text` → `ShapeType::TextRun`; accessor `.text()` → `.text_run_shape_handle().value`.
  - `tests/presets/test_presets.cpp`: asserzioni simili sui preset text-driven (al momento producono TextShape).

### Step 3 (3/5) — PLANNED

- `src/backends/software/processors/builtin_processors.cpp` line 22 + 70: drop `create_text_processor()` factory + forward-decl + `registry.register_shape(ShapeType::Text, create_text_processor())` call (quest'ultimo dentro `register_builtin_processors`). Rimuove la registrazione orphan dal dispatch ladder (anche se nessun nuovo chiamante usa `ShapeType::Text` post-Step-2, l'entry è orfana e deve uscire). Il comment block `TODO(P2 — Text pipeline clean-up)` viene rimosso (i due step A/B descritti dal comment sono ora in esecuzione come M1.5#9 Steps 2/3/4/5).

### Step 4 (4/5) — PLANNED

- Delete `src/backends/software/processors/text/` directory tree (7 file):
  - `software_text_processor.cpp` (308 LOC — il consumer legacy)
  - `software_text_effects.cpp` (246 LOC)
  - `text_glow.cpp` (261 LOC)
  - `text_shadow.cpp` (92 LOC)
  - `text_processor_helpers.hpp` (168 LOC)
  - `software_text_effects.hpp` (40 LOC)
  - `text_effects.hpp` (30 LOC)
- Aggiornamenti annessi:
  - `src/backends/software/processors/text/CMakeLists.txt`: rimuove `SOFTWARE_TEXT_PROCESSOR_SOURCES` list (vuota post-deletion).
  - `src/backends/software/processors/CMakeLists.txt`: rimuove `add_subdirectory(text)` reference (directory ora assente).
- L'INVENTORY header aggiunto in Step 1 (line 21-65 di `software_text_processor.cpp`) viene rimosso insieme al file.

### Step 5 (5/5) — PLANNED

- Delete legacy rasterizer infrastructure:
  - `include/chronon3d/backends/text/text_rasterizer_utils.hpp` (declares `rasterize_text_to_bl_image` + `hash_text_style`)
  - `src/backends/text/text_rasterizer_render.cpp` (implementazione del legacy raster pipeline)
  - `src/backends/text/text_rasterizer_cache.cpp` (cache key hashing legacy)
  - `src/backends/text/text_rasterizer_ink.cpp` (estratto da `rasterize_text_to_bl_image`)
  - `include/chronon3d/backends/text/text_layout_engine.hpp` (`TextLayoutEngine` class declaration)
- CMake cleanup: rimuove i 4 cpp da `chronon3d_backend_text` CMakeLists.
- Migration dei 2 test che usano direttamente l'API legacy:
  - **Opzione A (raccomandata):** migrate `tests/text/test_text_material.cpp` + `tests/text/test_text_cache_key.cpp` alla pipeline modern (`draw_text_run` + `TextRunShape` + `TextLayoutCacheKey::digest` instead of `hash_text_style`). NB richiede fixture font `Inter-Bold.ttf` reali perché la pipeline modern non ritorna hash-style deterministic su asset-missing.
  - **Opzione B (fallback):** delete i 2 test (ridondanti dato che `tests/text/test_text_run_driver.cpp` + `tests/text/test_text_run_umbrella_contract.cpp` coprono le invariants della pipeline modern). Scegliamo **B** se i test legacy non hanno coverage unique vs il cluster modern.

## Trade-offs architetturali cross-step

| Trade-off | Decisione | Rationale |
|---|---|---|
| Step 1 sceglie `tests/install_consumer/main.cpp` (STANDALONE consumer) come callsite pilota invece di `RenderNodeFactory::text()` | Mantieni Step 2 per il producer più invasivo | `install_consumer` NON linka agli in-tree target, confina il diff di Step 1 a 1 file; `RenderNodeFactory::text()` richiede update di 2 test callers + signature change |
| `modern_shape->layout = nullptr` in Step 1 invece di popolazione real | Accettabile per il contratto Phase 4 | Il consumer NON possiede `FontEngine`; il renderer-side logica successivamente conduce shaping (asset-missing → silent skip, ok per il pixel-hash via GridBackground) |
| Step 5 Opzione A vs Opzione B per i 2 test legacy | B (delete) salvo coverage gap | Step 5 deve eliminare TUTTI i callsites `rasterize_text_to_bl_image`; Opzione A reintroduce `rasterize_text_to_bl_image` indirettamente; Opzione B è più pulita |
| INVENTORY block in `software_text_processor.cpp` (Step 1) rimane vs ticket file separato (Step 1 review fix) | Ticket file separato `docs/tickets/TICKET-M1.5#9-SEQUENCE.md` (questo file) accanto a INVENTORY comment (ridotto a 3-line breadcrumb) | Ticket file è la canonical source of truth; il comment nel source diventa solo breadcrumb a questo file |

## Vincoli architetturali di esecuzione

- Un commit per step (5 commits totali), direttamente su `main`, **NO branch**;
- `tools/wrap_push.sh origin main` prima di ogni push (GATE-MNT-01 auto-FF + check_main_clean gate);
- Aggiornare `CURRENT_STATUS.md` + `FOLLOWUP_TICKETS.md` + `CHANGELOG.md` nello stesso commit (gate #7 `check_doc_sync.sh`);
- ZERO nuovi singleton / registry / cache / service-locator (regola permanente AGENTS.md);
- ZERO nuove classi pubbliche, ZERO modifiche a `include/chronon3d/` API surface (il legacy rasterizer è AGENTS.md Cat-3 internal-only);
- `tools/check_legacy_text_pipeline.sh` rimarrà PASS (nessuna nuova call a `rasterize_text_to_bl_image` production-side viene introdotta; anzi il numero di call diminuisce monotonamente attraverso gli step).

## Statistiche finali attese (post-Step-5)

| Metric | Pre-M1.5#9 | Post-M1.5#9 (5/5) |
|---|---|---|
| File `src/backends/software/processors/text/*` | 7 file (1145 LOC) | 0 file |
| File in `include/chronon3d/backends/text/` (legacy) | `text_rasterizer_utils.hpp` + `text_layout_engine.hpp` | 0 file (entrambi cancellati) |
| File in `src/backends/text/` (legacy rasterizer) | 4 cpp | 0 file (eccetto `text_render_resources.cpp` che resta) |
| Production callers di `TextShape` | 4 (RenderNodeFactory::text + test_render_node_factory + test_presets + install_consumer) | 0 |
| Production callers di `rasterize_text_to_bl_image` | 1 (`software_text_processor.cpp:108`) | 0 |
| Production callers di `TextLayoutEngine::layout` da SoftwareTextProcessor | 1 | 0 |
| Variants in `Shape` variant<11> | 11 inclusi case 6 `TextShape` | 10 (case 6 rimosso in sub-step opzionale, vedi Step 5 Opzione B) |
| `ShapeType::Text` enum in shape.hpp | presente | rimosso (cfr. Step 5 sub-cleanup) |
| `ShapeType::TextRun` | presente | presente (canonical) |
| `text_node.shape.set_type(ShapeType::TextRun)` references in `tests/install_consumer` | 1 (Step 1) | 1 (mantenuto) |
| Public API symbols change | 0 | 0 (Cat-3 internal-only) |

## Cross-references

- M1.5#1..#6 cluster (`text_run_node` ↔ `text_run_driver` ↔ `text_run.hpp` ↔ `text_run.cpp` ↔ `text_run_builder.cpp` ↔ `text_run_processor.cpp`): la pipeline modern è established canonical dalla cluster pre-M1.5#9.
- AGENTS.md §"Agenti operativo" → "Regole di lavoro": "Ogni nuova feature deve usare il registry, resolver o sampler canonico già esistente."
- AGENTS.md §"Regole di lavoro": "Una piccola PR, mirate, senza mescolare refactor indipendenti." — i 5 step rispettano.
- Schema ticket file: vedi `docs/DOCUMENTATION_GOVERNANCE.md` §"Scheda ticket specifico".
