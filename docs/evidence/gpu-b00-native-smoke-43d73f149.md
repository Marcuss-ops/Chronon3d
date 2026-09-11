# GPU B00 native smoke evidence — `43d73f149`

## Verdict

PASS for the embedded `DarkGridBackground` smoke case on the real GPU lane.
This is not the official B00–B11 + GPU_TEXT certification: the external
content pack is not installed on this host.

## Host and command

- Commit: `43d73f149bf1a5eb15e886b7fc0bacb2e078e293`
- GPU: NVIDIA RTX A4000, 16,376 MiB
- Driver: 595.84
- Backend: Vulkan
- Encoder: native FFmpeg/NVENC
- Build: CMake/Ninja, `ccache`, `mold`, `-j16`
- Case: `B00=DarkGridBackground`
- Frames: 30, warm-up framebuffers: 10

```bash
CHRONON3D_BASELINE_DIR="$PWD/.tmp/gpu-baseline-43d73" \
bash tools/benchmark_gpu_baseline.sh \
  --cli "$PWD/build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli" \
  --case B00=DarkGridBackground --frames 30 --warmup 10 \
  --backend vulkan --hardware nvenc --encoder-backend native \
  --gpu-hot-path-mode require_gpu_native --assets-root "$PWD"
```

## Timing result

| Measure | Value |
|---|---:|
| process startup | 1,028.008 ms |
| prepare | 2,137.132 ms |
| prewarm | 23.764 ms |
| steady state | 6.392 ms/frame |
| encode drain | 584.237 ms |
| finalize | 0.045 ms |
| total wall | 4,236.707 ms |
| wall attribution | 99.114% |
| unaccounted wall | 26.109 ms |

The runner accepted the case because the attribution contract is satisfied
(`attributed_ms + unaccounted_ms ~= process_wall_ms`).

## GPU and zero-copy counters

- 30/30 native Vulkan surfaces and 30/30 native NVENC frames.
- `gpu_surface_copy_frames=0`.
- CPU pixel readback: `0` frames / `0` bytes.
- Encoder staging copy: `0` bytes.
- GPU upload: `0` bytes; CUDA host upload: `0` bytes in both windows.
- Software fallback nodes and encode failures: `0`.
- Frame-slot waits: `0`; CUDA encode-event waits: `0`; encoder queue peak: `1`.
- Graph reuse: 29/30 frames (`96.67%`).

The lane is still `full_graph_native`: `rgba_to_nv12_frames=30` remains the
known Z2/Z4 debt, so this evidence does not claim the final single-backing
DirectYUV path.

`full_file_reread_count=1` is the explicit MP4 seek/backpatch fallback during
mux finalization. It is surfaced in the sidecar and is not reported as an
incremental SHA256 success.

Raw artifacts from the run are under `.tmp/gpu-baseline-43d73/` in the local
workspace; the durable contract is the versioned runner and sidecar schema.
