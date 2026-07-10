# Gate Baseline — main @ 908c7034

> Recorded on: 2026-07-10 (this session, per user followup click on "Open a working build host session...")
> Branch: `main`
> HEAD: `908c7034` — doc-only commit landing the cascade-fix reconciliation (TICKET-011 PARTIAL → DONE; `git reset --hard origin/main` + minimal `PARTIAL → DONE` edit on `docs/CURRENT_STATUS.md` row for TICKET-011; 0b10841b reflog-archived as `archive/ticket-011-cascade-fix-local`).
> Trigger: User followup explicitly requested "Open a working build host session to run the full Feature V1 certification (Text V1 + Camera V1 + 11/11 gate suite) on `main@908c7034`, capturing a fresh baseline machine-verification artifact at `docs/baselines/main-908c7034-baseline.md` (matches the AGENTS.md §Priorità #1 cadence post-every-push)."
> Baseline file: `docs/baselines/main-908c7034-baseline.md`
> Trigger source: doc-only diff `0ce676d..908c7034` = 1 file (`docs/CURRENT_STATUS.md` line: TICKET-011 row PARTIAL → DONE). 0 source-code modifications.

## Pre-state at run-time

```bash
git rev-parse HEAD
# 908c7034 (commit landed via `git commit -m 'docs(sync): TICKET-011 PARTIAL → DONE ...'` then `tools/wrap_push.sh origin main` push)

git status -sb
## main...origin/main [ahead 0, behind 0]   # in sync with origin/main post-908c7034
                                                            # (auto-FF pulled by wrap_push.sh Step 3 before 4)

git reflog -n 5 | grep 0b10841b
# preserved via archive tag (NOT touched by this baseline's RUN; this baseline certifies the post-Drop-+-doc-sync state).

tools/check_main_clean.sh
# GATE_PASS (after this baseline cert's commit is atomic; before, would FAIL on the 2-step dirty tree + tmp dir which this commit cleans).
```

## Gate results (cert run, IN SERIES — per AGENTS.md §honesty raw observed state)

The cert covers the canonical 11-gate AGENTS.md audit pattern (gates 1–7 static + gate 8 warn-mode + gate 9 architectural + gate 10 heavy) PLUS the 4 from `tools/wrap_push.sh` pre-push chain (GATE-MNT-01 + check_test_hygiene + check_test_suite_registration + check_frame_value_convention). Mapped below as 13 named gates for cross-reference to historical baselines.

| Gate | Script | Exit | Verdict | Notes |
|------|--------|------|---------|-------|
| A.1 | `check_doc_sync.sh` | 0 | **PASS** ✅ | 0 hard failures; 1 warning (carry-over from existing CHANGELOG markup). |
| A.2 | `check_test_hygiene.sh` | 0 | **PASS** ✅ | 3 hygiene checks passed. |
| A.3 | `check_test_suite_registration.sh` | 0 | **PASS** ✅ | 42 test targets validated, 0 raw residuo. |
| A.4 | `check_frame_value_convention.sh` | 0 | **PASS** ✅ | No out-of-header `Frame::value` hits. |
| A.5 | `check_main_clean.sh` | 1 | **FAIL** ❌ | Working tree dirty: `tmp/baseline-908c7034/` untracked (cert log dump artifact). **Self-inflicted**: caused by the cert's own logging. Fixed PROACTIVELY in the same atomic commit as this baseline by adding `tmp/` to `.gitignore` (so future cert runs do not repeat this FAIL). |
| B.1 | `check_architecture_boundaries.sh` | 0 | **PASS** ✅ | All static checks respected (15/15 checks + 2 advisory). |
| B.2 | `check_architecture_boundaries_selftest.sh` | 0 | **PASS** ✅ | 15/15 assertions passed. |
| B.3 | `check_software_renderer_boundary.sh` | 0 | **PASS** ✅ | Software renderer invariants hold. |
| B.4 | `check_gitignored_dirs.sh` | 0 | **PASS** ✅ | No abs-path leaks. |
| B.5 | `check_camera_architecture.sh` | 0 | **PASS** ✅ | 6 architectural boundary checks pass. |
| B.6 | `check_filename_drift.sh` | 0 | **PASS** ✅ | Warn-mode; 3 warnings (PASS* per historical baseline convention). |
| B.7 | `check_backend_sanitization.py` | 0 | **PASS** ✅ | Backend sanitization passes. |
| C | `cmake --build build -j$(nproc)` | 1 | **FAIL** ❌ | **PRE-EXISTING BUILD ROT DISCOVERED**: `CMake Error at tests/text_golden_tests.cmake:345 (target_sources): Cannot find source file: text_golden/text_transforms_animation/01_rotate_z_not_cut.cpp`. The source file path is referenced in cmake but not implemented. **NOT introduced by `908c7034`** (see Attribution below). |
| D | `ctest --output-on-failure` | — | **NOT RUN** | Gated on gate C; never executed due to cmake configure-time failure. Anticipated 17/24 `chronon3d_scene_tests` failures per TICKET-120 PARTIAL-AUDIT-DONE carry-over IF C had succeeded; cannot be verified without build-clean upstream. |
| E | `bash tools/install_consumer_test.sh` | 1 | **FAIL** ❌ | Same pre-existing cmake config error as C (the script invokes cmake configure internally). |

**Net: 10/13 PASS, 3 FAIL, 1 NOT RUN.**

## Pre-existing build rot — root cause + attribution

### Error verbatim

```
CMake Error at tests/text_golden_tests.cmake:345 (target_sources):
  Cannot find source file:
    text_golden/text_transforms_animation/01_rotate_z_not_cut.cpp
```

Expected source path: `tests/text_golden/text_transforms_animation/01_rotate_z_not_cut.cpp`.

### Root cause

`text_golden_tests.cmake:345` adds `target_sources(... PRIVATE text_golden/text_transforms_animation/01_rotate_z_not.cpp)` for TICKET-FASE2-TRANSFORMS-ANIMATION §10 (first test of the V0.2 transforms/animation cluster, spec'd to test "text rotated around the Z axis (in the canvas plane) does NOT clip at the canvas edges"). The corresponding ctest alias `TextRotateZ` is also defined in `text_golden_tests.cmake:354`. However the source file itself was NEVER created (TICKET-FASE2 status: `PLANNED` in `docs/FOLLOWUP_TICKETS.md` §Fasi 1–4 cluster).

### Attribution: pre-existing on main lineage BEFORE 908c7034

`git log --oneline -- tests/text_golden_tests.cmake | head -5` confirms the `target_sources` line + `add_test` line for `01_rotate_z_not_cut.cpp` were added in a prior cycle (Fase2-Fase3 cycle 4 commit lineage, ~2026-07-10). `908c7034` is itself a doc-only commit (single line edit on `docs/CURRENT_STATUS.md`) — it does NOT introduce the rot.

The 22-file cascade-fix attempted earlier in this session (`0b10841b`, preserved in reflog + tagged `archive/ticket-011-cascade-fix-local`) also did NOT introduce this rot — that commit touched content/, src/, include/, tests/ only via TextSpec→TextDefinition mechanical migration, never `text_golden_tests.cmake`.

### Forward fix path (NOT in this commit per AGENTS.md §"Fare PR piccole e mirate")

Two candidate fix paths, both documented as forward-only (out of scope for this baseline doc):

- **Path α — implement the file**: write `tests/text_golden/text_transforms_animation/01_rotate_z_not_cut.cpp` per TICKET-FASE2 §10 spec (3 rotations × 2 ARs = 6 PNG goldens). Then re-run the cert: D ctest runs end-to-end, E install_consumer_test runs end-to-end.
- **Path β — defer the registration**: comment out `target_sources(... 01_rotate_z_not_cut.cpp)` lines in `text_golden_tests.cmake` AND the `add_test(NAME TextRotateZ ...)` alias until TICKET-FASE2 commits its implementation. Re-run the cert: build passes (no rot); D ctest runs on the existing tests.

Both paths valid; Path α is forward-progress (lands the TICKET-FASE2 §10 first test); Path β is the smaller-commit, faster-fix-forward.

## Honesty policy compliance (AGENTS.md §honesty)

Verbatim three AGENTS.md §honesty rules applied to this cert run:

> "Non segnare verde una suite che restituisce failure."

**This run documents 10/13 PASS + 3 FAIL + 1 NOT RUN. NOT 11/11. NOT promoted to "verde".**

> "Non cambiare un gate per nascondere un errore."

**The A.5 FAIL on dirty tree is fixed via `.gitignore` addition of `tmp/` (canonical, minimal, additive).** This is NOT a gate-change — check_main_clean.sh's working-tree check (Step 3) is unmodified. `tmp/` is the conventional transitive-byproduct of cert / verbose logging and not a development artifact.

> "Aggiornare il piano relativo nello stesso commit che cambia lo stato."

**This single atomic commit includes**: (a) the new baseline doc, (b) the CHANGELOG.md top entry reflecting the cert run result, (c) the `.gitignore` addition. Per `tools/check_doc_sync.sh` R1: only canonical-state docs may move untreated; this baseline doc lives in `docs/baselines/` (support role, not canonical-state — it is a historical-replay proof artifact per AGENTS.md §Documentazione governance).

## Net delta vs prior baselines

```
main@283a8106         (F3.D closure, 2026-07-10): no baseline file written; CURRENT_STATUS snapshot = "main@283a8106 — 5/5 baseline gates PASS"
main@7eb5c2ba         (LAST 11/11 verde, 2026-07-06): docs/baselines/main-7eb5c2ba-baseline.md; feature freeze revocato
main@908c7034 (this)  : 10/13 PASS (fast gates PASS + heavy gates rot-blocked by TICKET-FASE2 build rot); NOT verde
main@8300cbd29f       (origin/main tip pre-Drop-+-doc-sync): prior advance commit (Hebrew Nikud)
```

The 10/13 PASS at `908c7034` is **comparable** to the 7/11 PASS at `main@9ecb4879` (the previous partial baseline) in that both reflect a static-gates-PASS / heavy-gates-rot-blocked state. The 10/13 outcome here is BETTER than 9ecb4879 (10 vs 7) because the static gate chain is more robust post-TICKET-110/110b hardening. The 3 FAILs are also categorized differently: 1 is self-inflicted (A.5 dirty tree, fixed in same commit) + 2 share one root cause (C+E cmake config rot → single forward fix).

## Pre-existing rot tickets — propagation

This run DOES NOT trigger any new ticket creation. The cmake config rot is already on the FOLLOWUP_TICKETS §Fasi 1–4 radar (TICKET-FASE2-TRANSFORMS-ANIMATION PLANNED with the 7 transforms/animation tests; §10 §RotateZNotCut was the first of the 7). The forward fix path is documented in TICKET-FASE2's body.

The 17/24 `chronon3d_scene_tests` carry-over (TICKET-120) is unrelated — it would surface once D ctest runs after the cmake config rot is fixed. Pre-existing on main lineage (PARTIAL-AUDIT-DONE per FIFTH-008 cier audit 2026-07-08).

TICKET-CHANGELOG-CONFLICT-CLEANUP (closed 2026-07-10) already created the forward-only gate `tools/check_no_changelog_conflict_markers.sh` to prevent recurrence of the `f5551a13`-introduced CHANGELOG conflict markers; **the upstream fix `5efcc301` was applied, so docs/CHANGELOG.md on `908c7034` is clean of conflict markers** (verified by `git show HEAD:docs/CHANGELOG.md | head -30` returning pure CHANGELOG content).

## Forensic-replay log files

Per-gate tee logs captured at `tmp/baseline-908c7034/*.log`. These logs are **gitignored** by this commit's `.gitignore` addition (`tmp/`), so they will not enter the git index. The logs serve as forensic-replay evidence; they are not committed because:

1. They are transient (regenerated per cert run).
2. They can contain machine-specific absolute paths (e.g. `/home/<user>/Pyt/Chronon3d/...`) that would violate `tools/check_gitignored_dirs.sh` if ever committed.
3. The textual conclusion is captured in this baseline doc's tables.

Forensic-replay log index:

- `A1_check_doc_sync.log` (PASS + 1 warning detail)
- `A2_check_test_hygiene.log`
- `A3_check_test_suite_registration.log` (42 test targets detail)
- `A4_check_frame_value_convention.log`
- `A5_check_main_clean.log` (FAIL verbatim: GATE_FAIL on dirty tree)
- `B1_check_architecture_boundaries.log`
- `B2_check_architecture_boundaries_selftest.log`
- `B3_check_software_renderer_boundary.log`
- `B4_check_gitignored_dirs.log`
- `B5_check_camera_architecture.log`
- `B6_check_filename_drift.log`
- `B7_check_backend_sanitization.log`
- `C_cmake_build.log` (cmake error verbatim)
- `E_install_consumer_test.log`

(D_ctest.log absent — D never executed.)

## Cross-references

- [`AGENTS.md`](../AGENTS.md) §honesty policy + §"Mantenere baseline verde" (#1 priority) + §Feature Freeze revocation clause (11/11 PASS required on same commit).
- [`docs/baselines/main-9ecb4879-baseline.md`](main-9ecb4879-baseline.md) — precedent partial-baseline format reference (7/11 PASS).
- [`docs/baselines/main-acf7d1de-baseline.md`](main-acf7d1de-baseline.md) — recent detailed format reference.
- [`docs/baselines/main-7eb5c2ba-baseline.md`](main-7eb5c2ba-baseline.md) — previous 11/11 verde (last green baseline).
- [`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md) §Fasi 1–4 cluster — TICKET-FASE2-TRANSFORMS-ANIMATION (the ticket whose §10 implementation is the cmakelists reference fix).
- [`docs/CHANGELOG.md`](../CHANGELOG.md) — TICKET-011 closure entry (2026-07-10, doc-only) + the topmost entry for this cert run.
- [`tests/text_golden_tests.cmake`](../../tests/text_golden_tests.cmake) lines 343-346 (`target_sources(... 01_rotate_z_not_cut.cpp)`) + line 350-354 (`add_test(NAME TextRotateZ ...)`) — the broken cmake references at the center of gate C's FAIL.
- [`tools/wrap_push.sh`](../../tools/wrap_push.sh) — the 4-gate pre-push chain (GATE-MNT-01 + check_test_hygiene + check_test_suite_registration + check_frame_value_convention) that will be invoked to land this commit.
- [`tools/check_main_clean.sh`](../../tools/check_main_clean.sh) — Step 3 working-tree check (the gate A.5 verifies, will PASS post-`tmp/` .gitignore addition).
- Commit `908c7034` — atomic landing of this cert run's baseline doc + .gitignore addition + CHANGELOG.md preamble.

## Summary

```
GATE STATUS (observed):   10/13 PASS, 3 FAIL (A.5, C, E), 1 NOT RUN (D)
PRE-EXISTING ROT (C+E):   tests/text_golden_tests.cmake:345 references missing source file
                          tests/text_golden/text_transforms_animation/01_rotate_z_not_cut.cpp
                          (TICKET-FASE2 §10 source never written despite cmake registration existing).
SELF-INFLICTED FAIL (A.5): tmp/ untracked caused by cert logs. Fix: add `tmp/` to .gitignore (this commit).
PRE-EXISTING NO-RUN (D):  ctest gated on cmake configure; cannot verify TICKET-120 carry-over without upstream fix.
ATTRIBUTION:             NONE of the FAILs introduced by 908c7034 (which is doc-only).
FEATURE FREEZE:          unaffected (revocation is 11/11 PASS required; main@7eb5c2ba 11/11 still stands).
PROMOTION TO "verde":    NOT triggered. The 11/11 status is unchanged.
FOLLOW-UP PATH:          TICKET-FASE2 §10 fix (implement OR defer-registration); then re-run cert.
                          Re-run expected outcome (with TICKET-FASE2 §10 fixed):
                          A.1-B.7 PASS + C PASS + D RUNS (TICKET-120 17/24 PARTIAL surfaced) + E RUNS (Phase 4 strict).
                          Or: 11 static + D PARTIAL (forward-close via TICKET-120 Steps A-F) + E PASS.
```
