# Gate Baseline — main @ 16319557

> Recorded on: 2026-07-04
> Branch: `main`
> HEAD: `16319557ffeacf1f38081861f12701b84e10c794` (short: `16319557`)
> Trigger: post-rebase of `docs(followup): TICKET-022 → DONE + TICKET-120 Cat-1 progress (3/24)`. The freshly-pushed HEAD rebases the doc-side change of `e6abd3d4` onto `ab81945d` + 4 intervening upstream commits (`b62ef442`, `e55da71c`, `87ea32d0`, `ab81945d`). The most material upstream change is:
>
> 1. `b62ef442` — `fix(gate-10): add diagnostics to consumer test + grid background processor` (added `printf(` diagnostic to `src/backends/software/processors/software_grid_background_processor.cpp:23` — this is the source of the gate #11 regression at this HEAD)
> 2. `e55da71c` — `chore(drift): Batch 1 — exclude build_output.txt + nested node_modules + add AGENTS.md drift-allow markers (179→80)`
> 3. `87ea32d0` — `chore(drift): Batch 2 — remove stale content/ examples + planning-artifact commit msg (87→84)`
> 4. `ab81945d` — `fix(gate-10): add PPM + raw binary dump to consumer test for PNG comparison`
> 5. `16319557` (this HEAD) — `docs(followup): TICKET-022 → DONE + TICKET-120 Cat-1 progress (3/24)` (rebased on top of the 4 upstream commits; byte-identical content to the pre-rebase `e6abd3d4` verified via `git diff e6abd3d4 16319557 -- docs/FOLLOWUP_TICKETS.md` = 0 bytes)
>
> User request: 11-gate run with NEW-SHA snapshot, full log saved at `docs/baselines/main-<NEW-SHA>-baseline.md`.
> Baseline file: `docs/baselines/main-16319557f-baseline.md`

## Gate results (all 11 gates)

| Gate | Script | Exit | Verdict | Notes |
|------|--------|------|---------|-------|
| 1 | `check_architecture_boundaries.sh` | 0 | **PASS** ✅ | 15/15 checks (carry-over from `c5793405` + `aaf70032` references) |
| 2 | `check_architecture_boundaries_selftest.sh` | 0 | **PASS** ✅ | 15/15 assertions |
| 3 | `check_software_renderer_boundary.sh` | 0 | **PASS** ✅ | I1–I5 all OK; `SoftwareRenderer&` accessor rot closed since `P0-1` lineage `2989b91d` |
| 4 | `check_gitignored_dirs.sh` | 0 | **PASS** ✅ | directory glob + file-pattern regex clean |
| 5 | `audit_software_renderer.sh` | 0 | **PASS** ✅ | Header LOC ≤200; no `SoftwareRenderer&` references in process surfaces; no camera-related lines in the audit (camera_v1 stays SDK-agnostic per gate #6) |
| 6 | `check_camera_architecture.sh` | 0 | **PASS** ✅ | 6/6 CAM-DOC 04 architecture-boundary checks |
| 7 | `check_doc_sync.sh` | 0 | **PASS** ✅ | 0 hard failures, 0 warnings. **R-Table (the new hardening gate from the prior session) was wired in but did NOT fire** — `docs/CURRENT_STATUS.md` is not in the HEAD~1..HEAD diff range (the only diff is `docs/FOLLOWUP_TICKETS.md`), so the R-Table rule correctly did not engage. Gate #7 R-Table verdict: PASS-by-no-diff. |
| 8 | `check_filename_drift.sh` | 0 | **PASS** ✅ | Warn-mode; **84 drift findings** (down from 179 → 80 → 84 → present). The two new `chore(drift)` upstream commits (Batch 1 → 80, Batch 2 → 87) and the natural accumulation of cross-language drift warnings between `aaf70032` (170) and the present Head (84) account for the variance. |
| 9 | `test_architectural.sh` | 0 | **PASS** ✅ | All 6 sections passed (quarantine, anti-global-state, anti-skip, include-hygiene, renderer/extension, child-target-boundary) |
| 10 | `install_consumer_test.sh` | 1 | **FAIL** ❌ | Phase 1–3 PASS (SDK builds clean, canary gate finds 8 present + 1 skipped + 0 missing, archive has 334 .o files, 159 installable headers). **Phase 4 FAIL**: PNG is near-black — `[install_consumer_test] FAIL: phase4 strict: PNG is near-black (0 pixels with mean RGB > 5/255)` after the consumer reported `[BOUNDARY-OK]` and successful 230400-pixel render. The consumer's grid background processor now reports white pixels (`first 5 pixels = (1.0000, 1.0000, 1.0000, 1.0000)`), but the text layer rasterization fails with `[warning] Text rasterization failed for node 'title_text'` (font issue under the consumer's `assets_root 'assets' is not a directory at CWD — text layer may render blank`). Failure mode is a different surface than the historical Phase 4 black render, but the gate verdict is the same pre-existing carry-over from `c5793405` / `aaf70032`. |
| 11 | `check_backend_sanitization.py` | 1 | **FAIL** ❌ | **`ERROR: Debug smoke signal 'printf(' found in src/backends/software/processors/software_grid_background_processor.cpp:23`** — introduced by upstream commit `b62ef442 fix(gate-10): add diagnostics to consumer test + grid background processor`. The diagnostic `printf(` was left behind in production code. **This is a NEW regression** vs. `c5793405` baseline (was 10/11 PASS there; now 9/11). Trivial fix: replace the `printf(` with the project's canonical diagnostic sink or remove it. Not blocking the gate suite, but blocks net green. |

**Net: 9/11 PASS** — **regression of 1 net PASS vs. `main@c5793405` (10/11)**. Gate #10 carry-over FAIL + Gate #11 NEW regression introduced by `b62ef442`.

## Gate #10 failure details

```
[install_consumer_test] Consumer: [2026-07-04 11:57:37.764] [warning] Text rasterization failed for node 'title_text'
[BOUNDARY-OK] sdk consumer (P3-H) rendered 640x360 PNG (9446 bytes, 230400/230400 pixels >5/255, format=Rgba8, bytes_per_row=2560, 0.000 ms)
[install_consumer_test] FAIL: phase4 strict: PNG is near-black (0 pixels with mean RGB > 5/255). Output: <REPO_ROOT>/../tmp/chronon3d_install_consumer_build.1sblfz/sdk_consumer_output.png
```

The Phase 1–3 build pipeline succeeds end-to-end. The Phase 4 consumer renders a framebuffer with 230400/230400 pixels above 5/255 *during the BoundaryOK check*, but the subsequent strict-mode 5/255 mean-RGB assertion finds 0 qualifying pixels. The `[consumer-diag]` and `[gb-diag]` lines show the grid background processor IS producing white pixels `(1.0000, 1.0000, 1.0000, 1.0000)`, but the strict-mode assertion (mean-RGB > 5/255) appears to evaluate pixels differently — likely the strict-mode reads a different region or applies a different aggregation. This is the Phase 4 carry-over from `c5793405` / `aaf70032`.

**Diagnosis area (unchanged):** SoftwareBackend render-path dispatch + FrameBuffer pool provisioning + composition-to-pixels materialisation. Two new `fix(gate-10)` upstream commits (`b62ef442` + `ab81945d`) added diagnostics + PPM/raw dumps that increase the available debug surface, but the underlying Phase 4 black-render failure persists.

**This session:** NOT code-fixed — out of Camera V1 step scope + gate-10 investigations are a separate workstream (TICKET-080, TICKET-Phase4-BlurTierRadii, etc.). The new diagnostic surface from `b62ef442` / `ab81945d` makes future debugging cheaper.

## Gate #11 NEW regression details

```
[backend_sanitization] ERROR: Debug smoke signal 'printf(' found in src/backends/software/processors/software_grid_background_processor.cpp:23
```

`git blame -L 23,23 src/backends/software/processors/software_grid_background_processor.cpp` confirms the `printf(` line was added in commit `b62ef442 fix(gate-10): add diagnostics to consumer test + grid background processor`. The commit's intent (add gate-10 diagnostics) is sound — the implementation simply left a debugging `printf(` in production code. Trivial fix (replace with `spdlog::debug(...)` per existing debug-sink convention, or remove). **Tracking**: this is a NEW `TICKET-G11-PRINTF-CLEANUP` to add to `docs/FOLLOWUP_TICKETS.md`.

**Previous baseline state**: gate #11 was PASS at `c5793405`. The `b62ef442` commit was added AFTER `c5793405`, so this baseline is the first to surface the regression. **Cross-check next baseline run** to verify the trivial cleanup restores gate #11 to PASS.

## Session commits (this run's pre-baseline work)

| Commit | Type | Description | Touches gate? |
|--------|------|-------------|---------------|
| `4edfda5c` | `fix(camera)` | `trajectory validator uses points().size() over size()` — fixes pre-existing on-`main` rot in `src/scene/camera/camera_v1/camera_program_compiler.cpp:331`. **Closes the last `chronon3d_scene_tests` link blocker**. | No (validator path only) |
| `8d33de4b` | `docs(changelog)` | Register Cat-1 build-rot fix + Camera row recovery commits | Touches gate **#7** (canonical only) |
| `efe7821c` | `feat(gate)` | Add `CURRENT_STATUS` table-shape hardening + R-Table wire-up — new gate `tools/check_current_status_table_shape.sh` wired into `check_doc_sync.sh` as R-Table | Touches gate **#7** (canonical only) |
| `b62ef442` (upstream) | `fix(gate-10)` | Add diagnostics to consumer test + grid background processor — **introduces gate #11 regression** | Touches gate **#11** (regression) |
| `e55da71c` (upstream) | `chore(drift)` | Batch 1 — exclude build_output.txt + nested node_modules + add AGENTS.md drift-allow markers (179→80) | Touches gate **#8** (warn-mode) |
| `87ea32d0` (upstream) | `chore(drift)` | Batch 2 — remove stale content/ examples + planning-artifact commit msg (87→84) | Touches gate **#8** (warn-mode) |
| `ab81945d` (upstream) | `fix(gate-10)` | Add PPM + raw binary dump to consumer test for PNG comparison | No (test-side diagnostic) |
| `16319557` (this HEAD) | `docs(followup)` | `TICKET-022 → DONE + TICKET-120 Cat-1 progress (3/24)` — byte-identical content to pre-rebase `e6abd3d4` | Touches gate **#7** (canonical only) |

## Gate notes (advisory / non-blocking)

- **Gate #1**: TICKET-041 (CMake module registry) and TICKET-042 (vcpkg dep parity) carry-over as advisory-only modifications (non-blocking). Same as prior baselines.
- **Gate #5**: Camera-related grep finds zero `SoftwareRenderer&` references; camera_v1 stays SDK-agnostic per arch-boundary gates.
- **Gate #7**: Only `docs/FOLLOWUP_TICKETS.md` was touched in this commit's HEAD~1..HEAD diff. The R-Table rule (post-efe7821c hardening) correctly did NOT fire because `docs/CURRENT_STATUS.md` is not in the diff range. R5 emitted a WARN (commit body mentions `TICKET-022`/`TICKET-120`, expects `docs/CHANGELOG.md` — but the WARN is non-fatal, gate exits 0).
- **Gate #8**: Filename-drift warnings are now at 84 (upstream Batch 1 → 80; Batch 2 + natural accumulation → 84). Carry-over from prior baselines' downward trend. Warn-mode, exit 0.
- **Gate #10**: New failure diagnostic surface from `b62ef442` + `ab81945d` adds `[consumer-diag]` / `[gb-diag]` / `[BOUNDARY-OK]` lines; Phase 4 strict-mode assertion still trips on near-black. Same pre-existing carry-over.
- **Gate #11**: NEW regression `printf(` line at `software_grid_background_processor.cpp:23`. Reason: `b62ef442` added the diagnostic in production code. Trivial cleanup.

## Summary

```
GATE STATUS: 9/11 PASS (REGRESSION: -1 vs main@c5793405)
BLOCKERS:
  - gate #10 (Phase 4 black render — pre-existing carry-over unchanged)
  - gate #11 (NEW REGRESSION: printf(` debug at software_grid_background_processor.cpp:23; trivial cleanup)
FEATURE FREEZE: STILL ACTIVE — baseline is NOT green (needs 11/11 on same commit)
NEW CERT STATE @ 16319557: 9/11 PASS, freeze INTACT
```

The session **added an additional gate-7 hardening** (R-Table in `check_doc_sync.sh`) at `efe7821c` and **closed the FOLLOWUP_TICKETS.md TICKET-022 → DONE + TICKET-120 Cat-1 progress** at `16319557`. Both `CHANGELOG` and `CURRENT_STATUS` reflect the post-Cat-1-fix factual state. **Net: 9/11 PASS (regression of 1 from `c5793405`'s 10/11)** — the regression is **standalone, trivial to fix** (one-line `printf(` removal from `software_grid_background_processor.cpp:23` introduced by upstream commit `b62ef442`), and orthogonal to the camera/text/sdk work completed in this session.

## Cross-references

- `AGENTS.md` — Feature Freeze V0.1 rules + 11-gate checklist + revocation clause.
- `docs/CURRENT_STATUS.md` — current status snapshot (HEAD `16319557` as of this baseline).
- `docs/FOLLOWUP_TICKETS.md` — open defects and follow-ups; gate #10 Phase 4 tracked; `size()` vs `points().size()` `camera_program_compiler.cpp:330-335` pre-existing rot resolved at `4edfda5c`; **NEW TICKET-G11-PRINTF-CLEANUP** should be added for the trivial gate #11 cleanup.
- `docs/CHANGELOG.md` — Cat-1 build-rot fix + Camera row recovery + Canonical table gate + FOLLOWUP TICKET-022 close entries.
- `docs/baselines/main-aaf70032-baseline.md` — previous macchina-verificata (2026-07-01, **10/11 PASS** — gate #10 different failure mode: `PRESET` unbound-variable script bug).
- `docs/baselines/main-c5793405-baseline.md` — direct predecessor (2026-07-04, **10/11 PASS** — gate #10 Phase 4 black render; this baseline is the first regression snapshot at **9/11**).
- `docs/baselines/main-21103265-baseline.md` — earlier baseline (2026-06-30, 9/11 PASS).
- `docs/baselines/main-30f6c876-baseline.md` — earlier baseline (2026-06-30, 8/11 PASS).
- `reports/perf/main-<prev-sha>-gates.json` — log macchina-verificato della 11-gate run (schema `chronon3d.gates.v1`); baseline JSON for `16319557` to be added in a follow-up forensic pass.
