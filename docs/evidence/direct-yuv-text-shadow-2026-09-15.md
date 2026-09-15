# DirectYUV text shadow evidence — 2026-09-15

## Why

Every official overlay preset (RenderingGen `apple_v2` and the Chronon
`shadow_stroke` / `typewriter_shadow_stroke` family) ships an enabled drop
shadow. The DirectYUV text contract rejected **any** enabled shadow
(`text_shadow_unsupported`), so every real overlay phrase carrying our own
presets left the zero-conversion lane and fell back to FullGraph RGBA. The
shadow itself was the blocker, not the stroke (the stroke already lowered).

## What changed

| File | Change |
|---|---|
| `src/media/video/direct_yuv/direct_yuv_text_contract.hpp` (new) | ONE classifier for the DirectYUV text contract: layout/MTSDF readiness, style representability, and the canonical single shadow. |
| `src/media/video/direct_yuv/direct_yuv_capability.cpp` | Fills `LayerYuvQuery` from that classifier and lets the declared matrix decide the lane; reports the classifier's precise reason. The second authority (`direct_yuv_text_rejection`) is gone. |
| `include/chronon3d/media/video/composition_capability.hpp` | `LayerYuvQuery::text_style_representable` (default `false`, fail closed) is the matrix input for the TextOverlay row. |
| `include/chronon3d/media/video/cuda_layer_resource.hpp`, `src/media/video/compositor/cuda/cuda_nv12_compositor_detail.hpp`, `direct_yuv_program.hpp` | One canonical shadow travels with the text resource: colour, opacity (kept a separate factor), dx/dy, blur. |
| `src/media/video/compositor/cuda/cuda_nv12_kernels.hpp` | MTSDF branch paints the shadow first (under stroke, under fill) by **resampling the same prepared distance field**; blur 0 is the hard offset silhouette, a blur wider than the field band is completed by a normalized binomial 3×3 kernel at half the authored radius. The shadow envelope is honoured outside the glyph box. |
| `src/media/video/direct_yuv/direct_yuv_program.cpp` | Lowers the classifier's shadow into the layer resource, fail closed on any rejection. |
| `tests/video/test_direct_yuv_text_contract.cpp` (new), `tests/video/test_composition_capability.cpp`, `tests/media/nv12/nv12_equivalence_test.cpp` | Contract, matrix and GPU coverage. |

Not a second rasterization and not a per-frame cost: the shadow is parameters
plus samples of the already-prepared, already-uploaded, content-addressed glyph
field.

## Host and commands

- Host: NVIDIA RTX A4000 (same host as
  `direct-yuv-native-release-8c2fbd54.md`).
- `cmake --build build/chronon/linux-video-test --target chronon3d_zero_copy_capability_tests`
  → `./build/chronon/linux-video-test/tests/chronon3d_zero_copy_capability_tests`
- `cmake --build build/chronon/linux-video-test --target chronon3d_nv12_compositor_contract_tests`
  → `CHRONON3D_RUN_CUDA_TESTS=1 ./build/chronon/linux-video-test/tests/chronon3d_nv12_compositor_contract_tests`
- `cmake --build build/chronon/linux-video-test --target chronon3d_media_direct_yuv` (compiles the
  classifier and the program plumbing)

## Results

Contract + capability matrix (CPU):

```
test cases: 34 | 34 passed | 0 failed
assertions: 179 | 179 passed | 0 failed
```

GPU contract suite (NVRTC compiles the edited kernel into a fresh program
artifact `program-v1-2f29e037e971e33e`, 114279 bytes):

```
[NV12-EQ] matrix result: 10/10 cases within documented expectations
[NV12-SHADOW] below edge: 7px soft=23 hard=0 | 10px soft=1 (Y delta)
[NV12-SHADOW] instance agreement: identical=1 diff=0/230400
[NV12-EQ] determinism: A Y identical=1 UV identical=1 | B Y identical=1 UV identical=1
test cases: 4 | 4 passed | 0 failed    assertions: 63 | 63 passed
```

Reading of the shadow numbers, on a disc silhouette with the canonical preset
shadow (`#000000`, opacity 0.72, offset (0,4), blur 12) over a static NV12
background:

- 7 px below the glyph edge: **23** luma levels of shadow with blur 12, **0**
  with blur 0 — the authored blur verifiably reaches past the hard offset
  silhouette (which is why a stroke-only lane could never stand in for it).
- 10 px below the same edge: **1** luma level — the falloff is soft, not a step.
- Both independent compositor instances agree bit-exactly (0 of 230400 bytes
  differ), so the shadow adds no nondeterminism to the lane.
- The pre-existing 10-case NV12 matrix (including the NV12-overlay rejection and
  the bit-exact background passthrough) is unchanged.

## Honest limits

- The shadow profile is an **approximation** built from a clamped signed
  distance field plus a 3×3 binomial tap kernel, not a separable Gaussian. It is
  measured (23 → 1 luma levels from 7 px to 10 px outside the silhouette), not
  claimed pixel-identical to the FullGraph RGBA result.
- The prepared atlas caps the softness the field can express on its own
  (`distance_range_px`); the tap kernel completes the authored radius beyond it.
  A larger prepared radius would sharpen fidelity further, at a cache-key cost.
- More than one enabled shadow (e.g. the Neon two-layer glow) still fails closed
  to the explicit FullGraph fallback: one distance field, one shadow.
- The full CLI canary (`tools/fixtures/direct-yuv-text-shadow-cert.plan.json`,
  `--gpu-hot-path-mode require_direct_yuv`) could not run: `chronon3d_cli`
  currently fails to compile in an unrelated, pre-existing place
  (`commands/video/common/pipe_export_helpers.cpp:131` reads
  `FfmpegExportOptions::audio_target_sample_rate`, a member the struct does not
  declare). That breakage is not touched by this change.
