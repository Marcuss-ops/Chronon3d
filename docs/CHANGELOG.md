## 2026-08-22
### `feat(compiler): has_compiled_recorder + fully_recorded fast-path (Fase D/E)`
  ([TICKET-VIDEO-COMPILER-ARCH-V1](tickets/TICKET-VIDEO-COMPILER-ARCH-V1.md))

`has_compiled_recorder()` virtuale su `RenderGraphNode` (default false,
override true su Source/MultiSource/TextRun/Transform/Composite/Video).
Il compiler setta `CompiledFrameProgram::fully_recorded = true` quando
tutti i nodi reachable hanno il recorder. L'executor attiva un fast-path
shallow-copy che salta `clone_for_node_execution()`.

### `refactor(telemetry): remove dead event-store layer`
  ([TICKET-TELEMETRY-STORE-CONSUMER-AUDIT](tickets/TICKET-TELEMETRY-STORE-CONSUMER-AUDIT.md))

Rimozione fisica del dead layer: `TextTelemetryRecord` + `TileTelemetryRecord`
(zero writer) e relativi store/collect/clear, `TelemetrySession` (collector
morto senza consumer), tabelle SQLite `render_text_events`/
`render_tile_events` + `write_text_events`/`write_tile_events`
sull'interfaccia store. Restano 5 store (node/layer/cache/culling/image) al
servizio del solo consumer SQLite (default OFF). Cronaca in ticket-home.

## 2026-08-20
### `chore(cleanup): drop orphan + duplicate files (post motion_studio sweep)`

A targeted second-pass sweep across `src/`, `apps/`, `content/` and
`include/` removed 13 files (`~60 KB`) that were confirmed dead code with
zero downstream callers. The pre-pass gate
`tools/check_unique_cmake_source_ownership.py` (and the matching
script-side audit) flagged them as never referenced from any CMakeLists
entry and never `#include`d from any project TU.

Deleted (with reason):

- `include/chronon3d/backends/software/renderer_buffer_ring.hpp` +
  `src/backends/software/renderer_buffer_ring.cpp` — class `RendererBufferRing`
  is now defined only in `include/chronon3d/backends/software/buffer_ring.hpp`
  with impl `src/backends/software/buffer_ring.cpp`; the older pair was an
  orphan duplicate with `ensure_capacity(w,h)` superseded by
  `ensure_size(w,h,FrameBufferPool*)`.
- `src/render_graph/pipeline/scene_program_refresh.cpp` — orphan TU, not
  wired into any target; the refresh logic is now part of the live
  pipeline pipeline TUs.
- `include/chronon3d/scene/model/camera/camera_null_rig.hpp` +
  `src/scene/model/camera/camera_null_rig.cpp` — `CameraNullRig` had zero
  consumers outside its own self-references; the chain-of-null-objects
  pattern was replaced by `scene/model/core/scene.cpp` + the canonical
  `camera_null_rig.hpp` was never wired.
- `apps/chronon3d_cli/command_registry.cpp` — file defined
  `register_all_commands()` which is never called anywhere (the actual entry
  point is `register_all_groups()` in `commands/cli_groups.hpp`,
  invoked from `main.cpp:89`). The file also referenced undeclared
  functions like `register_basic_commands` / `register_inspect_commands`,
  so it would not have compiled if added back.
- `apps/chronon3d_cli/commands/video/exporters/pipe_export_loop.cpp` —
  full impl of `run_pipe_export_loop()` duplicating the canonical version
  in `pipe_export_pipeline.cpp:197`. If both were ever linked together the
  build would fail with a multiple-definition error.
- `apps/chronon3d_cli/commands/video/exporters/pipe_export_pool_warmup.cpp` —
  `warmup_text_size_classes(void)` + `warmup_pipe_pool()` duplicates the
  versions defined in `pipe_export_pipeline.cpp:330,360`.
- `content/showcases/minimalist/minimalist_animations.hpp` +
  `content/showcases/minimalist/minimalist_animations.cpp` — leftover from
  the P1 refactor that split the monolithic minimalism catalogue into
  per-preset modules (`minimalist_text_intro/exit/common/registration`
  + `minimalist_image_presets`); this file would conflict if rebuilt.
- `content/showcases/cinematic/cinematic_showcase.hpp` — empty umbrella
  re-exporter with zero `#include` consumers; superseded by the
  `cinematic_text_camera.hpp` umbrella + per-composition headers.
- `apps/chronon3d_cli/commands/dev/text_inspection_json.hpp` +
  `apps/chronon3d_cli/commands/dev/text_inspection_json.cpp` — empty
  façade: `text_inspection_json.hpp` had no `#include` consumers and its
  cpp counterpart was not wired into any CMakeLists; the only mentions were
  ghost-comments in `text_inspection_collector.{hpp,cpp}`. Comments
  updated to reference the abstract "JSON serialisation layer" instead.

No CMakeLists.txt edits required: every deleted file was unreferenced.
The `tools/test_architectural.sh` quarantine-leak sentinel
(`<chronon3d_experimental/...>`) still passes vacuously (no experimental
subtree remains).

## 2026-08-20
### `chore(cleanup): drop abandoned motion_studio + Expressions V2`

Removed 13 `motion_studio` files (5 cpp + 8 hpp — `src/motion_studio/` and
`include/chronon3d/motion_studio/`) that were orphan code: never wired into
any `CMakeLists.txt`, never linked, never consumed from outside the
self-referencing TU set. UiBuilder / LayoutResolver / SvgSceneImporter /
InteractionSimulator / ChartRegistry were left over from an abandoned
declarative-UI effort and now have zero downstream callers.

Also dropped the entire `experimental/expressions/` subtree (Expressions V2
Path-B engine — 8 hpp + 7 cpp + 3 tests + 2 CMakeLists.txt) along with the
gating/scaffolding that was protecting it from the production build:

- root `CMakeLists.txt`: removed the `CHRONON3D_BUILD_EXPERIMENTAL` /
  `CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2` option block and the
  `add_subdirectory(experimental/expressions)` line.
- `src/CMakeLists.txt`: removed the residual comment about the quarantine.
- `CMakePresets.json`: removed include of the deleted
  `cmake/presets/experimental.json`.
- `cmake/presets/release.json`: removed the `linux-experimental-validation`
  configure/build/test presets now that no flag activates an experimental
  subtree.
- `cmake/presets/optimizations.json`: removed the dead
  `CHRONON3D_BUILD_EXPERIMENTAL=OFF` cacheVariables key (the option itself no
  longer exists).
- Deleted `cmake/presets/experimental.json` and the three
  `docs/EXPRESSIONS_V2_*.md` files (API / Pipeline / Promotion spec).
- Refreshed forward-looking doc references in `docs/ORIENTATION.md`,
  `docs/ARCHITECTURE_EVOLUTION_PLAN.md`, `docs/FEATURES.md`.

The `tools/test_architectural.sh` regression-sentinels for
`<chronon3d_experimental/...>` includes and the obsolete
`CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2` flag are intentionally
preserved (vacuous-OK now: any future reintroduction attempt will fail
loudly). Historical references in `docs/MIGRATION_TEXT_SPEC.md` are kept
verbatim as part of the project lineage.

## 2026-08-18
### `feat(verification): canonical render receipt (M6) + release gate`
  ([TICKET-RENDER-RECEIPT-M6](tickets/TICKET-RENDER-RECEIPT-M6.md))

Receipt canonico promosso al layer SDK (`src/verification/` +
`include/chronon3d/verification/render_receipt.hpp`, header pubblico): verifier
granulare (ffprobe/decode/frame_count/codec/pix_fmt/resolution/fps/audio) +
`assets::sha256_file` + `copy_eligible`, riusando content/request digest.
Release gate NOT RUN — blocked sul broker terminal. Cronaca in ticket-home.

## 2026-08-10
### `perf(render): eliminate per-frame depth buffer allocations (DepthBufferPool)`

Nuovo `DepthBufferPool` (session-scoped, bucket-rounded, sempre zeroizzato
sulla porzione usata incluso il grow path) in
`SoftwareSessionResources`. `SoftwareMeshProcessor::draw()` e
`draw_fake_box3d()` ora riusano il buffer dal pool invece di allocare un
`std::vector<float>(width*height)` a ogni frame — lo stesso pattern di
`TransformScratchBuffer`/`dof_depth`. Audit frame loop: anche il clip
path di `mesh_renderer.cpp` passa da `std::vector` a stack array
bounded (`kMaxClipVerts=6`, Sutherland-Hodgman ≤ 5 vertici per 2 piani)
con assert diagnostico, eliminando le allocazioni per-triangolo. Nuova
suite `test_depth_buffer_pool` (7 test case: zeroing, reuse pointer,
grow, bucket rounding, reset, move, stale-data guard sul grow path).
Verificato: saturation report `Allocations/frame: 0`.

## 2026-08-10
### `feat(core): single global concurrency budget (oversubscription prevention)`

`tbb::global_control(max_allowed_parallelism, render_threads)` set in `main.cpp` — unica autorità sul parallelismo condivisa da frames, tiles ed effetti. CpuBudget (render/decode/encode split) guida il budget; ExecutionScheduler::task_arena è limitata a render_threads slots. Nuovo test `chronon3d_core_tests` (3 test case: CpuBudget partition invariants, ExecutionScheduler arena concurrency match, Sequential mode arena(1)). Certificazione: N frame threads NON creano N*T worker TBB perché ogni frame usa la stessa arena TBB capped dal global_control.

## 2026-08-10
### `feat(cli): add benchmark-machine host certification`

## 2026-08-10
### `feat(cli): add saturation report command`

`chronon benchmark --scene <id> --duration <sec> --saturation` produce il
CHRONON3D SATURATION REPORT completo: CPU (modello, logical CPUs, NUMA,
context switches), THROUGHPUT (FPS, P50/P95/P99 frame times), HARDWARE
(cycles/frame, IPC, branch miss, LLC miss via perf stat, N/A senza perf),
MEMORY (allocations, framebuffer copies, full-frame passes, peak RSS),
PARALLELISM (workers, tile size, SIMD) e EFFICIENCY (PASS/FAIL per area).

## 2026-08-10
### `feat(cli): add benchmark-machine host certification`

`chronon benchmark-machine` stampa il banner canonico "Chronon CPU":
modello CPU, logical CPUs, NUMA nodes, SIMD supported/selected (verificato
a runtime via cpuid/hwcap, non assunto dai flag di build) e budget worker
TBB. Landed anche l'implementazione dell'API di detection SIMD
(`detect_cpu_capabilities`, `cpu_isa_name`, `parse_cpu_isa`,
`CpuCapabilities::supports`) che era dichiarata ma non implementata dallo
scaffold ADR-025 (forward-point TICKET-SIMD-REGISTRY-CANONICAL), con nuova
suite unit `chronon3d_cpu_isa_tests`.

## 2026-08-02
### `fix(gates): scan all markdown conflict markers` (`bf413d58`)

Chiuso `TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX`: il gate non controlla più
soltanto `docs/CHANGELOG.md`, ma tutti i Markdown sotto `docs/`, compresi
ticket, roadmap, archive e baseline. Il checkout corrente passa senza marker
di conflitto residui.


---

> Storico precedente archiviato: `docs/CHANGELOG.archive.md` (da 2026-08-02 in poi, 3259 righe).
