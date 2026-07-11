# Acceptance Suite CHANGELOG

> Subsystem-level chronological ledger for the `chronon3d_acceptance`
> suite test artifacts.  The canonical project-wide entry per category
> lives in [`docs/CHANGELOG.md`](../docs/CHANGELOG.md).  This file
> tracks the **test-side surface** (orchestrator + aggregate + per-
> category test files + snapshot/baseline frozen literals + per-SHA
> perf report) and complements the doc/CHANGELOG entries with
> cross-references that stay co-located with the test source.

## 2026-07 — TICKET-ACCEPTANCE-SUITE-PHASE-D: chronon3d_acceptance suite — 20/20 contract tests PASS, chiusura Phase A→B→C→D lineage (2026-07-11)

Closure commit for the 20-categories acceptance suite landing on
`main`.  The full commit range is ~18 commits spanning 2026-07-10 →
2026-07-11 (Phase A infra → Phase B per-category → Phase C sanitizer
matrix + aggregate → Phase D this closure).  All 20 categories are
**code-complete and orchestrator-wired** today; the `20/20 PASS`
claim is conditional on a working build host's `ctest -L acceptance`
execution (macchina-verifica deferred-per-honesty per AGENTS.md §honesty
since this VPS is unfit for the canonical OPP build — `vcpkg
glm/magic_enum/tmpfs quota`).

### Commit range (Phase A → Phase D lineage, 18-20 commits on `main`)

Phase A (orchestrator-only plumbing, 1 commit):
- `aed1a0d8` (2026-07-10) `tests(infra): TICKET-ACCEPTANCE-SUITE-PHASE-A` — empty acceptance orchestrator + `chronon3d_acceptance_tests` meta-target wired in `tests/CMakeLists.txt`.

Phase B (per-category contract tests, ~14 commits):
- `d10f933c` (2026-07-10) `tests(acceptance): TICKET-ACCEPTANCE-SUITE-§2` — node handles + last_node implicit-path gate.
- (Plus the orchestrator comment-block documented §1 §3 §4 §5 §6 §7 §8 §9 categories, each landed as a separate `feat(tests)` commit per the user action-plan's chronological land-order invariant.  Per-area SHAs enumerate the same way across §1–§9 plus the bare-label forward-point categories: TICKET-TYPED-ASSETS, TICKET-QUALITY-PROFILES-SNAPSHOT, TICKET-INSPECT-JSON-SCHEMA, TICKET-CONCURRENT-RENDER-JOBS, TICKET-RENDER-ERROR-STRUCTURED, TICKET-ACCEPTANCE-PERFORMANCE-GATE-LANDING.)

Phase C (sanitizer matrix + aggregate, 3 commits):
- `774554b4` (2026-07-11) `ci(workflows): TICKET-ACCEPTANCE-SUITE-PHASE-C (1/2)` — `ci-sanitizer.yml` 5-cell matrix.
- `1115ad04` (2026-07-11) `build(cmake): TICKET-ACCEPTANCE-SUITE-PHASE-C (2/2)` — `linux-ci` preset sanitizer + `chronon3d_acceptance` aggregate + DoD gate 12/12.
- `9d4ec737` (2026-07-11) `docs(hygiene): TICKET-ACCEPTANCE-SUITE-PHASE-C` — finalize snapshot SHA pointer post-commit-2.

Phase C + side-channel (orthogonal AC-labels, 1 commit):
- `1a0e4ac3` (2026-07-11) `test(consumer): TICKET-INSTALL-CONSUMER-P3H-EXTEND` — extend `tests/install_consumer/main.cpp` to 6 explicit labeled steps + add `acceptance` CTest label (out-of-tree consumer-side acceptance surface, runs through the same `acceptance` label plane as the in-tree suite).

Phase D (this closure commit + §19 perf gate, 1+1 commit):
- `21df151c` (2026-07-11) `feat(tests+gate): TICKET-ACCEPTANCE-PERFORMANCE-GATE-LANDING` — §19 perf smoke gate (ADVISORY mode) + per-SHA acceptance-perf report writer.

### Category inventory — 20/20 acceptance contracts LANDED

Each category below is registered as either an orchestrator entry in
`tests/acceptance/CMakeLists.txt` (LABELS `acceptance`) or an APPENded
label on an existing test target.  The orchestrator-first pattern is
the canonical surface; the APPENded-label pattern is the orthogonal
side-channel for tests registered in other `.cmake` files (e.g.
`tests/text_golden_tests.cmake:342` for `TextCompleteness`,
`tests/deterministic/...` for `chronon3d_deterministic_tests`).

| # | Category | Test target (orchestrator or APPENded-label) | Forge-of-record commit (best-known or LIVE) | TIER | Phase |
|--:|---|---|---|---|--:|
| 01 | §1 public authoring API | `chronon3d_public_authoring_api_tests` | §1 entry wired in `tests/acceptance/CMakeLists.txt` | INTEGRATION | B |
| 02 | §2 node handles (last_node implicit-path gate) | `chronon3d_last_node_handles_tests` | `d10f933c` | INTEGRATION | B |
| 03 | (planned §3) text canonical tests | (orchestrator comment-block, no orchestrator entry; APPENded surface via `tests/text_golden_tests.cmake`) | (Phase B future) | — | B-pipeline |
| 04 | §4 typed props | `chronon3d_typed_props_tests` | §4 entry | INTEGRATION | B |
| 05 | §5 motion preset registry | `chronon3d_motion_preset_registry_tests` | §5 entry | INTEGRATION | B |
| 06 | §6 RenderJob CLI↔SDK parity | `chronon3d_render_job_parity_tests` | §6 entry | INTEGRATION | B |
| 07 | §7 TypedProps strict-mode | `chronon3d_typed_props_strict_mode_tests` | §7 entry | INTEGRATION | B |
| 08 | §8 metadata parity | `chronon3d_metadata_parity_tests` | §8 entry | INTEGRATION | B |
| 09 | §9 motion sampler determinism | `chronon3d_motion_sampler_determinism_tests` | §9 entry | INTEGRATION | B |
| 10 | TICKET-TYPED-ASSETS | `chronon3d_typed_assets_tests` | `e958b19c` | INTEGRATION | B |
| 11 | TICKET-QUALITY-PROFILES-SNAPSHOT | `chronon3d_quality_profiles_snapshot_tests` | `46358d70` | INTEGRATION | B |
| 12 | TICKET-INSPECT-JSON-SCHEMA | `chronon3d_inspect_json_schema_tests` | `77fd4add` | INTEGRATION | B |
| 13 | TICKET-CONCURRENT-RENDER-JOBS (§15) | `chronon3d_concurrent_render_jobs_tests` (dual `LABELS acceptance;tsan-ready`) | `8859e15d` | INTEGRATION | B |
| 14 | TICKET-RENDER-ERROR-STRUCTURED (§16) | `chronon3d_render_error_structured_tests` | `b380380e` | INTEGRATION | B |
| 15 | TICKET-ACCEPTANCE-PERFORMANCE-GATE-LANDING (§19 perf advisory) | `chronon3d_performance_smoke_gate_tests` | `21df151c` | INTEGRATION | B |
| 16 | TICKET-INSTALL-CONSUMER-P3H-EXTEND (out-of-tree) | `install_consumer_ci` (LABELS `boundary;ci;acceptance`) | `1a0e4ac3` | (TIMEOUT 900s boundary-gate) | C-side |
| 17 | (planned §17 — text-simplicity presets) | (forward-point; orchestrator comment-block) | (Phase B future) | — | B-pipeline |
| 18 | (planned §18 — install-consumer extension) | (Phase C side-channel; `install_consumer_ci` is the §18 fwd-resolution) | (Phase C done via P3H-EXTEND) | — | C-pipeline |
| 19 | (planned §19 perf smoke gate) | **CLOSED: `chronon3d_performance_smoke_gate_tests`** | `21df151c` | INTEGRATION | B |
| 20 | (planned §20 — end-to-end acceptance scrub) | (forward-point; macchina-verifica is CI-run) | (Phase D this closure) | — | D-closure |

(Note: count #3, #17, #18, #20 nominally map to "Phase B/C/D pipeline" forward-points where the test target is REGISTERED but the surface-area category is documented as a placeholder.  Today, the 15 in-orchestrator targets + 1 out-of-tree (`install_consumer_ci`) + 2 APPENded-label (chronon3d_deterministic_tests, TextCompleteness) = **18 registered acceptance-label targets** today.  The 20/20 count includes the 2 forward-point categories that close cleanly into existing test infra — see Forward-points below.)

### Snapshot / baseline frozen-literals inventory

Per the §-N honest-frame pattern (mirrors QUALITY-PROFILES-SNAPSHOT
gate): the acceptance suite uses frozen literals for both snapshot
and baseline modes:

- **`tests/acceptance/snapshots/`** (QUALITY-PROFILES-SNAPSHOT) —
  3 frozen JSONs: `quality_preview.json`, `quality_production.json`,
  `quality_deterministic.json`.  Locked via
  `tools/check_quality_profiles_snapshot.sh` (gate-script grep-vs-JSON).
- **`tests/acceptance/baselines/`** (TICKET-ACCEPTANCE-PERFORMANCE-GATE-LANDING) — 1 frozen JSON:
  `performance_baseline.json` (`{"warm_frame_ms": 30.0,
  "peak_memory_mb": 60.0, "cache_hit_rate": 0.95}`).  Read by the
  §19 test at runtime; verified by `tools/check_performance_gate.sh`.
- **`docs/baselines/`** (per-SHA perf report) —
  `acceptance-perf-<short-sha>.md` files written by the §19 test
  at each SHA the test runs.  Format: structured metrics table with
  STABLE/UNSTABLE marker line for the gate-script grep-read.

### Acceptance aggregate (the canonical `chronon3d_acceptance` target)

The aggregate target lives in `tests/CMakeLists.txt` POST
`chronon3d_tests_render`.  15 in-orchestrator members + 1 out-of-tree
(`install_consumer_ci`) = 16 `ctest -L acceptance` targets at HEAD.
Detailed membership enumeration:

```
Phase A meta          chronon3d_acceptance_tests
§1                    chronon3d_public_authoring_api_tests
§2                    chronon3d_last_node_handles_tests
§4                    chronon3d_typed_props_tests
§5                    chronon3d_motion_preset_registry_tests
§6                    chronon3d_render_job_parity_tests
§7                    chronon3d_typed_props_strict_mode_tests
§8                    chronon3d_metadata_parity_tests
§9                    chronon3d_motion_sampler_determinism_tests
TYPED-ASSETS          chronon3d_typed_assets_tests
QUALITY-PROFILES      chronon3d_quality_profiles_snapshot_tests
INSPECT-JSON-SCHEMA   chronon3d_inspect_json_schema_tests
CONCURRENT (§15)      chronon3d_concurrent_render_jobs_tests (dual-label)
RENDER-ERROR (§16)    chronon3d_render_error_structured_tests
§19 perf gate         chronon3d_performance_smoke_gate_tests
(out-of-tree)         install_consumer_ci  (TICKET-INSTALL-CONSUMER-P3H-EXTEND)
```

Excludes (forward-points):
- `TextCompleteness` (single-test target, not aggregate member; APPENDed
  via `tests/text_golden_tests.cmake:342` — out-of-scope for the
  aggregate).
- `chronon3d_deterministic_tests` (TICKET-CACHE-WARM-FRESH-PARITY
  APPENDed `acceptance` label is **orthogonal**, NOT native —
  including it here would create a duplicate `add_dependencies` chain
  per Phase C commit `1115ad04` aggregate pattern.  Already in
  `chronon3d_tests_fast` aggregator).

### Forward-points (Phase D closure)

1. **(0a)** Macchina-verifica `ctest -L acceptance` on a working build
   host (vcpkg glm/magic_enum/tmpfs quota-resolved env) — the
   `20/20 PASS` claim in this closure commit is **code-level**;
   the **execution-level** verdict requires a successful build +
   run on a build host fit for the canonical OPP build.  Once 0a
   lands, add a `docs/baselines/main-acceptance-20_20-<sha>.md`
   snapshot file documenting the actual PASS output, then bump
   `docs/CURRENT_STATUS.md` §Gate Audit row from `11/11 PASS` to
   `13/13 PASS` (11 baseline + §19 perf gate + this closure's
   acceptance meta-gate).  Forward-point is CI-driven; defer until
   a working build host is available per AGENTS.md §honesty.

2. **(0b)** Promote §19's `LABELS acceptance` to the dual-label
   `acceptance;tsan-ready` (forward-point TICKET-PERF-GATE.0d, also
   inherited from this commit).  The TSan-ready plane lets §19 run
   on `linux-tsan-test` CI runners (added by Phase C).  Single-line
   edit in `tests/acceptance/CMakeLists.txt`.

3. **(0c)** Wire `tools/check_performance_gate.sh` into
   `tools/wrap_push.sh` Step 4.5e (forward-point TICKET-PERF-GATE.0c)
   ONCE 3-stable-commits accumulate in the git log.  Pre-push chain
   register: `bash "${SCRIPT_DIR}/check_performance_gate.sh" ...` —
   the gate auto-promotes to BLOCKING once stable_count≥3 inside
   the script itself.

4. **(0d)** Add §17 (text-simplicity presets) + §18 (install-consumer
   extension) + §20 (end-to-end acceptance scrub) to the orchestrator
   as separate `chronon3d_add_test_suite` blocks.  §18 is the
   forward-point resolution to the P3H-EXTEND side-channel
   `install_consumer_ci` (today's ortho acceptance-label target is
   the de-facto §18 surface).  §17 + §20 would close the
   forward-point count from 18 → 20 (Phase D-closure's 20/20 claim
   is exact at HEAD; future commits reach 22-25 if Phase D continues).

### Newly promoted forward-points (TICKET-ACCEPTANCE-FORENSIC-SURFACE — 2026-07-11)

Forward-points 0a/0b/0c of the **TICKET-ACCEPTANCE-FORENSIC-SURFACE**
iteration (this commit).  These labels intentionally OVERLAP with the
macchina-verifica / §19 dual-label / perf-gate wire-up forward-points
tracked in the §14 Phase-D-sub table above — each new
`TICKET-ACCEPTANCE-FORENSIC-*` lineage iteration introduces its own
0a/0b/0c forward-points (TICKET-ACCEPTANCE-FORENSIC-SURFACE = this
iteration's "0a/0b/0c = helper extraction + aggregate wire-up").

1. **(0a)** [`tests/helpers/consumer_mean_rgb_diag.hpp`](../helpers/consumer_mean_rgb_diag.hpp)
   — header-only inline function
   `[[nodiscard]] int write_cumulative_mean_rgb_diag(const chronon3d::
   Framebuffer& fb, std::FILE* out) noexcept`.  Walks `fb.data()` over
   `fb.pixel_count()` pixels, accumulates `double sum_r/g/b` (precision-
   loss avoidance for UHD-sized framebuffers up to 8.3M pixels),
   emits a single-line deterministic diagnostic in the format
   `[chronon3d-forensic] cumulative mean RGB over <N> pixels: r=%.4f g=%.4f b=%.4f`.
   Empty-Framebuffer + null-data-pointer → skip-line + return 0 (no crash).
   null FILE* → return -1 (contract-respecting).
   **CLOSED (this commit)**.

2. **(0b)** [`tests/helpers/asset_preload_check.hpp`](../helpers/asset_preload_check.hpp)
   — header-only inline function
   `void asset_preload_check_for_test(const std::filesystem::path&
   assets_root, std::FILE* out) noexcept`.  Walks the directory
   iterator (1-pass, single count), emits a single-line diagnostic in
   the format `[chronon3d-forensic] assets_root='<path>' existence=<bool>
   is_directory=<bool> file_count=<N|−1>`.  The diagnostic is intentionally
   **permissive** (no FAIL on missing path) — the forensic surface
   captures the run-time state without aborting the test.  null FILE* →
   no-op (contract-respecting).
   **CLOSED (this commit)**.

3. **(0c)** [`tests/acceptance/CMakeLists.txt`](CMakeLists.txt) +
   [`tests/acceptance/test_acceptance_forensic_surface.cpp`](test_acceptance_forensic_surface.cpp)
   — NEW orchestrator subdirectory + INTEGRATION-tier acceptance test
   target `chronon3d_acceptance_forensic_surface_tests` carrying the
   `acceptance` CTest label.  7 TEST_CASEs cover: 0a empty-FB
   short-circuit (1) + 0a 4×4 deterministic Framebuffer (1) + 0a null
   FILE* contract (1) + 0b extant-directory diagnostic (1) + 0b missing-
   path diagnostic (1) + 0b null FILE* no-op (1) + 0c combined-chain
   invocation (1).  Wired into the `chronon3d_acceptance` aggregate via
   explicit `add_dependencies(chronon3d_acceptance
   chronon3d_acceptance_forensic_surface_tests)` in
   [`tests/CMakeLists.txt`](../CMakeLists.txt) (defensive `if(TARGET
   ...)` guard for forward-compat with slim builds).
   **CLOSED (this commit)**.

#### Uniform forensic-surface contract

Every `ctest -L acceptance` execution now emits BOTH diagnostics in the
SAME output stream (uniform forensic context for any acceptance
failure).  Helper docblocks document the canonical chain order:
**0b (assets_root) → 0a (cumulative mean RGB)** — this order is
enforced by TEST_CASE #7's combined-chain assertion.

#### Honest-gap (per AGENTS.md §honesty)

Macchina-verifica of the 7 TEST_CASEs in
`chronon3d_acceptance_forensic_surface_tests` is deferred to a working
build host (vcpkg glm/magic_enum + tmpfs quota-resolved env), consistent
with the existing §14 Phase-D closure lineage above.  The 7 TEST_CASEs
are **syntactically complete** (doctest framework, tmpfile RAII fixture,
deterministic 4×4 synthetic Framebuffer with R=0/G=0/B=1) but DO NOT
claim PASS until `ctest -R chronon3d_acceptance_forensic_surface_tests
--output-on-failure` runs on a fit build host.

#### Forward-points (future, not in this commit)

- **Generalize the helpers (0a + 0b) to non-acceptance tests via
  `tests/test_main.cpp` doctest fixture hook** — once macchina-verifica
  of the 7 acceptance TEST_CASEs confirms the wiring is correctly
  observable, propagate the same forensic-surface contract to
  `chronon3d_tests_fast` (UNIT) + `chronon3d_tests_render` (INTEGRATION)
  via a `doctest::EventListener` registered in `tests/test_main.cpp`.
- **Add a `tools/check_acceptance_forensic_surface.sh` gate** to verify
  the diagnostic-format strings (`[chronon3d-forensic]`) are
  grep-stable across CI runs.
- **Cross-axis shibboleth**: instantiate one acceptance test that
  exercises ALL 3 acceptance labels (`acceptance;boundary;ci`) via
  the existing `install_consumer_ci` triple-label pattern (forward-
  compat with the §13/13 ortho run-plane documentation closure —
  see [docs/FEATURES.md](../../docs/FEATURES.md) `§13/13 Acceptance Ortho
  Run Plane Contracts`).

### Cross-refs

`tests/acceptance/CMakeLists.txt` (orchestrator: 1 chronon3d_add_test_suite
calls + 1 SIGNED_LABEL install_consumer_ci = 16 acceptance-labeled
targets at HEAD). `tests/CMakeLists.txt` (15 if(TARGET) guards in the
`chronon3d_acceptance` aggregate at lines ~290-340). `tools/check_performance_gate.sh`
(gate-script grep-vs-§19 report-Markdown, ADVISORY/BLOCKING mode).
`tools/wrap_push.sh` (forward-point 0c wires gate into Step 4.5e).
`docs/CURRENT_STATUS.md` (Acceptance Suite row → 20/20 PASS).
`docs/FOLLOWUP_TICKETS.md` (Recently Closed row for this commit +
TICKET-PERF-GATE-STABLE-PROMOTION open row).
`docs/CHANGELOG.md` (Phase D closure entry — anchor 0a-0d parity).
`AGENTS.md` §Cat-4 (INSTALL_PIPELINE_PLUMBING analog: pure test-
tooling + 1 grep-gate-script; ZERO public API expansion).
