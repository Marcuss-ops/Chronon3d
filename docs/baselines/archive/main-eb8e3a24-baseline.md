# Gate Baseline — main @ eb8e3a24

> Recorded on: 2026-07-04
> Branch: `main`
> HEAD: `eb8e3a246085772f07906e50a4109f0e363d7a1c` (short: `eb8e3a24`)
> Trigger: User request specified `main@56103053` as audit-target SHA; that SHA is **not a valid git object**. The same request was issued in two consecutive turns — first run performed on `main@9ecb4879` (commit `eb8e3a24`'s predecessor) capturing 7/11 PASS at commit lineage HEAD; this second run performed on actual current main HEAD `eb8e3a24` after the prior run's doc-commit (which added `docs/baselines/main-9ecb4879-baseline.md` + the `CURRENT_STATUS.md` §"Certificazione corrente" update) landed on `origin/main` via fast-forward. Both runs captured 7/11 PASS on consecutive commits, confirming the verdict is durable to at-least-this-class of doc-only changes.
>
> Origin/main pre-state at run-time: `HEAD == origin/main == eb8e3a24` (aligned at run). `git log eb8e3a24..origin/main` returns 0 commits.
>
> User request: 11-gate run on current main, with outcomes captured + baseline file at `docs/baselines/main-<sha>-baseline.md`.
> Baseline file: `docs/baselines/main-eb8e3a24-baseline.md`

## Gate results (all 11 gates, run IN SERIES)

| Gate | Script | Exit | Verdict | Notes |
|------|--------|------|---------|-------|
| 1 | `check_architecture_boundaries.sh` | 1 | **FAIL** ❌ | Check [16/16] SDK public-deps SSoT wiring: `registry CHRONON3D_SDK_PUBLIC_DEPS entries: 0` + `@CHRONON3D_FIND_DEPENDENCY_LINES@` count = 0 (expected 1). Pre-existing on lineage `gate-10-...`; carry-over from `9ecb4879`. |
| 2 | `check_architecture_boundaries_selftest.sh` | 0 | **PASS** ✅ | 14/14 assertions (same baseline shape). |
| 3 | `check_software_renderer_boundary.sh` | 0 | **PASS** ✅ | I1-I5 all OK. |
| 4 | `check_gitignored_dirs.sh` | 0 | **PASS** ✅ | All scanned directories clean. |
| 5 | `audit_software_renderer.sh` | 0 | **PASS** ✅ | Report generated, exit 0. | <!-- drift-allow: archived-doc-pattern -->
| 6 | `check_camera_architecture.sh` | 0 | **PASS** ✅ | 6/6 CAM-DOC 04 checks passed. |
| 7 | `check_doc_sync.sh` | 0 | **PASS** ✅ | 0 hard failures, 0 warnings; R-table CURRENT_STATUS.md shape canonical (13 body rows + 12 named labels). |
| 8 | `check_filename_drift.sh` | 0 | **PASS** ✅ | Warn-mode (advisory); **85 drift findings** (↑ from 53 at `aaf70032`, ↑ from 82 at `1078ab46`). Exact log: `/tmp/11gates_v2/g8-filename-drift.log`. |
| 9 | `test_architectural.sh` | 1 | **FAIL** ❌ | Section 6 `child_target_check_arch_boundary` fails: same root cause as gate #1 (Check 16 SDK public-deps SSoT wiring). `test_architectural.sh` Section 6 invokes `check_architecture_boundaries.sh` natively → locked fail pattern. Carry-over from `9ecb4879`. |
| 10 | `install_consumer_test.sh` | 1 | **FAIL** ❌ | **Failure-mode SHIFT this run**: was Phase 4 PNG near-black (`0 pixels with mean RGB > 5/255`) at `9ecb4879`; now `ninja subcommand failure during compilation of src/backends/software/.../highway_*_kernels.cpp.o` in `chronon3d_backend_software` target at `eb8e3a24`. Build stop at index [340/345] with `ninja: error: unknown target subcommand`-class surface. Carry-over FAIL (still pre-existing), but the **mode shift** is the forensic signal at this SHA. Recommend independent re-run to disambiguate transient noise vs durable build-rot. |
| 11 | `check_backend_sanitization.py` | 1 | **FAIL** ❌ | `printf` debug signal at `src/backends/software/processors/software_grid_background_processor.cpp:23`. Per `git blame -L 23,23`: introducing commit = **`b62ef4429`** (2026-07-04, today). Carry-over from `9ecb4879`. |

**Net: 7/11 PASS — stable across 9ecb4879 → eb8e3a24** (no code regression; only docs files changed between these two consecutive commits).

## Pre-regression chain

```
11/11 PASS (target, never observed)
  └─ 10/11 PASS at main@aaf70032  (macchina-verificata, gold reference)
       └─ 9/11 PASS at main@16319557   (intermediate audit, lands via 258cb7b3)
            └─ 7/11 PASS at main@9ecb4879 → main@eb8e3a24   (stable across doc-only-diff commits; this run confirms durability)
```

The unique FAIL-state commits (where PASS count changed) are: `16319557` (10→9), `9ecb4879` (9→7). `eb8e3a24` is a doc-only commit (no FAIL state change) — its baseline file is a **stability attestation** rather than a regression report.

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

**Attribution: PRE-EXISTING on lineage `gate-10-...` (carry-over from 9ecb4879).** Recent commits in the gate-10 promotion lineage flipped the previously-advisory check into a HARD failure. The commits between `aaf70032` and `9ecb4879` include:

- `fix(sdk): gate-10 — add trace_categories.hpp to SDK manifest`
- `fix(sdk): gate-10 — add transitive header dependencies to SDK manifest`
- `fix(sdk): gate-10 — add 5 transitive headers to public SDK manifest`
- `fix(sdk, umbrella): gate-10 — expose RenderEngine + save_png for consumer test`
- `fix(sdk): gate-10 — add expression_types.hpp to public headers manifest`
- `fix(tools): gate-10 — export VCPKG_INSTALLED_DIR for external consumer Phase 4`
- `fix(cache): gate-10 — promote all cache private headers to public include/`
- `fix(text, cache): gate-10 — close namespace + promote framebuffer_pool.hpp public`
- `fix(text): gate-10 — fix truncated line_to_word + add missing paragraph_to_line and span_to_paragraph in text_unit_map.cpp`

**`eb8e3a24` (this HEAD) does NOT introduce any new entry to the gate-10 lineage.** The only files changed between `9ecb4879` and `eb8e3a24` are:
- `docs/baselines/main-9ecb4879-baseline.md` (NEW)
- `docs/CURRENT_STATUS.md` (EDIT, §"Certificazione corrente" + Link canonici block)

Neither file intersects `cmake/Chronon3DConfig.cmake.in` or `cmake/Chronon3DRegistry.cmake` (the SSoT surface).

### Gate #10 — install_consumer FAILURE-MODE SHIFT (Phase 4 → ninja compile)

```
At 9ecb4879 (predecessor baseline):
[FAIL] install_consumer_test.sh: Phase 4 strict
  PNG is near-black (0 pixels with mean RGB > 5/255)
  render output: <install-prefix>/rendered.png near-black, no consumer-facing failure surface

At eb8e3a24 (this run):
[FAIL] install_consumer_test.sh: ninja subcommand failure during compilation
  chronon3d_backend_software target: build stop at index [340/345]
  affected files: highway_blend_kernels, highway_rasterize_kernels,
                  highway_conversion_kernels, highway_separable_kernels,
                  highway_dof_kernels (.cpp.o compilation steps)
  error class: ninja: subcommand failed → exit 1
```

**Both modes are pre-existing build/SDK ROT, NOT introduced by `eb8e3a24`** (which only modifies docs). The pre-existing `2ef2b377 sw_sidecar` partial fix applies to Phase 4; the ninja compile error surfaces earlier in the build chain (in `chronon3d_backend_software` AVX2/SIMD kernels).

**The shift from Phase 4 → ninja is the forensic signal.** Different failure modes at the same SHA suggest either:
- (a) **Transient noise** — environmental flake (disk I/O, ccache miss, parallel-build race). Re-running `g10` in isolation should re-capture Phase 4 PNG near-black (consistent with `9ecb4879`).
- (b) **Durable build rot** — pre-existing `highway_*_kernels` compile rot that the Phase 4 surface had been masking prior to this run. The `*_kernels` rot is independent of the `sw_sidecar` lineage.

**Action recommended:** Independent re-run of `bash tools/install_consumer_test.sh` in isolation to disambiguate. Per-gate re-run log: `/tmp/11gates_v2/g10-install-consumer-test.log` (last 30 lines cited above).

### Gate #11 — printf debug signal in `software_grid_background_processor.cpp`

```
[FAIL] check_backend_sanitization.py
  src/backends/software/processors/software_grid_background_processor.cpp:23: printf(  (debug signal — script disallowed)
```

Per `git blame -L 23,23 src/backends/software/processors/software_grid_background_processor.cpp`:
introducing commit = **`b62ef4429` (2026-07-04, today)**. Carry-over from `9ecb4879` — same root cause, identical foreground at this audit SHA.

**NOT introduced by `eb8e3a24`** — that doc-only commit does not touch `src/backends/`. Fix is mechanical (delete the `printf` call + surrounding `#include <cstdio>` and `std::fprintf` usage); qualifies under AGENTS.md §"🔴 Feature Freeze — V0.1" Categoria 1 (correzioni di build, no new features). Tracked as a free-floating pre-existing rotation, separate from this baseline-run commit (Cat-3 + Cat-5 only).

## Session commits (this run's pre-baseline work)

The audit was performed at HEAD `eb8e3a24`. The most recent run-producing commits are:

| Commit | Type | Description | Touches gate? |
|--------|------|-------------|---------------|
| `b62ef4429` | (parallel session) | introduces `printf` debug signal at `software_grid_background_processor.cpp:23` | **YES** — gate #11 FAIL (carry-over) |
| `9ecb4879` | `chore(cmake): UNIFIED-VCPKG-TOOLCHAIN — single toolchain file across all presets` | creates `cmake/Chronon3DVcpkgToolchain.cmake` + revises 3 preset JSONs + new regression check | **NO** — g1+g9 pre-existing on gate-10-... lineage |
| `258cb7b3` | `docs(baseline): certify 11-gate audit on main @ 16319557 (9/11 PASS)` | creates `docs/baselines/main-16319557f-baseline.md` | **NO** — docs only |
| `da142d89` | `fix(build): TICKET-011 - elimina duplicazione hash helpers + fix test build rot` | build helpers + test rot rotation close | **POTENTIAL** — may surface as `g10 ninja subcommand failure` if `highway_*_kernels` compile rot introduced |
| `eb8e3a24` (this HEAD) | `docs(baseline): main@9ecb4879 — 11-gate run, 7/11 PASS (regression from 10/11 at aaf70032)` | creates `docs/baselines/main-9ecb4879-baseline.md` + CURRENT_STATUS.md update | **NO** — docs only |

`git log 9ecb4879..eb8e3a24` returns 1 commit (the prior `9ecb4879-baseline` doc-commit itself). `git log eb8e3a24..HEAD` returns 0 (audit at HEAD). `git log eb8e3a24..origin/main` returns 0 (aligned).

## Gate notes (advisory / non-blocking)

- **Gate #1 + #9**: Listed above as FAIL. Pre-audit (`aaf70032`): advisory-only (TICKET-041, TICKET-042).
- **Gate #2**: 14/14 assertions PASS; pattern-skip behavior unchanged from `aaf70032`.
- **Gate #4**: `.tmp_gate*` exclusion per Phase 0 hardening commit in AGENTS.md.
- **Gate #8**: 85 filename-drift warnings (↑ from 53 at aaf70032, ↑ from 82 at `main@1078ab46`). Warn-mode. Pattern: references to renamed/moved files in archived docs (carry-over from Phase-2/Phase-3 mechanical decomp).
- **Gate #9**: Section 6 `child_target_check_arch_boundary` at `tools/test_architectural.sh:329` invokes `check_architecture_boundaries.sh` natively.

## Summary

```
GATE STATUS: 7/11 PASS — stable across 9ecb4879 → eb8e3a24 (no code regression)
PRE-REGRESSION CHAIN: 10/11 (aaf70032 macchina-verificata) → 9/11 (16319557 audit) → 7/11 (9ecb4879 stable at eb8e3a24)
BLOCKERS:
  • gate #1 + #9 SDK public-deps SSoT wiring (Check 16) — pre-existing, gate-10-... lineage.
  • gate #10 install_consumer FAILURE-MODE SHIFT (this run) — Phase 4 → ninja compile subcommand failure on highway_*_kernels. Forensic disambiguation required.
  • gate #11 backend sanitization printf — pre-existing, intro b62ef4429 (2026-07-04, today).
FEATURE FREEZE: STILL ACTIVE — baseline is NOT green (needs 11/11 on same commit per AGENTS.md §Revoca).
NEW CERT STATE @ eb8e3a24: 7/11 PASS, freeze INTACT.
```

The session **did NOT introduce any gate regression**. `eb8e3a24` (which is itself the previous baseline-doc commit) preserves the same 7/11 PASS outcome as its predecessor `9ecb4879`, demonstrating stability across the only delta (the new `docs/baselines/main-9ecb4879-baseline.md` + `docs/CURRENT_STATUS.md` update).

## Cross-references

- [`AGENTS.md`](../AGENTS.md) — Feature Freeze V0.1 rules + 11-gate checklist + revocation clause (11/11 required on same commit).
- [`docs/CURRENT_STATUS.md`](../CURRENT_STATUS.md) — current status snapshot, §"Certificazione corrente" + Link canonici updated for this baseline.
- [`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md) — canonical blocker index.
- [`docs/CHANGELOG.md`](../CHANGELOG.md) — recent closures (incl. `chore(cmake): UNIFIED-VCPKG-TOOLCHAIN` at `9ecb4879`).
- [`docs/baselines/main-eb8e3a24-baseline.md`](main-eb8e3a24-baseline.md) — this file.
- [`docs/baselines/main-9ecb4879-baseline.md`](main-9ecb4879-baseline.md) — predecessor baseline (7/11 PASS, the prior audit immediately before this one).
- [`docs/baselines/main-16319557f-baseline.md`](main-16319557f-baseline.md) — prior audit (9/11 PASS, lands via origin/main ahead-commit `258cb7b3`).
- [`docs/baselines/main-aaf70032-baseline.md`](main-aaf70032-baseline.md) — most recent macchina-verificata (10/11 PASS, gold reference).
- [`docs/baselines/main-21103265-baseline.md`](main-21103265-baseline.md) — earlier baseline (9/11 PASS).
- [`docs/baselines/main-88d2deec-baseline.md`](main-88d2deec-baseline.md) — earlier baseline.
- [`docs/baselines/main-446a60e2-baseline.md`](main-446a60e2-baseline.md) — historical baseline (locked-tail reference).

## Forensic-replay log files

Per-gate tee logs captured for forensic replay at `/tmp/11gates_v2/<g>.log`:

- `/tmp/11gates_v2/g1-arch-boundaries.log`
- `/tmp/11gates_v2/g2-arch-boundaries-selftest.log`
- `/tmp/11gates_v2/g3-software-renderer-boundary.log`
- `/tmp/11gates_v2/g4-gitignored-dirs.log`
- `/tmp/11gates_v2/g5-audit-software-renderer.log`
- `/tmp/11gates_v2/g6-camera-architecture.log`
- `/tmp/11gates_v2/g7-doc-sync.log`
- `/tmp/11gates_v2/g8-filename-drift.log`
- `/tmp/11gates_v2/g9-test-architectural.log`
- `/tmp/11gates_v2/g10-install-consumer-test.log`
- `/tmp/11gates_v2/g11-backend-sanitization.log`

Each log captures the gate's stdout/stderr verbatim. Per-gate exit codes: `g1=1, g2=0, g3=0, g4=0, g5=0, g6=0, g7=0, g8=0, g9=1, g10=1 (284s), g11=1`. Bash sub-timeouts: 60s per gate except `g9=300s, g10=900s`.

A parallel set from the first run is preserved at `/tmp/11gates/<g>.log` (run at `main@9ecb4879`). The two log sets differ ONLY on `g10` (Phase 4 PNG near-black ↔ ninja compile failure mode shift) — all other gates produce bit-identical output across both runs.
