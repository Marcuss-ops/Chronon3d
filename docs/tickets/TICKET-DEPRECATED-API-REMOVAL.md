# TICKET-DEPRECATED-API-REMOVAL — Remaining deprecated public APIs

## Stato: CLOSED (2026-09-03) — census-based closure

Il ticket storico è stato ristretto il 2026-08-01. Le migrazioni già
atterrate non sono più blocker: `CompositionRegistry::add(name, factory)`,
la mappa `factories_`, gli alias temporali di `LayerBuilder`, gli alias delle
transizioni camera, il percorso `CompositionSpec::assets_root` e il
costruttore deprecato di `ShapedGlyphLine` non esistono più sul `main` attuale.
In questa tranche sono stati rimossi anche `ShapedGlyphLine::try_shape`,
`measure_text_width` e `layout_glyphs`: i chiamanti residui erano soltanto
test di equivalenza e sono stati portati sulla primitiva `shape_glyph_line`.
Anche la soppressione globale `-Wno-error=deprecated-declarations` è assente.

## Chiusura 2026-09-03 — censimento per area

Ogni voce del backlog è stata verificata con un caller census su
`include/`, `src/`, `apps/`, `tests/`, `tools/` + controllo della baseline
ABI `tools/sdk/chronon3d_c.abi`:

| Simbolo | Esito |
|---|---|
| `SoftwareRenderer(Config)` | già rimosso: resta solo il ctor canonico `(RenderRuntime&, Config)`. |
| `GraphExecutor::execute` senza `ExecutionScope` | già rimosso: firma unica `execute(compiled, ctx, scope, scheduler)`. |
| Preset testuali senza `CanvasInfo` | già rimossi: zero caller senza canvas. |
| `authoring::{Scene,Layer}::context()`, `Layer::configure_core(...)` | assenti dall'albero (già rimossi). |
| `SceneBuilder`/`Layer::local_frame(...)` adapter | assenti dall'albero (già rimossi). |
| `TileExecutionPolicy` | RIMOSSO in questa tranche: alias di `ExecutionResolver` con un solo caller di test (static_assert tautologico); nessun simbolo ABI. |
| `Camera2_5DProjectionMode` + campi `projection_mode` write-only | RIMOSSI in questa tranche: zero lettori, zero caller esterni, nessun simbolo ABI; `CameraOpticsMode` è l'unica authority. |
| `render_scene`/`debug_render_graph`/`debug_graph`/`render_engine::render_scene` | RETAINED (ABI required): simboli definiti nella baseline; il percorso produttivo è `render_compiled`/`render`. |
| `materialize_text_run_shape` | RETAINED (ABI required): simbolo nella baseline, zero call-site in-tree, delega a `materialize_prepared_text`. |
| `compile_composition(const CompositionDefinition&, …)` | RETAINED (ABI required): caller produttivi in `render_plan_compiler_scene.inc` + test C-ABI. |

## Criteri residui

- Nessuno: tutte le aree sono chiuse o classificate con evidence nel
  registro `docs/NAMING_COMPATIBILITY_DEBT.md`.
- Le sole deprecazioni residue sono marker documentali su nomi ABI-required
  (`gpu_text_atlas_cache` field name, `materialize_text_run_shape`) che
  non possono essere rimossi finché la baseline ABI `chronon3d_c.abi`
  li richiede.

## Evidenza di chiusura parziale

L'assenza dei simboli legacy di composition, timing, camera, glyph shaping e
font fallback è
verificabile con `rg` sui path `include/`, `src/`, `content/`, `apps/` e
`tests/`. I dettagli storici dei singoli refactor restano nelle schede
specifiche e non fanno più parte dell'inventario operativo di questo ticket.
