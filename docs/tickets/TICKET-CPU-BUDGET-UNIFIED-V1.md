# TICKET-CPU-BUDGET-UNIFIED-V1 — Unified CpuBudget (bounded queue + mode enum)

## Stato: OPEN-APERTURA (2026-07-13, FASE 4.2 chore)

## Problema

`include/chronon3d/core/cpu_budget.hpp` (67 LoC, Cat-3 minimal-surface
config container) ships a 3-field `CpuBudget { render, decode, encode }`
struct with a `total()` method that computes the sum dynamically. The
prior setup implicitly lets `tbb::global_control` + FFmpeg thread pool +
x264 thread pool run as 3 INDEPENDENT pools, with no bounded-frame-queue
ceiling per pool — a single render job can queue up unbounded decode
frames, monopolizing memory on the worker host.

F4.2 (user-spec verbatim) mandates:

> struct CpuBudget { int total_threads; int render_threads;
>                    int decode_threads; int encode_threads; }
> decode: 2-4 frame; render: 2 frame; encode: 2-4 frame;
> ripartizione statica di default o dinamica con backlog.

The refactor adds 4 new public-API surface items:

1. **`int total_threads` field** — the canonical process-wide anchor
   stored AS A FIELD (not derived); replaces the `total()` dynamic
   sum. Existing `total()` method preserved (returns the field) for
   call-site back-compat.

2. **`*_max_inflight` fields** (3 of them) — bounded per-pool frame
   queue depth:
   - `render_max_inflight = 2` (canonical double-buffered render)
   - `decode_max_inflight` = 2 (Desktop/Laptop), 4 (Server), 1 (Embedded)
   - `encode_max_inflight` = 4 (Desktop/Server), 2 (Laptop), 1 (Embedded)

3. **`enum class BudgetMode { Static, Dynamic }`** — static
   allocation (default, matches prior behavior) vs dynamic-with-
   backlog (forward-pointed implementation per §Forward-points).

4. **`CHRONON3D_CPU_BUDGET_MODE` env var** — opt-in toggle for
   `BudgetMode::Dynamic`. Default mode for env-unset = Static.

## Soluzione

### 6-file change-set

**EDIT** `include/chronon3d/core/cpu_budget.hpp` (67 → ~120 LoC):
- ADD `int total_threads{0}` as 1st field per user-spec verbatim order
- Migrate `render/decode/encode` positions to follow (preserves Designer
  intent: `total_threads` is the process-wide anchor, per-pool is
  derived from it)
- ADD `int render_max_inflight{2}` + `decode_max_inflight{2}` +
  `encode_max_inflight{2}` with default-initializer 2 (lower bound)
- ADD `BudgetMode mode{BudgetMode::Static}` field
- KEEP `total()` method (returns `total_threads` field for back-compat)
- KEEP `empty()` method (returns `total_threads == 0` for back-compat)
- ADD `cpu_budget_mode_name(BudgetMode)` + `parse_cpu_budget_mode(...)`
  factory helpers (parallel to existing `cpu_machine_class_name` / `parse_cpu_machine_class`)

**EDIT** `src/core/cpu_budget.cpp` (155 → ~210 LoC):
- REWRITE `cpu_budget_for_class` to set `total_threads` field +
  max-inflight fields per class-conditional logic
- UPDATE `cpu_budget_from_environment` to honor `CHRONON3D_CPU_BUDGET_MODE`
- ADD 3 helper fns: `render_inflight_for(CpuMachineClass)`,
  `decode_inflight_for(...)`, `encode_inflight_for(...)`

**EDIT** `tests/core/test_cpu_budget.cpp` (143 → ~200 LoC):
- MIGRATE positional aggregate inits to C++20 designated initializers
  (positional init `{12, 2, 2}` would now ambiguously map to
  total_threads/render/decode because `total_threads` is 1st field
  per user-spec verbatim)
- ADD 3 NEW TEST_CASEs: `total_threads` equals factory input,
  per-pool max-inflight is class-conditional, BudgetMode
  parses + names roundtrip

**EDIT** `tools/measure_cpu_budget.sh` (~230 → ~260 LoC):
- ADD a B06-specific measurement hint noting the bottleneck =
  decode/encode queue saturation; the RSS/time measurement section
  now emits a `[INFO]` line citing B06 corpus
- macchina-verifica end-to-end is PARTIAL-WBH-required (this VPS
  cannot link `chronon3d_cli`); see §honesty-limitation disclosure

**NEW** `docs/tickets/TICKET-CPU-BUDGET-UNIFIED-V1.md` (this file).

**EDIT** `docs/CHANGELOG.md` — F4.2 entry prepended at TOP per Cat-5
newer-at-top + Cita-Only ticket-link (cronaca lives in this ticket
scheda per AGENTS.md §Disciplina di aggiornamento dei canonici).

### Back-compat strategy

Per AGENTS.md §regole "Fare PR piccole e mirate": minimum blast
radius. Existing positional `CpuBudget{12, 2, 2}` test cases MUST
migrate to designated init `{.render_threads=12, ...}` because the
struct order changed. 6 existing test cases + 3 NEW = 9 total.

### AGENTS.md compliance

- **Cat-3 minimal-surface**: 4 NEW public symbols (field + 3 fields +
  enum) + 2 NEW factory helpers (`cpu_budget_mode_name` +
  `parse_cpu_budget_mode`). 0 new SDK cat-1 surface — the changes
  are all bulk-replace on existing `CpuBudget`.
- **No new singleton/resolver/cache**: `BudgetMode::Dynamic` is
  forward-pointed (no implementation land). Static mode matches
  prior behavior 1:1 — no mutable global state emerges.
- **No ADR needed**: the dynamic-mode runtime manager (if/when
  implemented) will require ADR per AGENTS.md "no singleton"; the
  static-mode refactor does NOT trigger the rule.
- **Cat-5 2-doc same-commit**: this file + `docs/CHANGELOG.md`
  EDIT co-updated (per AGENTS.md ticket-home rule + §Disciplina di
  aggiornamento dei canonici).

### §honesty-limitation disclosure (B06 smoke test)

The user spec asks: "Smoke test: su B06 VideoOverlay1080p misurare
RSS + tempo totale; atteso RSS < baseline".

Per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` precedent (this VPS
lacks vcpkg glm/magic_enum + tmpfs quota + rot-cascade blockers),
macchina-verifica end-to-end is DEFERRED-WBH. The harness-ready
scaffold includes:

- `tools/measure_cpu_budget.sh` updated to emit a B06 measurement
  hint + the user-spec verbatim "RSS < baseline" assertion (`assert
  rss_after < rss_before`).
- 3 NEW doctest TEST_CASEs in `tests/core/test_cpu_budget.cpp`
  validate the new fields are set correctly per class preset (the
  verification ON THIS VPS; full B06 RSS/time E2E requires WBH).

The §honesty pattern mirrors prior CAT-15 / Test 19 disclosure: the
deliverable IS the harness + scope guards; the actual RSS measurement
on B06 is the working build host's job.

## Criteri di accettazione

- [ ] `include/chronon3d/core/cpu_budget.hpp` field order: `[total_threads, render_threads, decode_threads, encode_threads, *_max_inflight, mode]`.
- [ ] `cpu_budget_for_class(CpuMachineClass::Desktop, 16)` returns `total_threads=16, render=12, decode=2, encode=2` (per user-spec verbatim example).
- [ ] `*_max_inflight` fields: Desktop {r=2, d=2, e=4}, Server {r=2, d=4, e=4}, Laptop {r=2, d=2, e=2}, Embedded {r=2, d=1, e=1}.
- [ ] `BudgetMode { Static, Dynamic }` enum + `cpu_budget_mode_name` + `parse_cpu_budget_mode` envelope wires in correctly.
- [ ] `CHRONON3D_CPU_BUDGET_MODE=dynamic` honored by `cpu_budget_from_environment` end-to-end.
- [ ] Existing positional tests migrate to designated init (no broken positional init in the test file).
- [ ] 11/11 baseline suite verde on working build host (range: 12–16/11 currently; see CURRENT_STATUS.md).
- [ ] `bash tools/measure_cpu_budget.sh` exit 0 + [INFO] line citing B06 corpus on this VPS (PARTIAL-WBH OK).
- [ ] Forward-points (a, b) below implemented in next-chore.

## Out of scope (F4.2 = SCAFFOLD ONLY)

- `BudgetMode::Dynamic` runtime implementation FORWARD-POINTED to
  §Forward-point (a) below.
- Working-build-host macchina-verifica E2E RSS/time measurement
  PARTIAL-WBH per §honesty-limitation.
- Cat-3 fix to the pre-existing TWO-FILES-ADR-024 collision on
  `origin/main` (`ADR-024-composite-node-counter.md` +
  `ADR-024-deprecate-persistent-framebuffer-cache.md`): §honesty
  corruption visible in the repo pre-existing this ticket; deferred
  to a separate ADR-renumber chore per AGENTS.md "Fare PR piccole e
  mirate". Documented in §Cross-link below.

## Forward-points

(a) **`TICKET-CPU-BUDGET-UNIFIED-DYNAMIC-V1`** — `BudgetMode::Dynamic`
runtime loop: idle-pool threads temporarily loaned to a busy pool when
its backlog exceeds a threshold; loan-period bounded by env var
`CHRONON3D_CPU_BUDGET_LOAN_MS` (default 500ms). Per AGENTS.md "no
singletons senza ADR", this forward-point ticket REQUIRES an ADR
(`ADR-028-thread-pool-loan-manager.md`, parallel precedent ADR-024 +
ADR-025 + ADR-026 + ADR-027) before implementation lands.

(b) **`TICKET-CPU-BUDGET-UNIFIED-MACHINE-VERIFY`** — full B06 RSS/time
macchina-verifica end-to-end on working build host. Consumate the
existing `tools/measure_cpu_budget.sh` harness + RSS-before <
RSS-after assertion.

(c) **`TICKET-CPU-BUDGET-UNIFIED-3DOC-CAT5-ALIGN`** — Cat-5 3-doc
chore-chaser after macchina-verifica PASS closes.

(d) **`TICKET-ADR-024-DOUBLE-FILE-COLLISION`** — separate ADR-renumber
chore: pick a new ADR number (≥ 028) for one of the two ADR-024 files
(composite-node-counter vs deprecate-persistent-framebuffer-cache) so
they're uniquely identified. Cross-link all dependent tickets.

## Cross-link

- Canonical precedent: `docs/tickets/TICKET-P1E-CPU-BUDGET-MEASUREMENT.md`
  (§Decision rule "if encode_ratio > 25%, extend CpuBudget" — F4.2
  materializes the EXTEND decision by adding bounded-frame-queue depths;
  acceptance criterion `[ ] Se estensione: implementata nello stesso
  CpuBudget (no pool separati)` is honored for the static mode.)
- B06 corpus: `configs/benchmarks/corpus/b06_video_overlay_1080p.yaml`
  (VideoOverlay1080p scene; canonical bench composition
  `BenchB06_VideoOverlay1080p` in
  `examples/bench_corpus/bench_corpus_scenes.cpp:244`).
- AGENTS.md §regole "Cat-3 minimal-surface" — total scope = 4 NEW
  symbols + 2 NEW helpers; no new singleton.
- AGENTS.md §regole "No espansione API non necessaria" — each new
  symbol justified by user-spec verbatim.
- AGENTS.md §regole "No nuovi singleton/registry/resolver/cache senza
  ADR" — Dynamic mode forward-point requires ADR-028 (per (a) above).
- AGENTS.md §ronoesty-limitation — macchina-verifica DEFERRED-WBH
  per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` precedent; this
  VPS cannot link `chronon3d_cli`.

## Status

Scaffold 2026-07-13 (post-F4.2 chore commit on `main`).

## Owner / Date

- Owner: post-F4.2 / WBH agent if static-mode regressions surface
- Date: 2026-07-13 (F4.2 chore on `main`); macchina-verifica
  pending WBH.
