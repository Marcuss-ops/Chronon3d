# Expressions V2 — Controlled Promotion Criteria (Opzione B)

> **Status:** Quarantined (Opzione A) **+** this Opzione B criteria document is the
> forward-looking contract for promoting the engine to a stable feature.
> Apply rigorously before merging any change that removes the quarantine.

Expressions V2 is a "Path B" expression engine that lives under
`experimental/expressions/` while it has not yet proven the eight gates below.
It MUST NOT be moved out of the quarantine without satisfying every gate in
this document.

The document is the gate contract *in addition to* `docs/ANTI_DUPLICATION_RULES.md`
and the architectural ownership in `docs/ARCHITECTURE_EVOLUTION_PLAN.md`.

---

## What the quarantine guarantees

While `experimental/expressions/` is gated by `CHRONON3D_BUILD_EXPERIMENTAL=OFF` (default):

1. Headers are NOT in the SDK public include path — they live under
   `experimental/expressions/include/chronon3d_experimental/`, with a distinct
   `chronon3d_experimental/` prefix. The umbrella headers (`chronon3d.hpp`,
   `runtime.hpp`, `internal.hpp`) do NOT include them.
2. `chronon3d_expressions_v2` is NOT listed in `_chronon3d_install_targets` in
   the root `CMakeLists.txt`. It is NOT installed. `cmake --install` will not
   ship the library or its headers.
3. `Chronon3D::SDK` does NOT transitively link it. Consumers using
   `find_package(Chronon3D)` and linking only `Chronon3D::SDK` will never see
   the engine.
4. Core, scene, render_graph, etc. have NO public dependency on it. The engine
   has no entry point in the productive render path.
5. While quarantined, broken behaviour on `main` is contained to opt-in
   developers / `full-validation` presets. Default-configured CI ignores it.

The guarantee is enforced mechanically by:

- `CMakeLists.txt`: `if(CHRONON3D_BUILD_EXPERIMENTAL AND EXISTS ...) add_subdirectory(...)`.
- `CMakeLists.txt`: `_chronon3d_install_targets` does NOT list it.
- `tools/test_architectural.sh`: greps for `chronon3d_experimental` includes,
  allowing them only under `experimental/**`.
- New CI `architecture-check` job that fails the PR if `chronon3d_experimental/...`
  leaks into a non-experimental file outside the quarantine.

---

## Promotion gates — the eight required criteria

A PR removing the quarantine MUST satisfy ALL EIGHT gates below. Any one
"NOT DONE" entry blocks the promotion; the gate-completing PRs are split into
focused commits, but the merge that removes the quarantine lands them all.

### Gate 1 — Zero disabled tests without a filed ticket

| Status | Criterion |
|---|---|
| ☐ | `grep -rn '\\* doctest::skip()' tests/` returns 0 hits that lack a `TICKET-XXX` marker in the preceding 3 lines (per `tools/check_skipped_tests.sh`). |
| ☐ | Every `* doctest::skip()` carries `Issue:`, `Owner:`, `Motivation:`, `Data introduzione:`, `Deadline rimozione:` per the skip-ticket convention (see `CONTRIBUTING.md`). |
| ☐ | All previously-disabled tests (camera hierarchy, motion blur torture, determinism gradient, mask rendering, etc.) are either re-enabled and green, OR carry a live ticket with a deadline. |

### Gate 2 — Determinism tests

| Status | Criterion |
|---|---|
| ☐ | A determinism suite covering the engine's output (compiled program + Vm::run result) is part of the default `chronon3d_tests` aggregate target. |
| ☐ | The suite catches single-frame and multi-frame execution drift, including Vec3 arithmetic completeness and ternary right-associativity regression cases. |
| ☐ | The engine has no static `std::unordered_map` / `std::vector` cache. Any state required for determinism is reset between test runs (verified by an explicit reset API in tests). |

### Gate 3 — Real integration with `AnimatedValue`

| Status | Criterion |
|---|---|
| ☐ | The Path-A scalar parser in `AnimatedValue` either delegates to `compile()` from this engine (one source of truth), or the migration path is documented with a deprecation timeline. |
| ☐ | No two parsers / VMs / dependency graphs operate in the productive render path simultaneously. The engine replaces Path A, not co-exists with it. |
| ☐ | A benchmark demonstrates the path-A → path-B migration is a measurable win (or explanatory neutral with documentation). |

### Gate 4 — Documented public API

| Status | Criterion |
|---|---|
| ☐ | Every public symbol in `include/chronon3d_experimental/expressions/v2/*.hpp` has a doc comment, lifetime/invariant contract, and threading model. |
| ☐ | An API reference lives in `docs/` (e.g. `docs/EXPRESSIONS_V2_API.md`) with concrete usage examples. |
| ☐ | An "expression → bytecode → AST" pipeline diagram is in `docs/EXPRESSIONS_V2_PIPELINE.md` so new contributors can ramp up without reading the compiler source. |

### Gate 5 — Benchmark against the previous system

| Status | Criterion |
|---|---|
| ☐ | A reproducible benchmark comparing the engine against AnimatedValue's inline parser is added to `tools/` or `tests/bench/`. The benchmark runs as part of `linux-full-validation`. |
| ☐ | Results are stored under `reports/perf/` with a baseline JSON for regression detection. |
| ☐ | A threshold is documented: any > 5% regression vs. the baseline triggers a "regression" label on the PR that introduces it. |

### Gate 6 — V1 → V2 replacement map

| Status | Criterion |
|---|---|
| ☐ | A migration map exists at `docs/EXPRESSIONS_V1_TO_V2.md` enumerating every Path-A surface (`AnimatedValue::parse(...)`, `Expression::compile(...)`, etc.) and its Path-B replacement. |
| ☐ | For every Path-A surface, the map lists: (a) replacement, (b) adapter if co-existence is required, (c) criterion for removal of the old path. |
| ☐ | The map's "removal criterion" column is objective and verifiable — no subjective "when usage drops" without a numeric threshold. |

### Gate 7 — Removal deadline for the old system

| Status | Criterion |
|---|---|
| ☐ | A specific version tag (e.g. `0.4.0`) or calendar date (e.g. `2026-12-31`) is recorded in this document and `ARCHITECTURE_EVOLUTION_PLAN.md` as the deadline by which Path-A parsers are removed from the productive path. |
| ☐ | Per the deadline, the engine is the single source of truth. Static and CI rules enforce "no Path A codepaths in `src/` / `include/chronon3d/` outside `experimental/`". |
| ☐ | A grace-period branch policy is documented: "Path A removed from main on date X, deprecation warnings emitted for ≥ N weeks beforehand." |

### Gate 8 — Single parser, single VM, single dependency graph

| Status | Criterion |
|---|---|
| ☐ | Inventory check: at most one `compile()` function, one `Vm` class, one `DependencyGraph` class reachable from the productive render path. Variants live only as experimental sub-types. |
| ☐ | `grep -rn 'EXPRESSION_V1\\|EXPRESSION_A ' src/ include/chronon3d/` returns no productive references (only docs/tests). |
| ☐ | `tools/test_architectural.sh` includes this grep as a permanent check before every merge. |

---

## Process

1. Open a tracking issue / ticket per gate (e.g. TICKET-EXP2-G1 .. -G8).
2. Each gate may resolve across multiple PRs; mark the gate ☐ Done when the
   acceptance criteria are met **and** the CI gate that enforces the criteria
   is itself green.
3. The PR that removes the quarantine (deletes `experimental/`, moves files
   back under `include/chronon3d/`, adds the engine to
   `_chronon3d_install_targets`, removes `CHRONON3D_BUILD_EXPERIMENTAL` from
   the build) MUST reference the eight completed gate tickets in its commit
   message and description.
4. Two approvals required: one from the engine owner, one from the core
   architecture owner. No self-merge of the quarantine-removed PR.

---

## Audit log

| Date | Action | Tool/Commit |
|---|---|---|
| 2026-06-20 | Quarantined under `experimental/expressions/` with `CHRONON3D_BUILD_EXPERIMENTAL=OFF` default | this PR |
| 2026-06-20 | Opzione B criteria document published | this PR |
| 2026-06-20 | Gate 1: zero disabled tests without ticket — compliant (TICKET-007 umbrella applied; underlying bugs NOT fixed by this PR) | TICKET-007 |
| 2026-06-20 | Gate 2: determinism suite landed in `experimental/expressions/tests/test_expressions_v2_determinism.cpp` (sections A–E; executable `chronon3d_expressions_v2_tests`); `Vm::reset()/empty()/env_size()` exposed in `vm.hpp` | this PR |
| 2026-06-20 | Gate 3: kickoff — `TICKET-EXP2-G3` opened; Path A audit captured (10-row surface table); delegation design landed (TWO-PR split: 3a Path B feature parity, 3b AnimatedValue swap); Vm resolver hook + lazy-compile-on-AnimatedValue + per-property cache shape designed in TICKET-EXP2-G3 (no global LRU per Gate 2 determinism contract); code changes TBD on 3a/3b | TICKET-EXP2-G3 |
| 2026-06-20 | Gate 4: kickoff — `docs/EXPRESSIONS_V2_API.md` (per-symbol reference for all 8 `chronon3d_experimental/expressions/v2/*.hpp` headers; lifetime/invariant/threading + 7 end-to-end usage examples) and `docs/EXPRESSIONS_V2_PIPELINE.md` (source → lex → parse → type-check → emit → VM trace + `Type::Top` permissiveness rationale + 5 onboarding notes for new contributors) scaffolded; each Gate 4 sub-criterion (#symbol doc, #API ref with examples, #pipeline diagram) covered. Cross-references `docs/EXPRESSIONS_V2_API.md` and `docs/EXPRESSIONS_V2_PIPELINE.md` from this row for grep-to-source symmetry | this PR |
| TBD | Gate 5: benchmark vs Path A |  |
| TBD | Gate 6: V1 → V2 map |  |
| TBD | Gate 7: removal deadline |  |
| TBD | Gate 8: single parser / VM / graph |  |
| TBD | Quarantine removed |  |
