# T16 Vulkan DoF ablation — 2026-10-21

The existing 150-frame T16 three-depth RenderPlan was rendered at frame 75 on
Vulkan with all inputs unchanged except `camera.dof.enabled` in each of its
150 sampled camera states. The baseline frame is the existing no-DoF raw
Vulkan random-access frame 75.

| Variant | Raw result | Observation |
|---|---:|---|
| DoF disabled (baseline) | 8,294,400 RGBA bytes | Visible, `content_fraction=0.837132`. |
| DoF enabled, aperture 0.015, focus distance 1000 | 8,294,400 RGBA bytes | Exactly zero RGBA; alpha bbox is empty. |

The RGB MAE between the zero output and the visible baseline is 66.78. The
RenderPlan enables DoF with ordinary circular bokeh (no unsupported iris or
highlight shaping). This result is specific to the default raw-readback route:
it reaches the CPU-depth path, while `VulkanBackend::apply_per_pixel_dof`
requires a native depth surface. The CLI reports success and writes the
zero-filled frame. A later strict-native Vulkan/NVENC render uses the existing
device-local depth-surface path successfully; the route-specific raw failure
does not invalidate that native result.

Artifacts: `t16_three_depth_dof_on.plan.json`, raw output, timing sidecar, log,
and PNG projection. The no-DoF baseline raw frame and sidecar are in the parent
T16 evidence directory as `t16_three_depth_random_075_vulkan_auto.raw.rgba`.

Reproduce the ablated frame from `Chronon3d/` with:

```bash
build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli render \
  --backend vulkan --plan docs/evidence/camera-authority-migration/T16/dof-ablation/t16_three_depth_dof_on.plan.json \
  --assets-root . --video-sink raw --pipe-pixfmt rgba \
  --start-frame 75 --end-frame 75 -o t16_three_depth_dof_on_frame_075.rgba
```

## Strict-native GPU DoF retest (2026-10-21)

The same DoF-on plan also renders successfully with
`--gpu-hot-path-mode require_gpu_native --encoder-backend native` through the
native Vulkan depth-surface and DoF compute-shader path. Its one-frame matched
NVENC A/B against DoF off submits one frame per variant, reports zero GPU
readback bytes and zero encoder staging-copy bytes, and visually shows the
out-of-focus foreground subject soften while the title remains sharp.

The full 150-frame strict-native DoF-on video is
`t16_three_depth_dof_on_strict_gpu_150f.mp4`; it decodes to 1920×1080, 30 fps,
150 frames. Its sidecar reports `required_gpu_native_full_graph`, 150 native
encode frames, zero GPU readback bytes, zero encoder staging-copy bytes, and
zero execution downgrades. Reviewed frames 0, 30, 75, 120, and 149 are in
`t16_dof_on_strict_gpu_contact.webp`; the matched DoF-off/on comparison is
`t16_dof_strict_gpu_ab_contact.webp`.

Over those five decoded frames, mean face ROI edge gradient falls by 19.95%
with DoF on, while the title ROI retains 98.27% of its DoF-off sharpness. Fixed
ROIs and per-frame measurements are recorded in
`t16_dof_strict_gpu_roi_metrics.json`. CAM-05 passes for this focused T16
strict-native GPU scene; the default raw-readback path above still fails with
CPU depth and remains a separate backend limitation.

Reproduce the complete strict-native DoF-on clip from `Chronon3d/` with:

```bash
build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli render \
  --backend vulkan --gpu-hot-path-mode require_gpu_native \
  --encoder-backend native --video-sink ffmpeg --pipe-pixfmt nv12 \
  --plan docs/evidence/camera-authority-migration/T16/dof-ablation/t16_three_depth_dof_on.plan.json \
  --assets-root "$PWD" --start-frame 0 --end-frame 149 \
  -o docs/evidence/camera-authority-migration/T16/dof-ablation/t16_three_depth_dof_on_strict_gpu_150f.mp4
```
