# Final documentary parallax certification — 2026-10-29

## Current result

The strict-native black-frame defect is fixed for the available T16 reproduction on this RTX A4000. A Vulkan memory dependency is now recorded between native Render Graph levels. With no snapshot diagnostics enabled, the three-layer strict-native MP4 decodes to a visible background, title, and foreground subject; DoF-off and DoF-on frame checks also decode visibly. The available 150-frame T16 strict-native NVENC export completed, all 150 frames decode at 1920×1080/30 fps, and `blackdetect` found no black interval. Evidence files are `current-three-layers-barrier-build.mp4`, `current-dof-off-barrier-build.mp4`, `current-dof-on-barrier-build.mp4`, and `current-t16-150-barrier-build.mp4`.

This resolves the reproduced black-output failure on the available plan, but it is not a full production certification. The exact issue #67 source checkout and its 240-frame plan are absent, as are the requested food product assets. I rendered the same DoF-on frame through software and strict-native Vulkan: RGB MAE is 0.388 and p99 absolute error is 3, and the visible softness/edge halo appears in both. That rules out a Vulkan-only DoF regression; it does not prove the shared DoF appearance is desirable, so DoF quality remains uncertified. Historical failing outputs below are retained as pre-fix evidence.

## Reproduction environment

- Chronon3D HEAD: `556a4ae58933d0c6b72eb22829b415e08ffc43d4` (working tree dirty)
- Current Release CLI SHA-256: `9ba3c94ed246fd57ff39b61279267b5cb46411c867921fa7cf076a4486988ba6`
- GPU: NVIDIA RTX A4000, 16,376 MiB VRAM; 10,543 MiB free at last check
- Build: `cmake --build build/chronon/linux-video-release --target chronon3d_cli -j12` succeeded
- Host memory at last check: 24 GiB available; no OOM observed
- Main scene: `docs/evidence/camera-authority-migration/T16/dof-ablation/t16_three_depth_dof_on.plan.json`
- Main scene SHA-256: `d3a10211509578d4dbaca96fc33e3d9fb6de266458e405c3ccc2242abcb05fc1`

## Historical pre-fix failures

The following outputs document the failure before the per-level memory dependency was added. They are preserved for comparison and are not the current result.

DoF-off command:

```bash
build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli render \
  --backend vulkan --gpu-hot-path-mode require_gpu_native \
  --encoder-backend native --video-sink ffmpeg --pipe-pixfmt nv12 \
  --plan docs/evidence/final-documentary-parallax-20261029/current-dof-off-frame0.plan.json \
  --assets-root "$PWD" --start-frame 0 --end-frame 0 \
  -o docs/evidence/final-documentary-parallax-20261029/current-dof-off-finalcandidate.mp4
```

The encoder accepted one frame and the MP4 decoded, but the decoded frame was entirely black. SHA-256: `b2ce3065a331471e7ba489e9a18aec4758085f9f6edd06d8138374c5a12b42de`.

DoF-on used the T16 plan above with the same strict-native arguments and frame range. The encoder accepted one frame and the MP4 decoded, but only the foreground subject was partially visible. SHA-256: `97a6f48e5586a2ef6565887521f8c1b3209da87b12e5d60c4622f3f56ae65274`.

Additional comparisons are saved as `current-background-only-frame0.*`, `current-background-title-frame0.*`, `current-three-layers-frame0.*`, `current-three-layers-2d-foreground.*`, `current-three-layers-identity*.log`, and corresponding plans/logs. These are diagnostics, not acceptance artifacts.

The original issue #67 reproduction is separate: RTX 4060 Ti, source SHA `84c8a4498a456f3aaf6d12353b725d3824a10379`, 240 frames. Its plan and checkout were unavailable, so the passing A4000/150-frame run is the available current-source reproduction, not a retest of that exact artifact.

## Checklist status

See `certification.json` for the machine-readable results. The eight-test documentary checklist is incomplete. No 240-frame food-product export, portrait variant, random-access run, or three-job stress run is certified.

Current verdict: `CORE_READY=NOT_CERTIFIED`, `NATIVE_READY=PARTIAL` (available T16 passes; exact issue #67 run unavailable), `CINEMATIC_READY=NOT_CERTIFIED`.
