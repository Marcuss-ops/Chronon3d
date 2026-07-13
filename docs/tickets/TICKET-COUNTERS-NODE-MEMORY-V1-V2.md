# TICKET-COUNTERS-NODE-MEMORY-V1-V2 — Canonical NodeMemoryMetrics + NodeStatsReporter

## Stato: OPEN-APERTURA (2026-07-13, FASE 2.1 first-commit canonical)

## Scope (per user spec verbatim)

> "FASE 2.1 — implementa TICKET-COUNTERS-NODE-MEMORY-V1-V2: integrare
> `NodeMemoryMetrics` struct (8 field `atomic<uint64_t>`) nel solver render
> graph executor + hot-path instrumentation in `node_runner` + `NodeStatsRepo-
> rter` per-session lifecycle verificato.  Same Cat-3 minimal-surface dis-
> cipline; ADR-024-style forward-point se servono nuovi simboli in `include/
> chronon3d/`.  Macchina-verifica DEFERRED-WBH fino a WBH con `chrono3d_cli`
> binary compilato + B03 CinematicGlow1088p scene presente."

This ticket lands the canonical `<chronon3d/graph/NodeMemoryMetrics>` +
`<chrono3d/graph/NodeStatsReporter>` surfaces, replacing the SYNTHETIC STAND-
IN in `tests/perf/test_node_memory_counters_v1.cpp`.  V2 is the V1 follow-up
implementation chore (the V1 forward-point `<a>` from TICKET-PERF-COUNTERS-
NODE-MEMORY-V1).

## Pattern precedent

- `tests/perf/test_node_memory_counters_v1.cpp` — Phase 1 SYNTHETIC STAND-
  IN (locks the 8-name shape via 8 `static_assert`s), the per-session zero-
  static-state invariant, the monotonic accumulation, and the defensive
  B03 CinematicGlow1088p 90-frame gate.
- `include/chronon3d/render_graph/executor/text_bbox_reporter.hpp` —
  header-only inline reporter pattern (forward-decl on
  `render_graph_context.hpp`).
- `include/chronon3d/cache/lru_cache.hpp` — atomic counter snapshot
  pattern (`memory_order_relaxed` load + `.fetch_add`).
- `CHRONON_COUNTERS_GRAPH` macro in
  `include/chronon3d/core/profiling/render_counter_macros.hpp` exists
  for graph-wide totals but does NOT support per-node-keyed aggregation;
  the canonical NodeMemoryMetrics is a parallel dedicated struct.

## 8 named fields (LOCKED, locked by `static_assert` test suite)

| Field | Type | Meaning |
|---|---|---|
| `pixels_read` | `std::atomic<u64>` | per-node pixel-touch accounting (read side) |
| `pixels_written` | `std::atomic<u64>` | per-node pixel-touch accounting (write side) |
| `bytes_read` | `std::atomic<u64>` | per-node byte-touch accounting (read side; bpp × pixels) |
| `bytes_written` | `std::atomic<u64>` | per-node byte-touch accounting (write side) |
| `framebuffer_copies` | `std::atomic<u64>` | count of fallback `acquire_owned_fb(const FB&)` `std::copy` paths |
| `framebuffer_clears` | `std::atomic<u64>` | count of explicit full/partial clears |
| `allocations` | `std::atomic<u64>` | byte-total of FB-pool allocations |
| `temporary_buffers` | `std::atomic<u64>` | count of allocated scratch / ping-pong FBs |

Total = 64 bytes per `NodeMemoryMetrics` struct = exactly one 64-byte
cache line.  Cache-line sized for NO false-sharing in dedicated allocator
slot.

## Per-session NodeStatsReporter — lifecycle invariant

```pseudo-code
// In RenderSession init (forward-pointed to a later chore):
auto stats_reporter = std::make_unique<chronon3d::graph::NodeStatsReporter>();
ctx.node_exec.node_stats_reporter = stats_reporter.get();

// In hot-path execute_single_node (forward-pointed to a later chore):
if (ctx.node_exec.node_stats_reporter) {
    NodeMemoryMetrics delta = /* per-node delta is computed from cache_eval + allocator + clear events */;
    ctx.node_exec.node_stats_reporter->observe_node(node_id, delta);
}

// In RenderSession end-of-life (forward-pointed):
ctx.node_exec.node_stats_reporter->snapshot() → emit --stats-json
```

KEY INVARIANT: **zero static state**.  Each reporter instance has its own
`std::map<std::string, NodeMemoryMetrics>` of per-node accumulators; when
the host `RenderSession` is destroyed, the `std::unique_ptr` releases the
map deterministically.  Verified by `tests/perf/test_node_memory_counters_
v1.cpp` "contract: per-session zero static state" TEST_CASE.

## Hot-path perf discipline (forward-pointed to V2.1)

- ALL atomic ops use `std::memory_order_relaxed` (relaxed = the right
  choice for monotonic counters with no causal-ordering requirement).
- 64-byte cache-line alignment (one cache line per accumulator = zero
  cross-thread false-sharing in dedicated allocator slot).
- `observe_node` does ONE mutex-free std::map find + 8 atomic fetch_add +
  one std::map emplace on first observation.  No locks; no atomics on the
  map traversal (std::map is std::allocator-backed; the map's own thread-
  safety is the consumer's responsibility — typically one writer thread +
  one reader thread, both serialized at the session boundary).

## Criteri di accettazione (8)

1. **NEW canonical header** `include/chronon3d/render_graph/executor/node_memory_metrics.hpp`
   with `chronon3d::graph::NodeMemoryMetrics` + `NodeStatsSnapshot` +
   `NodeStatsReporter`.
2. **8 named `std::atomic<std::uint64_t>` fields** — the EXACT 8 names from
   the synthetic stand-in (locked by 8 `static_assert`s in the test
   bridge).
3. **Test bridge** in `tests/perf/test_node_memory_counters_v1.cpp` —
   replacement of `using synthetic_node_memory_metrics_v1::NodeMemoryMet-
   rics` → `using chronon3d::graph::NodeMemoryMetrics`; the test's existing
   `static_assert` suite continues to PASS without modification.
4. **NodeStatsReporter** has methods `observe_node(const std::string&, const
   NodeMemoryMetrics&)`, `snapshot()` → `std::vector<NodeStatsSnapshot>`,
   `reset()`.  Copy-deleted (lifetime invariant).  Default-constructible.
5. **Forward-decl on `render_graph_context.hpp`** — add `NodeStatsReporter*
   node_stats_reporter{nullptr}` to `NodeExecutionContext` (Cat-2 additive
   field; forward-decl pattern per TextBboxReporter precedent).
6. **NEW ADR-026** (docs/adr/ADR-026-node-memory-metrics.md) — justifies
   new public SDK API per AGENTS.md Cat-2 freeze compliance + parallel
   ADR-024 precedent.
7. **CHANGELOG entry** (prepended, CITA-only) — 1 ticket link only, no
   cronaca per AGENTS.md `## Regole di lint documentale`.
8. **Subject envelope ≤ 72 chars** — verified by gate `tools/check_commit_
   subject_length.sh` forward-pointed.

## Forward-points

### (a) TICKET-COUNTERS-NODE-MEMORY-V1-V2-EXECUTOR-WIRING

On a machine with `chronon3d_cli` binary compilable + `B03 CinematicGlow1088p`
scene available: thread the canonical `NodeStatsReporter` into `RenderSes-
sion` init + `SoftwareRenderer::setup_session()` + verify per-session
zero-static-state contract via REPLAY.  Macchina-verifica DEFERRED-WBH per
`TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` precedent.

### (b) TICKET-COUNTERS-NODE-MEMORY-V1-V2-HOT-PATH

Wire `execute_single_node` to call `ctx.node_exec.node_stats_reporter->
observe_node(node_id, delta)` when the report ptr is non-null + compute
the per-node delta from cache_eval + framebuffer_acquire + clear_node
event types.  Top-3 instrumentation sites per design validation:
- Pixels/Bytes: pre/post `node->execute()` snapshot width/height × bpp
- Allocations: snapshot delta of `thread_local_counters().framebuffer_-
  allocations` pre/post
- Copies/Clears: inspect LRU cache_hit state + fallback paths

### (c) TICKET-COUNTERS-NODE-MEMORY-V1-V2-CLI-EMISSION

Add `chronon3d_cli --stats-json` flag emitting `docs/schemas/chrono3d.stats.
v1.schema.json` payload from `NodeStatsReporter::snapshot()`.  Schema
already declares the per-session per-node aggregate shape.

### (d) TICKET-COUNTERS-NODE-MEMORY-V1-V2-GLOW-KERNEL

CinemáticoGlow1088p smoke-test from the synthesis contract: 90-frame
stream of glow kernel traffic on gpu-down + blur3x3 + threshold mask +
1088p accumulation, all 8 counters > 0.  Macchina-verifica DEFERRED-WBH.

### (e) TICKET-COUNTERS-NODE-MEMORY-V1-V2-SESSION-LIFECYCLE-VERIFY

Per-session zero-static-state REPLAY test: assert `RenderSession A` +
`RenderSession B` (sequential on the SAME runtime) report independent
per-session accumulators without cross-contamination.  Is the parallel
test to the Contract Lock tests in V1's `tests/perf/test_node_memory_
counters_v1.cpp` but with the CANONICAL type.

## Cross-references

| Resource | Path |
|---|---|
| Canonical type | [`../include/chronon3d/render_graph/executor/node_memory_metrics.hpp`](../include/chronon3d/render_graph/executor/node_memory_metrics.hpp) (NEW this chore) |
| ADR | [`../docs/adr/ADR-026-node-memory-metrics.md`](../docs/adr/ADR-026-node-memory-metrics.md) (NEW this chore) |
| Test bridge | [`../tests/perf/test_node_memory_counters_v1.cpp`](../tests/perf/test_node_memory_counters_v1.cpp) (EDIT this chore) |
| Context | [`../include/chronon3d/render_graph/render_graph_context.hpp`](../include/chronon3d/render_graph/render_graph_context.hpp) (EDIT this chore) |
| V1 ticket | [`../docs/tickets/TICKET-PERF-COUNTERS-NODE-MEMORY-V1.md`](../docs/tickets/TICKET-PERF-COUNTERS-NODE-MEMORY-V1.md) (forward-point `<a>` closure) |
| SCHEMA | [`../docs/schemas/chrono3d.stats.v1.schema.json`](../docs/schemas/chrono3d.stats.v1.schema.json) |
| B03 YAML | [`../configs/benchmarks/corpus/b03_cinematic_glow_1088p.yaml`](../configs/benchmarks/corpus/b03_cinematic_glow_1088p.yaml) |

## §honesty closure

This ticket's chore lands just the canonical TYPE + the test bridge + ADR-
026 + forward-points.  The macchina-verifica end-to-end (V2.1 hot-path
integration + Real B03 CinematicGlow1088p with compiled `chrono3d_cli`) is
DEFERRED-WBH per the user's explicit directive: "Macchina-verifica DEFERRED-
WBH fino a WBH con `chrono3d_cli` binary compilato + B03 CinematicGlow1088p
scene presente."

Per AGENTS.md, F2.1 V2 first-commit chore lands:
- 1 NEW canonical header (`include/chronon3d/render_graph/executor/node_memory_
  metrics.hpp`, ~120 LoC header-only inline)
- 1 NEW ADR file (docs/adr/ADR-026-node-memory-metrics.md parallel
  ADR-024 precedent for new public SDK API struct)
- 1 EDIT test bridge (synthetic stand-in `using` swap → canonical alias)
- 1 EDIT render_graph_context.hpp (NodeStatsReporter* forward-decl additive
  field on `NodeExecutionContext`)
- 1 NEW this ticket
- 1 EDIT CHANGELOG (prepended CITA-only)

NO new SDK ABI beyond the 3 NEW canonical types.  Cat-3 minimal-surface
verified.
