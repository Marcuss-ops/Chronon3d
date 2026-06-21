# Work Package 0 — Remaining validation repairs

## Current state

The architecture target, no-content preset, install-consumer test, public-header checks, and archive-measurement tooling already exist.

The remaining problem is trustworthiness: the current boundary script checks `FAILED` before running check 5, so a forbidden `plan_cache` hit can still print PASS and exit zero. The registered scheduler determinism test currently contains exactly that stale API and is therefore not protected by the gate.

## TODO

### PR 0.0 — Fix final boundary result

File:
- `tools/check_architecture_boundaries.sh`

Actions:
- [ ] Run all checks before the final PASS/FAIL branch.
- [ ] Exit non-zero when check 5 finds `plan_cache` or `ExecutionPlanCache`.
- [ ] Rename the check to cover both spellings explicitly.
- [ ] Add a shell regression test that injects a temporary forbidden source hit and expects exit code 1.
- [ ] Ensure the script never prints PASS after any check has set `FAILED=1`.

### PR 0.1 — Add missing P0 guards

- [ ] Fail if `make_execution_scheduler` appears under graph executor implementation.
- [ ] Fail if direct `tbb::parallel_for` appears in the tile coordinator.
- [ ] Fail if an arena override parameter returns to `GraphExecutor`.
- [ ] Fail if `PrecompNode` constructs `GraphExecutor` locally.
- [ ] Fail if `PrecompNode` stores a composition-name-only owner key.
- [ ] Fail if `RenderRuntime::backend()` is declared `noexcept` while its body throws.
- [ ] Fail if generated build/output directories or `*.tsbuildinfo` are tracked.

### PR 0.2 — Add compile-coherence targets

- [ ] Add a focused build target that compiles `precomp_node_execute.cpp` and its direct test translation units.
- [ ] Ensure `chronon3d_deterministic_tests` is built in at least one required CI configuration.
- [ ] Ensure removed headers cannot remain hidden behind feature-disabled test targets.
- [ ] Keep the test target routed through the installed/current public executor contract.

### PR 0.3 — Finish archive audit with real numbers

The measurement script exists, but the roadmap still contains `X/Y/Z` placeholders.

- [ ] Record actual Debug, Release, and no-content archive sizes.
- [ ] Record member count and duplicate object/symbol findings.
- [ ] Confirm whether debug symbols explain the large archive.
- [ ] Confirm no OBJECT library is aggregated more than once.
- [ ] Replace all placeholder values with measured data.

### PR 0.4 — Confirm remote validation

- [ ] Record a successful architecture-check run.
- [ ] Record a successful deterministic-test build after WP-1 repair.
- [ ] Record a successful no-content run.
- [ ] Record a successful install-consumer run.
- [ ] Document why GitHub combined status is empty if status integration remains unavailable.

## Completion record

- Audited commit: `591f8e1ea0793902684389b97d1e509aae455533`
- Architecture target:
- Boundary regression test:
- Deterministic test build:
- No-content validation:
- Install consumer:
- Debug archive size:
- Release archive size:
- No-content archive size:
- Duplicate aggregation result:
- Remote workflow/run reference:

## Exit criteria

- [ ] Every boundary rule can fail the script.
- [ ] Stale executor/cache APIs are caught before merge.
- [ ] Precomp integration is compiled by a required target.
- [ ] Archive sizes are measured, not placeholders.
- [ ] Required local or remote validation is recorded.
