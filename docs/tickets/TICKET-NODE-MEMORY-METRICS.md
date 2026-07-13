# TICKET-NODE-MEMORY-METRICS — Per-nodo memory accounting + gate CI

**Stato**: TODO OPEN (2026-07-13)
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
- `allocations` (heap alloc in steady-state; deve essere **0**)
- `temporary_buffers` (scratch buffer per-node creates)

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

- [ ] `include/chronon3d/render_graph/core/node_memory_metrics.hpp`
      esiste, compila in modo standalone, namespace
      `chronon3d::graph::NodeMemoryMetrics` documentato in commento.
- [ ] `src/render_graph/executor/node_runner.cpp` popola i 8 campi nei
      5 siti `emit_node_records` (Composite, Clear, TextRun, Effect,
      Transform — chiamate distinte, NON un singolo helper).
- [ ] `include/chronon3d/runtime/telemetry/render_telemetry_record.hpp::NodeTelemetryRecord`
      esteso con 8 nuovi campi backward-compat (pre-esistenti non rimossi).
- [ ] `tools/check_node_memory_metrics.sh` esiste, `bash -e` passa, fa
      PASS su mock-empty-allocs + FAIL con mock-allocs>0.
- [ ] `tests/ci/test_check_node_memory_metrics_selftest.sh` attesta
      PASS + FAIL path con mock fixtures (cat-3 sibling self-test pattern).
- [ ] ADR-026 approvato e linkato sia dal che verso questo ticket
      (`docs/adr/ADR-026-node-memory-metrics.md`).
- [ ] Subject envelope commit principale ≤72 char (canonical
      `chore(...)` o `feat(...)`).
- [ ] `git diff origin/main..HEAD -- docs/CURRENT_STATUS.md docs/FOLLOWUP_TICKETS.md docs/CHANGELOG.md docs/ROADMAP.md`
      rispetta `Docs canonical update discipline` rule (cronaca estesa SOLO
      nel ticket, canonici ≤1 riga sintetica con link al ticket).
- [ ] 11/11 baseline suite verde post-merge (gate `tools/wrap_push.sh
      origin main` + tools/check_main_clean.sh + final SHA-triple equality
      verificato).

## Forward-points

1. **TICKET-NODE-MEMORY-METRICS-IMPLEMENTATION** (next chord): implementa
   `node_memory_metrics.hpp` + population-in `node_runner.cpp` +
   extension `NodeTelemetryRecord` + ADR-026 + tools/check_node_memory_metrics.sh
   + tests/ci/test_check_node_memory_metrics_selftest.sh. Subject
   envelope tipico: `feat(render_graph): per-nodo memory metrics
   (5 hot + fail-loud gate)`.
2. **TICKET-NODE-MEMORY-METRICS-MACHINE-VERIFY**: run `bench_corpus B00-B11`
   (configs/corpus/b*yaml + examples/bench_corpus/run_corpus_v1.sh) per
   produrre json records per-node; fixture per
   tools/check_node_memory_metrics.sh (PASS + FAIL scenarios); target:
   `docs/baselines/bench-corpus-v1-p50p95.csv` come documentazione di
   baseline per-node memory budget.
3. **TICKET-EXPAND-NODE-METRICS**: applica la stessa population
   pattern (5 nodi → 15+ nodi: SourceNode, PrecompNode, TransitionNode,
   VideoNode, AdjustmentNode, MotionBlurNode, ColorConvertNode,
   TrackMatteNode, MultiSourceNode, MaskNode, OutputNode, Output-related
   nodes). Wire nel gate CI come `strict-mode` (FAIL su QUALSIASI
   allocations > 0 su QUALSIASI nodo).
4. **TICKET-ALLOCATIONS-FAIL-LOUD-CI**: promuove
   `tools/check_node_memory_metrics.sh` a blocco del 11/11 baseline gate
   (attualmente advisory). Pre-requisito: machine-verify di tutti i
   15+ nodi su `bench_corpus B00-B11` con fragments = 0 allocazioni.
5. **TICKET-PMR-HOOKS-PER-NODE**: allocazione-tracking via `std::pmr`
   hooks più invasivo (cat-5 deferred); quando i `framebuffer_pool.cpp`
   ed `arena.hpp` instrumented hops potranno popolare
   `NodeMemoryMetrics.allocations` da un raw counter (no proxy da
   `thread_local_counters().framebuffer_allocations`).
6. **TICKET-INFO-DIAGNOSTIC-LINT** (cross-link AGENTS.md §"INFO-level
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
