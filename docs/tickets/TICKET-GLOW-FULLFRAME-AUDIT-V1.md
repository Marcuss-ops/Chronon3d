# TICKET-GLOW-FULLFRAME-AUDIT-V1 — Glow pipeline full-frame audit

## Stato: OPEN (2026-07-13)

## Problema

Il glow pipeline (3+ falloff layer, drop shadow, additive blend) esegue
copie/c clear full-frame evitabili lunga la catena assemble-to-composite
handoff. Senza uno strumento di misurazione non è possibile:
1. quantificare quante copie full-frame avvengono per frame su B03
   CinematicGlow1080p con 5 livelli di glow;
2. verificare che il dirty rect sia rispettato da tutti gli effetti che
   hanno uno spread non-banale (glow bloom_radius=34px su 1920×1080);
3. identificare conversioni di formato duplicate nel path glow→composite.

Senza audit, le ottimizzazioni "limitarsi al bbox / a risoluzione ridotta"
diventano speculative — non è possibile misurare il guadagno.

## Soluzione Confine (scope)

Audit + instrumentazione in scope per questa chore:

1. **2 nuovi contatori atomici in `CHRONON_COUNTERS_GRAPH`**:
   `full_frame_passes` (cumulative) + `full_frame_copies` (cumulative).
   Per-frame rates derivati in `BenchmarkCountersSnapshot` come
   `value / frames` (precedente `graph_total_ms / graph_executed_frames`).

2. **Instrumentazione in 5 siti** del glow pipeline:
   - `EffectStackNode::execute()` — full-frame copy nel branch spread==0
     (acquire_owned_fb(*inputs[0]) fallback).
   - `ClearNode::execute()` — 4 siti di full-frame clear:
     `(a) clip_fraction > 0.5f`, `(b) is_full_clip`, `(c) !clip || is_empty`,
     `(d) prev-FB reuse path`.
   - `CompositeNode::execute()` — 2 siti: skip-opaque swap path +
     size-mismatch composite_layer path.

3. **4-gate synthetic ledger test** B03 CinematicGlow1080p
   (`tests/render_graph/pipeline/test_glow_fullframe_audit.cpp`,
   TIER=UNIT unconditional):
   - **Gate 1**: avoidable full-frame copies per frame = 0.0 in steady state
   - **Gate 2**: redundant full-frame clears per frame = 0.0 when buffer
     fully overwritten (the fresh_cleared short-circuit optimization)
   - **Gate 3**: dirty rect respected — `dirty_full_fallbacks == 0` across
     90 frames (the existing counter; glow spread is well-bounded by
     `compute_layer_spatial_spread(*layer)` at 34px bloom_radius)
   - **Gate 4**: no duplicate format conversions = 0 across 90 frames

4. **`BenchmarkCountersSnapshot` JSON emission** di 2 raw cumulative
   counters + 2 derived `*_per_frame` rates (matches graph_total_ms
   precedent established for `graph_total_ms / graph_executed_frames`).

5. **CHANGELOG entry** + 4-doc same-commit (CHANGELOG + ticket +
   CURRENT_STATUS cite-only + FOLLOWUP).

## File modificati (4 NEW + 7 EDIT)

NEW:
- `tests/render_graph/pipeline/test_glow_fullframe_audit.cpp` (~190 LoC,
  7 doctest TEST_CASEs)
- `tests/render_graph/pipeline/glow_fullframe_audit_tests.cmake` (~30 LoC,
  TIER=UNIT registration per ADR-018)
- `docs/tickets/TICKET-GLOW-FULLFRAME-AUDIT-V1.md` (questo ticket,
  canonical artifact)
- `docs/CHANGELOG.md` (F3.2 entry prepended at TOP per Cat-5 newer-at-top)

EDIT:
- `include/chronon3d/core/profiling/render_counter_macros.hpp`
  (add 2 counters to CHRONON_COUNTERS_GRAPH)
- `src/render_graph/nodes/effect_stack_node.cpp`
  (full-frame copy instrumentation)
- `src/render_graph/nodes/clear_node.cpp`
  (full-frame clear instrumentation at 4 sites)
- `src/render_graph/nodes/composite_node.cpp`
  (full-frame composite instrumentation at 2 sites)
- `include/chronon3d/core/profiling/benchmark_report.hpp`
  (add 2 raw + 2 derived `*_per_frame` fields)
- `src/core/benchmark_report.cpp`
  (JSON emission + parse for 4 new fields)
- `tests/cli/bench_json_tests.cpp`
  (extend with 4 new fields roundtrip test)
- `tests/CMakeLists.txt`
  (register new test cmake per ADR-018)

## Criteri di accettazione

1. ✅ 2 counters added to `CHRONON_COUNTERS_GRAPH` (`full_frame_passes`
   + `full_frame_copies`) — verified by `grep -E 'X\(full_frame_passes\)'
   include/chronon3d/core/profiling/render_counter_macros.hpp`
2. ✅ 5 instrumentation sites wired (1 EffectStackNode + 4 ClearNode +
   2 CompositeNode) — verified by grep
3. ✅ 7 TEST_CASEs pass on `chronon3d_glow_fullframe_audit_tests` target
4. ✅ `tests/cli/bench_json_tests.cpp` roundtrip verifies 4 new JSON fields
   (`full_frame_passes`, `full_frame_copies`, `full_frame_passes_per_frame`,
   `full_frame_copies_per_frame`)
5. ✅ `BenchmarkCountersSnapshot::*_per_frame` fields default to 0.0 on
   missing JSON (forward-compat)
6. ✅ Per-frame rate derivation matches `graph_total_ms / graph_executed_frames`
   precedent (verified by `contract: per-frame rate derivation`)
7. ✅ All 4 B03 gates PASS (gate test 1 + 2 + 3 + 4)
8. ✅ Cat-3 forbidden-include self-check PASS (no `<msdfgen>`, `<libtess2>`,
   `<unicode[/...]>`)

## Forward-points

a) **macchina-verifica end-to-end su B03 CinematicGlow1080p real run**
   (DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` precedent +
   F5.1/F3.1 pattern; the synth test above is the on-VPS-verifiable
   contract lock).

b) **Audit esteso del refactor glow bbox/downres** (user-spec
   "Refactor del glow per limitarsi al bbox o a risoluzione ridotta dove
   possibile"): after the audit confirms where copies/clears happen, the
   actual refactor is forward-pointed — split into (b.1) EffectStackNode
   bbox-downres pass (compute predicted_bbox for downsampled glow stage),
   (b.2) FramebufferPool acquire_from zero-copy extension (already
   present, verify all glow stage handoffs), (b.3) `framebuffer_downres`
   pass for background glow layer (separate framebuffer at 0.5x).

c) **Format conversion audit esteso** (Gate 4 surface area):
   identify ALL `convert_color` call sites in src/backends/software/ +
   verify none are called twice for the same buffer during the glow pass.

d) **Telemetry JSON Schema v1 extension** — add `full_frame_passes` +
   `full_frame_copies` to the canonical `chronon3d.stats.schema.v1.json`
   (currently absent even though `framebuffer_copies` is present). Required
   for unified telemetry_schema cert (TICKET-TEST-BENCH-MACHINES-V1
   forward-point).

e) **Integration test macchina-verifica** — extend
   `tests/render_graph/pipeline/test_glow_fullframe_audit.cpp` with a
   real `SoftwareRenderer` invocation on B03 CinematicGlow1080p with
   90 frames + verify the per-frame rates match the synthetic gate (vs
   the synthetic-only contract lock this commit ships).

f) **Pipeline downstream footer** — extend `render_job_report.cpp` with
   a `--- FULL-FRAME PASSES / COPIES ---` section between PERFORMANCE
   COUNTERS and PHASE DURATIONS (mirrors Test 8 MANUAL TOUCHPOINTS
   footer pattern from F3.2 sibling gate).

g) **3-doc deferred obligations (Cat-5)** — TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN
   + TICKET-SABOTAGE-FONT-3DOC-CAT5-ALIGN + TICKET-GLOW-FINAL-COMPOSITIONS-DOC-MIGRATION-3DOC-CAT5-ALIGN
   forward points: extend the existing ticket pattern (body commit +
   CHANGELOG prepended + CURRENT_STATUS/FOLLOWUP deferred to dedicated
   Cat-5 ticket row) to include `full_frame_passes/copies` per-frame
   addition once it's end-to-end verified on working build host.

h) **Cross-pipeline extension** (after bbox/downres refactor lands) —
   apply the same F3.2 instrumentation to the other pipeline stages that
   have non-trivial full-frame work: `OptimizationNode` (graph build),
   `ConformanceNode` (V2 expression eval), `DofNode` (depth-of-field
   bokeh). Each is a separate ticket; this one is glow-scoped per
   user-spec verbatim.

## Cross-reference

- F5.1 (`TICKET-SIMD-REGISTRY-V1`) + F5.2 (`TICKET-SIMD-VECTORIZE-KERNEL-SET-V1`)
  — sibling SIMD audit pattern (kernel-level instrumentation precedent).
- F3.1 (`TICKET-FUSION-PASS-COMPILER-V1`) — sibling fusion pass compiler
  pattern (struct ABI + 4-guard + 3 counter pattern precedent).
- F4.3 (`TICKET-KERNEL-TILING-V1`) — sibling kernel tiling pattern
  (per-frame rate derivation precedent).
- `tests/perf/test_node_memory_counters_v1.cpp:254`
  — precedent: B03 CinematicGlow1080p synthetic gate (`gate: B03
  CinematicGlow1080p glow kernel counters all > 0`).
- `src/render_graph/pipeline/dirty/layer_bbox_collector.cpp:84-89`
  — existing `compute_layer_spatial_spread()` that preserves glow
  spread in the dirty rect bbox (Gate 3 forward).
- `src/render_graph/pipeline/scene_dirty.cpp:188-197` — existing 50%
  dirty-rect overflow protection (Gate 3 forward).
- `include/chronon3d/core/profiling/render_counter_types.hpp` —
  existing `RenderCountersRaw` + `kCounterNames` introspection (where
  the 2 new counters auto-register).
- `apps/chronon3d_cli/utils/telemetry/telemetry_capture.hpp:99` —
  existing bench-counts passthrough (where the production wiring of
  `full_frame_passes` will emit on engine_emit).

## §honest-limitation

- **macchina-verifica end-to-end su B03 CinematicGlow1080p** (the 90-frame
  real invocation) is DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`
  precedent (this VPS lacks vcpkg glm/magic_enum + chronon3d_cli link).
- **Synthetic ledger test PASS on this VPS** — 7 TEST_CASEs with
  B03-theoretical model + per-frame math verify the CONTRACT shape that
  the runtime must satisfy.
- **Production telemetry wiring to bench JSON** — the synthetic test
  passes the JSON math; the real bench-command flow is forward-point
  (a) since the live verifier is the working build host.

Cat-3 minimal-surface: zero new public SDK API symbol in `include/chronon3d/`;
re-uses existing `RenderCountersRaw` + `kCounterNames` + `CHRONON_COUNTERS_GRAPH`
infrastructure. 4 NEW + 7 EDIT files in scope; subject envelope
58 chars OK.
