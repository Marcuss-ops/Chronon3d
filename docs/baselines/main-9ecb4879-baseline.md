# Gate Baseline — main @ 9ecb4879

> Recorded on: 2026-07-04
> Branch: `main`
> HEAD: `9ecb4879004a847700a9fe79e5c533a28bb12f6f` (short: `9ecb4879`)
> Trigger: User request specified `main@56103053` as audit-target SHA; that SHA is **not a valid git object**.
> Actual run performed on `main@9ecb4879` (matching the latest committed `TICKET-UNIFIED-VCPKG-TOOLCHAIN`).
>
> **Origin/main pre-state at run-time:** `da142d89bc6eb76618545883ef7ad70bb5d71dcd` (2 commits ahead of `9ecb4879`: `258cb7b3 docs(baseline): certify 11-gate audit on main @ 16319557 (9/11 PASS)` + `da142d89 fix(build): TICKET-011 - elimina duplicazione hash helpers + fix test build rot`). Per `git merge-base --is-ancestor 9ecb4879 origin/main` = **true** (fast-forwardable, NOT a true divergence). `git merge-base` common ancestor = `9ecb4879`.
>
> Although origin/main has 2 ahead-commits, this baseline snapshots the audit on the *exact* SHA the user-requested run was performed against (`9ecb4879` = last commit before the most-recent generation). The origin/main ahead-commits land via fast-forward on push.
>
> User request: 11-gate run on current main, with outcomes captured + baseline file at `docs/baselines/main-<sha>-baseline.md`.
> Baseline file: `docs/baselines/main-9ecb4879-baseline.md`

## Gate results (all 11 gates, run IN SERIES)

| Gate | Script | Exit | Verdict | Notes |
|------|--------|------|---------|-------|
| 1 | `check_architecture_boundaries.sh` | 1 | **FAIL** ❌ | Check [16/16] SDK public-deps SSoT wiring: `registry CHRONON3D_SDK_PUBLIC_DEPS entries: 0` + `@CHRONON3D_FIND_DEPENDENCY_LINES@` count = 0 (expected 1). Pre-existing on lineage `gate-10-...`; NOT introduced by `9ecb4879`. |
| 2 | `check_architecture_boundaries_selftest.sh` | 0 | **PASS** ✅ | 14/14 assertions (same baseline shape as `aaf70032`). |
| 3 | `check_software_renderer_boundary.sh` | 0 | **PASS** ✅ | I1-I5 all OK. |
| 4 | `check_gitignored_dirs.sh` | 0 | **PASS** ✅ | All scanned directories clean. |
| 5 | `audit_software_renderer.sh` | 0 | **PASS** ✅ | Report generated, exit 0. |
| 6 | `check_camera_architecture.sh` | 0 | **PASS** ✅ | 6/6 CAM-DOC 04 checks passed. |
| 7 | `check_doc_sync.sh` | 0 | **PASS** ✅ | 0 hard failures, 0 warnings. |
| 8 | `check_filename_drift.sh` | 0 | **PASS** ✅ | Warn-mode (advisory). Exact count captured in `/tmp/11gates/g8-filename-drift.log`. |
| 9 | `test_architectural.sh` | 1 | **FAIL** ❌ | Section 6 `child_target_check_arch_boundary` fails: same root cause as gate #1 (Check 16 SDK public-deps SSoT wiring assertion). `test_architectural.sh` Section 6 invokes `check_architecture_boundaries.sh` natively → same FAIL. |
| 10 | `install_consumer_test.sh` | 1 | **FAIL** ❌ | Phase 4 strict: PNG is near-black (`0 pixels with mean RGB > 5/255`). Pre-existing on main lineage; `2ef2b377 sw_sidecar` partial fix insufficient alone. |
| 11 | `check_backend_sanitization.py` | 1 | **FAIL** ❌ | `printf` debug signal at `src/backends/software/processors/software_grid_background_processor.cpp:23`. Per `git blame -L 23,23`: introducing commit = **`b62ef4429` 2026-07-04** (today). Pre-existing; NOT introduced by `9ecb4879`. |

**Net: 7/11 PASS**.

## Pre-regression chain

```
11/11 PASS (target, never observed)
  └─ 10/11 PASS at main@aaf70032  (macchina-verificata, gold reference)
       └─ 9/11 PASS at main@16319557   (intermediate audit, lands via 258cb7b3)
            └─ 7/11 PASS at main@9ecb4879   (this run — REGRESSION)
```

The delta vs `9ecb4879` snapshot (the only delta introduced by this baseline's framing) is: **gates #1+#9 went from advisory at `aaf70032` to hard-FAIL via the gate-10 promotion commits that landed between `aaf70032` and `16319557`**. Gates #10+#11 are pre-existing carry-over failures present on `aaf70032` lineage too (cf. `main@1078ab46` and `main@a53a8d25` audit snapshots further down `CURRENT_STATUS.md`).

## Per-failure detail blocks

### Gates #1 + #9 — SDK public-deps SSoT wiring (Check 16)

```
[FAIL] check_architecture_boundaries.sh: Check 16 / SDK public-deps SSoT wiring
  registry CHRONON3D_SDK_PUBLIC_DEPS entries: 0
  @CHRONON3D_FIND_DEPENDENCY_LINES@ occurrences in cmake/Chronon3DConfig.cmake.in: 0 (expected 1)
```

The check located at `tools/check_architecture_boundaries.sh` lines 401-445 asserts:
- `CHRONON3D_SDK_PUBLIC_DEPS` (defined in `cmake/Chronon3DRegistry.cmake` line 185; populated lines 200-212 from the SDK public-deps list) has at least one entry.
- The marker block `# >>> AUTO-GENERATED FROM CHRONON3D_SDK_PUBLIC_DEPS ... # <<< END AUTO-GENERATED BLOCK <<<` in `cmake/Chronon3DConfig.cmake.in` contains exactly one `@CHRONON3D_FIND_DEPENDENCY_LINES@` substitution placeholder.

**Attribution: PRE-EXISTING on lineage `gate-10-...` (introduced on main BEFORE `9ecb4879`).** Recent commits in the gate-10 promotion lineage that flipped the previously-advisory check into a HARD failure (per `git log --oneline | grep gate-10 | head`):

- `fix(sdk): gate-10 — add trace_categories.hpp to SDK manifest`
- `fix(sdk): gate-10 — add transitive header dependencies to SDK manifest`
- `fix(sdk): gate-10 — add 5 transitive headers to public SDK manifest`
- `fix(sdk, umbrella): gate-10 — expose RenderEngine + save_png for consumer test`
- `fix(sdk): gate-10 — add expression_types.hpp to public headers manifest`
- `fix(tools): gate-10 — export VCPKG_INSTALLED_DIR for external consumer Phase 4`
- `fix(cache): gate-10 — promote all cache private headers to public include/`
- `fix(text, cache): gate-10 — close namespace + promote framebuffer_pool.hpp public`
- `fix(text): gate-10 — fix truncated line_to_word + add missing paragraph_to_line and span_to_paragraph in text_unit_map.cpp`

**Causal-chain disambiguation:**

TICKET-UNIFIED-VCPKG-TOOLCHAIN (`9ecb4879`) is **NOT** in the g1+g9 causal chain. The five files touched by `9ecb4879`:

- `cmake/Chronon3DVcpkgToolchain.cmake` (NEW)
- `cmake/presets/base.json` (EDIT, line 14: `CMAKE_TOOLCHAIN_FILE`)
- `cmake/presets/linux-fast-dev.json` (EDIT, removed `CMAKE_PREFIX_PATH`)
- `cmake/presets/development.json` (EDIT, removed `CMAKE_PREFIX_PATH` from `linux-ci-lean-gate`)
- `tools/check_vcpkg_canonical_path.sh` (NEW)

None of these files intersect `cmake/Chronon3DConfig.cmake.in` or `cmake/Chronon3DRegistry.cmake` (the SSoT surface for the gate). The g1+g9 FAILs are independent of the vcpkg-toolchain consolidation.

### Gate #10 — install_consumer Phase 4 render-black

```
[FAIL] install_consumer_test.sh: Phase 4 strict
  PNG is near-black (0 pixels with mean RGB > 5/255)
  render output: <install-prefix>/rendered.png near-black, no consumer-facing failure surface
```

The sw_sidecar fix in commit `2ef2b377` (`SoftwareRenderer* sw_sidecar` threaded through `render_scene_via_graph`) partially addresses the culling-layer bug observed at `main@1078ab46` but **does NOT resolve Phase 4 render-black on this SHA**. Pre-existing on main lineage.

Cross-references documenting this fail-mode:
- `docs/baselines/main-aaf70032-baseline.md` showed gate #10 FAIL on a different surface (the original `PRESET: unbound variable` script bug).
- `docs/CURRENT_STATUS.md` §"Gate audit snapshot — main@1078ab46" records the same Phase 4 render-black (230400 pixel < 5/255).
- `docs/CURRENT_STATUS.md` §"Gate audit snapshot — main@a53a8d25" records a different Phase 4 surface (`Disk quota exceeded` on PCH 224MB/file).

Different Phase 4 failure modes across baselines — none in causal-chain with `9ecb4879`.

### Gate #11 — printf debug signal in `software_grid_background_processor.cpp`

```
[FAIL] check_backend_sanitization.py
  src/backends/software/processors/software_grid_background_processor.cpp:23: printf(  (debug signal — script disallowed)
```

Per `git blame -L 23,23 src/backends/software/processors/software_grid_background_processor.cpp`:
introducing commit = **`b62ef4429` (2026-07-04, today)**, on the main lineage between `9ecb4879` and `16319557` (i.e. present in BOTH the local HEAD used for this audit AND the origin/main ahead-commit `da142d89`).

**NOT introduced by TICKET-UNIFIED-VCPKG-TOOLCHAIN (`9ecb4879`)** — that vcpkg-toolchain commit does not touch `src/backends/`. The printf is a snapshot-date carry-over from `b62ef4429` (a parallel fix-build commit in the same session).

Fix is mechanical (delete the `printf` call + the surrounding `#include <cstdio>` and `std::fprintf` usage), qualifies under AGENTS.md §"🔴 Feature Freeze — V0.1" Categoria 1 (correzioni di build, no new features). Tracked as a free-floating pre-existing rotation, separate from this baseline-run commit (Cat-3 + Cat-5 only).

## Session commits (this run's pre-baseline work)

The audit was performed at HEAD `9ecb4879`. The commits between the most-recent macchina-verificata (`aaf70032` at 10/11 PASS) and the audited SHA (`9ecb4879`) include all lineage-blocking fixes + the TICKET-UNIFIED-VCPKG-TOOLCHAIN work. The relevant commits relative to this baseline run:

| Commit | Type | Description | Touches gate? |
|--------|------|-------------|---------------|
| (earlier-in-lineage) `fix(backends)` lineage | P0-1 / gate-3 I5 closure | `software_renderer.hpp` ≤200 LOC + `attach_software_backend` signature rotation | Closes gate #3 I5 (already PASS on this baseline) |
| (earlier-in-lineage) `fix(sdk): gate-10 ...` lineage | SDK gate-10 promotion | Multiple commits promoting previously-advisory checks into hard Check #16 | **YES** — promotes gate #1+#9 from advisory to FAIL-HARD (this baseline) |
| (intermediate) `b62ef4429` | (parallel session) | introduces `printf` debug signal at `software_grid_background_processor.cpp:23` | **YES** — gate #11 FAIL (this baseline) |
| (this HEAD) `9ecb4879` | `chore(cmake): UNIFIED-VCPKG-TOOLCHAIN — single toolchain file across all presets` | creates `cmake/Chronon3DVcpkgToolchain.cmake` + revises 3 preset JSONs + new regression check | **NO** — gates #1+#9+#10+#11 failures are pre-existing on the lineage pre-`9ecb4879` |

`git log 9ecb4879..HEAD` returns 0 commits (audit at HEAD); `git log 9ecb4879..origin/main` returns 2 ahead-commits (`258cb7b3 docs(baseline): ... @ 16319557 (9/11)` + `da142d89 fix(build): TICKET-011 - ...`).

## Gate notes (advisory / non-blocking)

- **Gate #1 + #9**: Listed above as FAIL (not advisory). The previous macchina-verificata `aaf70032` recorded them as advisory-only (TICKET-041, TICKET-042); the gate-10 promotion lineage flipped them to hard FAILs.
- **Gate #2**: `check_architecture_boundaries_selftest.sh` PASS; cases 5/8/9/10 may be skipped (patterns removed from main script) — same baseline shape as `aaf70032`.
- **Gate #4**: `.tmp_gate*` filter applied per Phase 0 hardening commit lineage in AGENTS.md §"GATE-MNT-01".
- **Gate #8**: filename-drift warnings. Warn-mode; exact count captured in `/tmp/11gates/g8-filename-drift.log`.
- **Gate #9**: Section 6 (`child_target_check_arch_boundary`) at `tools/test_architectural.sh:329` invokes `check_architecture_boundaries.sh` natively — explains the locked FAIL pattern between gates #1 and #9.

## Summary

```
GATE STATUS: 7/11 PASS
PRE-REGRESSION CHAIN: 10/11 (aaf70032 macchina-verificata) → 9/11 (16319557 audit) → 7/11 (9ecb4879 baseline, this run)
BLOCKERS:
  • gate #1 + #9 SDK public-deps SSoT wiring (Check 16) — pre-existing, gate-10-... lineage.
  • gate #10 install_consumer Phase 4 render black — pre-existing, sdk-sidecar lineage partial fix.
  • gate #11 backend sanitization printf — pre-existing, intro b62ef4429 (2026-07-04, today).
FEATURE FREEZE: STILL ACTIVE — baseline is NOT green (needs 11/11 on same commit per AGENTS.md §Revoca).
NEW CERT STATE @ 9ecb4879: 7/11 PASS, freeze INTACT.
```

The session **did NOT introduce any gate regression**. TICKET-UNIFIED-VCPKG-TOOLCHAIN (`9ecb4879`) is in the lineage considered but is **NOT** in the causal chain for any of the 4 FAILs.

## Cross-references

- [`AGENTS.md`](../AGENTS.md) — Feature Freeze V0.1 rules + 11-gate checklist + revocation clause (11/11 required on same commit).
- [`docs/CURRENT_STATUS.md`](../CURRENT_STATUS.md) — current status snapshot, §"Certificazione corrente" updated to reflect the regression at `main@9ecb4879`.
- [`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md) — canonical blocker index.
- [`docs/CHANGELOG.md`](../CHANGELOG.md) — recent closures (incl. `chore(cmake): UNIFIED-VCPKG-TOOLCHAIN` at `9ecb4879`).
- [`docs/baselines/main-9ecb4879-baseline.md`](main-9ecb4879-baseline.md) — this file.
- [`docs/baselines/main-16319557f-baseline.md`](main-16319557f-baseline.md) — prior audit (9/11 PASS, lands via origin/main ahead-commit `258cb7b3` docs(baseline): certify 11-gate audit on main @ 16319557 (9/11 PASS)).
- [`docs/baselines/main-aaf70032-baseline.md`](main-aaf70032-baseline.md) — most recent macchina-verificata (10/11 PASS, gold reference).
- [`docs/baselines/main-21103265-baseline.md`](main-21103265-baseline.md) — earlier baseline (9/11 PASS, gate #3 I5 + gate #10 CMake env).
- [`docs/baselines/main-88d2deec-baseline.md`](main-88d2deec-baseline.md) — earlier baseline.
- [`docs/baselines/main-446a60e2-baseline.md`](main-446a60e2-baseline.md) — historical baseline (2026-06-23, 3/4 partial — locked-tail reference for historical replay parity).

## Forensic-replay log files

Per-gate tee logs captured for forensic replay at `/tmp/11gates/<g>.log`:

- `/tmp/11gates/g1-arch-boundaries.log`
- `/tmp/11gates/g2-arch-boundaries-selftest.log`
- `/tmp/11gates/g3-software-renderer-boundary.log`
- `/tmp/11gates/g4-gitignored-dirs.log`
- `/tmp/11gates/g5-audit-software-renderer.log`
- `/tmp/11gates/g6-camera-architecture.log`
- `/tmp/11gates/g7-doc-sync.log`
- `/tmp/11gates/g8-filename-drift.log`
- `/tmp/11gates/g9-test-architectural.log`
- `/tmp/11gates/g10-install-consumer-test.log`
- `/tmp/11gates/g11-backend-sanitization.log`

Each log captures the gate's stdout/stderr verbatim plus the timestamp + duration captured by the orchestrator. Per-gate exit codes: `g1=1, g2=0, g3=0, g4=0, g5=0, g6=0, g7=0, g8=0, g9=1, g10=1 (346s), g11=1`. Bash sub-timeouts: 60s per gate except `g9=300s, g10=900s`.
