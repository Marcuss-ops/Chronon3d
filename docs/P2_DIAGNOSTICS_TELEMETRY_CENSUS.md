# P2.11 diagnostics / telemetry census

Status: architecture census and boundary cleanup for P2.11.

## Environment ownership

Environment variables are process-boundary input. `Config::from_environment()` is the canonical parser for runtime configuration; render/runtime/backend code consumes typed/value configuration and must not call `getenv()` again.

### Diagnostic/debug variables owned by `Config`

- `CHRONON_DEBUG_GLOW`
- `CHRONON_DEBUG_DUMP_ALPHA_MASK`
- `CHRONON_DEBUG_DUMP_TEXT_RASTER`
- `CHRONON_DEBUG_TEXT_RASTER`
- `CHRONON_DEBUG_TEXT_BBOX`
- `CHRONON3D_TEXT_CLIP_DEBUG`
- `CHRONON3D_PROJ_DIAG`

These populate `DebugConfig` once. Per-frame render code reads `DebugConfig`; it does not own environment parsing.

### Runtime/config variables already owned by `Config`

Scheduler:

- `CHRONON_PINGPONG_FRAMEBUFFER`
- `CHRONON_PREFETCH`
- `CHRONON_PIP_MODE`
- `CHRONON3D_PIN_MAIN_THREAD`
- `CHRONON3D_SCHEDULER_MODE`
- `CHRONON3D_SCHEDULER_WORKERS`

Cache/memory:

- `CHRONON_FB_POOL_MAX_MB`
- `CHRONON3D_FB_POOL_BUDGET_MB`
- `CHRONON_IMAGE_CACHE_MAX_MB`
- `CHRONON_NODE_CACHE_MAX_MB`
- `CHRONON_GLYPH_ATLAS_MAX_MB`
- `CHRONON_TEXT_CACHE_MAX_MB`
- `CHRONON_SHADOW_CACHE_MAX_MB`
- `CHRONON_GLOW_CACHE_MAX_MB`
- `CHRONON3D_FRAME_CACHE_MAX_ENTRIES`
- `CHRONON3D_VIDEO_FRAME_MAX_ENTRIES`
- `CHRONON3D_CONVERTED_FRAME_CACHE_MAX_BYTES`
- `CHRONON3D_SCENE_PROGRAM_CACHE_MAX_ENTRIES`
- `CHRONON3D_FB_POOL_CLEAR_POLICY`

Paths / execution:

- `CHRONON3D_CLI_ASSETS_ROOT`
- `CHRONON3D_GPU_HOT_PATH_MODE`

Observability, moved fully to the boundary by P2.11:

- `CHRONON3D_TELEMETRY_PATH`
- `CHRONON3D_RUN_ID`
- `HOME` only for resolving the default telemetry directory

`TelemetryManager` receives a `TelemetryRuntimeConfig` containing the already-resolved path/default directory/run-id override. It performs no environment lookup.

### Intentional app-boundary lookups

The CLI executable still reads these directly before runtime construction:

- `CHRONON3D_DEV_CRASH_HANDLER`: application-only opt-in for signal-handler installation.
- `CHRONON3D_THREADS`: legacy total CPU-budget input before `CpuBudget` is constructed.

These are startup boundary reads, not hot-path/runtime reads.

## Telemetry ownership and naming

The following boundaries are authoritative and must not grow overlapping copies:

| Surface | Owner | Meaning |
| --- | --- | --- |
| `RenderTelemetryRecord` | runtime telemetry | persisted/queryable job aggregate and stable reporting surface |
| `FrameTelemetry` | runtime telemetry | one canonical record per frame; render and encoder fill disjoint fields |
| `RenderPhaseTimings` | runtime telemetry | canonical five non-overlapping phases: scene eval, GPU render, GPU readback, encode, disk I/O |
| backend stats | backend implementation | raw backend-local measurements; may feed aggregate telemetry but are not a second job-level metrics model |
| pipe/native timing | media/video producer | raw transport/encoder timing; folded into frame/job telemetry and canonical phases |
| trace | tracing/profiling | event stream with timestamps/spans; never the authority for persisted counters or job aggregates |

Naming rule: producer-specific values retain a producer prefix (`gpu_`, `ffmpeg_`, `native_`, `pipe_`) until they are deliberately folded into one of the canonical phase fields. Canonical phase fields must not be recomputed independently by multiple consumers.

## Counter census policy

`RenderTelemetryRecord` currently carries a large compatibility surface of raw counters, derived metrics and phase timings. The CLI telemetry report consumes a significant subset, and SQLite stores persist additional sidecars/counters that are not printed by the summary.

A field is therefore not classified as dead merely because it is absent from the Markdown summary. Removal requires all three checks:

1. no producer writes the field/event;
2. no telemetry store persists it or schema column depends on it;
3. no query/report/test consumes it.

The repository code-search index is currently unavailable, so P2.11 does not perform speculative field deletion from a zero-result search. Future dead-field removal must name the producer, store/schema and consumer evidence in the deleting commit.

## Sidecar census

The telemetry store API intentionally accepts these sidecars:

- frame records
- phase records
- generic counters
- node events
- layer events
- cache events
- culling events
- image events
- render artifacts

They are persistence inputs, not duplicate `RenderMetrics` authorities. A sidecar may be removed only after its writer, store method/schema and CLI/query consumers are all removed together.

## Hot-path rule

No new `getenv()` is permitted in render loops, graph execution, backend submission, encoder/pipe loops, telemetry record emission, or store writes. Environment access belongs in app/config startup. A runtime subsystem that needs a new diagnostic switch must receive it through an existing typed/value config authority rather than adding a local environment parser.
