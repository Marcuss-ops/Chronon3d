# Camera-ring text drive — 1080p certification (2026-11-08)

## Result

Six authored RenderPlan v3 clips, 1920×1080 at 30 fps, rendered on the
RTX A4000 with `--backend vulkan --gpu-hot-path-mode require_gpu_native`.
All six exit 0. Under `require_gpu_native` a missing native route fails the
graph, so these renders are the zero-fallback gate: no `EffectStackNode` fell
back to software.

The stack is native end to end: `camera_ring.comp`, the new Vulkan
`sdf_bevel.comp` (exact separable signed distance field, dynamic
bevel/rim/specular shading and projected shadow), and the existing native
`generate.noise` kernel. The older statement that bevel and grain were
software-only is obsolete.

| clip | frames | render ms | mean ms/frame | p95 ms/frame | wall ms |
|---|---:|---:|---:|---:|---:|
| `camera_ring_text_01_calories` | 300 | 2915.75 | 9.71 | 14.65 | 4508.43 |
| `camera_ring_text_02_sharp_center` | 240 | 2406.99 | 10.02 | 15.73 | 3990.45 |
| `camera_ring_text_03_every_detail` | 180 | 1867.35 | 10.37 | 31.12 | 3127.11 |
| `camera_ring_text_04_soft_amber` | 240 | 2568.03 | 10.70 | 31.61 | 4004.04 |
| `camera_ring_text_05_high_chroma` | 180 | 1780.30 | 9.88 | 28.48 | 3054.11 |
| `camera_ring_text_06_monochrome` | 210 | 2200.12 | 10.47 | 30.86 | 3523.39 |

These are total frame-loop render times from the per-clip timing sidecars, not
just the camera-ring pass. The clips use `samples: 64`; they are not the
16-sample Petzval fixture whose 0.428 ms budget is tracked separately.

## Plan sets

Both `*.plan.json` and `gpu_native/*.plan.json` now contain the full authored
materiality stack: camera ring, peripheral chromatic dispersion, vignette,
`light.sdf_bevel` (bevel + rim + contact shadow) and `generate.noise` film
grain. The `gpu_native` copies differ only in their `output.path`; the previous
strict-GPU variant that removed bevel and grain is gone.

## Audits

- `audit_full_materiality.json` — `overall: PASS`. Both supplied sub-pixel
  checks pass (`5a_subpixel_4k`, `5b_subpixel_720`). Every clip records
  `1_velocity_profile: PASS`, `3_hierarchy_stagger: N/A`,
  `4_trim_dynamics: N/A`, `6_materiality_static: PASS`.
- `gpu_native/audit_gpu_native.json` — `overall: PASS`, same six per-clip
  results, now including bevel, contact shadow, grain, chroma and vignette.
- `6_materiality_static` is a plan-level gate. The native SDF kernel is also
  pixel-verified by the strict Vulkan parity test
  (`Strict Vulkan SdfBevel matches software shading…`): max absolute error
  `1.19e-7` with the projected shadow disabled. The six strict renders exercise
  the shadow path; the software shadow uses a different blur kernel, so the
  parity test intentionally isolates the SDF/bevel/rim shading.

The broadcast six-test canary is a separate fixture and is **not** strict-GPU:
`shape_repeater` still has no native draw route. The six clips here do not use
`shape_repeater`; see [`../broadcast-six-test-20261108/README.md`](../broadcast-six-test-20261108/README.md)
for that pre-existing limitation.

## Reproduction

Render one clip:

```bash
./build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli render \
  --plan docs/evidence/camera-ring-text-drive-20261108/gpu_native/camera_ring_text_01_calories.plan.json \
  --assets-root "$PWD" --backend vulkan --gpu-hot-path-mode require_gpu_native \
  --start-frame 0 --end-frame 299 \
  -o docs/evidence/camera-ring-text-drive-20261108/gpu_native/camera_ring_text_01_calories_gpu_native_1080p.mp4
```

Regenerate the plan audits (the 5a/5b raw frames live in
`${TMPDIR:-/tmp}/chronon-broadcast-six` and are produced by
`../broadcast-six-test-20261108/run_evidence.sh`):

```bash
python3 tools/broadcast_six_diagnostics.py \
  --audit-dir docs/evidence/camera-ring-text-drive-20261108 \
  --subpixel-4k-raw /tmp/chronon-broadcast-six/subpixel_4k.raw \
  --subpixel-4k-width 3840 --subpixel-4k-height 2160 \
  --subpixel-720-raw /tmp/chronon-broadcast-six/subpixel_720.raw \
  --subpixel-720-width 720 --subpixel-720-height 405 \
  --output docs/evidence/camera-ring-text-drive-20261108/audit_full_materiality.json
```

Use `--audit-dir .../gpu_native` and
`--output .../gpu_native/audit_gpu_native.json` for the strict-GPU copies.

## Artifacts

Tracked: the six authored plans, the six `gpu_native` plans, both audit JSONs,
and this README. The `.mp4` files and `*.timing.json` sidecars are retained
locally but ignored by `.gitignore` (`docs/evidence/**/*.mp4`, `*.timing.json`).
