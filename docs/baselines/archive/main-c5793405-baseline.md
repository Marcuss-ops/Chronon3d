# Gate Baseline — main @ c5793405

> Recorded on: 2026-07-04
> Branch: `main`
> HEAD: `c5793405812c666bc3188b3b360960fa3d116dae` (short: `c5793405`)
> Trigger: post-Cat-1 build-rot fix + `docs/CURRENT_STATUS.md` Camera row recovery. The freshly-pushed HEAD exercises:
>
> 1. `7ee302bf` — `feat(camera): Cat-1 build rot — qualify FocalPx + FrameRate field-rename + CameraSession include` (3 Agent3-introduced regressions closed; full `chronon3d_scene_tests` link now GC=0)
> 2. `c5793405` (this HEAD) — `docs(status): recover missing Camera Production V1 row in CURRENT_STATUS` (recovery commit after `7ee302bf`'s sed-conflict over-match removed both Camera variants)
>
> User request: 11-gate run with NEW-SHA snapshot, full log saved at `docs/baselines/main-<NEW-SHA>-baseline.md`.
> Baseline file: `docs/baselines/main-c5793405-baseline.md`

## Gate results (all 11 gates)

| Gate | Script | Exit | Verdict | Notes |
|------|--------|------|---------|-------|
| 1 | `check_architecture_boundaries.sh` | 0 | **PASS** ✅ | 15/15 checks pass (matches `main@aaf70032` reference; advisory-only carry-overs TICKET-041, TICKET-042) |
| 2 | `check_architecture_boundaries_selftest.sh` | 0 | **PASS** ✅ | 15/15 assertions passed |
| 3 | `check_software_renderer_boundary.sh` | 0 | **PASS** ✅ | I1–I5 all OK; **closes the persistent `SoftwareRenderer&` accessor rot** (clean since `P0-1` lineage `2989b91d`) |
| 4 | `check_gitignored_dirs.sh` | 0 | **PASS** ✅ | directory glob + file-pattern regex clean |
| 5 | `audit_software_renderer.sh` | 0 | **PASS** ✅ | Header LOC ≤200; no `SoftwareRenderer&` references in process surfaces; no camera-related lines in `audit_software_renderer` audit (camera_v1 stays SDK-agnostic per gate #6) | <!-- drift-allow: archived-doc-pattern -->
| 6 | `check_camera_architecture.sh` | 0 | **PASS** ✅ | 6/6 CAM-DOC 04 architecture-boundary checks passed |
| 7 | `check_doc_sync.sh` | 0 | **PASS** ✅ | 0 hard failures, 0 warnings (only the 4 canonicals CURRENT_STATUS/ROADMAP/RELEASE_GATE/FOLLOWUP_TICKETS + CHANGELOG of support were touched; row recovery passed gate) |
| 8 | `check_filename_drift.sh` | 0 | **PASS** ✅ | Warn mode (`--warn`); drift warnings carry-over from prior baselines |
| 9 | `test_architectural.sh` | 0 | **PASS** ✅ | All 6 sections passed (quarantine, anti-global-state, anti-skip, include-hygiene, renderer/extension, child-target-boundary) |
| 10 | `install_consumer_test.sh` | 1 | **FAIL** ❌ | Phase 4 consumer SDK produces a black-output PNG: `[BOUNDARY-FAIL] all 230400 pixels below 5/255 — output PNG would be black`. Phase 1–3 PASS; the failure is in the render path (SoftwareBackend dispatch / framebuffer-pool / projection non-zero pixels candidate). Pre-existing carry-over from `main@aaf70032`; sw_sidecar thread fix in `2ef2b377` was insufficient. |
| 11 | `check_backend_sanitization.py` | 0 | **PASS** ✅ | `python3 tools/check_backend_sanitization.py` returns RC=0 with `SUCCESS: All sanitization checks passed!`. **Env note:** the canonical invocation in this AvC environment is `python3` (Python 3.14.4 at `/usr/bin/python3`); `python` is not in `$PATH` and exits 127 (command not found) on bare `python tools/...`. The gate is green under the env's actual interpreter. |

**Net: 10/11 PASS** — same net as `main@aaf70032`. Gate #10 fire-pre-existing-and-still-FAIL (Phase 4 black render); gate #11 green under the env's `python3` interpreter.

## Gate #10 failure details

```
[BOUNDARY-FAIL] all 230400 pixels below 5/255 — output PNG would be black
```

The consumer test pipeline runs Phase 1–3 cleanly (CMake configure + install + SDK link), then Phase 4 renders a `Composition` through the installed `sdk::RenderEngine` and writes a PNG output. The framebuffer comes back as 0 RGB (all pixels below the 5/255 threshold), so the boundary-fail assertion trips.

**Diagnosis (unchanged from `aaf70032`)**: `RenderEngineAccess::software_renderer()` bridge passes `sw_renderer` through `compute_dirty_rect` (the sw_sidecar fix in `2ef2b377` covered the culling-path null deref, but did not address render-output emptiness). The remaining surface to investigate:

- (a) `SoftwareBackend::draw_node()` not dispatching `GridBackgroundShape`/`TextRunShape` to the framebuffer.
- (b) `FrameBufferPool` not provisioning a render target that the render-graph executor writes to.
- (c) Camera projection producing all-zero pixels (camera_v1 tests pass in isolation but `sdk::RenderEngine` may use a different instantiation).

**This session**: NOT code-fixed — out of Camera V1 step scope. Tracked under `TICKET-120` (24 pre-existing fallimenti in `chronon3d_scene_tests`); Phase 4 black-render is a separate, distinct hierarchy that requires dedicated diagnostic time.

## Gate #11 env note (informational)

The canonical invocation in this environment uses `python3` (Python 3.14.4). A bare `python tools/check_backend_sanitization.py` exits 127 (command not found). The gate is GREEN when invoked with `python3` — all `SUCCESS: All sanitization checks passed!` confirms. Documented here so the next forensic run on this host uses the same interpreter.

## Session commits (this run's pre-baseline work)

| Commit | Type | Description | Touches gate? |
|--------|------|-------------|---------------|
| `e8f07116` | `feat(camera)` | Camera V1 trajectory lens/DOF/motion_blur/parent carry-forward + OrientAlongPath real tangent/roll + keep_horizon + degenerate-tangent safety + §10 GOLDEN regression + TICKET-021/022/024/025 DONE (the carry-chain previously tracked at `b06f65b5` / `88a2b39c`) | No (test + docs only) |
| `7ee302bf` | `feat(camera)` | Cat-1 build rot — qualify `FocalPx` (4 sites), rename `FrameRate.num/den` → `.numerator/.denominator` (2 sites), add `#include <camera_session.hpp>` (1 site). **Closes the 3 Agent3-introduced regressions** that previously blocked `chronon3d_scene_tests` from linking. Full scene-tests link now GC=0. | No (test-side only) |
| `c5793405` (this HEAD) | `docs(status)` | Re-insert the missing `Camera Production V1` row in `docs/CURRENT_STATUS.md` (the row was inadvertently dropped by an over-permissive sed regex during `7ee302bf`'s rebase-conflict resolution). Updated post-Cat-1 factual state. | Touches gate **#7** (canonical only, gate `check_doc_sync.sh` PASS) |

**No `tools/` changes** — gates behave deterministically relative to `aaf70032`. The 3 Agent3-introduced build-rot regressions are fixed; the pre-existing on-`main` rot `size()` vs `points().size()` at `src/scene/camera/camera_v1/camera_program_compiler.cpp:330-335` remains pre-existing (out of scope Cat-1 step; not yet fixed in any commit).

## Gate notes (advisory / non-blocking)

- **Gate #1**: TICKET-041 (CMake module registry) and TICKET-042 (vcpkg dep parity) carry-over as advisory-only modifications (non-blocking).
- **Gate #7**: Only CURRENT_STATUS.md / CHANGELOG.md touched; `check_doc_sync.sh` PASS with 0 hard failures.
- **Gate #8**: Filename-drift warnings stay advisory (`--warn` mode, exit 0). Reference carry-over from `aaf70032`.
- **Gate #5**: Camera-related grep finds zero `SoftwareRenderer&` references; camera_v1 stays SDK-agnostic per arch-boundary gates.

## Summary

```
GATE STATUS: 10/11 PASS (matches main@aaf70032)
BLOCKERS: gate #10 (Phase 4 black render — pre-existing carry-over)
FEATURE FREEZE: STILL ACTIVE — baseline is NOT green (needs 11/11 on same commit)
NEW CERT STATE @ c5793405: 10/11 PASS, freeze INTACT
```

The session **closed 3 Agent3 build-rot regressions** consolidated in `7ee302bf` + **recovered the canonical doc-side collateral damage** in `c5793405` (the missing Camera Production V1 row). Both `ChANGELOG` and `CURRENT_STATUS` reflect the post-Cat-1-fix factual state. Gate-level state is back to the `aaf70032` reference net; **no gate promotion / no gate promotion regression** — net flat.

## Cross-references

- `AGENTS.md` — Feature Freeze V0.1 rules + 11-gate checklist + revocation clause.
- `docs/CURRENT_STATUS.md` — current status snapshot (`main@c5793405` as of this baseline); `Camera Production V1` row reflects post-Cat-1-fix state.
- `docs/FOLLOWUP_TICKETS.md` — open defects and follow-ups (gate #10 Phase 4 tracked; `size()` vs `points().size()` `camera_program_compiler.cpp:330-335` pre-existing rot tracked).
- `docs/CHANGELOG.md` — `### camera — Camera V1 trajectory ... + §10 GOLDEN + Cat-1 build-rot fix + Camera row recovery` entries.
- `docs/baselines/main-aaf70032-baseline.md` — previous macchina-verificata (2026-07-01, 10/11 PASS — gate #10 different failure mode: `PRESET` unbound-variable script bug).
- `docs/baselines/main-21103265-baseline.md` — earlier baseline (2026-06-30, 9/11 PASS — gate #3 I5 + gate #10 CMake env).
- `docs/baselines/main-30f6c876-baseline.md` — earlier baseline (2026-06-30, 8/11 PASS).
- `reports/perf/main-<prev-sha>-gates.json` — log macchina-verificato della 11-gate run (schema `chronon3d.gates.v1`); baseline JSON for `c5793405` to be added in a follow-up forensic pass.
