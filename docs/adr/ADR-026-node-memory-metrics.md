# ADR-026 — node memory metrics canonical struct + per-session reporter

**Status**: ACCEPTED (locked commit `<sha-after-this-pr>`)

## Contest
- User spec FASE 2.1 verbatim: "integrare `NodeMemoryMetrics` struct (8 field
  `atomic<uint64_t>`) nel solver render graph executor + hot-path instrument-
  ation in `node_runner` + `NodeStatsReporter` per-session lifecycle veri-
  ficato".
- `TICKET-PERF-COUNTERS-NODE-MEMORY-V1` Phase 1 forward-point `<a>` (already
  documented): "Richiede ADR per Cat-2 freeze compliance (parallel precedent:
  ADR-024 composite-node-counter)".
- AGENTS.md `## Regole permanenti (ereditate dal freeze)`: "No espansione
  API non necessaria: ogni nuovo simbolo in `include/chronon3d/` va giusti-
  ficato".
- Existing infrastructure: `tests/perf/test_node_memory_counters_v1.cpp`
  holds the SYNTHETIC STAND-IN for the canonical type — `static_assert`
  suite that locks the 8-name shape, the per-session zero-static-state
  invariant, the monotonic accumulation contract, and the defensive
  B03 CinematicGlow1088p 90-frame gate.

## Decisione
APPROVE: Land the canonical struct + reporter class in the single new
header `include/chronon3d/render_graph/executor/node_memory_metrics.hpp`,
replacing the synthetic stand-in's `using` alias with `using
NodeMemoryMetrics = chronon3d::graph::NodeMemoryMetrics;` in the SAME
commit (atomic-replace pattern).  The canonical surface:

- `chronon3d::graph::NodeMemoryMetrics` — 8 named `std::atomic<std::uint64_t>`
  fields (`pixels_read`, `pixels_written`, `bytes_read`, `bytes_written`,
  `framebuffer_copies`, `framebuffer_clears`, `allocations`, `temporary_buf-
  fers`).  Total = 64 bytes = 1 cache line (cache-line aligned; NO false-
  sharing in dedicated allocator slot).
- `chronon3d::graph::NodeStatsSnapshot` — value-typed `Id + 8 counts`
  read-out of a single node's accumulator state.
- `chronon3d::graph::NodeStatsReporter` — per-session `std::map<std::string,
  NodeMemoryMetrics>` aggregator.  Default-constructible; copy DELETED
  (lifetime invariant).  Methods: `observe_node`, `snapshot`, `reset`.

Rationale:
- SINGLE SOURCE OF TRUTH: the synthetic stand-in's `static_assert` suite
  is the design contract; the canonical type MUST satisfy it verbatim
  (otherwise the test → false green).
- ZERO COUPLED CHURN: the canonical header is header-only inline (no
  `.cpp` impl); the test bridge is a 1-line `using` alias swap.
- AGENTS.md §Fare PR piccole e mirate: 1 NEW canonical header + 1 NEW
  ADR file + 1 EDIT test `using` swap + 1 EDIT `render_graph_context.hpp`
  forward-decl + 1 NEW ticket + 1 EDIT CHANGELOG = 6 files.  No new
  singleton / registry / cache (existing `NodeCache` and `TextureCache`
  remain the canonical acc-tracking surfaces — additive only).
- ALL atomic ops use `std::memory_order_relaxed` (per established
  codebase precedent — `include/chronon3d/cache/lru_cache.hpp`,
  `pipe_export_helpers.cpp`, etc.).  No causal-ordering requirement from
  monotonic counters; relaxed is the right choice.
- ZERO `#include <msdfgen>, <libtess2>, <unicode[/...]>` (forbidden by
  `tools/check_architecture_boundaries.sh` Check 11).
- ZERO new SDK symbols beyond the 3 NEW canonical types (the struct +
  value snapshot + the reporter class).  ADR-026 makes the case for
  Cat-2 freeze compliance.

## Alternative considerate
- **ALT-A: REUSE existing `CHRONON_COUNTERS_GRAPH` macro** (extend with
  8 new counters + add per-node-keyed aggregation logic to the existing
  counter macro).  Rejected: the existing macro covers render-GRAPH-wide
  totals keyed by counter NAME, not per-NODE aggregations keyed by
  `node_id`.  The user spec explicitly mandates per-node-keyed accounting;
  reusing the global counter would require a parallel 8×counter × N-nodes
  addition, polluting the macro counter namespace.  Cleaner to land a
  dedicated struct + reporter.
- **ALT-B: Skip ADR file** (treat as internal-only via private TU under
  `src/render_graph/internal/`).  Rejected per AGENTS.md requirement
  "ogni nuovo simbolo in `include/chronon3d/` va giustificato" + TICKET-PERF
  -COUNTERS-NODE-MEMORY-V1 §forward-point `<a>` explicit ADR ask.  Internal-
  only placement would lose the per-session observable that downstream
  `--stats-json` consumers need.
- **ALT-C: Defer type to a separate private-detail header + re-export**
  (proposed by some benches that fear SDK surface area bloat).  Rejected:
  dual-location re-export inherits the same Cat-3 anti-duplication risk
  the codebase has historically avoided.  Single new header = single
  source of truth.
- **ALT-D: Hot-path instrumentation in V2** (call `observer_node` from
  `execute_single_node` immediately when the report passes).  Rejected
  for V2: per user spec "Macchina-verifica DEFERRED-WBH fino a WBH con
  `chronon3d_cli` binary compilato + B03 CinematicGlow1088p scene presente";
  hot-path wiring is forward-pointed to a separate V2.1 next-chore that
  threads NodeStatsReporter via RenderGraphContext + integrates into
  the `node_runner` hot path on a machine with the compiled binary.

## Conseguenze

POS:
- 1 NEW canonical header satisfies the V1 forward-point `<a>` exactly.
- 1 `using` swap in the test bridge unifies the canonical and synthetic
  paths; future benches reuse the same struct identity without a parallel
  surface area.
- Per-session lifetime invariant is mechanical (the reporter holds a
  `std::map`; destruction releases it deterministically).
- ZERO new singleton / registry / cache / resolver.  AGENTS.md ADR
  trigger rule satisfied.

NEG:
- A new `chrono3d::graph::NodeStatsReporter*` slot is added to
  `RenderGraphContext::NodeExecutionContext`.  This is a Cat-2 additive
  field (forward-decl + null-pointer default); it does not break any
  existing call site.  Mirrors the existing pattern of
  `text_bbox_reporter`, `profiler`, `counters`, `asset_resolver`.
- ABI-additive: existing renderers that want the canonical lifetime
  observability MUST construct the reporter on `RenderSession` init;
  the test bridge does not exercise this path (WBH-only).
- One additional header budget (`include/chronon3d/render_graph/
  executor/node_memory_metrics.hpp` ~120 LoC header-only inline).  No
  `.cpp` impl = no link-time change to existing translation units.

## Cross-references
- AGENTS.md: §Regole permanenti "ogni nuovo simbolo in `include/chronon3d/`
  va giustificato" + §Fare PR piccole e mirate.
- TICKET-PERF-COUNTERS-NODE-MEMORY-V1: §forward-point `<a>`.
- ADR-024 (composite-node-counter): parallel singleton → DI refactor
  precedent.  ADR-026 is the parallel "new public SDK API surface" precedent.
- `tests/perf/test_node_memory_counters_v1.cpp`: synthetic stand-in +
  `static_assert` suite (locks the 8-name shape contractually).
- `docs/schemas/chronon3d.stats.v1.schema.json`: per-session aggregate
  of per-node NodeMemoryMetrics snapshots (forward-point: CLI emission).

## Owner / Date
- Owner: TICKET-COUNTERS-NODE-MEMORY-V1-V2 author (this session, post-task).
- Date: 2026-07-13.
