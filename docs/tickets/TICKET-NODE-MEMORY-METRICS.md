# TICKET-NODE-MEMORY-METRICS — Per-nodo memory accounting + gate CI

**Stato**: OPEN — per-node framebuffer/event metrics and local CPU gates landed;
general heap/scratch allocation coverage is deferred to PMR hooks; representative
corpus and 11/11 baseline certification pending
**Priorità**: P1 (audit gap: nessun tracking per-nodo di memory hot-path metrics nei
5 nodi più caldi; gate CI inesistente)

---

## Problema

Il `RenderEngine` espone `RenderCounters` (in
`include/chronon3d/core/profiling/render_counter_types.hpp`, popolato via
`thread_local_counters()` + `merge_tls()`) e `NodeTelemetryRecord` (in
`include/chronon3d/runtime/telemetry/render_telemetry_record.hpp`, popolato
via `emit_node_records(ctx, node, ...)`, dentro
`src/render_graph/executor/node_runner.cpp::execute_single_node`).

Ma **nessuna di queste superfici** traccia per-nodo le **metrics di memory
hot-path** richieste:
- `pixels_read` / `pixels_written` (sample cost sul framebuffer)
- `bytes_read` / `bytes_written` (memcpy + sampling bytes)
- `framebuffer_copies` (memcpy tra fbs)
- `framebuffer_clears` (counter clear espliciti, es. nei ClearNode)
- `allocations` (deve essere **0** nel report post-warmup; l'implementazione
  corrente conta eventi di allocazione del backing store `Framebuffer`, non
  tutte le allocazioni heap C++)
- `temporary_buffers` (l'implementazione corrente attribuisce il framebuffer
  risultato del nodo e i relativi live bytes; non conta ogni scratch interno)

Risultato: regressione di hot-path memory (es. un CompositeNode che
reintroduce un heap alloc, o un TextRunNode che ricrea un vector mid-frame)
passa silente attraverso 11/11 baseline perché la suite non ha modo di
farlo osservare. AGENTS.md §"Cercare prima il codice esistente" +
§"Non segnare verde una suite che restituisce failure" + §"No push
divergence silent" → questo gap è un blocker di osservabilità
che apre le porte a §honesty rot-pattern.

Il `NodeTelemetryRecord` (esistente) traccia già in parte:
- `pixels_touched` / `pixels_cleared` / `pixels_composited` /
  `pixels_transformed` / `pixels_blurred` (categorical per-kind)
- `output_bytes` (singolo)

Ma **non** traccia: `allocations`, `temporary_buffers`, `bytes_read`,
`bytes_written`, `framebuffer_copies`, `framebuffer_clears`. La
categorizzazione esistente è per-kind e non per-node; una regression
localizzata (es. solo CompositeNode) non viene isolata.

### Precisazione sulla copertura delle allocazioni

L'implementazione di questa scheda è un accounting per-node degli eventi
Framebuffer e del ciclo di vita delle superfici risultato, non un hook globale
per l'heap C++. `framebuffer_increment_allocations()` emette l'evento
`NodeMemoryEvent::Allocation`; `ScopedNodeMemory::record_allocation()` esiste,
ma non ha call-site di produzione nel checkout. Inoltre `node_runner` passa la
dimensione del risultato a `set_live_bytes()`, che contribuisce a
`temporary_buffers`. Il gate `check_node_memory_metrics.sh` quindi può
certificare **zero allocazioni backing-store Framebuffer nelle frame misurate**;
non dimostra zero `new`/`std::vector`/PMR allocazioni arbitrarie. Il tracking
heap generale resta il forward-point `TICKET-PMR-HOOKS-PER-NODE`.

## Evidenza (machine-verified 2026-07-13)

| Site | File | Note |
|------|------|------|
| Orchestrator | `src/render_graph/executor/node_runner.cpp` :: `execute_single_node` | `emit_node_records(...)` chiamato qui — population site CANONICO |
| Telemetry struct | `include/chronon3d/runtime/telemetry/render_telemetry_record.hpp` :: `NodeTelemetryRecord` | esiste ma senza `allocations` / `temporary_buffers` / bytes_read |
| Counter macros | `include/chronon3d/core/profiling/render_counter_macros.hpp` | esistente X-macro taxonomy (155+ counters) ma globale, non per-nodo |
| Profiler per-thread | `include/chronon3d/render_graph/core/graph_profiler.hpp` :: `RenderProfiler::record_node_tls` | esistente TLS pattern per-profiler ma limitato a duration_ms + cache_hit |
| std::pmr hooks | `src/render_graph/nodes/*` + `framebuffer_lifetime.hpp` + `input_resolver.hpp` | gia integrato nei node executor — `std::pmr::memory_resource` + `std::pmr::vector` esposti ma senza per-node counter linkage |
| 5 hottest nodes (per existing X-macro volume + telemetry counts) | `CompositeNode`, `ClearNode`, `TextRunNode`, `EffectNode` (Dof/Lighting/Glow), `TransformNode` | rispettivamente: `composite_calls/pixels + clearnode_*` (Composite copre entrambi come fase finale), `clear_calls/pixels + clearnode_memcpy_*` (ClearNode), `text_glyphs_rasterized + text_layout_ms + text_rasterization_ms` (TextRunNode), `effect_stack_calls/pixels + frame_conversion_copy_ms` (EffectNode), `transform_calls/pixels + projected_winding_flips` (TransformNode) |
| Existing CI gate template | `tools/check_determinism.sh` | benchmark brutale post-fix critical path (sha256/jq/convert) — non lightweight come serve qui |
| Existing CI gates landscape | `tools/check_architecture_boundaries.sh` (24 checks) + `tools/check_determinism.sh` + `tools/check_diagnose_*` + ... | nessun gate per-node memory accounting |

## Impatto

Senza questo accounting:
1. **Regressione memory hot-path non osservabile**: push che reintroduce
   heap alloc in `CompositeNode::execute()` o `TextRunNode::execute()`
   passa 11/11 baseline → §honesty rot-pattern + §"Non segnare verde
   una suite che restituisce failure" violato.
2. **Allocazioni frame-fragmentation non quantificabili**: `arena.hpp` +
   `framebuffer_pool.hpp` sono tuned ma senza baseline per-nodo non si
   può quantificare l'impatto di una nuova cache policy.
3. **Per-nodo memory budget non enforceable**: `bench_corpus` (B00-B11)
   non ha un budget per-nodo → regressioni in B07 nested-precomp o
   B11 portrait glow non vengono isolate al nodo colpevole.

## Confine (scope)

**DENTRO scope**:
- `include/chronon3d/render_graph/core/node_memory_metrics.hpp` (NEW canonical struct)
- `src/render_graph/executor/node_runner.cpp` (population minimale nei 5 siti `emit_node_records`)
- `include/chronon3d/runtime/telemetry/render_telemetry_record.hpp` (estensione `NodeTelemetryRecord`)
- `tools/check_node_memory_metrics.sh` (NEW gate CI)
- ADD `tools/check_node_memory_metrics.sh` a `tools/check_doc_sync.sh` gate-list-SSOT
  (forward-point) + `cmake/Chronon3DRegistry.cmake` (post-MERGE)
- Wire nel `chronon3d_architecture_check` custom target AGENTS.md §2
- ADR-026 (mandatory) — "No nuovi singleton/registry/resolver/cache senza ADR"
- `docs/CHANGELOG.md` entry + `docs/FOLLOWUP_TICKETS.md` row
- `tests/ci/test_check_node_memory_metrics_selftest.sh` (gate self-test
  Cat-3 sibling pattern, attestazione che `OK:` + `GATE_FAIL` paths sono
  entrambi coperti — parallelo a `tests/ci/..._selftest.sh` precedent)

**FUORI scope** (forward-points):
- 10+ nodi rimanenti (SourceNode, PrecompNode, TransitionNode, VideoNode,
  AdjustmentNode, MotionBlurNode, ColorConvertNode, TrackMatteNode,
  MultiSourceNode, MaskNode) — extension sotto `TICKET-EXPAND-NODE-METRICS`
- Allocazione-tracking via `std::pmr` hooks (più invasivo; cat-5 deferred
  a `TICKET-PMR-HOOKS-PER-NODE`)
- Allocazione per-kind totals (vs per-node) — già coperto da
  `RenderCounters` esistente thread-local; no duplicazione
- Replacement del `NodeTelemetryRecord` esistente — backward-compat:
  AGGIUNGERE i nuovi field, NON rinominare i pre-esistenti (Categorical
  per-kind + per-node metrics coesistono)

## Soluzione accettabile

1. **NEW struct** in
   `include/chronon3d/render_graph/core/node_memory_metrics.hpp`:
   ```cpp
   namespace chronon3d::graph {
   /// Per-node memory accounting. Populated minimal in node_runner.cpp
   /// for the 5 hottest nodes (per existing telemetry X-macro volumetrics).
   /// Steady-state `allocations` MUST be 0 (enforced by
   /// tools/check_node_memory_metrics.sh).
   struct NodeMemoryMetrics {
       uint64_t pixels_read{0};        // sampled from input fbs
       uint64_t pixels_written{0};     // written to output fbs
       uint64_t bytes_read{0};         // bytes sampled ≈ pixels_read × bpp
       uint64_t bytes_written{0};      // bytes written = pixels_written × bpp
       uint64_t framebuffer_copies{0}; // memcpys between fbs
       uint64_t framebuffer_clears{0}; // explicit clears (ClearNode path)
       uint64_t allocations{0};        // heap allocs in steady-state (MUST be 0)
       uint64_t temporary_buffers{0};  // scratch buffer creates mid-node
   };
   } // namespace chronon3d::graph
   ```

2. **Population minimal in `src/render_graph/executor/node_runner.cpp`**:
   aggiungere `out_metrics` (default nullptr) come parametro di
   `execute_single_node` (parallelo a `out_cache_ms`, `out_dirty_ms`,
   `out_telemetry_ms`, `out_execute_ms`); popolare da:
   - `pixels_read/written/bytes_*: pre/post `run_node()` inspect su cache_eval.result.width/height × bpp + input fbs size (via `cache_eval.result.use_count()`)
   - `framebuffer_copies/clears: campionatura da `state.resolved_cache_hit[id]` / `state.temp[id].use_count()` su LRU transition
   - `allocations/temporary_buffers`: instrumented via `thread_local_counters().framebuffer_allocations` delta across `run_node()` (existing macro taxonomy)

3. **Estensione `NodeTelemetryRecord`** in
   `include/chronon3d/runtime/telemetry/render_telemetry_record.hpp`:
   append 8 nuovi campi (backward-compat: i 5 esistenti per-kind restano)
   + forwarded-da `emit_node_records(...)` extension.

4. **NEW gate `tools/check_node_memory_metrics.sh`**:
   - Input: latest telemetry JSON da `bench_corpus B00-B11` (forward-point
     a `TICKET-NODE-MEMORY-METRICS-MACHINE-VERIFY` per produzione json)
   - Logic: parse `NodeTelemetryRecord` array; per ogni record in frame >
     warmup_count (default 5): se `kind ∈ {Composite, Clear, TextRun,
     Effect, Transform}` E `metrics.allocations > 0` → FAIL with
     `[INFO]` + `GATE_FAIL` secondo AGENTS.md §"INFO-level diagnostic
     style"
   - Style: bash shell script con `set -euo pipefail`; usa `jq` per
     parsing; emit `OK:` su PASS + `[INFO] check_node_memory_metrics:
     clean state ...` come riga addizionale (forward-point
     `TICKET-INFO-DIAGNOSTIC-LINT`, parallelo al pattern del precedent
     check_test_suite_registration)
   - Self-test attestazione via
     `tests/ci/test_check_node_memory_metrics_selftest.sh` (entambi
     PATH: PASS + FAIL con mock fixtures)

5. **ADR-026 (mandatory)**: scope `Cat-1 public surface expansion`
   (nuovo header `include/chronon3d/...` aggiunge campo al public ABI
   surface; `BackwardsCompat` deve coprire l'estensione non-breaking)
   AGENTS.md §Regole di lavoro → ADR first, no commit senza.

6. **Wire nel 11/11 baseline**: dopo machine-verify di
   `TICKET-NODE-MEMORY-METRICS-MACHINE-VERIFY` (run bench_corpus B00-B11
   → `docs/baselines/bench-corpus-v1-p50p95.csv` come records
   per-node memory).

## Criteri di accettazione

- [x] `NodeMemoryMetrics` compila standalone e resta documentato in
      `include/chronon3d/render_graph/executor/node_memory_metrics.hpp`.
      Il path originale `render_graph/core/` era stato sostituito dalla
      struttura executor già canonica nel checkout.
- [x] L’executor popola gli otto campi nel singolo scope per nodo in
      `node_runner_single_node_detail.hpp`, incluso dai runner canonici;
      la refactor dell’executor ha sostituito i cinque call site distinti
      `emit_node_records` dello sketch storico. Il test telemetry attraversa
      nodi clear e copy e verifica tutti gli otto contatori.
- [x] `NodeTelemetryRecord` ha gli otto campi additivi e conserva i campi
      preesistenti.
- [x] `tools/check_node_memory_metrics.sh` passa su report pulito e fallisce
      su allocazioni hot-node; `bash -e` è verificato sul report CPU locale.
- [x] `tests/ci/test_check_node_memory_metrics_selftest.sh` copre report
      pulito, allocating e mancante.
- [x] ADR-026 è ACCEPTED e collegato da entrambi i documenti.
- [ ] Representative B00-B11 corpus report and 11/11 baseline suite pass on
      the integration SHA (`tools/wrap_push.sh origin main`,
      `tools/check_main_clean.sh`, and final SHA-triple equality).

Implementation, ADR, local report validation, and gate self-test are complete
in this checkout. The remaining acceptance item is corpus/integration
certification; it cannot be claimed from the available CPU smoke. The B00-B11
runner and content pack are absent from this checkout. Commit subject and
post-merge SHA checks are integration workflow checks, not implementation gaps.

## Forward-points

1. **TICKET-NODE-MEMORY-METRICS-MACHINE-VERIFY**: run `bench_corpus B00-B11`
   (configs/corpus/b*yaml + examples/bench_corpus/run_corpus_v1.sh) per
   produrre json records per-node; fixture per
   tools/check_node_memory_metrics.sh (PASS + FAIL scenarios); target:
   `docs/baselines/bench-corpus-v1-p50p95.csv` come documentazione di
   baseline per-node memory budget.
2. **TICKET-EXPAND-NODE-METRICS**: applica la stessa population
   pattern (5 nodi → 15+ nodi: SourceNode, PrecompNode, TransitionNode,
   VideoNode, AdjustmentNode, MotionBlurNode, ColorConvertNode,
   TrackMatteNode, MultiSourceNode, MaskNode, OutputNode, Output-related
   nodes). Wire nel gate CI come `strict-mode` (FAIL su QUALSIASI
   allocations > 0 su QUALSIASI nodo).
3. **TICKET-ALLOCATIONS-FAIL-LOUD-CI**: promuove
   `tools/check_node_memory_metrics.sh` a blocco del 11/11 baseline gate
   (attualmente advisory). Pre-requisito: machine-verify di tutti i
   15+ nodi su `bench_corpus B00-B11` con fragments = 0 allocazioni.
4. **TICKET-PMR-HOOKS-PER-NODE**: allocazione-tracking via `std::pmr`
   hooks più invasivo (cat-5 deferred); quando i `framebuffer_pool.cpp`
   ed `arena.hpp` instrumented hops potranno popolare
   `NodeMemoryMetrics.allocations` da un raw counter (no proxy da
   `thread_local_counters().framebuffer_allocations`).
5. **TICKET-INFO-DIAGNOSTIC-LINT** (cross-link AGENTS.md §"INFO-level
   diagnostic style"): forward-point per lint tooling che verifica
   `tools/check_*.sh` emission pattern (`[INFO] ${GATE_NAME}: ...`
   su PASS addizionale al canonico `GATE_PASS`); questo ticket è uno
   dei gate-3+ che seguono il pattern.

## Origine

Apertura 2026-07-13 come ticket osservabilità per-nodo in
follow-up al TICKET-SIMD-REGISTRY-CANONICAL (aperto stesso giorno,
chore SHA `be0ca5e8` su origin/main ↔ questo ticket è la naturale
continuazione sul lato accounting dopo il resolver design). User
directive: "Apri ticket NodeMemoryMetrics per-nodo: struct
pixels_read/written, bytes_read/written, framebuffer_copies/clears,
allocations, temporary_buffers; popolamento minimo in node_runner.cpp per
i 5 nodi più caldi; nuovo gate check_node_memory_metrics.sh che fail-loud
su allocations_per_node>0 nei kernel steady-state."

## Cross-link

- AGENTS.md §"Cercare prima il codice esistente" + §"Non segnare verde
  una suite che restituisce failure" → questo ticket chiude un audit
  gap strutturale (5 hottest nodes senza alloc tracking observable).
- AGENTS.md §"No nuovi singleton/registry/resolver/cache senza ADR"

## Checkout audit — 2026-09-24 (`187c22f3b`)

The original implementation sketch is stale and must not be applied literally:

- The canonical types already live in
  `include/chronon3d/render_graph/executor/node_memory_metrics.hpp`; runtime
  accumulation is in `include/chronon3d/internal/render_graph/node_memory_tracker.hpp`.
- The active execution scope is in
  `src/render_graph/executor/node_runner_single_node_detail.hpp` (included by
  `node_runner.cpp`), not in five `emit_node_records()` call sites. That helper
  records temporary-buffer/live-byte lifetimes and the output framebuffer's
  live size; the requested pixel, byte, copy, clear, and allocation counters
  are not fed into it.
- `NodeStatsReporter*` is only a nullable context field; there are no runtime
  callers. `benchmark_args.hpp` declares `stats_json_file`, but the checkout
  has no writer using it. The `NodeSummaryTelemetryRecord` has memory fields,
  but there is no producer that fills or persists those rows from the session
  tracker. `NodeTelemetryRecord` is consumed by detailed telemetry stores, so
  it is the supported per-frame event surface.
- `tools/check_node_memory_metrics.sh` and its self-test do not exist.

The old checklist's `tools/check_doc_sync.sh`, `docs/CHANGELOG.md`, and
`examples/bench_corpus/run_corpus_v1.sh` are absent from this checkout. Gate
registration now belongs to `tools/gates/manifest.sh`; the full B00-B11
machine-verification input is unavailable here, so the historical baseline
acceptance remains explicitly pending rather than inferred from the CPU smoke.

### Re-audit after benchmark-fleet demolition — 2026-09-24

The B00-B11 CPU factories and their corpus runner are recoverable from Git
history, but they were deliberately removed in `d1d8265b5`
(`chore(demolition): remove visual/golden/demo fleet and dead tooling`). The
current `docs/benchmarks/gpu-corpus-v1.json` is a different, active manifest:
its B00-B11 scene names are resolved by an external content pack. The current
runtime composition registry exposes only `DarkGridBackground` and
`CameraImageClip`; neither manifest-named B00-B11 compositions nor the old
factory registrations are available in this checkout. The in-memory
`chronon3d_bench` target is also optional (`CHRONON3D_BUILD_BENCHMARK_CLI=OFF`)
and, even when enabled, registers the same current runtime compositions. Its
`--node-memory-json` path is CPU-capable, so the missing deliverable is a
representative set of runnable corpus scenes, not GPU certification. The
retired runner and factories are historical source, not current executable
evidence.

The two current built-in CPU reports remain smoke evidence only. They pass the
per-node allocation gate but cannot satisfy the representative B00-B11
acceptance item while the external scene pack (or a replacement CPU corpus)
is unavailable.

The existing structs/schema/tests proved the **data shape**, not the requested
runtime metrics. A local implementation increment now wires input/output pixel
and byte volumes into `ScopedNodeMemory`, and observes framebuffer backing-store
allocations, copies, and clears through a stack-scoped profiling sink. The
allocation fields deliberately describe these instrumented events plus explicit
`record_allocation()` calls; they are not general heap counters. The change is
covered by `tests/perf/test_node_memory_tracker.cpp`; an active-scope framebuffer
test exercises all eight reported counters (read/write pixels and bytes, copy,
clear, backing-store allocation, and temporary buffers). The memory suite
passes 12/12 cases and 111/111 assertions on this checkout.
`NodeTelemetryRecord` now has the eight additive counters and the executor
emits them only at Detailed/Trace capture level; a graph executor integration
test drains the actual event store from clear-producing and copy-producing
graphs and verifies that all eight additive counters are nonzero across the
captured events.

The session stats path is now exposed by the benchmark CLI as the separate
`--node-memory-json` output; the existing fusion-only `--stats-json` output is
unchanged. The report contains post-warmup measured frame count and the
`NodeMemoryReport` per-node counters. `tools/check_node_memory_metrics.sh`
validates report completeness and fails on nonzero post-warmup allocations;
`tests/ci/test_check_node_memory_metrics_selftest.sh` covers clean, allocating,
and missing-report cases.

The ticket remains OPEN pending a representative corpus run. A local CPU
smoke run used `DarkGridBackground` (`--frames 2 --warmup 1`; the benchmark
framework executed 11 measured iterations), emitted 22 measured frames and
two node rows (`Composite`, `Source`), and passed the gate with zero hot-node
allocations. The report at `.tmp/node-memory.json` was inspected; no GPU or
external host certification is implied. The existing summary persistence
projection still has no producer for `NodeSummaryTelemetryRecord` rows; the
new CLI report is the verified aggregate output path, while detailed per-frame
events are available through the existing node telemetry store. Do not infer
framebuffer copies or heap allocations from `shared_ptr::use_count()` or a
device/global counter.

An additional `CameraImageClip` CPU run needed the explicit asset root:
without `CHRONON3D_CLI_ASSETS_ROOT`, preparation reported the generated asset
`output/cache/dark_grid_background/ecd437cfe1127908.png` missing even though it
exists under the checkout. With the root mounted, the run emitted two measured
frames and two node rows (`Composite`, `Source`) and passed the gate with zero
hot-node allocations. This is a second runnable built-in scene, not a
multi-shape proxy for the absent B00-B11 corpus.

### Refreshed CPU evidence — 2026-09-24

Re-ran both built-in shapes with the current `linux-fast-bench` binary,
`--frames 10 --warmup 3`, and `--node-memory-json`:

| Scene | Measured frames in report | Rows | Composite allocations | Source allocations | Gate |
|---|---:|---:|---:|---:|---|
| `DarkGridBackground` | 110 | Composite, Source | 0 | 0 | PASS |
| `CameraImageClip` | 10 | Composite, Source | 0 | 10 | PASS |

Both reports were written under ignored `.tmp/` paths and passed
`tools/check_node_memory_metrics.sh`. The gate intentionally enforces the
zero-allocation rule for hot node types (Composite, Output, TextRun, Effect,
Transform); Source allocations are visible in the report but outside that
current gate contract. These CPU built-ins still do not cover the B00-B11
representative corpus or certify GPU behavior.

The current `linux-fast-dev` test binaries were rebuilt and run for the
instrumentation paths: `chronon3d_memory_tests` passed 12/12 cases and 111/111
assertions; the detailed telemetry integration test passed 1/1 case and 11/11
assertions. It reads actual emitted node records for clear and copy graphs and
checks all eight additive fields. This verifies local counter/event wiring,
not corpus coverage.

### Gate self-test re-run — 2026-10-11

`bash tests/ci/test_check_node_memory_metrics_selftest.sh` passed. It exercised
the clean report (`GATE_PASS`), allocating report (`GATE_FAIL` as expected),
and missing-report path (`GATE_FAIL` as expected), then emitted
`SELFTEST_PASS: clean, allocating, and missing-report paths verified`. This is
CPU gate evidence only; the representative B00-B11 corpus remains unavailable
in this checkout and is still required for ticket acceptance.

The executor telemetry regression was also rebuilt and rerun on
`linux-fast-dev`: `NodeMemoryMetrics are emitted on detailed node telemetry
events` passed **1/1 case, 11/11 assertions**. It reads actual records from
clear and copy graphs and validates all eight additive fields. This confirms
local CPU event wiring, not per-node measurements for the B00-B11 corpus.

### Corpus availability confirmation — 2026-10-11

The workspace owner confirmed that the required B00-B11 content pack is not
available in this workspace. The remaining corpus and 11/11 baseline
acceptance cannot be run from the current checkout; the ticket stays OPEN.

  → ADR-026 forward-point come pre-requisito impegment di implementation.
- AGENTS.md §"INFO-level diagnostic style" → il nuovo gate deve
  seguire `[INFO] check_node_memory_metrics: ...` su PASS addizionale
  al canonico `GATE_PASS`.
- AGENTS.md §"Post-push SHA-selfcheck invariant" + §21ece2b3 → durante
  implement, multi-agent race-window su cat-5 2-doc same-commit è il
  pattern atteso (precedent: SIMD REGISTRY CANONICAL recovery).
- ADR-020 (dependency registry) → ADR-026 deve dichiarare non-dup
  vs `RenderCounters` thread-local + `NodeTelemetryRecord` categoricals.
- TICKET-SIMD-REGISTRY-CANONICAL (aperto chore `be0ca5e8`) → ticket
  sibling (stesso P1 weight, stesso giorno, focus differente: SIMD
  dispatch vs memory accounting).
- TICKET-BENCHMARK-CORPUS-OFFICIAL (configs/corpus/b00..b11.yaml +
  examples/bench_corpus/) → forward-point #2 (machine-verify) usa
  runner canonico `examples/bench_corpus/run_corpus_v1.sh`.
- Categoria Cat-3 anti-dup: cronaca estesa solo in questa scheda
  ticket (canonical discipline AGENTS.md §"Docs canonical update
  discipline rule").
