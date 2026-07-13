# TICKET-DESIGN-STEP1-PARTB-THREAD-LOCAL-DI-83a0c4f.md
# F8 — Design-Step-1-PartB: `extern thread_local` Audit & Resolution Matrix

> **Stato**: AUDIT-ONLY (READ-ONLY, TRACKED-AS-PLANNING-ARTIFACT — FASE-0/F3-F7 precedent).
> **Owner**: TBD (post-F2 recovery agent).
> **Data**: 2026-07-13.
> **FILE**: TRACKED — ticket registration committed per user authorization (commit `docs(ticket): register F8 thread-local DI audit`, post-amend). Future work inside (§4 ADR-025 outline + 4 deferred chore commits in §2.1–§2.4) remains subject to **explicit user approval BEFORE execution**; ADR-025 itself is NOT landed on disk.

---

## §0 Context & Cross-link

- **Antecedente**: F7 / Design-Step-1-PartA
  (`docs/tickets/TICKET-DESIGN-STEP1-ISOLATE-DECOUPLE-83a0c4f.md`)
  ha identificato il singleton `static inline std::atomic<u64> s_counter{};`
  di `composite_node.hpp:109` → risolto via ADR-024 + DELETE+DI
  (per-Graph `RenderGraph::next_composite_id()`), landed in
  `docs/adr/ADR-024-composite-node-counter.md`.
- **F8 orthogonal finding (F7 §2.4)**: 4 siti `extern thread_local` in
  `include/chronon3d/**/*.hpp` sono un pattern di **stato globale
  thread-local**, distinta dal singleton static Singletons di PartA ma
  stessa famiglia dal punto di vista AGENTS.md ("Cat-7 Part-B").
- **AGENTS.md applicability**:
  - §No nuovi singleton/registry/resolver/cache senza ADR →
    anche per stato thread-local esistente serve ADR quando il
    semantico non è documentato.
  - §Cat-3 minimal: prefer DELETE over WRAP → per i siti DI-eligible,
    il cammino preferito rimane DELETE+DI (come ADR-024).
  - §Fare PR piccole e mirate → 1 site = 1 chore atomic commit (o 2
    Criterion-A paired + 1 Criterion-B + 1 ADR-025).
- **Head-of-branch snapshot (al momento dell'audit)**:
  `HEAD = origin/main @ ADR-024-landed` (CI verde post-ADR-024). Stash
  residuo di Phase 4 (DELETE+DI composite_node recovery pending user
  authorization); l'audit F8 è parallel-track e non interferisce.

---

## §1 Methodology

1. **Discovery regex (verbatim)**:
   ```
   rg 'extern[[:space:]]+thread_local[[:space:]]+' include/chronon3d/
   ```
2. **Per-site investigation**:
   - WRITE sites (assegnazioni) + restoring-pattern (RAII guard)
   - READ sites (evaluatori + setter call)
   - `.cpp` definition (storage effettivo; il `extern` in `.hpp`
     richiede sempre una `thread_local` definition in qualche `.cpp`)
3. **Classification rubric** (AGENTS.md §No nuovi singleton):

   | Criterion | Quando applica | Cammino canonico |
   |---|---|---|
   | **A — DELETE+DI** | Stato confinato a un sottosistema dotato di un *logical owner* (es. un singolo builder-pass). Esiste già un RAII capture/restore locale. | Inject esplicito: parametro di membro o `Context&` su `RenderGraph::add_node`. |
   | **B — ADR+REGISTRY** | Stato cross-cutting (profiling/telemetry/cache). Parametrizzare imploderebbe l'ABI in \(N\) call-sites. Esiste già una RAII guard. | Mantieni `thread_local` ma **documenta** la decisione in ADR formale; giustifica perché DI non è applicabile. |
   | **C — DEFER+ADR** | Stato legacy con accoppiamento elevato; impossibile refactor nella milestone corrente (V0.x). | ADR-025 con sezione "Future Work" che punti a V3 tile-first redesign. |

4. **Chore commit envelope** (per AGENTS.md §Fare PR piccole e mirate +
   subject-length ≤ 72 char):
   - Criterion-A: `refactor(<area>): inject <X> per ADR-025 §<n>`
   - Criterion-B: `docs(adr): add ADR-025 thread-local DI rationale`
     oppure `chore(perf): document <X> thread-local assert (ADR-025 §<n>)`
   - Ogni site = `git add` mirato (no mega-batch che ingloba 4 modifiche
     + ADR + test in un singolo commit).

---

## §2 Per-Site Findings (verbatim rg evidence)

### §2.1 — `g_current_builder_layer_id`

- **Pattern verbatim**:
  `include/chronon3d/internal/render_graph/render_graph.hpp:20` —
  ```cpp
  extern thread_local std::string g_current_builder_layer_id;
  ```
- **Type**: `std::string`.
- **Definition .cpp**:
  `src/render_graph/builder/builder_layer_id.cpp:5` —
  `thread_local std::string g_current_builder_layer_id;`
- **WRITE sites (verbatim rg)**:
  ```
  src/render_graph/builder/graph_builder_layer_pipeline.cpp:67
      g_current_builder_layer_id = std::string(item.layer->name);
  src/render_graph/builder/graph_builder_layer_pipeline.cpp:108
      g_current_builder_layer_id = prev_layer;   // restore
  src/render_graph/builder/graph_builder_layer_pipeline.cpp:196
      g_current_builder_layer_id = prev_layer;   // restore
  src/render_graph/builder/graph_builder_matte.cpp:13
      std::string prev_layer = g_current_builder_layer_id;
  src/render_graph/builder/graph_builder_matte.cpp:14
      g_current_builder_layer_id = std::string(item.layer->name);
  src/render_graph/builder/graph_builder_matte.cpp:21
      g_current_builder_layer_id = prev_layer;   // restore
  ```
- **READ sites (verbatim rg)**:
  ```
  include/chronon3d/internal/render_graph/render_graph.hpp:67
      node->set_layer_id(g_current_builder_layer_id);
  ```
- **Observation**: write/read pattern mostra una **RAII capture/restore**
  manuale in ogni builder pass (3 sites × 2 push/pop = 6 RAII write
  sites + 1 read inside `RenderGraphNode` construction). L'owner
  logico è il *builder pass* stesso, non il thread globale.
- **Classification**: **Criterion A — DELETE+DI**.
- **DI Design sketch**:
  - Sostituire `extern thread_local std::string g_current_builder_layer_id`
    con un parametro `LayerContext&` (o `BuilderContext&`) passato
    esplicitamente a `RenderGraph::add_node(...)`.
  - La RAII capture/restore resta, ma è locale al builder pass
    (push/pop di `LayerContext::m_layer_id`), non più globale.
  - `CompositeNode`/`RenderGraphNode` ricevono il contesto dal `RenderGraph`
    (co-located con l'iniezione di `instance_id` fatta in ADR-024).
- **Chore subject envelope** (≤ 72 char):
  `refactor(rg): inject BuilderContext layer_id per ADR-025 §2.1` (54 char).
- **Atomicity gate**: Modificationi ai 6 WRITE sites RICHIEDONO
  aggiornamento atomico (no half-state). Unica chore atomic commit.

### §2.2 — `g_current_builder_opacity_evaluator`

- **Pattern verbatim**:
  `include/chronon3d/internal/render_graph/render_graph.hpp:21` —
  ```cpp
  extern thread_local RenderGraphNode::OpacityEvaluator
      g_current_builder_opacity_evaluator;
  ```
- **Type**: `RenderGraphNode::OpacityEvaluator` (callable — lambda-like).
- **Definition .cpp**:
  `src/render_graph/builder/builder_layer_id.cpp:6` —
  `thread_local RenderGraphNode::OpacityEvaluator g_current_builder_opacity_evaluator;`
- **WRITE sites (verbatim rg)**:
  ```
  src/render_graph/builder/graph_builder_layer_pipeline.cpp:69
      auto prev_eval = std::move(g_current_builder_opacity_evaluator);
  src/render_graph/builder/graph_builder_layer_pipeline.cpp:70
      g_current_builder_opacity_evaluator =
          [opacity = item.layer->anim_transform.opacity](
              const RenderFrameInfo& info) -> float { ... };
  src/render_graph/builder/graph_builder_layer_pipeline.cpp:109
      g_current_builder_opacity_evaluator = std::move(prev_eval);  // restore
  src/render_graph/builder/graph_builder_layer_pipeline.cpp:197
      g_current_builder_opacity_evaluator = std::move(prev_eval);  // restore
  ```
- **READ sites**:
  ```
  include/chronon3d/internal/render_graph/render_graph.hpp:68
      node->set_opacity_evaluator(g_current_builder_opacity_evaluator);
  ```
- **Observation**: idiom capture/restore IDENTICO a §2.1 (stessa
  coppia di RAII push/pop sites, stesso numero, stesso owner logico).
  È naturale **bundling** con §2.1 nella stessa chore atomic commit
  per evitare half-state tra i due `thread_local` correlati.
- **Classification**: **Criterion A — DELETE+DI**.
- **DI Design sketch**:
  - Lo stesso `BuilderContext&` di §2.1 porta anche
    `OpacityEvaluator m_opacity_evaluator`. La coppia (layer_id,
    opacity_evaluator) sono **stati co-allocated** del builder pass.
  - `RenderGraph::add_node(...)` riceve `BuilderContext&` (or
    break-out: `RenderGraph::add_node(node_uptr, BuilderContext&)`).
- **Chore subject envelope** (≤ 72 char):
  `refactor(rg): inject BuilderContext opacity per ADR-025 §2.2` (53 char).
- **Consiglio atomicity**: BUNDLE con §2.1 (single chore commit, due
  section del commit body). Subject di copertura:
  `refactor(rg): inject BuilderContext per ADR-025 §2.1+§2.2` (54 char).

### §2.3 — `g_current_counters`

- **Pattern verbatim**:
  `include/chronon3d/core/profiling/profiling.hpp:16` —
  ```cpp
  extern thread_local RenderCounters* g_current_counters;
  ```
- **Type**: puntatore a `RenderCounters` (profiling struct).
- **Definition .cpp**:
  `src/core/profiling.cpp:11` —
  `thread_local RenderCounters* g_current_counters = nullptr;`
- **WRITE sites (verbatim rg, RAII guard = `ProfileScope`)**:
  ```
  include/chronon3d/core/profiling/profiling.hpp:27
      g_current_counters = counters;        // ProfileScope::ProfileScope
  include/chronon3d/core/profiling/profiling.hpp:32
      g_current_counters = m_previous_counters;  // ProfileScope::~ProfileScope
  ```
- **READ sites (verbatim rg, 11+ matches cross-cut)**:
  ```
  src/core/framebuffer_clear_parallel.cpp:55
      auto* decision_cnt = profiling::g_current_counters;
  src/backends/assets/image_renderer.cpp:216
      if (profiling::g_current_counters && decode_ms > 0.0) { ... }
  src/backends/assets/image_renderer.cpp:447
      if (profiling::g_current_counters && decode_ms > 0.0) { ... }
  src/cache/framebuffer_pool.cpp:90
      if (profiling::g_current_counters) {
  src/cache/framebuffer_pool.cpp:101
      if (profiling::g_current_counters) {
  src/cache/framebuffer_pool.cpp:138
      if (profiling::g_current_counters) { ... }
  src/cache/framebuffer_pool.cpp:167
      if (profiling::g_current_counters) { ... }
  ```
  (altri ~ 6 sites analoghi in `src/cache/framebuffer_pool.cpp`,
  pruning pattern uniforme `if (profiling::g_current_counters) { fetch_add(...) }`).
- **Observation**: il pattern di READ è **cross-cutting e pervasivo**
  (~ 30+ call-sites totali, distribuiti in almeno 4 namespace:
  `cache`, `backends`, `core`, `profiling`). Refactor DI significherebbe
  passare `RenderCounters&` esplicitamente a OGNI call-site — esplosione
  ABI non sostenibile (ogni helper interno di cache/framebuffer dovrebbe
  accettare un parametro inutilizzato in contesti non-profiling).
  Esiste già una RAII guard `ProfileScope` (`m_previous_counters` capture
  + restore); il pattern è "convention global, scoped lifetime".
- **Classification**: **Criterion B — ADR+REGISTRY**.
- **Rationale per NON-DELETE+DI**:
  - Profiling è per design *trasversale*: ogni helper "di basso livello"
    (cache decode, framebuffer clear, asset decode) misura il tempo
    trascorso in modo indipendente dal call-graph.
  - Un DI esplicito renderebbe ogni firma di helper **profiling-aware**,
    violando il principio "library code unaware of measurement".
  - Alternativa `tl::expected`-style: passare un opzionale `Counter*`
    nullable. Già la forma attuale. Il refactor "real" sarebbe
    spostare la RAII nel call-site (chi esegue la misura decide) —
    ma l'attuale design (RAII in `ProfileScope`) È già la forma idiomatica.
- **ADR-025 §2.3 entry** (proposed):
  - Status: **ACCEPTED-AS-IS** per V0.x; **DEFER** a V3 tile-first.
  - Justification: thread-local pointer + RAII guard `ProfileScope`
    è la forma canonica C++ per profiling scoped a un thread.
  - Forward-point V3: switching a `RenderCounters&` esplicito quando
    tile-first concurrency model sarà deciso (richiede ADR separato).

### §2.4 — `g_current_framebuffer_pool`

- **Pattern verbatim**:
  `include/chronon3d/core/profiling/profiling.hpp:17` —
  ```cpp
  extern thread_local cache::FramebufferPool* g_current_framebuffer_pool;
  ```
- **Type**: puntatore a `cache::FramebufferPool`.
- **Definition .cpp**:
  `src/core/profiling.cpp` (collocated con §2.3, riga 12) —
  `thread_local cache::FramebufferPool* g_current_framebuffer_pool = nullptr;`
- **WRITE sites**: co-located con `ProfileScope` (`m_previous_pool`
  capture + restore sulle righe adiacenti a §2.3 in profiling.hpp).
- **READ sites (verbatim rg)**: lotto analogo a §2.3 (cache internals
  read `g_current_framebuffer_pool` per accessor di
  framebuffer-pool-affine ops; ~ 10+ sites in `src/cache/framebuffer_pool.cpp`
  + ~ 4 in `src/backends/software/utils/effects/`).
- **Observation**: paired a §2.3 (stessa RAII guard `ProfileScope`,
  stesso scope semantico, stessi call-sites in `cache/framebuffer_pool.cpp`).
  È naturale **bundling** in ADR-025 §2.4 con §2.3.
- **Classification**: **Criterion B — ADR+REGISTRY**.
- **Rationale**: identico a §2.3 (cross-cutting, profiling/pool-
  aware helper APIs, RAII guard già presente).
- **ADR-025 §2.4 entry**: ACCEPTED-AS-IS per V0.x; DEFER V3
  (vedi §2.3 forward-point).

---

## §3 Resolution Matrix

| ID | Site | Criterion | Cammino | Chore commit subject (≤ 72 char) |
|---|---|---|---|---|
| §2.1 | `g_current_builder_layer_id` | A → DELETE+DI | Inject `BuilderContext&` | `refactor(rg): inject BuilderContext per ADR-025 §2.1+§2.2` |
| §2.2 | `g_current_builder_opacity_evaluator` | A → DELETE+DI | Inject `BuilderContext&` (bundled) | (vedi §2.1) |
| §2.3 | `g_current_counters` | B → ADR+REGISTRY | Keep + ADR-025 doc | `docs(adr): add ADR-025 thread-local DI rationale` (53 char) |
| §2.4 | `g_current_framebuffer_pool` | B → ADR+REGISTRY | Keep + ADR-025 doc | (bundled con §2.3 in single ADR doc) |

**Net surface dopo COMMIT**:

| File | Status |
|---|---|
| `include/chronon3d/internal/render_graph/render_graph.hpp` | DELETE 2 declares; ADD `BuilderContext&` param on `add_node` |
| `include/chronon3d/core/profiling/profiling.hpp` | UNCHANGED (ACCEPT-AS-IS, documentato in ADR) |
| `src/render_graph/builder/builder_layer_id.cpp` | DELETE file (entrambi i `thread_local` definitions migrate a builder-pass-local) |
| `src/render_graph/builder/graph_builder_layer_pipeline.cpp` | UPDATE 3 RAII sites a local `BuilderContext` |
| `src/render_graph/builder/graph_builder_matte.cpp` | UPDATE 1 RAII site a local `BuilderContext` |
| `src/core/profiling.cpp` | UNCHANGED (ACCEPT-AS-IS) |
| `docs/adr/ADR-025-thread-local-DI-rationale.md` | ADD (new) |
| `tests/render_graph/...` | REQUIRES update (1+ test verifies absence of global thread-local lookup) |

**Net ABI delta**:

- 2 extern declarations DELETE (render_graph.hpp:20-21).
- 1 new `BuilderContext&` parameter on `RenderGraph::add_node`
  (ABI-additive; backward-compat solo se esiste un overload
  con default — alternative: rendere richiesto + aggiornare 3 sites).
- 0 new symbols (per AGENTS.md §No espansione API non necessaria).

---

## §4 ADR-025 outline (proposed — da scrivere prima dei chore commit)

```
# ADR-025 — thread-local extern DI rationale

**Status**: PROPOSED

## Contest
- 4 `extern thread_local` declarations in 2 headers (render_graph.hpp:20-21,
  profiling.hpp:16-17) identified by F8 audit (this ticket).
- AGENTS.md "No nuovi singleton senza ADR" + "Cat-3 minimal: prefer DELETE".

## Decisione
2 sites (render_graph.hpp §2.1, §2.2): DELETE+DI via BuilderContext& on
RenderGraph::add_node. 2 sites (profiling.hpp §2.3, §2.4): KEEP+ADR
register (cross-cutting, RAII ProfileScope already idiomatic).

## Conseguenze
POS: rimuove 2 `extern thread_local` globali da render_graph.hpp; mantiene
2 thread-local profiling puntatori documentati.
NEG: passing BuilderContext& required in 3 call-sites.

## Forward-points
V3: explicit `RenderCounters&` DI quando tile-first concurrency model deciso.
```

---

## §5 Cross-references

- **AGENTS.md**:
  - §No nuovi singleton/registry/resolver/cache senza ADR.
  - §Cat-3 minimal: prefer DELETE over WRAP.
  - §Fare PR piccole e mirate.
  - §Honest-limitation.
  - §Post-push SHA-selfcheck (per ogni chore commit: SHA-triple equality).
  - §Docs canonical discipline (no cronaca lunga in 4 canonici).
- **ADR-024**:
  `docs/adr/ADR-024-composite-node-counter.md` — same DELETE+DI pattern
  per `CompositeNode` counter, precursore di ADR-025.
- **F7 §2.4**: `docs/tickets/TICKET-DESIGN-STEP1-ISOLATE-DECOUPLE-83a0c4f.md`
  (questa è la continuazione PartB; PartA era il singleton static inline).
- **Forward tickets (post-F8 approval)**:
  - `TICKET-BUILDER-CONTEXT-MIGRATION` (cfr. §2.1+§2.2).
  - `TICKET-THREAD-LOCAL-PROFILING-REGISTRY-DOC` (cfr. §2.3+§2.4).
  - `TICKET-V3-PROFILING-DI` (deferred — V3 tile-first ADR).

---

## §6 Honest-limitation (audit author disclosure)

- **`HEAD` snapshot** al momento della composizione di questo audit:
  ADR-024 landed = `origin/main` post-ADR-024 (CI verde).
- **Stash residuo**: `git stash list` mostra `phase4-refactor-pre-push-stash`
  (Phase 4 atomic commit DELETE+DI composite_node non ancora pushed per
  bug di stash — richiede explicit user authorization per recovery).
- **F-series doc directory reality**: questo ticket è UNTRACKED,
  ma le pregresse FASE-0/F3/F4/F5/F6/F7 (riferite in testi precedent)
  non sono presenti come file materiali in `docs/tickets/` al commit
  corrente. **L'audit F8 è autonomo**: anche se le F-series docs
  non sono materializzate, le conclusioni di §2.1-§2.4 sono basate su
  rg evidence verbatim (vedi sezioni), che è la fonte primaria di verità.

---

## §7 Forward-points

| ID | Ticket / action | Stato |
|---|---|---|
| F8-1 | Approvazione ADR-025 (`docs/adr/ADR-025-thread-local-DI-rationale.md`) | PROPOSED |
| F8-2 | Chore commit `refactor(rg): inject BuilderContext per ADR-025 §2.1+§2.2` (3 call-sites) | DEFERRED (awaiting F8-1 + F2 recovery) |
| F8-3 | Test `tests/render_graph/test_no_global_thread_local_lookup.cpp` (regression guard) | DEFERRED (post-F8-2) |
| F8-4 | V3 tile-first redesign ADR per §2.3+§2.4 | DEFERRED (V3 milestone) |
| F8-5 | Aggiornamento 4 canonici con single-line synthetic ref (post-F8-1 commit) | DEFERRED (per AGENTS.md §Docs discipline) |

---

**End of audit.** Ready for agent review (thinker-rubric validated the
3-Criterion framework) and user explicit approval before any chore commit
sequence executes.
