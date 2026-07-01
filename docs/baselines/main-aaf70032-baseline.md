# Gate Baseline — main @ aaf70032

> Recorded on: 2026-07-01
> Branch: `main`
> HEAD: `aaf70032a8ae597a54a5cce71e3560aed8d438b2` (short: `aaf70032`)
> Trigger: This session landed 3 atomic commits on `main` exercising the
> audit hotspot regression area (P0-1 → P0-3) — none touching `tools/`:
>
> 1. P0-1: `fix(backends): resolve TICKET-077 + TICKET-079 — header LOC ≤200 + SoftwareRenderer*` (closes the persistent `SoftwareRenderer&` accessor rot that failed gate #3 in the prior `21103265` and `88d2deec` baselines)
> 2. P0-2: `test(text): P0-1 regression — draw_text_run scratch_state + per-span font_handle` (3 TEST_CASEs locking the null-validation guards in `text_run_processor.cpp`)
> 3. P0-3: `test(render_graph): P0-3 regression — TextRunNode error cannot be silent` (3 TEST_CASEs locking the spdlog diagnostic contract of `TextRunNode::execute()`)
>
> User request: 11-gate run with NEW-SHA snapshot, with full log saved at `docs/baselines/main-<NEW-SHA>-baseline.md`.
> Baseline file: `docs/baselines/main-aaf70032-baseline.md`

## Gate results (all 11 gates)

| Gate | Script | Exit | Verdict | Notes |
|------|--------|------|---------|-------|
| 1 | `check_architecture_boundaries.sh` | 0 | **PASS** ✅ | 14/14 checks pass; checks [11/14] + [13/14] are advisory-only (TICKET-041 CMake registry, TICKET-042 vcpkg parity) |
| 2 | `check_architecture_boundaries_selftest.sh` | 0 | **PASS** ✅ | 14 assertions passed |
| 3 | `check_software_renderer_boundary.sh` | 0 | **PASS** ✅ | I1-I5 all OK; **closes the prior `21103265` I5 FAIL** (`SoftwareRenderer&` accessor rotated to `SoftwareRenderer*` per P0-1 commit lineage) |
| 4 | `check_gitignored_dirs.sh` | 0 | **PASS** ✅ | 28/28 directories clean |
| 5 | `audit_software_renderer.sh` | 0 | **PASS** ✅ | Header LOC=182, non-local includes=6, renderer& in proc=5 (all within budget); report written to `docs/audits/<date>-software-renderer-inventory.md` |
| 6 | `check_camera_architecture.sh` | 0 | **PASS** ✅ | 6/6 CAM-DOC 04 checks passed |
| 7 | `check_doc_sync.sh` | 0 | **PASS** ✅ | 0 hard failures, 0 warnings |
| 8 | `check_filename_drift.sh` | 0 | **PASS** ✅ | Warn mode; 53 drift warnings (advisory — same baseline carry-over) |
| 9 | `test_architectural.sh` | 0 | **PASS** ✅ | All 6 sections passed (quarantine, anti-global-state, anti-skip, include-hygiene, renderer/extension, child-target-boundary) |
| 10 | `install_consumer_test.sh` | 1 | **FAIL** ❌ | `tools/install_consumer_test.sh: line 82: PRESET: unbound variable` (script bug — `set -euo pipefail` plus the line 82 `cmake -S "$REPO_ROOT" -B "$SDK_BUILD" --preset "$PRESET"` references `$PRESET` before the export `PRESET="$CHRONON3D_INSTALL_TEST_PRESET"` further down). Different failure mode from the prior baseline (`cmake 3.27 required` at `21103265`). |
| 11 | `check_backend_sanitization.py` | 0 | **PASS** ✅ | |

**Net: 10/11 PASS** — improved from `9/11` at `21103265` (gate #3 closed by P0-1); gate #10 regressed onto a new failure surface (an unbound-var script bug, not the previous CMake-version mismatch).

## Gate #10 failure details

```
=== orchestrator ===
preset      : linux-ci
repo root   : <repo-root>
temp root   : /tmp
fast mode   : 0
ghost sweep : 0
tools/install_consumer_test.sh: line 82: PRESET: unbound variable
```

The offending line — verbatim from the script:

```bash
cmake -S "$REPO_ROOT" -B "$SDK_BUILD" --preset "$PRESET" \
    -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" 1>&2
```

The variable `PRESET` is **never bound** in the script above line 82.
`set -euo pipefail` (line 36) plus the unset-variable probe triggers
the failure. The export of `PRESET` is declared further down at the
"Export for phase-script consumption" block:

```bash
export SDK_BUILD SDK_PREFIX REPO_ROOT PRESET="$CHRONON3D_INSTALL_TEST_PRESET"
```

…but that runs **after** the `cmake` call at line 82. The Phase 1
configure uses an unbound `$PRESET`.

**Diagnosis**: The script was previously importing `PRESET` from
`CHRONON3D_INSTALL_TEST_PRESET` (default `linux-ci` at line 46) via
`: "${VAR:=default}"`. The current code references `$PRESET` without
ever deriving it from the env var. The previous baseline (`21103265`)
never reached this line because CMake 3.25.x was rejected earlier in
`require_cmake_3_27`. With the env meeting the CMake 3.27 requirement
(neither baseline actually exercised it here — `bash` evaluated
`require_cmake_3_27` early but proceeded), the script reaches line 82
and `set -u` trips.

**Fix options** (proposed; out of scope for this baseline-run commit):

- (a) Add `PRESET="${CHRONON3D_INSTALL_TEST_PRESET}"` near the top of the script (alongside the env-var defaulting block) — minimal change, zero behavioural drift.
- (b) Reorder: move the `export PRESET=…` line above the `cmake` call.
- (c) Replace `$PRESET` with `"$CHRONON3D_INSTALL_TEST_PRESET"` directly on line 82.

**This session**: NOT code-fixed — out of user scope ("document
gates, commit, push"), and freeze-blocking (gate-rule / `tools/`
changes are restricted under Cat-1). Recording only; tracked as a
script-class rot (different from the prior CMake-version rot).
CI re-verification after a one-line fix will be the canonical path to
green gate #10.

## Session commits (this run's pre-baseline work)

| Commit | Type | Description | Touches gate? |
|--------|------|-------------|---------------|
| `2989b91d`-range (P0-1 lineage) | `fix(backends)` | TICKET-077 + TICKET-079 resolution — `software_renderer.hpp` ≤200 LOC, `attach_software_backend` signature flip | **YES** — closes gate #3 I5 |
| `2989b91d` | `test(text)` | 3 TEST_CASEs locking null-guard paths + E2E stroke render-success | No (test-only) |
| `aaf70032` (this HEAD) | `test(render_graph)` | 3 TEST_CASEs locking `TextRunNode::execute()` diagnostic contract (audit hotspot #2 regression) | No (test-only) |

**Only `tools/`-adjacent work** was the P0-1 `fix(backends)` commit
that closed the persistent `SoftwareRenderer&` accessor rot (carry-over
from `88d2deec` and `21103265` baselines). Gate #3 promoted from
FAIL to PASS on the same commit that closes it.

## Gate notes (advisory / non-blocking)

- **Gate #1**: Checks [11/14] (CMake module registry — TICKET-041) and [13/14] (vcpkg dep parity — TICKET-042) report advisory failures (non-blocking, exit 0). P0 scope forbids closing either during freeze; both remain tracked under TICKET.
- **Gate #8**: 53 filename-drift warnings. Same baseline carry-over as `21103265` and `88d2deec` — references to renamed/moved files in archived docs (Path-2 mechanical decomp). Warn mode remains advisory.
- **Gate #9**: All 6 sections (quarantine, anti-global-state, anti-skip, include-hygiene, renderer/extension, child-target-boundary) passed cleanly.

## Summary

```
GATE STATUS: 10/11 PASS (improved from 9/11 at 21103265)
BLOCKERS: gate #10 (PRESET unbound-variable script bug, NEW failure mode)
FEATURE FREEZE: STILL ACTIVE — baseline is NOT green (needs 11/11 on same commit)
NEW CERT STATE @ aaf70032: 10/11 PASS, freeze INTACT
```

The session **closed one gate** (gate #3 I5 via P0-1) and **regressed one gate** (gate #10 onto a script-bound-variable bug, distinct from the prior CMake-version rot).

## Cross-references

- `AGENTS.md` — Feature Freeze V0.1 rules + 11-gate checklist + revocation clause.
- `docs/CURRENT_STATUS.md` — current status snapshot (`main@aaf70032` as of this baseline).
- `docs/FOLLOWUP_TICKETS.md` — open defects and follow-ups (gate #10 script rot tracked here).
- `docs/baselines/main-21103265-baseline.md` — previous baseline (2026-06-30, 9/11 PASS — gate #3 I5 FAIL, gate #10 CMake-version FAIL).
- `docs/baselines/main-88d2deec-baseline.md` — earlier baseline (2026-06-30).
- `docs/baselines/main-446a60e2-baseline.md` — historical baseline (2026-06-23, 3/4 partial — locked-tail reference for historical replay parity).
- `docs/baselines/main-9ef0fe33-dod-fail-matrix.md` — earlier baseline.
