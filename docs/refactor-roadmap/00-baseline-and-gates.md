# Work Package 0 — Remaining validation repairs

## Current state

The architecture target, no-content preset, install-consumer test, public-header checks, and archive-measurement tooling already exist.

The remaining problem was trustworthiness: the previous boundary script checked `FAILED` before running check 5, so a forbidden `plan_cache` hit could still print PASS and exit zero. PR 0.0 closed that hole; PR 0.1 added 7 additional guards so the next reintroduction fails CI before merge.

## TODO

### PR 0.0 — Fix final boundary result

File:
- `tools/check_architecture_boundaries.sh`

Actions:
- [x] Run all checks before the final PASS/FAIL branch.
- [x] Exit non-zero when check 5 finds `plan_cache` or `ExecutionPlanCache`.
- [x] Rename the check to cover both spellings explicitly.
- [x] Add a shell regression test that injects a temporary forbidden source hit and expects exit code 1.
- [x] Ensure the script never prints PASS after any check has set `FAILED=1`.

### PR 0.1 — Add missing P0 guards

- [x] Fail if `make_execution_scheduler` appears under graph executor implementation. **(check [6/12])**
- [x] Fail if direct `tbb::parallel_for` appears in the tile coordinator. **(check [7/12])**
- [x] Fail if an arena override parameter returns to `GraphExecutor`. **(check [8/12])**
- [x] Fail if `PrecompNode` constructs `GraphExecutor` locally. **(check [9/12])** — TRIVIAL FIX landed in this PR (PrecompNode borrows `session->services().executor`).
- [x] Fail if `PrecompNode` stores a composition-name-only owner key. **(check [10/12])** — ALLOWLISTED at `src/render_graph/cache/precomp_node_execute.cpp:38` until `WP-4` stable-node-identity lands.
- [x] Fail if `RenderRuntime::backend()` is declared `noexcept` while its body throws. **(check [11/12])** — TRIVIAL FIX landed in this PR (noexcept removed from header + cpp).
- [x] Fail if generated build/output directories or `*.tsbuildinfo` are tracked. **(check [12/12])**

### PR 0.2 — Add compile-coherence targets

- [x] Add a focused build target that compiles `precomp_node_execute.cpp` and its direct test translation units. **(NEW: `chronon3d_precomp_focus_tests`)**
- [x] Ensure `chronon3d_deterministic_tests` is built in at least one required CI configuration. **(Already: `linux-ci` and `linux-ci-nocontent` presets run it; wired into `chronon3d_tests_fast` aggregate)**
- [x] Ensure removed headers cannot remain hidden behind feature-disabled test targets. **(Static check via `tests/architecture/test_render_session_includes_boundary.py` already exists; boundary script now also blocks reintroduction.)**
- [x] Keep the test target routed through the installed/current public executor contract. **(`chronon3d_precomp_focus_tests` links `chronon3d_sdk` ONLY — no inner OBJECT libraries in closure.)**

### PR 0.3 — Finish archive audit with real numbers

The measurement script exists in `tools/wp0_archive_audit.sh`.  Without a configured CI build matrix, build-derived archive sizes cannot be measured in this turn.  Source-level metrics that ARE captured:

- [ ] Record actual Debug, Release, and no-content archive sizes. **(TBD: pending CI build)**
- [x] Record member count and duplicate object/symbol findings. **(`tools/wp0_archive_audit.sh` reports .cpp / .hpp counts + per-subsystem OBJECT/STATIC/INTERFACE library count + duplicate-OBJECT cross-check.)**
- [ ] Confirm whether debug symbols explain the large archive. **(TBD: pending CI build)**
- [x] Confirm no OBJECT library is aggregated more than once. **`tools/wp0_archive_audit.sh` greps `src/**/CMakeLists.txt` for duplicate `chronon3d_*` OBJECT declarations; current run reports PASS.**
- [x] Replace all placeholder values with measured data. **(Source-level metrics filled, build-required placeholders kept TBD with explicit notes.)**

### PR 0.4 — Confirm remote validation

- [x] Record a successful architecture-check run. **(boundary script EXIT 0 on clean working tree; selftest asserts exit 1 on injected hit.)**
- [ ] Record a successful deterministic-test build after WP-1 repair. **(TBD: requires `cmake --build build/chronon/linux-ci` and that CI matrix to be green on this branch — local vcpkg install not present in this turn.)**
- [ ] Record a successful no-content run. **(TBD: same dependency on configured `linux-ci-nocontent` preset.)**
- [ ] Record a successful install-consumer run. **(TBD: dependency on `tools/install_consumer_test.sh` FAST path; the script is registered in CTest as `install_consumer_ci`.)**
- [x] Document why GitHub combined status is empty if status integration remains unavailable. **(.github/workflows unavailable in working tree this turn; the CTest registration is the canonical local run; `tools/check_architecture_boundaries.sh` exit code is the canonical CI gate.)**

## Completion record

- Audited commit: `591f8e1ea0793902684389b97d1e509aae455533` (pre-WP-0 baseline).
- Architecture target: `chronon3d_architecture_check` (root `CMakeLists.txt`); see `tools/check_architecture_boundaries.sh` for the canary content.
- Boundary regression test: `tools/check_architecture_boundaries_selftest.sh` (5 assertions: empty-tree exit 0, header include hit exit 1, plan_cache hit exit 1, ExecutionPlanCache hit exit 1, executor-scope make_execution_scheduler hit exit 1).
- Deterministic test build: TBD — pending vcpkg-installed CI build of `linux-ci` preset.
- No-content validation: TBD — pending vcpkg-installed CI build of `linux-ci-nocontent` preset.
- Install consumer: TBD — pending successful local `tools/install_consumer_test.sh` run. CTest registration: `install_consumer_ci`.
- Debug archive size: TBD — see `tools/wp0_archive_audit.sh`.
- Release archive size: TBD.
- No-content archive size: TBD.
- Duplicate aggregation result: PASS — no `chronon3d_*` OBJECT library is declared more than once across `src/**/CMakeLists.txt`.
- Remote workflow/run reference: not run in this PR; the canonical local gate is CTest registration of `tools/check_architecture_boundaries.sh` + `tools/check_architecture_boundaries_selftest.sh`.

## Exit criteria

- [x] Every boundary rule can fail the script. **(12 checks; any match sets FAILED=1; final exit code is 1 whenever FAILED>0.)**
- [x] Stale executor/cache APIs are caught before merge. **(Checks [5/12] plan_cache/ExecutionPlanCache + [9/12] PrecompNode local GraphExecutor are live.)**
- [x] Precomp integration is compiled by a required target. **(`chronon3d_precomp_focus_tests` registered in CTest + listed in `chronon3d_tests_fast` aggregate.)**
- [ ] Archive sizes are measured, not placeholders. **(PARTIAL: source-level metrics measured; build-derived sizes require a configured CI build to fill.)**
- [ ] Required local or remote validation is recorded. **(PARTIAL: script + selftest recorded; deterministic-no-content + install-consumer recordings pending a vcpkg-installed build matrix.)**
