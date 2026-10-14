# BUG: Vulkan backend renders degraded frames from the third rendered frame of a run

**Status:** Fixed (2026-10-13). Root cause: cross-frame framebuffer retention
(NodeCache, static-bake) plus CPU-path SparseTiles served stale/retired
FrameTransient Vulkan surfaces. Four guards widened from
`supports_native_video_surface()` (CUDA-interop only, OFF in this build) to
`supports_native_surfaces()`; native-tile fallback renders full frame with
tile/dirty scoping cleared. Verified Vulkan==software within 0.35 mean abs
diff on filmstrip 0–12 / 0–30 / 70–84 and caption_rise 0–4; 8/8 relevant
ctest suites pass (render_graph, cache, compositor, software, registry,
vulkan_surface_materializer, cli, executor_authority_gate).

## Follow-up perf fix (same session): rounded-image promotion re-hashed 16 MB per frame

`try_native_image_impl` recomputed the rounded crop + `sha256(16 MB)` (~5 s)
every frame for static (source, crop, radius) inputs. Now memoized in a
canonical `LruCache` (ContentCache semantics) keyed by source digest + crop +
radius bits, with a microsecond composite key for the `GpuAssetCache`
lookup: 4.9 s → ~50 ms first hit, ~0 ms after. Pixel-identical
(verification tables above unchanged). Frame time on the filmstrip plan
roughly halves (less CPU stall in the level-0 image task).

## Residual tolerance

Vulkan==software within 0.35 mean abs diff (NV12 luma, /255): 0.002–0.16 on
0–12 (grows only while opacity fades interpolate), flat 0.32 post-fade on
70–84. This is CPU-double vs GPU-fp32 keyframe interpolation precision, not
a leak (flat within the 70–84 run). Suggested regression threshold: 0.5.
**Component:** Chronon3d, Vulkan backend (`src/backends/vulkan`)
**Severity:** High. Blocks GPU rendering of the ChrononTemplate entity V2 videos (15 plans). The software backend renders the same plans correctly.

## Summary

When a render runs on the Vulkan backend, the output frames diverge from the software backend starting at the **third rendered frame of the run** (index 2 of the run, not the plan's absolute frame). From that point the frames degrade, the layer alpha drops, and subsequent frames become identical to each other (frozen). The same plan rendered with the software backend is correct on the same frame range.

## Reproduction

Plan: `ChrononTemplate/out/entity_with_text_v2/entity_v2_archive_filmstrip.plan.json`
CLI: `Chronon3d/build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli`
Assets root must be an absolute path (the VeloxEditing root). A relative `.` fails.

```sh
ROOT=/home/pierone/src/go-master/projects/Pyt/VeloxEditing
CLI=$ROOT/Chronon3d/build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli
PLAN=$ROOT/ChrononTemplate/out/entity_with_text_v2/entity_v2_archive_filmstrip.plan.json
for be in vulkan software; do
  $CLI render --plan $PLAN --assets-root $ROOT --backend $be --profile preview \
    --fps 30 --video-sink raw --pipe-pixfmt nv12 --chunks 1 \
    --start-frame 0 --end-frame 84 -o /tmp/repro/$be.nv12
done
```

Comparison (NV12, 1920x1080, 85 frames per file): per-frame mean absolute difference of the full frame, and mean luma per frame.

## Observed results

- Both runs exit 0 and produce 85 frames.
- Frames 0–1 match. From frame 2 the mean absolute difference is about 6.5 (out of 255), and it stays there.
- Luma at sampled frames (every 6th frame):
  - Vulkan: 27.4, 38.1, 42.3, 52.6, 43.5, 43.1, 42.8, 42.8, ... 42.8, 16.7
  - Software: 27.4, 47.1, 51.3, 52.6, 52.5, 52.1, 51.8, 51.8, ... 51.8
- Vulkan frames from 2 onward are identical to each other (frozen), while the software frames keep their content.
- Earlier alpha measurements on the raw sink: the frame alpha falls from 255 to about 51 and then to about 1, with identical subsequent frames.

## Characteristics

- **Deterministic** for a given run, but **depends on the run**. The degradation always starts at the third rendered frame of the run:
  - range 0–12: degrades at frame 2
  - range 70–76: degrades at frame 72
  - range 74–84: degrades at frame 76
- The degradation starts at the third rendered frame regardless of the plan's absolute frame index. This points to a state or initialization issue in the Vulkan pipeline, not to content.
- The warning `GPU timing completion has no telemetry frame` appears on every Vulkan run. It may be related to slot cycling.

## Ruled out so far

- **Frame ring slot reuse**: no change in output.
- **In-flight race / synchronization**: `CHRONON3D_VULKAN_SINGLE_FLIGHT=1` (one frame in flight) gives identical output. The defect is not a race between frames in flight.
- **Plan surface aliasing**: `CHRONON3D_PLAN_NO_ALIAS=1` does not remove the defect (alpha of frame 2 still about 9.6 instead of 255).
- **The plan itself**: the software backend renders the same plan and ranges correctly.
- **Committed fix `89424154d`**: the CLI is built after it (build is up to date), and the defect is still present. That commit does not fix this issue.

## Not verified

- Whether the PNG lane shows the defect. It does not: it does not composite text layers and does not vary per frame, so it is not valid evidence.
- Whether the defect affects only this plan or all Vulkan plans. Only `entity_v2_archive_filmstrip` was checked in this pass.

## Suspected areas (not confirmed)

- `composite_surfaces` in the Vulkan backend.
- Initialization of destination surfaces. The comment in `initialize_transparent_surface` suggests that initialization outside the compiled pass index can move barriers and descriptors.
- Descriptor or barrier state carried over between the first two frames of a run.

## Next steps

1. Trace the native operation sequence for frames 0–3 (diagnostic already available) and compare with the first two frames.
2. Check whether the third-frame state comes from an uninitialized or stale descriptor set.
3. Add a regression test: Vulkan vs software per-frame comparison on a short range (0–12) of this plan, with a tolerance threshold.

## Workspace notes

- Uncommitted changes exist in the Chronon3d repo, including diagnostics (`CHRONON3D_LAYER_READ_DIAG`, `CHRONON3D_VULKAN_SINGLE_FLIGHT`, `CHRONON3D_PLAN_NO_ALIAS`) and modified files `vulkan_backend.cpp`, `vulkan_surface_store_private.cpp`, `vulkan_backend_operations_private.cpp`, `native_surface.hpp`, `scene_native_output.cpp`, plus `tests/motion_showcase/*`. Their origin is not confirmed. Review them before changing this code.

## Regression window (user report: "it worked yesterday")

The user reports that the V2 renders worked until 2026-10-12. Commits after that point:

- `e14422dd6` (2026-10-12 05:35), "Read back GPU-only frames on the CPU pipe fallback lane": changes Vulkan slot lifecycle in `vulkan_surface_authority.hpp`. Its CUDA cleanup is compiled only with `CHRONON3D_ENABLE_CUDA_INTEROP`, which is `OFF` in the current build, so it is inactive here and not the cause.
- `343b16ac4` (2026-10-12 11:54), "Extend the render-plan v3 shape and scene pipeline": changes path fill color. Unverified whether the V2 plans use shape/path layers.
- `a75291ca6` (2026-10-12 12:59): shape fill alias note, same area.
- `89424154d` (2026-10-13 14:26): Vulkan sync fix, does not resolve the issue.

Also: `ChrononTemplate/tools/entities_with_text/build_entity_with_text_v2.py` and `test_entity_with_text_v2.py` are untracked (not committed), so the V2 generator cannot be diffed against yesterday's version.

Not yet determined: the exact regression commit. A `git bisect` needs several Chronon3d rebuilds.

## Impact on deliverables

- No valid GPU-rendered MP4 for the 15 entity V2 plans exists yet.
- `ChrononTemplate/out/entity_with_text_v2/videos/entity_v2_archive_filmstrip.mp4` contains degraded frames and must not be uploaded.
- Nothing has been uploaded to Drive.
