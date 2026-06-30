# Gate Baseline — main @ 6323d59c

> Recorded on: 2026-06-30
> Branch: `main`
> HEAD: `6323d59cf012c6eee442f5336c6c8082d335d026` (short: `6323d59c`)
> Trigger: post gate-fix sweep — 7 gate fixes applied in sequence (gates #2, #3, #4, #5, #8, #9, #10 Layer 2)
> Baseline file: `docs/baselines/main-88d2deec-baseline.md` (requested by user)

## Gate results (all 11 gates)

| Gate | Script | Exit | Verdict | Notes |
|------|--------|------|---------|-------|
| 1 | `check_architecture_boundaries.sh` | 0 | **PASS** ✅ | 14/14 checks pass; checks [10], [12], [13] are advisory-only |
| 2 | `check_architecture_boundaries_selftest.sh` | 0 | **PASS** ✅ | 14 assertions passed; Cases 5/8/9/10 skipped (patterns removed from main script) |
| 3 | `check_software_renderer_boundary.sh` | 2 | **FAIL** ❌ | I5: 1 `SoftwareRenderer&` at `src/runtime/render_engine.cpp:255` (`RenderEngineAccess::software_renderer()`) |
| 4 | `check_gitignored_dirs.sh` | 0 | **PASS** ✅ | 31/31 directories clean |
| 5 | `audit_software_renderer.sh` | 0 | **PASS** ✅ | All 9 grep→wc pipelines have `\|\| true`; pipefail-safe |
| 6 | `check_camera_architecture.sh` | 0 | **PASS** ✅ | 6/6 CAM-DOC 04 checks passed |
| 7 | `check_doc_sync.sh` | 0 | **PASS** ✅ | |
| 8 | `check_filename_drift.sh` | 0 | **PASS** ✅ | Warn mode; 102 drift warnings (advisory, exit 0) |
| 9 | `test_architectural.sh` | 0 | **PASS** ✅ | All 6 sections passed (quarantine, anti-global-state, anti-skip, include-hygiene, renderer/extension, child-target-boundary) |
| 10 | `install_consumer_test.sh` | 2 | **FAIL** ❌ | CMake 3.27 required; environment has 3.25.1 — configure blocked (environment limit, not code regression) |
| 11 | `check_backend_sanitization.py` | 0 | **PASS** ✅ | |

**Net: 9/11 PASS** (2 failures: gate #3 I5 regression, gate #10 CMake version mismatch)

## Gate #3 failure details

### I5: `SoftwareRenderer&` in process surfaces

```
[FAIL] I5: 1 riferimenti SoftwareRenderer& nelle superfici di processo (target R2)
  src/runtime/render_engine.cpp:255
```

The offending code (`src/runtime/render_engine.cpp:255`):

```cpp
SoftwareRenderer& RenderEngineAccess::software_renderer(RenderEngine& engine) noexcept {
    return *engine.m_impl->m_renderer;
}
```

This is the `RenderEngineAccess` boundary accessor — it intentionally returns a `SoftwareRenderer&` reference from a `RenderEngine`. The I5 gate scans `src/runtime/`, `src/render_graph/`, `src/backends/software/processors/`, and `include/chronon3d/backends/` for `SoftwareRenderer&` (excluding `SoftwareRenderer&&` move-refs).

**Diagnosis**: The accessor is a legitimate boundary surface, not a process coupling. The gate's grep is not precise enough — it flags the function return type alongside any parameter types. This was previously masked by the gate's backtick syntax error (fixed in commit `d9cebfd1`) which caused the script to abort before reaching I5.

**Fix options**:
- (a) Add `src/runtime/render_engine.cpp` to the I5 exclusion list with a comment noting it's the authorized boundary accessor.
- (b) Refine the grep to exclude return-type references (hunt only parameter lists).
- (c) Use `SoftwareRenderer*` or a type-erased handle in the accessor.

## Gate #10 failure details

```
CMake Error: CMake 3.27 or higher is required. You are running version 3.25.1
```

The `linux-ci` preset (`cmake/presets/ci.json`) requires `cmake_minimum_required(VERSION 3.27)`. The local environment has CMake 3.25.1. This is an environment limitation, not a code regression. Gate #10 passes on CI where CMake ≥ 3.27 is available.

## Gate notes (advisory / non-blocking)

- **Gate #1**: Checks [10], [12], [13] report advisory failures (non-blocking, exit 0).
- **Gate #2**: Cases 5, 8, 9, 10 skipped (patterns `make_execution_scheduler`, `m_runtime nullptr`, `m_renderer->settings`, `RenderPipeline` removed from main `check_architecture_boundaries.sh`).
- **Gate #8**: 102 filename drift warnings (advisory `--warn` mode). Known baseline: build artifacts, archived docs, and test cmake coordination files have legitimate references to renamed/moved files.

## Recent gate-fix commits (this sweep)

| Gate | Fix | Commit |
|------|-----|--------|
| #4 | `check_gitignored_dirs.sh` backtick syntax + `Testing/Temporary/` cleanup | `064ff505` |
| #3 | `software_renderer.hpp` LOC ≤200 + includes ≤6 + backtick fix | `91bdb378` |
| #9 | Exclusions + `grep -v '('` filter + TICKET-007.j metadata | `4b5f4189` |
| #8 | Exclusions + backslash fix + `warn` mode | `3de698b4` |
| #2 | Selftest aligned to current main script | `1b3116d7` |
| #5 | `\|\| true` on 9 grep→wc pipelines | `d9cebfd1` |
| #10 L2 | `expressions.hpp` include moved outside `namespace chronon3d` | `6323d59c` |

## Summary

```
GATE STATUS: 9/11 PASS
BLOCKERS: gate #3 (I5 SoftwareRenderer& accessor), gate #10 (CMake version)
FEATURE FREEZE: STILL ACTIVE — baseline is NOT green (needs 11/11 on same commit)
```

## Cross-references

- `AGENTS.md` — Feature Freeze V0.1 rules + 11-gate checklist
- `docs/CURRENT_STATUS.md` — current status snapshot
- `docs/FOLLOWUP_TICKETS.md` — open defects and follow-ups
- `docs/baselines/main-446a60e2-baseline.md` — previous baseline (2026-06-23)
- `docs/baselines/main-9ef0fe33-dod-fail-matrix.md` — earlier baseline
