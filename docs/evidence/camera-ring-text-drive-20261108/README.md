# Camera-ring text drive — 1080p certification (2026-11-08)

## Result

Six authored RenderPlan v3 clips, 1920×1080 at 30 fps, rendered on the
RTX A4000 with `--backend vulkan --gpu-hot-path-mode require_gpu_native`.
All six exit 0, `effective_backend=vulkan`, and `software_fallback_nodes=0`.
The camera-ring pass is the native `camera_ring.comp` kernel:

| clip | frames | GPU blur total | avg/frame | fallback |
|---|---:|---:|---:|---:|
| `camera_ring_text_01_calories` | 300 | 625.133 ms | 2.08 ms | 0 |
| `camera_ring_text_02_sharp_center` | 240 | 597.999 ms | 2.49 ms | 0 |
| `camera_ring_text_03_every_detail` | 180 | 473.124 ms | 2.63 ms | 0 |
| `camera_ring_text_04_soft_amber` | 240 | 763.575 ms | 3.18 ms | 0 |
| `camera_ring_text_05_high_chroma` | 180 | 380.792 ms | 2.12 ms | 0 |
| `camera_ring_text_06_monochrome` | 210 | 536.345 ms | 2.55 ms | 0 |

These clips use `samples: 64`. They are **not** the 16-sample Petzval fixture:
the **0.428 ms < 0.45 ms** budget in
[`../camera-ring-petzval-20261107/README.md`](../camera-ring-petzval-20261107/README.md)
is measured on that fixture. The 64-sample clip kernel still runs on the GPU
with zero fallback, at 2–3 ms per 1080p frame.

## Two plan sets, and why

**`*.plan.json` — authored full-materiality plans.** Camera ring, peripheral
chromatic dispersion, vignette, `light.sdf_bevel` (bevel + rim + contact
shadow), `generate.noise` film grain, bezier scale and tracking animation.
`audit_full_materiality.json` records `1_velocity_profile: PASS` and
`6_materiality_static: PASS` for all six plans. Its top-level `overall` is
`FAIL` only because the optional 4K/720p raw inputs (5a/5b) are not supplied
and are reported `NOT_RUN`; no clip-level check failed.

**`gpu_native/*.plan.json` — strict-GPU render variants.** Same animation,
camera ring, chromatic dispersion and vignette, but `light.sdf_bevel` and
`generate.noise` removed. Those two effects have no Vulkan processor: under
strict native they force the whole `EffectStackNode` into software fallback,
measured at ~14–17 s/frame at 1080p (11 frames in 193 s). The GPU variants
render a whole clip in 3–4 s and keep zero fallback. Their
`gpu_native/audit_gpu_native.json` records `1_velocity_profile: PASS` and
`6_materiality_static: FAIL` for all six **by construction**, because the two
software-only effects are absent. This is a documented scope split, not a
hidden regression.

If visual bevel/grain must appear inside a GPU-native clip, the next step is a
native Vulkan SDF-bevel/noise implementation, not a software fallback.

## Reproduction

Full-materiality plans (software fallback allowed, for plan/audit work):

```bash
python3 tools/broadcast_six_diagnostics.py \
  --audit-dir docs/evidence/camera-ring-text-drive-20261108 \
  --output /tmp/camera_ring_audit_full.json
```

GPU-native clips:

```bash
chronon3d_cli render \
  --plan docs/evidence/camera-ring-text-drive-20261108/gpu_native/camera_ring_text_01_calories.plan.json \
  --assets-root "$PWD" --backend vulkan --gpu-hot-path-mode require_gpu_native \
  --start-frame 0 --end-frame 299 \
  -o docs/evidence/camera-ring-text-drive-20261108/gpu_native/camera_ring_text_01_calories_gpu_native_1080p.mp4
```

## Artifacts

Tracked: the six authored plans, the six `gpu_native` plans, both audit JSONs,
and this README. The `.mp4` files and `*.timing.json` sidecars are retained
locally but ignored by `.gitignore` (`docs/evidence/**/*.mp4`, `*.timing.json`).
