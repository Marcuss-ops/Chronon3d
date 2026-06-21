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

## Recorded baseline (built before applying any WP1 change)

- Commit:           see `git rev-parse HEAD` on the worktree at the time of recording
- Build preset:     `linux-ci` (Debug, no Unity, tests ON, install-consumer ON)
- Test preset:      `linux-ci-test` (ctest from `cmake --preset linux-ci-test`)
- Architecture target: `chronon3d_architecture_check` (see PR 0.1)
- Fast tests:       `chronon3d_tests_fast` aggregate (see tests/CMakeLists.txt)
- No-content build: `linux-ci-nocontent` inherits `linux-ci` and flips `CHRONON3D_BUILD_CONTENT=OFF`
- Install consumer: `tools/install_consumer_test.sh` invoked from CTest entry
                    `install_consumer_ci` (preset-driven via `CHRONON3D_BUILD_INSTALL_CONSUMER_TEST`)
- Release archive size: measured by `tools/measure_archive.sh` (Debug / Release / CI)
- CI archive size:      measured by `tools/measure_archive.sh --preset linux-ci`
- Known limitations:    `CHRONON3D_ENABLE_DIAGNOSTICS=OFF` when BUILD_TESTS=OFF — engine-side
                       observability disabled; tests force it ON (root CMakeLists.txt:65-71)

### Preset→flags delta (linux-ci)

| Flag                          | linux-ci  | linux-ci-nocontent |
| ----------------------------- | --------- | ------------------ |
| `CHRONON3D_BUILD_TESTS`       | `ON`      | `ON`               |
| `CHRONON3D_BUILD_CONTENT`     | `ON`      | `OFF`              |
| `CHRONON3D_BUILD_DIAGNOSTICS` | `OFF`¹    | `OFF`¹             |
| `CHRONON3D_UNITY_BUILD`       | `OFF`     | `OFF`              |
| `CHRONON3D_BUILD_INSTALL_CONSUMER_TEST` | `ON` | `ON`           |

¹ Tests force `CHRONON3D_ENABLE_DIAGNOSTICS=ON` regardless; this is a build-time-only toggle.

### WP0 PR 0.3 — Install-consumer wiring (verification)

The `install_consumer_ci` CTest entry is registered in the root CMakeLists.txt
when `CHRONON3D_BUILD_INSTALL_CONSUMER_TEST=ON`, which the `linux-ci` preset
sets.  `tools/install_consumer_test.sh` orchestrates:

1. Reuses `build/chronon/linux-ci/src/libchronon3d_sdk_impl.a` if present (FAST PATH).
2. Otherwise cold-configures + builds the SDK at an mktemp dir to `chronon3d_sdk_impl`.
3. Installs into an isolated prefix and asserts the boundary pre-conditions
   (`Chronon3DConfig.cmake`, `Chronon3DTargets.cmake`, `libchronon3d_sdk_impl.a`,
   public headers under `<prefix>/include/chronon3d/`).
4. Configures + builds + runs the `tests/install_consumer/` consumer project,
   which links ONLY `Chronon3D::SDK` and prints `[BOUNDARY-OK]`.
5. Exits 0 on success, 1 on boundary regression, 2 on pre-existing SDK-side
   TU breakage (CTest maps this to SKIP via `SKIP_RETURN_CODE 2`).

CI verification: the `linux-ci` matrix entry in `.github/workflows/ci.yml`
runs `ctest --preset linux-ci-test`; `install_consumer_ci` is included
automatically because CTest runs all registered tests by default.  We do
NOT add an explicit second CI invocation — registering under CTest is the
documented acceptance gate.

### WP0 PR 0.4 — No-content validation

The `linux-ci-nocontent` preset inherits `linux-ci` and flips
`CHRONON3D_BUILD_CONTENT=OFF`, no other flags change.  The CI matrix entry
"Linux (No Content)" in `.github/workflows/ci.yml` runs:

- `cmake --preset linux-ci-nocontent`
- `cmake --build --preset linux-ci-nocontent --target chronon3d_tests` (push)
  or `--target chronon3d_tests_fast` (PR)
- `ctest --preset linux-ci-nocontent-test`

This proves the SDK builds without `chronon3d_content` composition
modules, validates that the install-consumer gate does not silently
re-export content registrations, and exercises the same
`install_consumer_ci` entry on the no-content prefix.

Known caveat: tests force `CHRONON3D_ENABLE_DIAGNOSTICS=ON`
unconditionally — the no-content build therefore exercises the
diagnostics path even when content is off.  This is INTENTIONAL: tests
that depend on diagnostics must keep working when content is disabled.

### WP0 PR 0.5 — Archive size audit

A companion script `tools/measure_archive.sh` (introduced by this work
package) walks `libchronon3d_sdk_impl.a` and reports:

- `member_count` (nm -P output count)
- `top_15_largest_objects` (size-sorted)
- `duplicated_symbols` (sort | uniq -d over the symbol table)
- `archive_size_bytes` (stat)

The comparison is expected to surface:

| Preset           | Approx. archive size | Drivers                                    |
| ---------------- | -------------------: | ------------------------------------------ |
| `linux-ci`       | ~   X MB (Debug)     | debug symbols, diagnostics hooks, .bss     |
| `linux-release`  | ~   Y MB (Release)   | stripped debug, no diagnostics             |
| `linux-ci-nocontent` | ~ Z MB (Debug)  | minus `chronon3d_content.o` (≈W MB savings)|

`X > Y` is expected (debug info dominates).  Anomalies (e.g. duplicated
aggregation, ModuleMarker ODR collisions) become architectural
findings; normal Debug-vs-Release deltas are documented and accepted.

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
