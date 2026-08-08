# TICKET-PERF-COUNTERS-NODE-MEMORY-V1 — Per-Node Memory Metrics (Phase 1: Contract Lock)

## Stato

**PARTIAL** (2026-08-08).  Canonical `NodeMemoryMetrics`/`NodeMemoryTracker` accounting now separates allocation event count from allocated byte volume; focused doctest and schema compatibility checks PASS.  node_runner hot-path integration, CLI flag wiring and real-clock smoke verification remain DEFERRED to forward-point `<a>` per AGENTS.md §Cat-2 freeze.

## Priorità

P2 — abilita TICKET-PERF-GATE-V1 (F1.5, performance regression gate) + concreta la forward-point line tracciata in TICKET-BENCH-CORPUS-V1 §Forward-points catena.

## Problema

Chronon3D emitter counters esistenti (cache_hits, cache_misses, nodes_executed, pixels_touched, blur_pixels, images_sampled, text_glyphs_rasterized + framebuffer_allocations/reuses/bytes_*) sono predefiniti in `chronon3d::software::RenderCounters` (per la pipeline globale), ma:

1. **Assenza di counter per-nodo dedicati ai memory metrics**. Le metriche richieste (pixels_read, pixels_written, bytes_read, bytes_written, framebuffer_copies, framebuffer_clears, allocations, allocated_bytes, temporary_buffers) non hanno nessun emit-side specifico per nodo: si confondono con i counter globali.
2. **Assenza di superficie di esposizione canonica via `--stats-json`**. L'utente vuole i counter per-nodo esposti in JSON serializzabile per la macchina verifica su B03 (CinematicGlow1080p).
3. **Assenza di garantìa "zero static state"**. AGENTS.md §Cat-3 minimal-surface + §regole "non introdurre singleton/registry/resolver" richiedono che l'aggregatore sia **per-sessione** (lifetime-bound al RenderSession instance), non globale.

## Soluzione adottata (Phase 1: Contract Lock)

### Cat-3 + Cat-2 minimal-surface strategy

Il pattern canonico mantiene separati il contratto e l’integrazione completa:
- **Phase 1**: contract lock e schema `chronon3d.stats.v1`.
- **Phase 2 (questa tranche parziale)**: canonical `NodeMemoryMetrics` + `NodeMemoryTracker` con accounting distinto tra eventi e byte, supportato da ADR-026 e test mirati.
- **Forward-point `<a>`**: integrazione hot-path in `node_runner.cpp`, CLI `--stats-json` e real-clock smoke verification restano deferred per Cat-2/WBH.

La tranche canonical sostituisce il synthetic stand-in con `chronon3d::graph::NodeMemoryMetrics`; i test contract verificano separatamente conteggio degli eventi e volume dei byte.

### Original Phase 1 change-set (historical)

| File | Tipo | Ruolo |
|---|---|---|
| `tests/perf/test_node_memory_counters_v1.cpp` | NEW | Canonical contract-lock test (doctest + stdlib only): allocation count/byte volume, zero-static-state lifetime, monotonic accumulation, B03 gate and cat-3 self-check. |
| `docs/schemas/chronon3d.stats.v1.schema.json` | NEW | Canonical JSON contract for `--stats-json`; `allocated_bytes` is an optional additive v1 property for compatibility. |
| `docs/tickets/TICKET-PERF-COUNTERS-NODE-MEMORY-V1.md` | NEW | Questo file: cronaca + CONTRACT spec + forward-point chain. |
| `docs/CHANGELOG.md` | EDIT | Prepended Cita-Only entry per Cat-5 2-doc same-commit. |

### Cat-3 + Cat-3 anti-dup discipline

- ZERO nuovi simboli pubblici in `include/chronon3d/`.
- ZERO nuovi flag CLI su `chronon3d_cli` (lo schema canonico detta la shape; il flag `--stats-json` argomento del `--json-file` cluster rimane deferred al forward-point `<a>` impl chore).
- ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` (script + test only, no C++ modification).
- I campi di accounting (`allocations` come conteggio eventi, `allocated_bytes` come volume in byte, `temporary_buffers` come acquisizioni e `live_bytes`/`peak_live_bytes` come residency) sono **lockati** dai test contract; i campi aggiuntivi restano opzionali in `chronon3d.stats.v1` per preservare la compatibilità dei payload v1 esistenti.

### Cat-5 2-doc same-commit alignment

- CHANGELOG + TICKET-BENCH-CORPUS-V1 forward-point (questo chore) atomico come Phase 1.
- `docs/FOLLOWUP_TICKETS.md` DEFERRED per §Disciplina di aggiornamento dei canonici: questo ticket NON apre un §Open Blocker (è una Phase-1 contract lock, non un blocker); forward-point `<a>` (impl) sarà il row da aggiungere a FOLLOWUP_TICKETS quando il `<a>` sarà push-ready.
- `docs/CURRENT_STATUS.md` cite-only row DEFERRED al forward-point `<b> TICKET-PERF-COUNTERS-NODE-MEMORY-V1-3DOC-CAT5-ALIGN` (parallel precedent `TICKET-BENCH-MACHINES-V1-3DOC-CAT5-ALIGN`).

## Criteri di accettazione

| # | Criterio | Expected | Stato (Phase 1, post-implementation) |
|---|---|---|---|
| 1 | Synthetic test compiles + doctest PASS su questa VPS | PASS | Verified `doctest_tests_pass` (doctest + stdlib; no vcpkg glm/magic_enum dependency) |
| 2 | Allocation count/byte fields with `std::atomic<std::uint64_t>` lock via static_assert | PASS | Verified `static_assert` suite |
| 3 | Zero-static-state lifetime test (2 distinct reporters, isolated state) | PASS | Verified addressof + observe_node isolation |
| 4 | B03 CinematicGlow1080p synthesized stream gate (memory counters > 0) | PASS | Verified `CHECK(g.X > 0)` for the populated counters |
| 5 | Schema `docs/schemas/chronon3d.stats.v1.schema.json` is valid JSON Schema 2020-12 | PASS | `allocated_bytes` remains optional for v1 compatibility |
| 6 | Cat-3 minimal-surface: zero new symbols in include/chronon3d/ | PASS | Verified `git diff --stat include/chronon3d/` zero LoC delta |
| 7 | Forbidden checks: zero `#include <msdfgen>/<libtess2>/<unicode[/...]>` | PASS | Verified `grep -rE` (test file has only doctest + stdincludes) |
| 8 | Subject envelope ≤ 72 chars per AGENTS.md TICKET-GATE-SUBJECT-RANGE | PASS | `feat(perf): counters + smoke test (TICKET-PERF-COUNTERS-NODE-MEMORY-V1) = 71 chars` |

## Forward-points (registered, NOT in this commit)

- **`<a> TICKET-PERF-COUNTERS-NODE-MEMORY-V1-IMPLEMENTATION`** (forward-point): completare integrazione hot-path in `execute_single_node()`, CLI `--stats-json` e real-clock smoke verification. Il canonical type, tracker, ADR e accounting count/byte sono già atterrati in questa tranche.
- **`<b> TICKET-PERF-COUNTERS-NODE-MEMORY-V1-3DOC-CAT5-ALIGN`** (Cat-5 3-doc closure per CURRENT_STATUS): una volta che il `<a>` chore è push-ready, aggiungere cite-only row a `docs/CURRENT_STATUS.md` §Stato generale per area "Executor / Perf counters" + cat-5 row a `docs/FOLLOWUP_TICKETS.md` §Open Blockers row "TICKET-PERF-COUNTERS-NODE-MEMORY-V1-IMPLEMENTATION". Parallel precedent: `TICKET-BENCH-MACHINES-V1-3DOC-CAT5-ALIGN`.
- **`<c> TICKET-PERF-COUNTERS-NODE-MEMORY-V1-WBH-MACHINE-VERIFY`**: macchina-verifica del `<a>` impl chore su Working Build Host (post-vcpkg boostrap): `cmake --preset linux-fast-dev -B build/manual-test -DCHRONON3D_BUILD_TESTS=ON` + `cmake --build build/manual-test --target chronon3d_perf_tests -j4` + `ctest -R test_node_memory_counters_v1 --output-on-failure` + `chronon3d_cli bench BenchB03_CinematicGlow1080p --frames 90 --stats-json /tmp/b03_stats.json` + jq `.nodes[] | select(.node_id == "glow") | .pixels_read` deve essere > 0 + jq 8-field validation via `jq -s 'validate | errors' /tmp/b03_stats.json` against `chronon3d.stats.v1.schema.json` via python jsonschema library.

## Phase 1 (questo ticket) catena canonica

```
investigation: F1.4 spec landing
   ├─ scaffold: this ticket (Phase 1 contract lock)
   │   ├─ synthetic test file (lock contract shape + lifetime + B03 gate)
   │   ├─ JSON schema canonical (lock output contract version `chronon3d.stats.v1`)
   │   └─ ticket cronaca (single ticket home for the Phase 1 narrative)
   ├─ forward-point <a>: TICKET-PERF-COUNTERS-NODE-MEMORY-V1-IMPLEMENTATION (hot-path/CLI completion)
   │   ├─ existing canonical NodeMemoryMetrics + NodeMemoryTracker
   │   ├─ EDIT src/render_graph/executor/{node_runner, executor_levels}.cpp
   │   └─ EDIT apps/chronon3d_cli/commands/{render, bench}/command_*.cpp
   ├─ forward-point <b>: TICKET-PERF-COUNTERS-NODE-MEMORY-V1-3DOC-CAT5-ALIGN (Cat-5 closure)
   └─ forward-point <c>: TICKET-PERF-COUNTERS-NODE-MEMORY-V1-WBH-MACHINE-VERIFY (WBH macchina-verifica)
```

## Cross-link canonici

- [`AGENTS.md`](../../AGENTS.md) — Cat-3 (zero new SDK API symbols — satisfied by Phase 1 contract lock) + Cat-2 freeze (ADR required for Phase 2 implementation per forward-point `<a>`) + Cat-5 2-doc same-commit pattern + §honest-limitation (DEFERRED-WBH macchina-verifica acknowledge via §honesty context in Cita-Only pattern) + §Post-push SHA-selfcheck invariant (mandatory verify of `UPSTREAM_SHA == POSTPUSH_SHA` after `bash tools/wrap_push.sh origin main` per AGENTS.md §GATE-MNT-01 closure lineage).
- [`docs/schemas/chronon3d.bench.v3.schema.json`](../../docs/schemas/chronon3d.bench.v3.schema.json) — parallel precedent: schema versioning pattern (`chronon3d.bench.v3` → `chronon3d.stats.v1`) + per-section `required` field enum + `additionalProperties: false` enum-pattern discipline.
- [`src/render_graph/executor/telemetry_emitter.cpp`](../../src/render_graph/executor/telemetry_emitter.cpp) — parallel precedent: per-node telemetry emit pattern (`emit_node_records(...)` takes a `RenderGraphNode` + cache key + `CachedFB` + clip_rect + cache_status + duration_ms and writes to per-event records). The Phase 2 implementation will thread `NodeMemoryMetrics` accumulation through `execute_single_node()` in `src/render_graph/executor/node_runner.cpp`, parallel to the way `emit_node_records` writes per-node telemetry events.
- [`src/render_graph/executor/executor_levels.cpp`](../../src/render_graph/executor/executor_levels.cpp) — parallel precedent: per-level counter accumulation pattern (`parent_counters->level_parallel_count.fetch_add(1, ...)` is TBB-parallel-safe via `memory_order_relaxed`). The Phase 2 implementation will initialize a per-level `NodeStatsReporter` slice in `executor_levels()` and pass it down through the `LevelTimings` plumbing.
- [`apps/chronon3d_cli/commands/bench/command_bench.cpp`](../../apps/chronon3d_cli/commands/bench/command_bench.cpp) — parallel precedent: `--json-file <path>` flag wiring + JSON output emission + `chrono3d_cli bench <scene> --frames N --json-file out.json` invocation pattern. The Phase 2 implementation will wire `--stats-json <path>` as a peer flag.
- [`examples/bench_corpus/run_corpus_v1.sh`](../../examples/bench_corpus/run_corpus_v1.sh) — downstream consumer pattern: bash runner that already iterates the corpus across threads/fps dimensions and emits per-scene JSON. Phase 2 will extend this runner with `chronon3d_cli ... --stats-json /tmp/corpus_stats/<scene>.json` per `corpus_v1.json::scene_id`.
- `tools/wrap_push.sh` — canonical push wrapper per GATE-MNT-01 (mandatory SHA-triple selfcheck via `git rev-parse HEAD == '@{u}'`).
- `tools/check_main_clean.sh` — canonical READ-side triad gate per GATE-MNT-01.
- `tools/check_architecture_boundaries.sh` Check 11 — forbidden include gate (#include <msdfgen>/<libtess2>/<unicode[/...]>); this phase satisfies it (test file uses only doctest + stdincludes; no C++ modifies include/chronon3d/).
