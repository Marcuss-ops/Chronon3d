# Alpha-edge acceptance — frozen contract, PASS (2026-11-03)

**Date:** 2026-11-03
**Gate:** `TICKET-CAMERA-DEFINITIVE-CLOSURE` §8.3 bullets 2–3 (semi-transparent alpha
at the foreground edge, no spurious pixels/halo/colour contamination, and the DoF
control that must not introduce ghosting or bleeding on those edges).
**Verdict:** `PASS` — every preregistered gate passes on the frozen contract, the DoF
control is observable on all three sentinels, and Vulkan validation reports zero
errors. This is local dirty-tree evidence, not a same-SHA certification.

## Correction to the first run (same day)

The first run reported `BLOCKED_DOF_INERT`. That verdict was wrong: the defect was in
the probe generator, not in the renderer.

`make_probe_plans.py` set `camera.dof.enabled=false` only on the top-level `camera`
block. The frozen plan also carries 1350 `camera_samples`, and the runtime evaluates
`Camera2_5D` from those samples
(`src/render_plan/render_plan_compiler_animation_utils.cpp` → `make_camera_evaluator`),
where `dof.enabled` stayed `true`. Both "DoF off" probes were therefore DoF-on
renders, byte-identical to the DoF-on readback. The extreme diagnostic had the same
problem: only the top-level camera changed, so it was normal DoF as well.

The generator now writes the DoF switch to `camera` **and to every camera sample**.
`diagnostics-dof-probe-mismatch.json` records the proof and the code path. No
production renderer code was changed.

## What was built

| Artifact | Role |
|---|---|
| `measurement-contract.json` | preregistered band definition, metrics and frozen thresholds, hashed into the result. Written before any measurement. |
| `make_probe_plans.py` | derives `probe-dof-off.plan.json`, `probe-dof-off-alpha-off.plan.json` and the `diagnostic-dof-extreme.plan.json` lane diagnostic from the frozen acceptance plan. DoF is written to `camera` and all `camera_samples`; only layer `4` opacity also changes. |
| `measure_alpha_edge.py` | produces `alpha-edge-result.json`. Keeps the guard that aborts to `BLOCKED_DOF_INERT` when the DoF-on readback equals the DoF-off one. |
| `diagnostics-dof-probe-mismatch.json` | the corrected diagnosis: top-level-only DoF edit vs camera-sample authority, with the hashes before/after. |
| `probe-*.raw.rgba` | 1920×1080 RGBA8 Vulkan readbacks, frames 30/75/149, zero validation errors. |

## Why a matte instead of a projection

The edge band is derived from the engine itself: `M = |A − B|` where `A` is the full
scene rendered with DoF off and `B` is the same plan with the alpha layer at opacity
0. The band of the matte is therefore the projected edge of the foreground element
**as the renderer places it**, with no re-implementation of the camera model in
Python. The gates then compare the DoF-on frame against the sharp one inside that
band (halo = out-of-range value inside a 28 px neighbourhood, i.e. wider than
`camera.dof.max_blur`; contamination = a channel pushed up while another is pulled
down).

## What was measured

The matte is real and the fixture is suitable: ~448k–460k support pixels per frame,
25k–39k band pixels, 27k–32k of them with partial coverage, and the element moves
246.55 px between frame 30 and 149 with a stable area (ratio 1.0049).

With the corrected probes, `A` and the DoF-on frame `C` differ on every sentinel:

| Frame | DoF mean / max abs delta | Band px | Fractional-coverage px | G1 | G2 violations / max overshoot | G3 contaminated (ratio) |
|---|---|---|---|---|---|---|
| 30 | 3.1937 / 81 | 39,192 | 32,251 | PASS | 0 / 3 | 0 (0.0000) |
| 75 | 3.8717 / 87 | 37,648 | 28,527 | PASS | 0 / 0 | 0 (0.0000) |
| 149 | 4.0901 / 87 | 35,283 | 27,505 | PASS | 0 / 0 | 78 (0.00221) |

- G1 threshold: `>= 100 px` of fractional coverage — PASS on all frames.
- G2 threshold: `violating_ratio <= 0.002` and `max_overshoot <= 8` — PASS on all frames.
- G3 threshold: `contaminated_ratio <= 0.005` — PASS on all frames (worst 0.00221).
- G4 parallax and placement: displacement `246.5549 px`, area ratio `1.0049`,
  support inside the canvas — PASS.
- Vulkan validation errors total: `0`.

Result: `alpha-edge-result.json` → `aggregate_gate: PASS`,
`dof_observable_on_every_frame: true`.

## Reproduction

Binary `build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli`,
SHA-256 `db3425b8d6e4…` (built 2026-11-03 13:04 from a dirty tree), RTX A4000,
validation layers on:

```bash
python3 docs/evidence/camera-alpha-edge-20261103/make_probe_plans.py
for f in 30 75 149; do
  # C: DoF on, frozen acceptance plan
  CHRONON3D_VULKAN_VALIDATION=1 build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli \
    render --plan docs/evidence/camera-closure-final/optical_current_pair_v3.plan.json \
    --assets-root "$PWD" --backend vulkan --start-frame $f --end-frame $f \
    --video-sink raw --pipe-pixfmt rgba --encoder-backend pipe \
    -o docs/evidence/camera-alpha-edge-20261103/probe-dof-on-$(printf %03d $f).raw.rgba \
    > docs/evidence/camera-alpha-edge-20261103/probe-dof-on-$(printf %03d $f).log 2>&1
  for p in probe-dof-off probe-dof-off-alpha-off; do
    CHRONON3D_VULKAN_VALIDATION=1 build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli \
      render --plan docs/evidence/camera-alpha-edge-20261103/$p.plan.json \
      --assets-root "$PWD" --backend vulkan --start-frame $f --end-frame $f \
      --video-sink raw --pipe-pixfmt rgba --encoder-backend pipe \
      -o docs/evidence/camera-alpha-edge-20261103/$p-$(printf %03d $f).raw.rgba \
      > docs/evidence/camera-alpha-edge-20261103/$p-$(printf %03d $f).log 2>&1
  done
done
python3 docs/evidence/camera-alpha-edge-20261103/measure_alpha_edge.py   # exit 0: PASS
```

## What this does NOT certify

- Not same-SHA: the trees are dirty (Chronon3d `b38f7e8f9`, 29 modified files;
  ChrononMotion3D `83af9628`, 3 modified files) and the binary comes from that dirty
  tree. `alpha-edge-result.json` records the head, the dirty flags and the combined
  `git diff` digest next to the numbers.
- Not the 150-frame acceptance rerun (C6) on the current pair.
- Not the human whole-video review (C7), and not CI.

## Next action

Re-run the 150-frame C6 acceptance on the current pair with the same binary and record
it in the ticket's §8.4 table. C7 remains external: a nominated reviewer and a green
same-SHA CI run. The alpha-edge gate itself needs no further work.
