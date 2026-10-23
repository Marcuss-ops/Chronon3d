# Documentary Snapshot torture canary: Vulkan GPU attempt

The connected `torture` adapter plan was regenerated from the current pack:
601 frames, 68 layers, 1920×1080 at 30 fps. The plan is
`documentary_snapshot_adapter_canary_torture_v1.plan.json`.

## Full-sequence attempt

The adapter script initially resolved a stale, missing CLI path. Retrying with
`--cli Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli`
started a Vulkan render. It did not complete the first output frame after
about 2m23s. During the attempt, `nvidia-smi` observed GPU utilization ranging
from 0% to 77%; process RSS reached about 6.3 GiB and CPU use stayed near one
core. The partial MP4 remained zero bytes, so the run was interrupted. This is
not a delivered canary or a timing pass.

## Strict native one-frame probe

`strict_gpu_frame_000.log` records the original one-frame Vulkan attempt using
`--gpu-hot-path-mode require_gpu_native`, NVENC, and the native encoder. It
fails on frame 0 with:

```text
UnsupportedCapability: VulkanBackend::draw_node: no legacy-node fallback attached; node='image' shape_type=7
```

NVENC accepted zero frames. The rejected layer was the sole image with
`crop.flip_x=true`. The native affine image path now supports horizontal flips
by reversing its source-X mapping; the change is in
`src/render_graph/nodes/source_node_native_image.cpp`.

`strict_gpu_frame_000_flipfix.log` is the first follow-up on RTX A4000. All 68
composition layers execute on Vulkan (about 4.8 s render time), including the
flipped image. It exposed a stale native surface: a CPU-authoritative composite
still had a valid Vulkan handle, so DoF depth publication incorrectly marked
the depth as mixed-residency. `native_surface.hpp` now uploads CPU-authoritative
pixels before a native consumer reuses a valid surface; DoF provenance promotes
that source and stamps depth with the Vulkan kernel.

`strict_gpu_frame_000_dof-native.log` confirms the mixed-depth error is gone.
The original plan then reaches temporal accumulation, which throws “strict
GPU-native samples require a native weighted-add accumulator.” NVENC accepts
zero frames. The existing Vulkan weighted composite implements source-over
and cannot stand in for the additive shutter average. A separate no-shutter
DoF-on raw Vulkan probe now gets past depth stamping but the blur dispatch
fails with `Vulkan recording resolved an unpinned surface 0`; native DoF is
therefore still not visually verified on this plan.

For diagnosis only, a copy without `temporal_motion_blur` was rendered with
strict Vulkan/NVENC at frames 0, 30 and 90. Each encode accepted one frame, but
visual inspection found all three output PNGs fully black. At frame 30 the
authored title has opacity 1; its NVENC output remains black with DoF disabled
and with that title layer changed to screen-space. However, the same frame 30
rendered on Vulkan through the raw sink, with DoF disabled, contains the
expected portrait card and caption. So the composition/camera path can produce
visible content; the defect lies in the native NVENC handoff/encode path for
this scene. Encoding that same no-DoF Vulkan render through the FFmpeg pipe
with NVENC also produces the expected card, separating the defect to the
native direct-surface encoder path. A non-strict native-NVENC attempt was
visually wrong (a huge brown shape) while reporting zero conversion/staging-copy
bytes. These are failed visual checks, not passes. The diagnostic plans are
`documentary_snapshot_adapter_canary_torture_v1_single_sample_diagnostic.plan.json`,
`documentary_snapshot_adapter_canary_torture_v1_no_dof_diagnostic.plan.json`,
and `documentary_snapshot_adapter_canary_torture_v1_screen-space-diagnostic.plan.json`;
the raw Vulkan control is `strict_gpu_frame_030_no-dof.raw.rgba` with its PNG
projection beside it.

After changing `PerPixelDofNode` to keep GPU-authoritative inputs on a native
copy path, the no-shutter DoF-on raw Vulkan render completes and its RGBA image
contains the expected card (`frame030_dof_fixed.raw.rgba`). It is byte-identical
to the DoF-off frame (`MAE=0`), so this proves content survives the GPU blur
route but does not prove a visible focus effect. The correction avoids both an
invalid surface-0 blur input and uploading a stale CPU shadow. Full-shutter,
visible-DoF and direct native-NVENC visual gates remain open.

The 601-frame torture canary remains open until the historical black
single-sample direct-NVENC result and current direct-surface visual failure are
resolved, and the full sequence passes strict rendering and visual review.
The authored eight-sample, 90-degree-shutter GPU accumulation has since been
verified on frame 30 through the FFmpeg/NVENC pipe; see the probe below.

## GPU temporal accumulation probe (2026-10-21)

The Vulkan backend now accumulates the eight temporal samples with normalized
weighted additive GPU passes. Its accumulator uses job-persistent Vulkan
  storage across the independent sample batches, then copies into the configured
  frame output. The native encode branch releases the accumulator after that
  copy; the non-native output branch performs one final download and releases
  it before returning the frame. A one-frame probe of the original
  plan at frame 30 uses the
authored 90-degree shutter and eight samples:

- [`vulkan_pipe_frame_030_weighted-add.mp4`](vulkan_pipe_frame_030_weighted-add.mp4)
  was rendered with Vulkan and encoded by FFmpeg/NVENC pipe. The decoded frame
  [`vulkan_pipe_frame_030_weighted-add.webp`](vulkan_pipe_frame_030_weighted-add.webp)
  was reviewed: the portrait, border, caption, and black negative space are
  visible and coherent. This verifies the temporal GPU accumulation path for
  this frame; it is not a full-sequence pass or a multi-frame motion-blur
  quality gate.
- The same frame through native direct-surface NVENC accepts one frame but
  decodes to a large brown shape. The two artifacts are
  [`strict_gpu_frame_030_weighted-add_cuda-copy.mp4`](strict_gpu_frame_030_weighted-add_cuda-copy.mp4)
  and its PNG. The dedicated Vulkan-to-CUDA encoder copy did not correct the
  visual failure. This keeps the direct-surface NVENC gate open; the pipe
  control shows the Vulkan accumulation can produce the intended composition.
- `strict_gpu_frame_030_weighted-add_persistent.log` records the first native
  probe, which revealed that a `FrameTransient` accumulator lost its slot
  between sample batches. The accumulator is now `JobPersistent`.
  `strict_gpu_frame_030_weighted-add_cuda-copy.log` also records
  the encode surface (`3`) and accumulator (`206`) used by the subsequent
  probes.

The latest CLI build succeeded with `-j16` and `git diff --check` passed.
The one-frame strict native NVENC visual failure remains a blocker; no claim is
made that the 601-frame sequence or the overall camera ticket is complete.

### Deferred NVENC handoff probe (2026-10-21)

The sample graph now renders each temporal sample into one reusable,
job-persistent Vulkan RGBA32F surface. Native encoder ownership handoff is
deferred until the final weighted average is copied to the NVENC surface. The
strict-native frame 30 probe completed on the Vulkan GPU and NVENC accepted one
H.264 frame, but the decoded image remains the same oversized brown shape.
Artifacts: `strict_gpu_frame_030_deferred-handoff.mp4`, its timing sidecar, and
`strict_gpu_frame_030_deferred-handoff.png`. This rules out interleaved
per-sample encoder-surface handoffs as the cause; direct native NVENC remains
visually incorrect. The Vulkan-to-FFmpeg/NVENC pipe control remains correct.


The post-fix strict-native SplitDepth frame-53 probe now gets through the
weighted Vulkan accumulator and accepts one NVENC frame, but still decodes as
the large brown shape. This reproduces the direct-native visual blocker after
fixing both the cropped DOF surface extent and the deferred sample-batch
lifetime. Full evidence is under `../gpu-fullshutter-review/split-depth/` as
`frame_053_strict-native_recheck3.*`.


### Direct-surface NVENC alpha-matte fix follow-up (2026-10-21)

Historical brown-field failures in the strict-native GPU/NVENC logs were traced
to `rgba_surface_to_nv12_2x2` unpremultiplying framebuffer RGB even though NV12
has no alpha plane. The CUDA path now writes the premultiplied RGB as a black
matte, matching the CPU packed conversion contract. Strict-native SplitDepth
frame 90 now matches raw Vulkan negative-space samples within two RGB code
values. The older frame-30 torture failure and FocusDrop strict-native output
need fresh renders with the correction before those gates can be closed; the
601-frame sequence and moving-object blur gate remain open.
