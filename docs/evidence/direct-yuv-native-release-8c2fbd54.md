# DirectYUV native Release evidence — `8c2fbd54`

## Host and command

- Commit: `8c2fbd54d15c0528a84400c2df873a6becdabf8b`
- GPU: NVIDIA RTX A4000, 16,376 MiB
- Fixture: `tools/fixtures/direct-yuv-nv12-cert.plan.json`
- Input: `test_renders/overlay/trump-overlay-fixed-test.mp4`
- Output: 1280×720, 30 fps, 16 frames, native H.264 NVENC

The command used the Release CLI with `--gpu-hot-path-mode require_direct_yuv`,
Vulkan, native FFmpeg/NVENC, and four warmup framebuffers.

## Results

- Render loop: `106.23 ms` for 16 frames
- Steady state: `9.409 ms/frame` (`150.6 fps`)
- Execution path: `direct_yuv`
- Template preparation: `1 eval + 15 reuses`
- NVDEC frames: `16`
- NVENC frames: `16`
- `rgba_to_nv12_frames=0`
- GPU readback bytes: `0`
- CUDA host upload bytes: `0`
- Encoder staging-copy bytes: `0`

This confirms the DirectYUV lane is already the cheaper native path for an
eligible source. Further gains should come from moving more real compositions
into this lane or skipping unchanged compressed GOPs, not from adding another
template cache.
