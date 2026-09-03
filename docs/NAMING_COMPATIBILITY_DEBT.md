# Naming and compatibility debt register

Classification is evidence-based. A path is removed only when there are no
production callers, no supported consumer includes, no ABI symbol, and no
required test contract.

| Item | Classification | Evidence / action |
|---|---|---|
| `chronon3d::Composition` | ABI required / canonical | Public model used by authoring, registry and runtime; retain. |
| `CompositionDefinition` | deprecated compatibility alias | Existing ABI baseline and tests reference the name; alias to `Composition`, no second storage model. |
| `CompiledComposition::definition` | ABI/source compatibility adapter | Existing runtime/CLI/C ABI consumers use it; retain as a view to `composition`, migrate callers progressively. |
| `CompiledComposition::composition` | canonical runtime storage | New code must use it; owns the canonical `Composition` snapshot. |
| `compile_composition(CompositionDefinition, ...)` | deprecated adapter | Boundary-only forwarding overload; no independent compiler logic. |
| `TileExecutionPolicy` | RETIRED 2026-09-03 | Naming alias of `ExecutionResolver`. Exit condition reached: caller census found exactly one test caller (`test_frame_delta_compiler.cpp` static_assert, itself tautological); alias and assert removed; no ABI symbol existed. |
| `command_plan_executor.cpp` | ABI required compatibility shim | `execute_command_plan` is present in the ABI baseline; retain symbol, no production callers. |
| `command_plan_executor.hpp` under `include/` | dead public path | Removed from public include tree; test declaration lives under `tests/helpers/`. |
| `executor_levels.cpp`, `node_runner.cpp`, `node_executor.cpp` | active helpers | Profiling/call graph showed distinct ownership; do not remove or merge. |
| `docs/CHANGELOG.md` (retired) and old documentation paths | historical/deferred | Not code/API paths; classify per-line in filename drift audit, do not recreate files. | <!-- drift-class: historical -->

## 2026-09-03 migration-debt sweep batch 2

| Item | Classification | Evidence / action |
|---|---|---|
| `Camera2_5DProjectionMode` + write-only `projection_mode` fields on `CameraMotionPath` / `CatmullRomCameraMotion` | RETIRED (dead) | Enum documented as superseded by `CameraOpticsMode`; `Camera2_5D` dropped the field earlier. Census: zero readers of the fields (only default member initializers), zero external enum users, no ABI symbol. Enum + fields removed. |
| `SoftwareRenderer(Config)` legacy ctor | already removed (verified) | Header carries only the canonical `(RenderRuntime&, Config)` ctor; test fixtures construct via that or `test_utils` wrappers. |
| `GraphExecutor::execute(CompiledFrameGraph&)` without `ExecutionScope` | already removed (verified) | Sole API signature is `execute(compiled, ctx, scope, scheduler)` returning `FrameExecutionOutput`. |
| Text presets without `CanvasInfo` | already removed (verified) | All remaining preset callers pass `canvas`; no implicit-canvas overload exists. |
| `authoring::{Scene,Layer}::context()`, `Layer::configure_core()`, `SceneBuilder`/`Layer::local_frame()` adapters | already removed (verified) | `authoring/` contains only `subtitle_track_builder.hpp`; census of `include/` found none of these symbols. |
| `SoftwareRenderer::render_scene` / `debug_render_graph` / `RenderPipeline::render_scene` / `debug_graph` / `RenderEngine::render_scene` | ABI required (retained) | Symbols `_ZN9chronon3d...12render_scene...` and `debug_render_graph` are defined in `tools/sdk/chronon3d_c.abi`; production entry points are `render_compiled` / `render`, deprecated chain is boundary-only. |
| `materialize_text_run_shape` | ABI required alias (retained) | `_ZN9chronon3d26materialize_text_run_shapeE…` defined in ABI baseline; zero in-tree call sites (definition + header decl only); delegates to `materialize_prepared_text`. |
| Filename drift | RESOLVED 2026-09-03 | `tools/check_filename_drift.sh --strict`: 0 operational (was 116 → 103 at sweep start). 57 historical + 2 false-positive findings carry per-line `drift-class:` evidence; no generic allow markers. |

## 2026-09-03 deprecated-API cleansing batch (REMOVED)

Exit condition verified by caller census (no production, test or ABI-baseline
consumer remained):

| Item | Classification | Evidence / action |
|---|---|---|
| `RenderBackend::resolve_shape_processor` / `resolve_effect_processor` | retired virtuals | Removed from the interface; overrides deleted from `SoftwareBackend` and test fakes (`test_render_backend.cpp`, `test_frame_graph_compiler.cpp`). Snapshot path is the only dispatch route. |
| `SoftwareRegistry::get_shape` / `get_effect` (raw-pointer) | retired adapters | Only `get_shape_shared` / `get_effect_shared` remain. |
| `ProcessorRegistrySnapshot::shape()` / `effect()` (raw-pointer) | retired adapters | Only `shape_shared()` / `effect_shared()` + handles remain. |
| Text preset overloads without `CanvasInfo` (`title_preset(text)`, `title_centered(text, font_size)`, …) | retired migration shims | TICKET-SIMPLICITY-PRESETS exit condition reached: zero no-canvas callers. |
| `GpuStyledGlyphCache::acquire(PackedGlyphBitmap…)` (always-false stub) + `PackedGlyphBitmap` / `PackedTextAtlas` | retired phrase-atlas path | The stub always returned `false`; the whole persistent-atlas branch in `gpu_text_run.cpp` was unreachable dead code and is removed. |
| `gpu_text_atlas_cache.hpp` header (`GpuTextAtlasCache` alias) | retired naming alias | Canonical name is `GpuStyledGlyphCache`; all includes swapped. |

## 2026-09-03 reclassification (RETAINED, with reason)

| Item | Classification | Evidence / action |
|---|---|---|
| `RenderGraphContext::gpu_text_atlas_cache` field name | ABI-required naming legacy | Struct is consumed across TUs and telemetry capture; canonical type is `GpuStyledGlyphCache*`. Rename requires an ABI review of the internal struct layout consumers; do not rename casually. |
| `gpu_text_atlas_cache_hits` / `gpu_text_atlas_cache_misses` counters | telemetry wire contract | Emitted as `atlas_cache_hits`/`atlas_cache_misses` keys in the pipe-timing sidecar; renaming changes the observable sidecar schema. Already zero-dead counters. |
| `materialize_text_run_shape` | ABI required alias | `_ZN9chronon3d26materialize_text_run_shapeE…` is a defined symbol in the libabigail baseline (`tools/sdk/chronon3d_c.abi`); delegates to `materialize_prepared_text`. |
| `compile_composition(const CompositionDefinition&, …)` | ABI required boundary overload | Live production callers (`render_plan_compiler_scene.inc`) and C-ABI tests; forwarding-only. |

## Removal rule

Only the dead public `command_plan_executor.hpp` path was removed. The ABI
symbol and implementation remain intentionally. No other candidate met the
removal proof: unused by production is insufficient when ABI or test/source
compatibility still requires the name.
