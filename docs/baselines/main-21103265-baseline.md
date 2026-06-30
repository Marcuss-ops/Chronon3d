# Gate Baseline — main @ 21103265

> Recorded on: 2026-06-30
> Branch: `main`
> HEAD: `211032657b24ce5b702cc755b4d3bb034dcb493a` (short: `21103265`)
> Trigger: post session work — 3 atomic commits landed on `main` over this session:
>
> 1. `perf(dof): accept std::span<const float>; drop depth copy` (atomic via `a179b0fa`-equivalent; `TICKET-067` raw-push declared deviation)
> 2. `perf(text): surface-pool release + return on empty draw` (atomic, `85dec11a`-equivalent)
> 3. `chore(deprecate): remove dead glyph_selector_v2 variant pipeline (freeze cleanup)` (atomic, `21103265` — HEAD of this baseline)
>
> User request: 11-gate run with NEW-SHA snapshot, with full log saved at `docs/baselines/main-<NEW-SHA>-baseline.md`.
> Baseline file: `docs/baselines/main-21103265-baseline.md`

## Gate results (all 11 gates)

| Gate | Script | Exit | Verdict | Notes |
|------|--------|------|---------|-------|
| 1 | `check_architecture_boundaries.sh` | 0 | **PASS** ✅ | 14/14 checks pass; checks [10], [12], [13] are advisory-only |
| 2 | `check_architecture_boundaries_selftest.sh` | 0 | **PASS** ✅ | 14 assertions passed; Cases 5/8/9/10 skipped (patterns removed from main script) |
| 3 | `check_software_renderer_boundary.sh` | 2 | **FAIL** ❌ | I5: `SoftwareRenderer&` accessor at `src/runtime/render_engine.cpp:255` (carry-over from `88d2deec` baseline) |
| 4 | `check_gitignored_dirs.sh` | 0 | **PASS** ✅ | 31/31 directories clean (incl. `.tmp_gate*` per Phase 0 hardening) |
| 5 | `audit_software_renderer.sh` | 0 | **PASS** ✅ | All 9 grep→wc pipelines have `\|\| true`; pipefail-safe |
| 6 | `check_camera_architecture.sh` | 0 | **PASS** ✅ | 6/6 CAM-DOC 04 checks passed |
| 7 | `check_doc_sync.sh` | 0 | **PASS** ✅ | |
| 8 | `check_filename_drift.sh` | 0 | **PASS** ✅ | Warn mode; drift warnings advisory, exit 0 |
| 9 | `test_architectural.sh` | 0 | **PASS** ✅ | All 6 sections passed (quarantine, anti-global-state, anti-skip, include-hygiene, renderer/extension, child-target-boundary) |
| 10 | `install_consumer_test.sh` | 2 | **FAIL** ❌ | CMake 3.27 required; environment has 3.25.x — configure blocked (environment limit, not code regression; carry-over from `88d2deec`) |
| 11 | `check_backend_sanitization.py` | 0 | **PASS** ✅ | |

**Net: 9/11 PASS** (same 2 carries-over failures as `main@88d2deec`: gate #3 I5 SoftwareRenderer& accessor, gate #10 CMake version mismatch — both env-stable, no code regression in this session).

## Gate #3 failure details

### I5: `SoftwareRenderer&` in process surfaces

```
[FAIL] I5: 1 riferimenti SoftwareRenderer& nelle superfici di processo (target R2)
  src/runtime/render_engine.cpp:255
```

The offending code (`src/runtime/render_engine.cpp:255`) — verbatim from baseline `88d2deec`:

```cpp
SoftwareRenderer& RenderEngineAccess::software_renderer(RenderEngine& engine) noexcept {
    return *engine.m_impl->m_renderer;
}
```

This is the `RenderEngineAccess` boundary accessor — it intentionally returns a `SoftwareRenderer&` reference from a `RenderEngine`. The I5 gate scans `src/runtime/`, `src/render_graph/`, `src/backends/software/processors/`, and `include/chronon3d/backends/` for `SoftwareRenderer&` (excluding `SoftwareRenderer&&` move-refs).

**Diagnosis** (unchanged from `88d2deec` baseline): The accessor is a legitimate boundary surface, not a process coupling. The gate's grep is not precise enough — it flags the function return type alongside any parameter types. This was previously masked by the gate's backtick syntax error (fixed in commit `d9cebfd1`) which caused the script to abort before reaching I5.

**Fix options** (proposed in `88d2deec` baseline, still open):

- (a) Add `src/runtime/render_engine.cpp` to the I5 exclusion list with a comment noting it's the authorized boundary accessor.
- (b) Refine the grep to exclude return-type references (hunt only parameter lists).
- (c) Use `SoftwareRenderer*` or a type-erased handle in the accessor.

**This session**: FREEZE-blocked. AGENTS.md forbids new gate-rule adjustments during the feature-freeze period (the gate is the canonical contract; mutating it would be a `tools/` change prohibited under Cat-1/2/5). Recording only.

## Gate #10 failure details

```
CMake Error: CMake 3.27 or higher is required. You are running version 3.25.x
```

The `linux-ci` preset (`cmake/presets/ci.json`) requires `cmake_minimum_required(VERSION 3.27)`. The local environment has CMake 3.25.x. This is an environment limitation, not a code regression. Gate #10 passes on CI where CMake ≥ 3.27 is available.

**This session**: NO code change attempted; tracked as `TICKET-067`-class environment limitation. CI re-verification is the canonical path to a green gate #10.

## Gate notes (advisory / non-blocking)

- **Gate #1**: Checks [10], [12], [13] report advisory failures (non-blocking, exit 0).
- **Gate #2**: Cases 5, 8, 9, 10 skipped (patterns `make_execution_scheduler`, `m_runtime nullptr`, `m_renderer->settings`, `RenderPipeline` removed from main `check_architecture_boundaries.sh`).
- **Gate #4**: `.tmp_gate*` filter applied per Phase 0 hardening (commit lineage in `AGENTS.md`); local-run transient artifacts now correctly excluded.
- **Gate #8**: Filename drift warnings advisory `--warn` mode. Known baseline: build artifacts, archived docs, and test cmake coordination files have legitimate references to renamed/moved files (carry-over from Phase-2/Phase-3 mechanical decomp).

## Recent session commits (this run's pre-baseline work)

This session did **NOT** include any new gate-fix commits — failure modes #3 and #10 are both env-stable carry-overs from the prior `88d2deec` baseline. Session work was:

| Commit | Type | Description | Touches gate? |
|--------|------|-------------|---------------|
| `a179b0fa`-equivalent | `perf(dof)` | `std::span<const float>` depth — drop 8 MiB alloc per dispatch | No (perf-only) |
| `85dec11a`-equivalent | `perf(text)` | surface-pool release + return on empty draw | No (lifecycle) |
| `21103265` (this HEAD) | `chore(deprecate)` | freeze-cleanup: remove dead `glyph_selector_v2` variant pipeline (3 file deletions + 1 doc edit) | No (deletion) |

**No `tools/` changes** — gates behave identically to `88d2deec`; the run-faithful verdicts are the same 9/11.

## Summary

```
GATE STATUS: 9/11 PASS
BLOCKERS: gate #3 (I5 SoftwareRenderer& accessor), gate #10 (CMake version)
FEATURE FREEZE: STILL ACTIVE — baseline is NOT green (needs 11/11 on same commit)
NEW CERT STATE @ 21103265: 9/11 PASS, freeze INTACT
```

## Cross-references

- `AGENTS.md` — Feature Freeze V0.1 rules + 11-gate checklist + revocation clause.
- `docs/CURRENT_STATUS.md` — current status snapshot (`main@21103265` as of this baseline).
- `docs/FOLLOWUP_TICKETS.md` — open defects and follow-ups.
- `docs/baselines/main-88d2deec-baseline.md` — previous baseline (2026-06-30).
- `docs/baselines/main-446a60e2-baseline.md` — historical baseline (2026-06-23, 3/4 partial — the locked-tail reference for historical replay parity).
- `docs/baselines/main-9ef0fe33-dod-fail-matrix.md` — earlier baseline.
