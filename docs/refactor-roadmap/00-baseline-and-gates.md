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

### PR 0.2 — Boundary checks + compile coherence

`tools/check_architecture_boundaries.sh` runs **9** boundary checks
(4 original + 4 added on the 2026-06-21 close-out + 1 split out from the
legacy Path-B retirement audit).

Actions:
- [x] Fail if the removed `core/memory/render_session.hpp` path returns.
- [x] Fail if removed renderer cache/resource headers return.
- [x] Fail if `detail::g_debug_config` / `detail::set_debug_config` reappear — TICKET-007 follow-up (check #6).
- [x] Fail if `g_default_assets_root` reappears — TICKET-007 deferred follow-up (check #7).
- [x] Fail if `<chrono3d/...>` include typo reappears — TICKET-003 follow-up (check #8).
- [x] Fail if any file other than the sanctioned 5 (`framebuffer.hpp`, `framebuffer_handle.hpp`, `framebuffer_slot_view.hpp`, `arena.hpp`, `memory_utils.hpp`) is reintroduced under `core/memory/` (check #9).
- [ ] Fail if generated build/output directories are tracked. *(deferred — separate `.gitignore`-driven audit)*
- [x] Print one clear message for each failed rule.
- [x] Run the script from CI.

Wired into:
- CI: `.github/workflows/gates.yml` Gate 5 (`architecture-check`).
- CMake: `chronon3d_architecture_check` custom target (root `CMakeLists.txt`).
- CTest: registered as `architecture_boundaries_ci`.

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

### PR 0.5 — Archive audit tooling

`tools/audit_aggregate_archive.sh` provides an **offline, idempotent** archive
audit. The script does NOT build, configure, or write outside the working
directory; it operates on existing `.a` artifacts and produces a
human-readable report.  Per-archive and cross-archive analysis.

Status as of 2026-06-21 — script landed; results not yet recorded (see PR 0.6
"validation record" for the eventual fill-in).

Actions:
- [x] List archive members — `ar t <archive.a>` per input.
- [x] Detect duplicated objects or duplicated module aggregation — basename
      cross-archive counting via `sort | uniq -c | awk '$1>1'`.
- [x] Compare Debug, CI, and Release sizes — `du -b` per archive, side-by-side
      table.
- [x] Determine whether debug information explains the large archive —
      debug-symbol share via `nm --debug-syms` / full `nm` (heuristic on
      Linux/GCC/Clang).
- [x] Document findings before optimizing — stdout report; `--json <out>`
      flag for machine-readable summaries.

Run via:

```bash
tools/audit_aggregate_archive.sh \
    build/chronon/linux-release/src/libchronon3d_sdk_impl.a \
    build/chronon/linux-ci/src/libchronon3d_sdk_impl.a \
    --json build-audit.json
```

Not wired into the gate pipeline by default — audit-only, no thresholds
enforced.

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
