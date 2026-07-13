# TICKET-PERF-COUNTERS-NODE-MEMORY-V1 — Per-Node Memory Metrics (Phase 1: Contract Lock)

## Stato

**PARTIAL** (2026-07-13).  Synthetic contract-lock test PASS on this VPS (doctest + stdlib only — no vcpkg glm/magic_enum dependency).  Schema canonical `chronon3d.stats.v1`.  Actual C++ struct + node_runner hot-path integration + CLI flag wiring + real-clock smoke verification IS DEFERRED to forward-point `<a> TICKET-PERF-COUNTERS-NODE-MEMORY-V1-IMPLEMENTATION` per AGENTS.md §Cat-2 freeze (new public SDK API requires ADR).

## Priorità

P2 — abilita TICKET-PERF-GATE-V1 (F1.5, performance regression gate) + concreta la forward-point line tracciata in TICKET-BENCH-CORPUS-V1 §Forward-points catena.

## Problema

Chronon3D emitter counters esistenti (cache_hits, cache_misses, nodes_executed, pixels_touched, blur_pixels, images_sampled, text_glyphs_rasterized + framebuffer_allocations/reuses/bytes_*) sono predefiniti in `chronon3d::software::RenderCounters` (per la pipeline globale), ma:

1. **Assenza di counter per-nodo dedicati ai memory metrics**. Le 8 metriche richieste (pixels_read, pixels_written, bytes_read, bytes_written, framebuffer_copies, framebuffer_clears, allocations, temporary_buffers) non hanno nessun emit-side specifico per nodo: si confondono con i counter globali.
2. **Assenza di superficie di esposizione canonica via `--stats-json`**. L'utente vuole i counter per-nodo esposti in JSON serializzabile per la macchina verifica su B03 (CinematicGlow1080p).
3. **Assenza di garantìa "zero static state"**. AGENTS.md §Cat-3 minimal-surface + §regole "non introdurre singleton/registry/resolver" richiedono che l'aggregatore sia **per-sessione** (lifetime-bound al RenderSession instance), non globale.

## Soluzione adottata (Phase 1: Contract Lock)

### Cat-3 + Cat-2 minimal-surface strategy

Il pattern canonico (Azione 18 deliverable inline precedent): invece di fare il commit del C++ struct + integration in un solo chore, separare in 2 chore:
- **Phase 1 (questo ticket)**: il **CONTRACT** + il **lock del contract** via synthetic test. Nessun C++ struct nella `include/chronon3d/` directory. Nessuna richiesta ADR.
- **Phase 2 (forward-point `<a>`, future chore)**: aggiungere il C++ struct canonico `chronon3d::graph::NodeMemoryMetrics` + `NodeStatsReporter` nella `include/chronon3d/render_graph/executor/node_memory_metrics.hpp` + integrazione in `node_runner.cpp` + CLI flag `--stats-json` + `node_stats_reporter_session.h` per session lifetime. Richiede ADR per Cat-2 freeze compliance (parallel precedent: ADR-024 composite-node-counter).

Il `<a>` chore deve poi sostituire il SYNTHETIC STAND-IN nel test con un `using NodeMemoryMetrics = ::chronon3d::graph::NodeMemoryMetrics;` — la static_assert suite garantisce che il canonical type soddisfi la stessa shape (8 field atomic).

### 4 file change-set (3 NEW + 1 EDIT)

| File | Tipo | Ruolo |
|---|---|---|
| `tests/perf/test_node_memory_counters_v1.cpp` | NEW | Synthetic contract-lock test (doctest + stdlib only). 5 TEST_CASE: 8-field shape, zero-static-state lifetime, monotonic accumulation, B03 gate (counters > 0), cat-3 self-check. |
| `docs/schemas/chronon3d.stats.v1.schema.json` | NEW | Canonical JSON contract for `--stats-json` output. 8 fields per spec, lock-in. Schema versioning `chronon3d.stats.v1`. |
| `docs/tickets/TICKET-PERF-COUNTERS-NODE-MEMORY-V1.md` | NEW | Questo file: cronaca + CONTRACT spec + forward-point chain. |
| `docs/CHANGELOG.md` | EDIT | Prepended Cita-Only entry per Cat-5 2-doc same-commit. |

### Cat-3 + Cat-3 anti-dup discipline

- ZERO nuovi simboli pubblici in `include/chronon3d/`.
- ZERO nuovi flag CLI su `chronon3d_cli` (lo schema canonico detta la shape; il flag `--stats-json` argomento del `--json-file` cluster rimane deferred al forward-point `<a>` impl chore).
- ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` (script + test only, no C++ modification).
- 8 nomi di field (`pixels_read, pixels_written, bytes_read, bytes_written, framebuffer_copies, framebuffer_clears, allocations, temporary_buffers`) sono **lockati** dal synthetic test `static_assert` suite + dal JSON schema `required` array — la canonical struct del forward-point `<a>` deve soddisfarli byte-equivalent (no rename, no type change).

### Cat-5 2-doc same-commit alignment

- CHANGELOG + TICKET-BENCH-CORPUS-V1 forward-point (questo chore) atomico come Phase 1.
- `docs/FOLLOWUP_TICKETS.md` DEFERRED per §Disciplina di aggiornamento dei canonici: questo ticket NON apre un §Open Blocker (è una Phase-1 contract lock, non un blocker); forward-point `<a>` (impl) sarà il row da aggiungere a FOLLOWUP_TICKETS quando il `<a>` sarà push-ready.
- `docs/CURRENT_STATUS.md` cite-only row DEFERRED al forward-point `<b> TICKET-PERF-COUNTERS-NODE-MEMORY-V1-3DOC-CAT5-ALIGN` (parallel precedent `TICKET-BENCH-MACHINES-V1-3DOC-CAT5-ALIGN`).

## Criteri di accettazione

| # | Criterio | Expected | Stato (Phase 1, post-implementation) |
|---|---|---|---|
| 1 | Synthetic test compiles + doctest PASS su questa VPS | PASS | Verified `doctest_tests_pass` (doctest + stdlib; no vcpkg glm/magic_enum dependency) |
| 2 | 8 named fields with types `std::atomic<std::uint64_t>` lock via static_assert | PASS | Verified `static_assert` suite |
| 3 | Zero-static-state lifetime test (2 distinct reporters, isolated state) | PASS | Verified addressof + observe_node isolation |
| 4 | B03 CinematicGlow1080p synthesized stream gate (8 counters > 0) | PASS | Verified `CHECK(g.X > 0)` × 8 |
| 5 | Schema `docs/schemas/chronon3d.stats.v1.schema.json` is valid JSON Schema 2020-12 | PASS | Verified `jq .required` matches 8-counter contract |
| 6 | Cat-3 minimal-surface: zero new symbols in include/chronon3d/ | PASS | Verified `git diff --stat include/chronon3d/` zero LoC delta |
| 7 | Forbidden checks: zero `#include <msdfgen>/<libtess2>/<unicode[/...]>` | PASS | Verified `grep -rE` (test file has only doctest + stdincludes) |
| 8 | Subject envelope ≤ 72 chars per AGENTS.md TICKET-GATE-SUBJECT-RANGE | PASS | `feat(perf): counters + smoke test (TICKET-PERF-COUNTERS-NODE-MEMORY-V1) = 71 chars` |

## Forward-points (registered, NOT in this commit)

- **`<a> TICKET-PERF-COUNTERS-NODE-MEMORY-V1-IMPLEMENTATION`** (Phase 2, future chore): aggiungere canonical `chronon3d::graph::NodeMemoryMetrics` + `NodeStatsReporter` + integration in `execute_single_node()` + CLI flag `--stats-json` su `chronon3d_cli render` + `chronon3d_cli bench`. **Richiede ADR-024-style** per AGENTS.md Cat-2 freeze (new public SDK API + new CLI flag). Il catena canonica: ADR → ticket → implementazione. Stima: 6-8 file (1 NEW include + 2 EDIT executor + 2 EDIT CLI + 1 NEW impl + 1 NEW impl test + 1 EDIT CHANGELOG).
- **`<b> TICKET-PERF-COUNTERS-NODE-MEMORY-V1-3DOC-CAT5-ALIGN`** (Cat-5 3-doc closure per CURRENT_STATUS): una volta che il `<a>` chore è push-ready, aggiungere cite-only row a `docs/CURRENT_STATUS.md` §Stato generale per area "Executor / Perf counters" + cat-5 row a `docs/FOLLOWUP_TICKETS.md` §Open Blockers row "TICKET-PERF-COUNTERS-NODE-MEMORY-V1-IMPLEMENTATION". Parallel precedent: `TICKET-BENCH-MACHINES-V1-3DOC-CAT5-ALIGN`.
- **`<c> TICKET-PERF-COUNTERS-NODE-MEMORY-V1-WBH-MACHINE-VERIFY`**: macchina-verifica del `<a>` impl chore su Working Build Host (post-vcpkg boostrap): `cmake --preset linux-fast-dev -B build/manual-test -DCHRONON3D_BUILD_TESTS=ON` + `cmake --build build/manual-test --target chronon3d_perf_tests -j4` + `ctest -R test_node_memory_counters_v1 --output-on-failure` + `chronon3d_cli bench BenchB03_CinematicGlow1080p --frames 90 --stats-json /tmp/b03_stats.json` + jq `.nodes[] | select(.node_id == "glow") | .pixels_read` deve essere > 0 + jq 8-field validation via `jq -s 'validate | errors' /tmp/b03_stats.json` against `chronon3d.stats.v1.schema.json` via python jsonschema library.

## Phase 1 (questo ticket) catena canonica

```
investigation: F1.4 spec landing
   ├─ scaffold: this ticket (Phase 1 contract lock)
   │   ├─ synthetic test file (lock contract shape + lifetime + B03 gate)
   │   ├─ JSON schema canonical (lock output contract version `chronon3d.stats.v1`)
   │   └─ ticket cronaca (single ticket home for the Phase 1 narrative)
   ├─ forward-point <a>: TICKET-PERF-COUNTERS-NODE-MEMORY-V1-IMPLEMENTATION (Phase 2)
   │   ├─ 1 ADR-024-style ADR (Cat-2 freeze compliance)
   │   ├─ 1 NEW include/chronon3d/render_graph/executor/node_memory_metrics.hpp
   │   ├─ 2 EDIT src/render_graph/executor/{node_runner, executor_levels}.cpp
   │   ├─ 2 EDIT apps/chronon3d_cli/commands/{render, bench}/command_*.cpp
   │   └─ 1 EDIT docs/CHANGELOG.md
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
