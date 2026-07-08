# Chronon3D — Current Status History (pre-compression 2026-07-08)

> **Nota:** Questo file contiene la cronologia estesa rimossa da [`docs/CURRENT_STATUS.md`](../CURRENT_STATUS.md) durante la compressione del 2026-07-08 (Fix #7). Il contenuto è storico, non operativo.
> Per lo stato corrente vedi [`docs/CURRENT_STATUS.md`](../CURRENT_STATUS.md).

---

## Phase H (2026-07-07, TICKET-AE-CAM-PRECISION-COLLAPSE + TICKET-ae-cam-hash-collision Soluzione B + MultiSourceNode consistency, post-Phase-G — verification PENDING, tickets remain PARTIAL)

Soluzione B code landed on `origin/main` at commit `d39b37f1` (cumulative 27-file atomic). Includes: (a) `include/chronon3d/cache/node_cache.hpp` 3 helper methods (`mix_params_hash` + `camera_fingerprint_digest` + `fold_camera_into_params_hash`); (b) `fold_camera_into_params_hash` propagation to 7 `cache_key(ctx)` sites (`multi_source_node.cpp` + `source_node.cpp` + `TextRunNode.cpp` + 4 refresh/builder pass sites); (c) `src/render_graph/nodes/source_node.cpp` round-2 rendering-side mirror (predicted_bbox + execute + can_seed_full_frame apply 2.5D projection on global `has_camera_2_5d` trigger, NOT per-node `m_uses_2_5d_projection` flag, with native 3D shape exclusion). Forward-fix consistency: `src/render_graph/nodes/multi_source_node.cpp` 3 sites updated at commit `853ace48` to mirror the SourceNode pattern.

### Phase H.1 (2026-07-07, Soluzione B build-verification FAILED on working build host)

End-to-end verification attempted. Gate did NOT transition FAIL→PASS. Machine-verified evidence: 32/35 `chronon3d_ae_parity_tests` PASSED, 3 in-memory FB hash CHECKs FAILED at `tests/visual/ae_parity/ae_parity_tests.cpp:230/303/341` (AE_CAM_03_two_node_poi + AE_CAM_05_orbit + AE_CAM_06_dolly_zoom), 13 banned PNGs remain on disk.

## Phase G (2026-07-06, TICKET-AE-CAM-PRECISION-COLLAPSE / TICKET-ae-cam-hash-collision PARTIAL closure)

Camera 2.5D draw-matrix path switched from uniform `T(centroid)*S(perspective_scale)` composition to the resolver-published screen-space TRS. Two-file change: (a) `src/render_graph/nodes/multi_source_node.cpp` 3 sites → `proj.transform.to_mat4()`; (b) `include/chronon3d/math/camera_2_5d_projection.hpp::project_layer_2_5d()` normalizes `out.transform.scale.z = 1.0f` + `rotation = Quat(1,0,0,0)` + `anchor = Vec3(0,0,0)`. AE-parity golden re-bake: 24 PNG rigenerati; suite 35/35 PASS.

## Phase F (2026-07-06, TICKET-GOLDEN-CAPTURE empirical closure)

Capture pipeline verified working. Diagnosis originale Phase C (ZERO PNG) was a false negative of surface-level inspection (`find` doesn't enumerate files in .gitignored-friendly paths).

## Phase E (2026-07-06, AE-parity cinematic strategy)

Strategia AE-parity cinematic text-golden expansion formalizzata in ticket index. ROADMAP.md milestone "M1.6 — AE-Parity Cinematic Text Golden Expansion" 5-step workplan.

## Phase D (2026-07-05, post-`4d2e8235`)

AE-parity first 5 scene-builder implementations shipped under `tests/text_golden/ae_parity/`. 30 PNG floor target.

## Phase C (2026-07-05, post-`6f2595f6`)

Capture of 12 user-spec goldens on main DEFERRED. 12 polished scene-builder implementations ARE present in working tree.

## Phase B (2026-07-04, post-ADR-014)

Replaces 12 skeleton stubs with polished scene-builder implementations; 20 golden PNGs regenerated.

## ADR-014 (2026-07-04)

12 user-spec text golden tests added under `tests/text_golden/user_spec/`.

---

## Camera V1 — DoD (6 CAM-DOC 04 architecture-boundary checks)

Lock canonico verificato da `tools/check_camera_architecture.sh`. **6/6 PASS** su `main@3a5eb193`:

| # | Check | Stato |
|---|-------|-------|
| 1 | AnimatedCamera2_5D RETIRED | PASS |
| 2 | CameraRig authoring RETIRED | PASS |
| 3 | SceneBuilder::animated_camera() RETIRED | PASS |
| 4 | SceneBuilder::camera_rig() RETIRED | PASS |
| 5 | tan(fov) focal formulas canonical | PASS |
| 6 | compile_camera() call-site policy | PASS |

---

## AE Parity Camera Tests — FASE 3 (2026-07-05, `main@c472312a`)

| Categoria | Suite | PASS | FAIL |
|---|---|---|---|
| A. Projection & Optics | AE-CameraContract | 22 | 0 |
| B. Orbit/Dolly/Track | compiled_orbit* | 7 | 0 |
| C. Trajectory Motion | compiled_trajectory* | 5 | 1⚠️ |
| D. Depth of Field | compiled_dof* | 3 | 0 |
| E. Motion Blur | PR8:* | 12 | 0 |
| F. Near Plane Clipping | Near plane* | 10 | 0 |
| **Totale** | | **89+** | **1⚠️** |

---

## M1.5 Refactor History (2026-07-03 to 2026-07-07)

- **M1.5#1**: `TextRunNode.cpp` refactored into 5-stage orchestrator + 3 helpers in `src/render_graph/nodes/text_run/`.
- **M1.5#2**: `text_run_driver.cpp` (530 LOC) → orchestrator + 3 modules in `src/text/driver/`. New `EffectiveTextState` struct.
- **M1.5#3**: `include/chronon3d/text/text_run.hpp` (409 LOC) → umbrella header + 5 sub-headers.
- **M1.5#4**: `src/text/text_run.cpp` (363 LOC) → 3 sub-cpp: `text_run_hash.cpp`, `text_layout_cache.cpp`, `text_layout_identity.cpp`.
- **M1.5#5**: `src/text/text_run_builder.cpp` (830 LOC) → 1 slim orchestrator + 4 sub-cpp under `src/text/compiler/`.
- **M1.5#6**: `compile_text_layout` — 6 helper functions extracted. Pipeline riorganizzata in 7 stage.
- **M1.5#7**: OOM `cc1plus` su `stb_image.h` single-header risolto (3-layer fix). First wave TICKET-011 build rot.
- **M1.5#8**: `text_resolver.cpp` (391 LOC) → 6 sub-cpp under `src/text/resolver/`. Single `FontResolver::resolve()` service.
- **M1.5#9** (1/5–3/5): Migrate SDK consumer, refactor `RenderNodeFactory::text()`, drop `create_text_processor()` registration.
- **M1.5#10** (1/4–4/4): DELETE `rich_text.hpp` legacy polyfill pipeline — all 4 steps DONE.
- **M1.5#11**: `text_rasterizer_render.cpp` classification + extraction into `blend2d_glyph_conversion` + `freetype_outline_conversion`.
- **M1.5#12** (1/4): `software_backend_factory.cpp` extracted; UNIQUE validation source invariant restored.
- **M1.5#13** (1/4): `text_preset_factories_basic.cpp` extracted; `builtin_text_presets()` span accessor added.

---

## M1.7 Sequence + Asset Readiness (2026-07-07)

Steps 1-7 code-complete (commits `de10920c`..`6264614b`):
- Step 1: SequenceNode tree + TimelineResolver
- Step 2: SequenceBuilder facade + nested support
- Step 3: Animation context wiring + bbox collector local_frame fix
- Step 4: AssetRef + AssetManifest
- Step 5: AssetPreflightResolver
- Step 6: CLI integration
- Step 7: Integration tests + doc updates
- Migration Step 3: 5 new SequenceBuilder-only compositions
- Migration Step 4: grep-audit gate = 0/10 PASS

---

## Text Cleanup Sweep (items 7–11, 2026-07-07)

- **ITEM 7**: TextRunPlacement resolver in source pass (`f89662bb`)
- **ITEM 8**: TextLayoutSpec field audit — stale comments corrected (`7ed31a43`)
- **ITEM 9**: `expand_per_glyph_bbox()` duplicate eliminated (via TICKET-TEXT-CLEANUP-2)
- **ITEM 10**: 9 hardcoded TICKET104 diagnostic log sites removed (`27efc27b`)
- **ITEM 11**: `build-fast.sh` content/dashboard commands (`179d0e9f`)

---

## TICKET-011 (iii) — partial burn-down (2026-07-05)

Three target test files investigated. Sub-tickets (i) and (ii) opened for remaining work.

---

## Camera C1–C7 (2026-07-04)

Projection contract closed on `main@eb1ce8e5`. Golden test 6-mode in `tests/scene/camera/golden_projection_test.cpp`.

## Camera C9a (2026-07-04, `37c03c11`)

Runtime certifier — 1 TEST_CASE × 6 SUBCASEs, 71/71 assertion PASS.

---

## Fase A — P0 chiusi (2026-07-03)

A1 (symlink legacy + gate standalone compile), A2 (backend construction unificata), A3 (sdk::RenderEngine canonico), A4+A5 (error propagation), A6 (clone-before-mutate).

## Fase B2+B3 — Globali ELIMINATI (2026-07-03)

`process_wide_assets_root()`, `process_wide_resolver()`, `set_process_wide_assets_root()` rimossi. `shared_text_layout_cache()` / `reset_shared_text_layout_cache()` rimossi.

## Fase C2 — Factory unificata (2026-07-03)

`RenderRuntime::create(RuntimeConfig)` → `Result<RenderRuntime, RuntimeBuildError>`.

---

## Gate Audit Snapshots (pre-`7eb5c2ba`, historical only)

### `main@2b104072` (2026-07-06, post gate-10 fix — 9/11 PASS)

| # | Gate | Esito | Note |
|---|------|-------|------|
| 1 | `check_architecture_boundaries.sh` | ✅ PASS | |
| 2 | `check_architecture_boundaries_selftest.sh` | ✅ PASS | |
| 3 | `check_software_renderer_boundary.sh` | ✅ PASS | |
| 4 | `check_gitignored_dirs.sh` | ❌ FAIL | abs-path leak in CHANGELOG.md |
| 5 | `audit_software_renderer.sh` | ✅ PASS | |
| 6 | `check_camera_architecture.sh` | ❌ FAIL | Check 3/6 animated_camera() in test files |
| 7 | `check_doc_sync.sh` | ✅ PASS | |
| 8 | `check_filename_drift.sh` | ⚠️ PASS* | |
| 9 | `test_architectural.sh` | ✅ PASS | |
| 10 | `install_consumer_test.sh` | ✅ PASS | |
| 11 | `check_backend_sanitization.py` | ✅ PASS | |

### `main@2895bd88` (2026-07-04, 11-gate atomic audit)

8 PASS + 1 FAIL (g4) + 1 PASS* (g8) + 1 NOT RUN (g10) = 9/11 attivi.

### `main@1078ab46` (2026-07-04, 11-gate audit)

10/10 PASS + 1 NOT RUN (gate #10).

### `main@876a14ac` (2026-07-03, post-Fase A + B2+B3)

9/10 PASS + 1 PASS* (warn-mode) + 1 NOT RUN (gate #10).

### `main@e8623a8a` (2026-07-03, post-Fase A — P0)

10/10 verificati, 1 NOT RUN (gate #10).

### `main@a53a8d25` (2026-07-03, audit fresco — HISTORICAL)

8/11 PASS — 3 regressioni: gate #3 (I2 LOC), gate #7 (R0 ARCHIVE), gate #10 (disk quota).

### `main@c40ba16f` (2026-07-02, post-rebase + runtime-fix — HISTORICAL)

10/11 PASS osservato.

### `main@efd841f0` (HISTORICAL, 2026-07-02)

Gate #10 ❌ FAIL (`Could not find toolchain file`).

---

## P1 Backlog (post-baseline)

| ID | Area | Stato |
|---|---|---|
| TICKET-P1-07 | Asset resolver globale | PLANNED |
| TICKET-P1-08 | Text renderer monolite | PLANNED |
| TICKET-P1-09 | RenderRuntime service locator | PLANNED |
| TICKET-P1-11 | Timeline percorsi multipli | PLANNED |

## P1 Chiusi recenti

| P1 | Area | Commit | Stato |
|----|------|--------|-------|
| P1 #10 | Frame rate hardcoded | `6df9b429` + `560750e3` | ✅ DONE |
| P1 #12 | CMake ar merge + include_private | `59b2439f` | ✅ DONE |

---

## P1 Progress Summary (2026-07-04)

- P1 #1–#5: DONE (commit `0892a224`)
- P1 #10 (Frame rate hardcoded): DONE (commit `6df9b429` + `560750e3`)
- P1 #12 (CMake fragile): DONE (commit `59b2439f`)
- P1 #7, #8, #9, #11: PLANNED (post-baseline)

---

## Prossimo passo operativo (dal pre-compression snapshot)

1. Text V1 completamento — golden test suite, preset registry, kinetic typography
2. Camera V1 completamento — runtime certification, DOF, motion blur, framing
3. V0.1 release — SDK packaging, cross-language ABI, formato `.chronon`
4. P1 backlog — TICKET-P1-07/08/09/11
5. Mantenere baseline verde — 11/11 gate su ogni commit `main`

---

_Fine della cronologia estesa. Per lo stato corrente vedi [`docs/CURRENT_STATUS.md`](../CURRENT_STATUS.md)._
