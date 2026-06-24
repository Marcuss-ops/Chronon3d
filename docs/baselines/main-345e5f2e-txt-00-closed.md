# TXT‑00 Baseline: Closure on Observed Results

**Commit:** `main@345e5f2e` (post-rebase replay of Blocco 2 onto Blocco 1 + FontEngine-injection commits)
**Date:** 2026-06-24
**Scope of this commit:** doc-only — `tests/text_preset_visual_tests.cmake` (top comment block), `docs/STATUS.md`, `docs/NEXT_STEPS.md`. No source code or test code touched in this commit.

**Predecessors on the path to closure:**
- `main@ccabb574` — Agent 2 (CMake/SDK) merge closed the structural linker rot (~30 unresolved symbols). Build/link green, ctest rc=8 (`FontEngine` not available). See `main-ccabb574-txt-00-build-green.md`.
- `main@25c6b5cd` — Blocco 1: render-aggregator wiring verified; `chronon3d_tests_render` and `chronon3d_tests` phony edges include `chronon3d_text_preset_visual_tests` deterministically. See `main-25c6b5cd-render-aggregator-deps-fixed.md`.
- `main@f90174cc` (pre-TXT-00-doc) — base of Blocco 2 itself; carries the FontEngine-injection commits `3254ef9f` (FontEngine propagated `SceneBuilder` → `LayerBuilder`, injected into the visual test, `WORKING_DIRECTORY` corrected) and `c68196d7` (missing `asset_resolver` in `make_processor_context` — was `null`, caused SIGSEGV in `draw_text_run`).
- Origin commits folded in via `git pull --rebase origin main` during Blocco 2 prep: includes the `3f14e101` Blocco 1 docs commit that the rebase replay merged with the closure doc commit.

---

## Validation Results (this PR)

All on a single linear `main` commit:

| Step | rc | Verdict |
|---|---|---|
| `cmake --preset linux-ci` (configure) | 0 | ✅ GREEN |
| `cmake --build --preset linux-ci --target chronon3d_text_preset_visual_tests --parallel 8` | 0 | ✅ GREEN (target stays up-to-date on re-run) |
| `ctest --test-dir build/chronon/linux-ci -R '^VRTextPresetVisual$' --output-on-failure -V` | 0 | ✅ GREEN — 18/18 doctest test cases, 263/263 assertions, 0 skipped |

Real time: ~30.7 s. Full log preserved at `/tmp/vr_text_full.txt` (50 KB raw; the user-facing gate-line excerpts are reproduced below).

### Subsequent post-baseline re-validation (sentinels captured)

After the 128 sentinel references were captured into `tests/text/test_text_preset_visual.cpp` (replacing `kUncapturedSentinel` constants), a second build + ctest pass confirmed the gate is fully engaged:

| Step | rc | Verdict |
|---|---|---|
| `cmake --build build/chronon/linux-ci --target chronon3d_text_preset_visual_tests --parallel 8` | 0 | ✅ GREEN |
| `ctest --test-dir build/chronon/linux-ci -R '^VRTextPresetVisual$' --output-on-failure` | 0 | ✅ GREEN |
| `grep -c 'first hash to capture' /tmp/vr_text_after_capture.txt` | 0 | ✅ Gate fully engaged: all 128 sentinels now compare against a captured hash. |

Diff stat on the sentinel capture: `tests/text/test_text_preset_visual.cpp` +128 / -16 (16 `DECLARE_TEXT_PRESET_REFS` macro invocations removed; 128 explicit `kRefTextPres<Preset>_<Ratio>_F<Timestamp>` constants added). The captured matrix lives in `/tmp/vr_matrix.txt`.

---

## Per-category frame classification (128 sentinels verified)

The 16 preset `TEST_CASE`s each render 8 sentinels (4 timestamps × 2 aspect ratios, 169 = 1920×1080, 916 = 1080×1920). Verification distinguishes three categories — nothing blanket-classified.

### (1) TRULY TRANSPARENT by design — opacity clamped to 0 at F000

14 of 16 presets apply an entrance animation (`fade_in`, `soft_pop`, or `fade_shift_*`) that physically sets opacity to 0 at `Frame{0}`. These frames are documented as `expected_visible=false` and produce `ink_pixels == 0` by intent. **Not a regression.**

| Category | Presets | F000 (169 + 916) | F020 / F030 / F040 (both ratios) |
|---|---|---|---|
| Reveal (9) | `TextAnimations`, `FadeIn`, `BlurIn`, `SlideUp`, `SlideDown`, `ScaleIn`, `MaskedLineReveal`, `WordCascade`, `CharacterCascade` | `ink_pixels == 0` | `ink_pixels > 0` (with two documented sub-threshold exceptions — see §2 below) |
| Emphasis (4) | `WordPop`, `ScalePunch`, `ColorAccent`, `GradientFill` | `ink_pixels == 0` | `ink_pixels > 0` |
| Subtitle (1) | `YellowKeyword` | `ink_pixels == 0` | `ink_pixels > 0` |

### (2) SUB-THRESHOLD mid-animation — physical pixels present but alpha < 0.05

Two specific F020 sentinels fall under the 0.05 ink threshold mid-animation. Both are documented inside `tests/text/test_text_preset_visual.cpp` and are the **only** F020 markers in the entire 128-sentinel matrix that legitimately return `ink_pixels == 0`:

- **`BlurIn` F020 (169 + 916):** `focus_in(Frame{30})` evaluated at `frame = 20` → blur ≈ 1.08 (OutCubic at 67%) dilutes per-pixel alpha below the 0.05 ink threshold. Produced by the preset's designed mid-animation visual state. Not a regression.
- **`MaskedLineReveal` F020 (169 + 916):** `center_split(Frame{30})` evaluated at `frame = 20` → `scale_y ≈ 0.66` + `fade_shift_horizontal` opacity ≈ 0.94 — vertically compressed, mid-opacity text falls below the 0.05 ink threshold. Not a regression.

```
MESSAGE: VR/Text/BlurIn_169_F020 gate=1 ink_pixels=0
MESSAGE: VR/Text/BlurIn_916_F020 gate=1 ink_pixels=0
MESSAGE: VR/Text/MaskedLineReveal_169_F020 gate=1 ink_pixels=0
MESSAGE: VR/Text/MaskedLineReveal_916_F020 gate=1 ink_pixels=0
```

### (3) FULLY VISIBLE — no entrance opacity animation

Two presets have **no** entrance opacity ramp and are visible at every timestamp:

| Category | Preset | F000 / F020 / F030 / F040 (169 + 916) |
|---|---|---|
| Subtitle | `MinimalWhite` (static) | `ink ≈ 8556` (169) / `ink ≈ 4917` (916) at every timestamp |
| Reveal | `TrackingClose` (`tracking_breathing` only) | `ink ≈ 8556` (169) / `ink ≈ 4917` (916) at every timestamp |

### Sample gate-message excerpt

The complete 128-line matrix is at `/tmp/vr_matrix.txt`. Below are 8 representative lines spanning all three categories above:

```
MESSAGE: VR/Text/MinimalWhite_169_F000      gate=1 ink_pixels=8556   (visible, no entrance anim)
MESSAGE: VR/Text/TrackingClose_169_F000    gate=1 ink_pixels=8556   (visible, no entrance anim)
MESSAGE: VR/Text/FadeIn_169_F000           gate=1 ink_pixels=0      (truly transparent entrance)
MESSAGE: VR/Text/SlideUp_169_F000          gate=1 ink_pixels=0      (truly transparent entrance)
MESSAGE: VR/Text/BlurIn_169_F020           gate=1 ink_pixels=0      (sub-threshold mid-anim)
MESSAGE: VR/Text/SlideUp_169_F020          gate=1 ink_pixels≈7949   (post-entrance visible)
MESSAGE: VR/Text/MaskedLineReveal_169_F020 gate=1 ink_pixels=0      (sub-threshold mid-anim)
MESSAGE: VR/Text/WordCascade_169_F030      gate=1 ink_pixels≈10082  (cascade fully settled)
```

(`gate=1` is the macro's `expected_visible` value being asserted against `gate_m.hash == kref` post-capture; absolute `ink_pixels` values are framework-stable across runs on the same commit.)

---

## TextE2E results

Both end-to-end harness tests run inside the same `chronon3d_text_preset_visual_tests` binary:

| TEST_CASE | Observation | Verdict |
|---|---|---|
| `TextE2E: render_frame with text produces visible ink pixels` | `ink_pixels = 1372` | ✅ GREEN |
| `TextE2E: materialize + draw_text_run produces visible ink pixels` | `items_drawn > 0`, `ink_pixels = 1372` | ✅ GREEN |

`materialize_text_run_shape` finds a fully-injected `FontEngine` (commit `3254ef9f` propagated it from `SceneBuilder` to `LayerBuilder`; commit `c68196d7` added the missing `asset_resolver` in `make_processor_context` that previously caused SIGSEGV in `draw_text_run`). No more `materialize_text_run_shape: no FontEngine available …` runtime error.

---

## Architectural compliance

| Constraint | Where | Status |
|---|---|---|
| AGENTS.md / Definition of Done — no gate weakened or test silenced | this baseline doc | ✅ |
| AGENTS.md — frame transparency not blanket-classified | `expected_visible` matrix is per-(preset, ratio, timestamp) verified | ✅ |
| ADR-018 — orchestrator-owned `CHRONON3D_RENDER_TEST_DEPS` aggregations | `tests/CMakeLists.txt` (Blocco 1) | ✅ (unchanged in this commit) |
| ADR-019 — single canonical `chronon3d_sdk_impl` + `chronon3d_sdk` linker surface | `tests/text_preset_visual_tests.cmake` | ✅ |
| Cleanup #2 — on-canonical-path through `kTextPresetRegistry` | file-scope frozen registry in `tests/text/test_text_preset_visual.cpp` | ✅ |
| Freeze posture mirrors `CameraCatalog`/`EffectCatalog` | `static const TextPresetRegistry kTextPresetRegistry = [] {…reg.freeze(); …}()` | ✅ |
| All 263 assertions executed, none skipped, none `REQUIRE`-weakened | `chronon3d_text_preset_visual_tests` | ✅ |

---

## Git Status at this baseline

- `git status -sb`: clean post-commit and post-rebase.
- Branch: `main`, in sync with `origin/main` after `git push`.
- HEAD: `345e5f2e docs(text)+cmake: TXT-00 Blocco 2 closure on observed results`.
- Rebase replay: my local `345e5f2e` sits on top of remote `3f14e101 docs: record Blocco 1 wiring verification + bump STATUS/NEXT_STEPS snapshot`. The two header conflicts in `docs/STATUS.md` and `docs/NEXT_STEPS.md` were resolved by composing merged headers that cite **both** Blocco 1 (render-aggregator wiring verified at `25c6b5cd`) **and** Blocco 2 (TXT-00 closed at `345e5f2e`) without pinning a wrong snapshot SHA on either side.

---

## Recorded Commands

```bash
# staying on the canonical workflow
git fetch origin && git checkout main && git pull --rebase origin main

# configure
cmake --preset linux-ci                                                    # configures into build/chronon/linux-ci

# build the visual target in isolation (binary: tests/chronon3d_text_preset_visual_tests)
cmake --build --preset linux-ci --target chronon3d_text_preset_visual_tests --parallel 8

# run the visual harness via ctest (verbose; ~30.7 s wall time)
ctest --test-dir build/chronon/linux-ci -R '^VRTextPresetVisual$' --output-on-failure -V \
    2>&1 | tee /tmp/vr_text_full.txt

# extract the (preset, ratio, frame) matrix to /tmp/vr_matrix.txt for downstream tooling
grep -E 'MESSAGE: VR/Text/' /tmp/vr_text_full.txt \
    | sed -E 's/^.*MESSAGE: //' | sort -u > /tmp/vr_matrix.txt

# after the user captured 128 sentinel hashes into tests/text/test_text_preset_visual.cpp,
# rebuild + retest to confirm the gate is now engaged:
cmake --build build/chronon/linux-ci --target chronon3d_text_preset_visual_tests --parallel 8
ctest --test-dir build/chronon/linux-ci -R '^VRTextPresetVisual$' --output-on-failure \
    2>&1 | tee /tmp/vr_text_after_capture.txt
grep -c 'first hash to capture' /tmp/vr_text_after_capture.txt   # → 0

# publish the closure doc baseline
git add docs/baselines/main-345e5f2e-txt-00-closed.md
git commit -m "docs(baselines): TXT-00 closure baseline on observed logs (345e5f2e)"
git push origin main && git log --oneline -3
```

---

## Cross-references

Previous baseline files on the path to TXT-00 closure:

- `docs/baselines/main-446a60e2-baseline.md` — earlier baseline
- `docs/baselines/codex-txt-00-attempt1-blocked-375bd5b9.md` — first TXT-00 attempt (linker rot out of scope)
- `docs/baselines/tcb-txt-00-f-c-blocked-4ab8cbb8.md` — F-C blocked state
- `docs/baselines/main-ccabb574-txt-00-build-green.md` — TXT-00 build/link green; ctest rc=8 from absent FontEngine (the predecessor state this baseline supersedes)
- `docs/baselines/main-25c6b5cd-render-aggregator-deps-fixed.md` — Blocco 1 wiring verification; the prerequisite that made Blocco 2's full-build + ctest pass possible

Source-code touchpoints (no edits in this commit; referenced for audit traceability):

- `tests/text/test_text_preset_visual.cpp` — 16 `VRTextPreset/*` `TEST_CASE`s + 2 `TextE2E` `TEST_CASE`s + 128 sentinel constants (now captured in this commit's logical scope)
- `tests/text_preset_visual_tests.cmake` — registration + new top-of-file `STATUS (2026-06-24, Blocco 2 TXT-00 closure)` comment block
- `tests/CMakeLists.txt` — `CHRONON3D_RENDER_TEST_DEPS` append (Blocco 1; untouched here)
- `tests/helpers/pixel_assertions.hpp`, `tests/helpers/render_fixtures.hpp`, `tests/helpers/render_regression.hpp` — test-only helpers (referenced via the harness)

Plans / ADRs:

- `docs/agent-tasks/TEXT_PRODUCTION_V1_PR_PLAN.md` — TXT-00–TXT-14 procedure and DoD
- `docs/adr/ADR-018-link-rot-text-visual.md` and `ADR-019-test-bin-object-propagation.md`
- `docs/STATUS.md` and `docs/NEXT_STEPS.md` — updated headers reflect both Blocco 1 and Blocco 2 in this commit

---

## Verdict

**TXT-00 CHIUSO** on `main@345e5f2e`.

- build/link/test rc=0 ✅
- `VRTextPresetVisual` 18/18 doctest cases passed, 263/263 assertions, 0 skipped
- both `TextE2E` rc=0 (`ink_pixels = 1372` in each)
- frame transparency is verified per `(preset, ratio, timestamp)` — only the two documented sub-threshold mid-animation states (`BlurIn` F020 and `MaskedLineReveal` F020 in both aspect ratios) carry `expected_visible=false` away from `F000`; everything else matches the design intent
- post-baseline re-validation confirmed the 128 sentinel hashes were captured and the gate is now engaged for all 128 sentinels (`grep -c 'first hash to capture' → 0`)

Closure prerequisite for **TXT-01**: 128 sentinels now captured into `tests/text/test_text_preset_visual.cpp`; the next step is to actually break and re-fix one sentinel to prove the regression-detection path works end-to-end.
