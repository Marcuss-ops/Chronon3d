# Final documentary parallax certification — 2026-10-29

## Current result

The strict-native black-frame defect is fixed for the available T16 reproduction on this RTX A4000. A Vulkan memory dependency is recorded between native Render Graph levels. The terminal DoF pass also copies its device-local result into the NVENC surface when those surfaces differ, without CPU readback. The latest rebuilt CLI completed the 150-frame strict-native NVENC export; all 150 frames decode at 1920×1080/30 fps, and `blackdetect` found no black interval. Its output is byte-identical to the earlier passing run (`SHA-256 42b4f320cdbf41699a354e516e3c5cafb550051f0c6d803c6de34c94c3bb93a9`). The fresh log and MP4 are `current-t16-150-final-verify.log` and `current-t16-150-final-verify.mp4`; earlier frame diagnostics remain in the same directory.

This resolves the reproduced black-output failure on the available plan, but it is not a full production certification. The exact issue #67 source checkout and its 240-frame plan are absent, as are the requested food product assets. I rendered the same DoF-on frame through software and strict-native Vulkan: RGB MAE is 0.388 and p99 absolute error is 3, and the visible softness/edge halo appears in both. That rules out a Vulkan-only DoF regression; it does not prove the shared DoF appearance is desirable, so DoF quality remains uncertified. Historical failing outputs below are retained as pre-fix evidence.

## Reproduction environment

- Chronon3D HEAD: `bb819884345333df3a7662d16630ec47467ab2cd` plus the recorded working-tree fixes (working tree dirty at render)
- Current Release CLI SHA-256: `30ea796714dfdfeabd3b53376391ffd29b2b9c0cd26cc814b5fd55ccedea497f`
- GPU: NVIDIA RTX A4000, 16,376 MiB VRAM; 10,543 MiB free at last check
- Build: `cmake --build build/chronon/linux-video-release --parallel 16 --target chronon3d_cli` was stopped when available RAM fell below 6 GiB; resumed with `--parallel 8` and succeeded. No OOM occurred.
- Strict-native render: 150 NVENC frames accepted and muxed (`packet_pts=0..149`); `ffprobe` decoded 150 H.264 frames; `blackdetect=d=0.001:pix_th=0.015` found no black interval. Its receipt says `execution_path=full_graph_native`, zero downgrades, zero CPU pixel readback bytes, and zero encoder staging-copy bytes.
- Fresh verification MP4 SHA-256: `42b4f320cdbf41699a354e516e3c5cafb550051f0c6d803c6de34c94c3bb93a9`.
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

The original issue #67 reproduction is separate: RTX 4060 Ti, source SHA `84c8a4498a456f3aaf6d12353b725d3824a10379`, 240 frames. Its plan and checkout are unavailable, so the passing A4000/150-frame run is the available current-source reproduction, not a retest of that exact artifact. Issue #67 therefore remains open pending the exact 240-frame plan and RTX 4060 Ti rerun.

## Checklist status

See `certification.json` for the machine-readable results. The eight-test documentary checklist is incomplete. No 240-frame food-product export, portrait variant, random-access run, or three-job stress run is certified.

Current verdict: `CORE_READY=NOT_CERTIFIED`, `NATIVE_READY=PARTIAL` (available T16 passes; exact issue #67 run unavailable), `CINEMATIC_READY=NOT_CERTIFIED`.
