# CM3D camera / DoF renders — 2026-10-30

These are **ChrononMotion3D threepp/GL renders**, not Chronon3D RenderPlan
renders. They exercise the CM3D `CameraRig`, `MotionSceneBridge`, threepp
`GLRenderer`, `EffectComposer`/`BokehPass`, GL PBO → CUDA → NVENC on the
RTX A4000. They are the direct answer to "show me the CM3D camera".

| Test | What it shows | Frames | Local output | Drive |
|---|---|---:|---|---|
| **T18** documentary crowd particle reveal | CM3D camera orbit over a 3D crowd, animated lead character, shadows, particles | 150 @ 1920×1080/30 | `ChrononMotion3D/out/threepp_certification/nvidia/T18/T18_documentary_crowd_particle_reveal_1080p30.mp4` | [open](https://drive.google.com/file/d/181KtU8VTU9HEcVACauSFaMmDOR9JAv9q/view?usp=drivesdk) |
| **T17** product orbit + rack focus | CM3D camera orbit and `CameraRig.focusDistance` track through `BokehPass` | 150 @ 1920×1080/30 | `ChrononMotion3D/out/threepp_certification/nvidia/T17/T17_product_orbit_1080p30.mp4` | [open](https://drive.google.com/file/d/1m93j4RgwOfosl0OTXUerUBL_X8Oz5Jh4/view?usp=drivesdk) |
| **T10** depth of field | CM3D `CameraRig` focus rack 6 → 7.5 → 9 units through `BokehPass` | 150 @ 1920×1080/30 | `/tmp/cm3d_camera/T10/T10_dof_1080p30.mp4` (re-rendered 2026-10-30) | [open](https://drive.google.com/file/d/1pg8bsIJ08SJaQSy1ztSbU6cDZZdptAA0/view?usp=drivesdk) |

All three are H.264, 1920×1080, 30 fps, 150 frames (4.967 s), `h264_nvenc`,
NVIDIA RTX A4000. `blackdetect` found zero black intervals in each.

| File | Bytes | SHA-256 |
|---|---:|---|
| `cm3d_camera_t18_documentary_crowd_1080p.mp4` | 4,171,373 | `a67099c185c505f2f5a801decbf23179b99d035bc5bcfa303c2763a510418f10` |
| `cm3d_camera_t17_product_orbit_1080p.mp4` | 3,175,100 | `2be47deb9e84eee9d8d3e2215955c02c46f37c0a36049edc6dfb15c7f1788dc5` |
| `cm3d_camera_t10_dof_1080p.mp4` | 1,703,394 | `998d03e8ada2c099703a090e658f77caeed7fc87a6dfb3acf5c9f2586cf2cbbb` |

Drive parent: [folder](https://drive.google.com/drive/u/1/folders/1J_xUGo_bchzXDIGqSX04CU44c_Dm3SxS)
(`1J_xUGo_bchzXDIGqSX04CU44c_Dm3SxS`). Uploaded with
`RenderingGen/renderinggen/cmd/drive-upload`; all three returned
`DRIVE_UPLOAD_PASS`.

## What each render proves

**T18** (`metadata.json`): `MotionSceneBridge (CameraRig orbit) → threepp
GLRenderer → GL PBO → CUDA → NVENC`; camera travel 9.58389 world units, 40
mannequin meshes, lead subject height 66.39% at frame 75, shadow-change pixels
27,142/55,459/117,931, particles visible at frame 45, 150/150 frames submitted
and muxed, frame 77 random-access identical.

**T17**: four Khronos GLBs with `CameraRig` orbit/focus tracks plus `BokehPass`;
focus distances 3.758/4.960/5.960/6.960; `focus_follows_depth=true`, frame 77
random-access identical, 150/150 frames. This is the camera + DoF path, not a
C3D RenderPlan.

**T10**: `CameraRig.focusDistance` track 6/7.5/9 through `BokehPass`; same-subject
ROI gradients peak at the requested focus, `focus_follows_depth=true`, frame 77
random-access identical, 150/150 frames. Reproduce with:

```bash
cd ChrononMotion3D
./build/release-fast/render/chrononmotion_render_dof_certification /tmp/cm3d_camera/T10
```

## Architecture note — two passes, not "C3D has a camera"

The CM3D renderer is good at exactly what threepp is: 3D geometry, camera,
lights/shadows, depth and DoF. Chronon3D is not a competing camera; it is the
compositor and text/SVG/2D engine. The production-quality direction is a
two-pass composite:

1. **CM3D pass:** 3D scene + `CameraRig` + lights/shadows + DoF/post →
   an RGBA plate with color-space and alpha metadata.
2. **C3D pass:** text, SVG, 2D overlays and final composition over that plate.

This is the After Effects-style split the user is asking for. It is **not** the
same as the current T16 path, which sends Motion camera samples into C3D and
lets C3D render the whole 2.5D composition, including DoF. That path is useful
for standalone RenderPlans, but it is not the layered CM3D-render + C3D-comp
architecture.

The missing production gate is the plate handoff: a GPU-native alpha/RGBA
plate contract. The existing T13 still-image alpha composition passes, but the
animated alpha path is PARTIAL (the qtrle source is CPU-readback/CPU-encoded
and strict-native C3D rejects it). Until that is closed, these CM3D renders are
standalone 3D/DoF demonstrations, not a certified CM3D-plate + C3D-overlay
master.

These are technical certification fixtures, not art direction. T18 is the most
complete scene; T17/T10 are focus/DoF proofs. The camera ticket remains
**PARTIAL** for contract, validation and CI, not because the CM3D camera is
missing.
