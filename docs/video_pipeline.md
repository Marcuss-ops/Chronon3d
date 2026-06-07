# Video Export Pipeline

This document maps the call chain from the CLI video export command down to the
Highway SIMD YUV converter, so it's clear where to intervene for performance
work or bug fixes.

## Call Chain

```
CLI: chronon3d_cli video <composition> -o output.mp4
  │
  └─► pipe_export_session.cpp          (orchestrates render loop + writer thread)
        │
        └─► ffmpeg_pipe_encoder.hpp    (thin wrapper — no conversion logic here)
              │
              ├─► convert_framebuffer_to_yuv420p()   ◄── apps/…/ffmpeg_pipe_yuv.cpp
              │                                         (delegates immediately ↓)
              ├─► convert_framebuffer_to_nv12()
              └─► convert_framebuffer_to_rgba()       (kept for RGB pass-through)

        All three call:
              │
              └─► video::convert_frame_tight()        ◄── src/video/frame_converter.cpp
                    │
                    └─► video::convert_frame()
                          │
                          ├─ Fast path (YUV420P / NV12):
                          │   convert_framebuffer_to_yuv_direct()
                          │     │                                       ◄── src/video/direct_yuv_converter.cpp
                          │     ├─► convert_to_yuv420p_hwy()            ◄── src/video/direct_yuv_converter_hwy.cpp
                          │     │     (Highway SIMD — active backend)
                          │     │
                          │     └─► convert_to_yuv420p_parallel()
                          │           (scalar TBB fallback — pixel-identical reference)
                          │
                          └─ Slow path (RGB24, or YUV fallback):
                              convert_rgba_to_target()
                                │
                                ├─► convert_fb_to_rgba8()     (float→RGBA8 staging)
                                └─► sws_scale()               (RGBA8→target via libswscale)
```

## Key Files

| File | Role |
|------|------|
| `src/video/direct_yuv_converter_hwy.cpp` | **Active HWY SIMD backend** for float→YUV420P/NV12 |
| `src/video/direct_yuv_converter.cpp` | Dispatch (HWY first, TBB fallback) + scalar reference |
| `include/chronon3d/video/direct_yuv_lut.hpp` | Shared 64 KB sRGB gamma LUT + BT.709/601 coefficients |
| `src/video/frame_converter.cpp` | `convert_frame()` / `convert_frame_tight()` entry points |
| `apps/chronon3d_cli/utils/video/ffmpeg_pipe_yuv.cpp` | **Thin wrappers — no conversion logic here** |
| `apps/chronon3d_cli/commands/video/pipe_export_session.cpp` | Render loop + writer thread orchestration |

## Rules

1. **All float→YUV logic lives in `src/video/direct_yuv_converter*.cpp`**
   and `include/chronon3d/video/direct_yuv_*.hpp`.
2. `ffmpeg_pipe_yuv.cpp` is a **thin wrapper** — do not add conversion kernels there.
3. The HWY SIMD backend is the **primary active path**; the scalar TBB path is
   the fallback/reference. When optimising, focus on `direct_yuv_converter_hwy.cpp`.
4. New pixel formats should be added to `direct_yuv_converter.cpp` (dispatch)
   and `direct_yuv_converter_hwy.cpp` (SIMD fast-path).

## Current Performance Notes

- The gamma LUT (64 KB, `g_srgb_lut`) is scalar but L1-cache resident — it is
  **not** the bottleneck.
- The BT.709/601 matrix is already HWY SIMD (FMA).
- The main video pipeline bottleneck is in the **render graph execution**
  (compositing, transforms, framebuffer pool), not in the YUV conversion.
