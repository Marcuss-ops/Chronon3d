# GPU B00 native smoke evidence — `a16c86de2`

## Verdict

PASS for the embedded `DarkGridBackground` FullGraph smoke case. This is not
the official B00–B11 + GPU_TEXT certification because the external content
pack is not installed on this host.

## Host and command

- Commit: `a16c86de2178270852075526a0dd27b96721a5d1`
- GPU: NVIDIA RTX A4000, 16,376 MiB
- Backend: Vulkan
- Encoder: native FFmpeg/NVENC
- Case: `B00=DarkGridBackground`
- Frames: 120, warm-up framebuffers: 10

```bash
CHRONON3D_BASELINE_DIR="$PWD/.tmp/gpu-baseline-a16c86de2-run2" \
bash tools/benchmark_gpu_baseline.sh \
  --cli "$PWD/build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli" \
  --case B00=DarkGridBackground --frames 120 --warmup 10 \
  --backend vulkan --hardware nvenc --encoder-backend native \
  --gpu-hot-path-mode require_gpu_native --assets-root "$PWD"
```

## Timing result

| Measure | Value |
|---|---:|
| process wall | 3,800.864 ms |
| startup | 1,247.822 ms |
| prepare | 2,478.760 ms |
| prewarm | 28.371 ms |
| steady state | 5.381 ms/frame |
| encode drain | 628.533 ms |
| wall attribution | 99.247% |
| unexplained wall | 28.614 ms |

## GPU and handoff counters

- 120/120 native Vulkan surfaces and 120/120 native NVENC frames.
- `gpu_surface_copy_frames=0`, GPU readback bytes `=0`, CPU readback frames `=0`.
- GPU upload bytes `=0`, CUDA host upload bytes `=0`, encoder staging bytes `=0`.
- `frame_slot_wait_count=0`, CUDA encode-event waits `=0`, queue peak `=2`.
- Execution path: `full_graph_native`; `surface_handoff_path=direct`.
- `rgba_to_nv12_frames=120` is expected for this FullGraph case and does not
  claim DirectYUV certification.
- `full_file_reread_count=1` remains the explicit MP4 seek/backpatch path.

The full corpus remains pending the external fixture/content-pack host.
