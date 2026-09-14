# GPU B00 native Release evidence — `8c2fbd54`

## Verdict

PASS for the embedded `DarkGridBackground` FullGraph smoke in an optimized
Release build. This is a baseline, not the full B00–B11 corpus certification.

## Host and build

- Commit: `8c2fbd54d15c0528a84400c2df873a6becdabf8b`
- GPU: NVIDIA RTX A4000, 16,376 MiB
- Build: `linux-video-release`, `Release`, `-O3 -DNDEBUG`, CUDA/Vulkan/native FFmpeg
- Build command: `ninja -C build/chronon/linux-video-release -j12 chronon3d_cli`
- Resource guard before build: 25.4 GiB RAM available, 1.3 GiB swap free,
  58.4 GiB disk free

## Command

```bash
bash tools/benchmark_gpu_baseline.sh \
  --cli "$PWD/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli" \
  --out "$PWD/.tmp/gpu-baseline-release-repeats-20260914" \
  --frames 120 --warmup 10 \
  --backend vulkan --hardware nvenc --encoder-backend native \
  --gpu-hot-path-mode require_gpu_native --assets-root "$PWD" \
  --case B00_R1=DarkGridBackground \
  --case B00_R2=DarkGridBackground \
  --case B00_R3=DarkGridBackground
```

## Results

| Measure | R1 | R2 | R3 | Median |
|---|---:|---:|---:|---:|
| render loop (ms) | 506.2 | 520.5 | 507.1 | 507.1 |
| steady frame (ms) | 4.379 | 4.504 | 4.389 | 4.389 |
| steady FPS | 228.3 | 222.0 | 227.9 | 227.9 |

The run stayed on `full_graph_native`: 120 native Vulkan surfaces and 120
NVENC frames, with zero GPU readback bytes, CUDA host upload bytes, and encoder
staging-copy bytes. `rgba_to_nv12_frames=120` remains the FullGraph conversion
cost; it is not a zero-copy DirectYUV result.

For throughput-oriented encoding, the same 120-frame case with
`--encode-preset p1` completed with `render_loop_ms=240.57`,
`steady_avg_ms=2.065` (~499 fps), and a valid H.264 file containing 120 frames.
This is a speed/quality trade-off and is therefore exposed as an explicit
benchmark option rather than changing the production default.

## Interpretation

The Release build removes Debug overhead, but the remaining FullGraph cost is
still one graph submission/conversion path per frame. The Z6 sweep showed zero
frame-slot waits and no steady-state improvement from depth 3 to 6, so increasing
the ring further is not the current bottleneck. The next high-leverage work is
DirectYUV coverage, codec-boundary execution, and budgeted multi-job admission.
