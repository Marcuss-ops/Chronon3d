# Gate Baseline — main @ 30f6c876

> Recorded on: 2026-06-30
> Branch: `main`
> HEAD: `30f6c876590b9213957c44e4473f1068efb244ad` (short: `30f6c876`)
> Trigger: post-analysis P0 sweep — baseline osservabile richiesta dall'utente dopo l'analisi dello stato repository
> Baseline file: `docs/baselines/main-30f6c876-baseline.md`

## Gate results (all 11 gates)

| Gate | Script | Exit | Verdict | Notes |
|------|--------|------|---------|-------|
| 1 | `check_architecture_boundaries.sh` | 0 | **PASS** ✅ | 14/14 checks pass; checks [10], [12], [13] advisory-only |
| 2 | `check_architecture_boundaries_selftest.sh` | 0 | **PASS** ✅ | 14 assertions passed |
| 3 | `check_software_renderer_boundary.sh` | 2 | **FAIL** ❌ | I5: 1 `SoftwareRenderer&` at `src/runtime/render_engine.cpp:255` |
| 4 | `check_gitignored_dirs.sh` | 0 | **PASS** ✅ | 31/31 directories, 8 globs, 49 file patterns clean |
| 5 | `audit_software_renderer.sh` | 0 | **PASS** ✅ | All pipelines pipefail-safe |
| 6 | `check_camera_architecture.sh` | 1 | **FAIL** ❌ | P0: non-canonical `tan(fov)` focal formulas in `camera_test_orchestrator.cpp` lines 363, 557 |
| 7 | `check_doc_sync.sh` | 0 | **PASS** ✅ | 0 hard failures, 0 warnings |
| 8 | `check_filename_drift.sh` | 0 | **PASS** ✅ | Warn mode; 114 drift warnings (advisory, exit 0) |
| 9 | `test_architectural.sh` | 0 | **PASS** ✅ | All static architectural checks passed |
| 10 | `install_consumer_test.sh` | 2 | **FAIL** ❌ | `INTERFACE_INCLUDE_DIRECTORIES` contains source-prefixed paths for `chronon3d_cache` and `chronon3d_graph` |
| 11 | `check_backend_sanitization.py` | 0 | **PASS** ✅ | All sanitization checks passed |

**Net: 8/11 PASS** (3 failures: gate #3 I5, gate #6 P0 tan(fov), gate #10 CMake include paths)

## Gate #3 failure details

### I5: `SoftwareRenderer&` in process surfaces

```
[FAIL] I5: 1 riferimenti SoftwareRenderer& nelle superfici di processo (target R2)
  src/runtime/render_engine.cpp:255
```

Same regression as `main@88d2deec` baseline. The `RenderEngineAccess::software_renderer()` accessor returns `SoftwareRenderer&` from `src/runtime/render_engine.cpp:255`. This is the authorized boundary accessor but the gate's grep is not precise enough to distinguish return types from parameter types.

## Gate #6 failure details

### P0: Non-canonical tan(fov) focal formulas

```
[5/6] tan(fov) focal formulas canonical: FAIL
```

Two occurrences in `content/experimental/camera/camera_test_orchestrator.cpp`:
- Line 363: `"W = 2 * |Z| * tan(FOV/2) (derived from camera_math::focal_from_camera)"`
- Line 557: `"W = 2 * |Z| * tan(FOV/2) (derived from camera_math::focal_from_camera)"`

These appear to be comments or string literals containing formula references. The canonical formula is expected to use `camera_math::focal_from_camera()` directly rather than inline `tan(fov/2)` expressions.

## Gate #10 failure details

### INTERFACE_INCLUDE_DIRECTORIES source-prefixed paths

```
CMake Error in src/cache/CMakeLists.txt:
  Target "chronon3d_cache" INTERFACE_INCLUDE_DIRECTORIES property contains
  path: "<repo-root>/src/cache/include_private"
  which is prefixed in the source directory.

CMake Error in src/render_graph/CMakeLists.txt:
  Target "chronon3d_graph" INTERFACE_INCLUDE_DIRECTORIES property contains
  path: "<repo-root>/src/render_graph/include_private"
  which is prefixed in the source directory.
```

The `include_private/` directories under `src/cache/` and `src/render_graph/` are exposed as `INTERFACE_INCLUDE_DIRECTORIES` on the respective library targets. CMake's install(EXPORT) mechanism rejects source-prefixed paths in `INTERFACE_INCLUDE_DIRECTORIES` because they would be invalid at the consumer's install location.

These paths need to be moved to `PRIVATE` include directories (if the internal headers are not part of the public SDK surface) or the headers need to be relocated to the public `include/chronon3d/` tree.

## Gate notes (advisory / non-blocking)

- **Gate #1**: Checks [10], [12], [13] report advisory failures (non-blocking, exit 0): I5 `SoftwareRenderer&`, CMake module registry, vcpkg dep parity.
- **Gate #8**: 114 filename drift warnings (advisory `--warn` mode). Known baseline: build artifacts, archived docs, test cmake files have legitimate references to renamed/moved files.

## Comparison with previous baseline

| Metric | `main@88d2deec` | `main@30f6c876` |
|--------|-----------------|-----------------|
| Net result | 9/11 PASS | 8/11 PASS |
| Gate #3 (I5) | FAIL | FAIL (regression persists) |
| Gate #6 (camera) | PASS | **FAIL** (new: P0 tan(fov)) |
| Gate #10 (consumer) | FAIL (CMake 3.27 env) | **FAIL** (new: include_private paths) |

Gate #6 and #10 show **new regressions** relative to `main@88d2deec`. The CMake env version issue from #10 in the previous baseline is resolved, but replaced by the `include_private` path issue.

## Summary

```
GATE STATUS: 8/11 PASS
BLOCKERS: gate #3 (I5 SoftwareRenderer&), gate #6 (P0 tan(fov) formulas), gate #10 (include_private INTERFACE paths)
REGRESSIONS FROM 88d2deec: gate #6 (was PASS), gate #10 (CMake env fixed, new path issue)
FEATURE FREEZE: STILL ACTIVE — baseline is NOT green (needs 11/11 on same commit)
```

## Cross-references

- `AGENTS.md` — Feature Freeze V0.1 rules + 11-gate checklist
- `docs/CURRENT_STATUS.md` — current status snapshot
- `docs/FOLLOWUP_TICKETS.md` — open defects and follow-ups
- `docs/baselines/main-88d2deec-baseline.md` — previous baseline (9/11 PASS, 2026-06-30)
- `docs/baselines/main-446a60e2-baseline.md` — earlier baseline (2026-06-23)
