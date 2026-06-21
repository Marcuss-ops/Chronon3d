# Work Package 0 — Baseline and validation gates

`PR` in this roadmap means an implementation package, not necessarily a GitHub pull request.

## Goal

Make every later architectural change verifiable. This package must not change rendering behavior.

## TODO

### PR 0.0 — Record the baseline

- [ ] Record the starting commit SHA and the presets used.
- [ ] Record enabled features: content, diagnostics, text, video, native FFmpeg.
- [ ] Record the Release and no-content size of `libchronon3d_sdk_impl.a`.
- [ ] Add the results to the completion log below.

### PR 0.1 — Add an architecture build target

Files:
- `CMakeLists.txt`
- `src/CMakeLists.txt`
- test CMake files only when needed

Actions:
- [ ] Add `chronon3d_architecture_check`.
- [ ] Depend on the existing runtime, graph executor, graph pipeline, software backend, fast tests, and SDK aggregate targets.
- [ ] Reuse existing targets; do not create duplicate compile targets.

### PR 0.2 — Add boundary validation

Create `tools/check_architecture_boundaries.sh`.

Actions:
- [ ] Fail if the removed `core/memory/render_session.hpp` path returns.
- [ ] Fail if removed renderer cache/resource headers return.
- [ ] Fail if generated build/output directories are tracked.
- [ ] Print one clear message for each failed rule.
- [ ] Run the script from CI.

### PR 0.3 — Require install-consumer validation

Files:
- `.github/workflows/ci.yml`
- `tools/install_consumer_test.sh`
- `tests/install_consumer/CMakeLists.txt`

Actions:
- [ ] Run the install-consumer check on Linux validation.
- [ ] Preserve the configured toolchain and dependency search path.
- [ ] Ensure the consumer links only `Chronon3D::SDK`.
- [ ] Ensure the consumer does not use source-tree paths.
- [ ] Fail on install, configure, link, or smoke-test errors.

### PR 0.4 — Add no-content validation

- [ ] Configure with content and diagnostics disabled.
- [ ] Build `chronon3d_sdk_impl`.
- [ ] Build `chronon3d_architecture_check`.
- [ ] Run supported fast tests.
- [ ] Run the install-consumer check against that installation.
- [ ] Do not silently enable disabled modules.

### PR 0.5 — Audit aggregate archive size

- [ ] List archive members.
- [ ] Detect duplicated objects or duplicated module aggregation.
- [ ] Compare Debug, CI, and Release sizes.
- [ ] Determine whether debug information explains the large archive.
- [ ] Document findings before optimizing.

### PR 0.6 — Complete the validation record

Fill this section when finished:

- Commit:
- Build preset:
- Test preset:
- Architecture target:
- Fast tests:
- No-content build:
- Install consumer:
- Release archive size:
- CI archive size:
- Known limitations:

## Exit criteria

- [ ] Architecture target builds.
- [ ] Fast tests pass.
- [ ] No-content validation passes.
- [ ] Install-consumer validation passes.
- [ ] Boundary checks pass.
- [ ] Aggregate archive size is measured and explained.
- [ ] Rendering behavior is unchanged.

## Out of scope

- Scheduler changes.
- Executor API changes.
- Session ownership changes.
- Precomposition changes.
- New rendering features.
