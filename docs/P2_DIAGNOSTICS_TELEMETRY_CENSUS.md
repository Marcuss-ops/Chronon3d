# P2.11 — Diagnostics and telemetry census

Status: repository audit at the P2 cleanup boundary.

## Boundary rule

Diagnostic configuration is resolved before execution. Runtime render, backend,
media and graph hot paths must consume typed/configured state and must not call
`getenv()` to decide whether to collect diagnostics.

`Config::from_env()` is the process/environment boundary for runtime environment
configuration. Direct environment reads below that boundary require an explicit
architecture exception and must never occur per frame, per node, per layer, or
per packet.

## Diagnostic environment census

The current runtime environment surface is centralized in `src/core/config.cpp`.
The observed variables are:

- `CHRONON3D_FB_POOL_BUDGET_MB`
- `CHRONON3D_FB_POOL_MAX_PER_CLASS`
- `CHRONON3D_ENABLE_LRU_EVICTION`
- `CHRONON3D_FB_POOL_CLEAR_POLICY`
- `CHRONON3D_FRAME_CACHE_MB`
- `CHRONON3D_NODE_CACHE_ENTRIES`
- `CHRONON3D_GRAPH_CACHE_MB`
- `CHRONON3D_MAX_CPU_MEMORY_MB`
- `CHRONON3D_MAX_GPU_MEMORY_MB`
- `CHRONON3D_MAX_OUTPUT_MEMORY_MB`
- `CHRONON3D_GPU_DEVICE_ID`
- `CHRONON3D_GPU_MEMORY_BUDGET_MB`
- `CHRONON3D_CPU_WORKERS`
- `CHRONON3D_IO_WORKERS`
- `CHRONON3D_COMPRESS_CPU_LIMIT`
- `CHRONON3D_DAEMON_SOCKET`
- `CHRONON3D_DAEMON_BUILD_COMMAND`
- `CHRONON3D_ASSETS_ROOT`
- `CHRONON3D_WORKERS`
- `CHRONON3D_IO_THREADS`
- `CHRONON3D_BATCH_SIZE`

Repository code-search found no additional runtime `getenv()` consumer outside
that boundary. CMake `$ENV{...}` lookups are configure-time inputs and are not
runtime hot-path lookups.

## Telemetry surfaces and ownership

| Surface | Owner | Scope/lifetime | Authority | Rule |
| --- | --- | --- | --- | --- |
| Render job aggregate / `RenderTelemetryRecord` | render execution | one job | canonical job summary | Stable aggregate facts and derived job metrics only. |
| Per-frame telemetry | render execution | one frame / diagnostic capture | diagnostic sidecar | No second job-level authority; frame samples roll up into the job aggregate only through one owner. |
| Backend stats | selected backend | backend instance / job snapshot | backend-specific facts | GPU/API counters stay owned by the backend and are snapshotted at a boundary. |
| Pipe/native encode timing | media/video execution | one export | media-stage facts | Conversion, queue, encode, mux and backpressure timings stay owned by media execution. |
| Trace | tracing subsystem | trace session | event timeline, not metrics | Trace events never become an alternate numeric metrics database. |
| Legacy node/layer/cache/culling/image event stores | diagnostics | diagnostic build only | sidecar samples | Compiled out of normal runtime; no locks/allocations when diagnostics are disabled. |

## Dead/duplicate telemetry census

`core/telemetry/render_telemetry.hpp` exposed five process-global event stores:
node, layer, cache, culling and image. The stores used sharded mutexes, atomics
and growing vectors. No current repository consumer was found for the
`collect_*` APIs beyond the aggregate `TelemetryBundle` helper itself.

P2.11 therefore makes these stores diagnostic-only at compile time. The public
record/collect function shape is retained for source compatibility, but in a
non-diagnostic build the record functions are no-ops and no stores are
instantiated. This removes dead sidecar work from normal render hot paths while
keeping diagnostic builds available for targeted investigations.

`RenderTelemetryRecord` currently contains historical fine-grained fields that
span framebuffer, graph, video pipe and GPU statistics. They are compatibility
surface, not permission to create new duplicate counters. New measurements must
be added at the owning subsystem first and may be copied into the job summary
only when a named consumer exists.

## Naming rules

Use these suffixes consistently:

- `_count`: integer event/cardinality counter.
- `_bytes`: byte quantity.
- `_ms`: measured duration in milliseconds.
- `_ratio`: unitless ratio in `[0, 1]` unless documented otherwise.
- `_peak`: observed high-water mark.
- `_total`: explicit aggregate over the named scope.

Avoid `time`, `timing`, `stats`, or `metric` in individual field names when the
unit/meaning can be encoded directly. Do not add both `foo_wall_ms` and
`foo_ms` for the same measurement authority.

## Admission rule for new telemetry

A new counter or sidecar field needs all of:

1. an owning subsystem;
2. a precise lifetime/scope;
3. one named consumer (report, decision, alert, benchmark, or test);
4. a unit and reset rule;
5. proof it does not duplicate an existing RenderMetrics/backend/media/trace fact.

Fields with no consumer should be deleted rather than retained "for later".
