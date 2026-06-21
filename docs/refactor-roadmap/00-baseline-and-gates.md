# Work Package 0 — Remaining validation work

## Goal

Keep the existing build, install-consumer, public-header, and architecture gates trustworthy.

## TODO

### PR 0.0 — Fix the boundary script result

File:
- `tools/check_architecture_boundaries.sh`

Actions:
- [ ] Move the final PASS/FAIL decision after check 5.
- [ ] Exit non-zero when `plan_cache` is found.
- [ ] Add a shell-level regression test that injects a temporary forbidden hit and expects failure.
- [ ] Keep documentation references outside the searched source surface.

### PR 0.1 — Complete missing boundary rules

- [ ] Fail if `make_execution_scheduler` appears inside graph executor implementation.
- [ ] Fail if direct TBB orchestration returns in the tile coordinator.
- [ ] Fail if `FrameArena* arena_override` returns.
- [ ] Fail if `PrecompNode` owns an executor, session, or program cache.
- [ ] Fail if generated build/output directories or `*.tsbuildinfo` are tracked.

### PR 0.2 — Audit the aggregate SDK archive

- [ ] Record Release, CI, and no-content sizes of `libchronon3d_sdk_impl.a`.
- [ ] List archive members and detect duplicated objects.
- [ ] Check whether debug information explains the current size.
- [ ] Check whether any OBJECT library is aggregated more than once.
- [ ] Document the result before changing packaging.

### PR 0.3 — Complete the validation record

Record:

- Commit:
- Build preset:
- Test preset:
- Architecture target:
- Fast tests:
- No-content build:
- Public-header gate:
- Install consumer:
- Release archive size:
- CI archive size:
- Known limitations:

### PR 0.4 — Confirm remote gate results

- [ ] Confirm all six cheap gates run on push to `main`.
- [ ] Confirm the full-validation workflow runs for C++ and CMake changes.
- [ ] Record the successful workflow run or document the missing GitHub status integration.

## Exit criteria

- [ ] Every architecture check can actually fail the script.
- [ ] Scheduler, executor, precomp, and arena boundaries are guarded.
- [ ] Aggregate archive size is measured and explained.
- [ ] A complete validation record exists.
- [ ] Remote gate execution is confirmed.
