# GPU baseline — `cfd178b59`

Date: 2026-09-11  
Host path: `build/chronon/linux-video-fast-dev`  
Fixture: `SMOKE=DarkGridBackground`  
Frames: `12`  
Warm-up framebuffers: `2`  
Backend: `vulkan` / native encoder request

## Reproduction

```bash
tools/benchmark_gpu_baseline.sh \
  --out /tmp/chronon-gpu-baseline-cfd178b59 \
  --frames 12 --warmup 2 \
  --case SMOKE=DarkGridBackground
```

## Result

```text
exit_code                 0
frames                    12/12
execution_path            full_graph_native
total_wall_ms             5659.319
process_startup_ms        2327.444
prepare_ms                3330.999
warmup_ms                 25.519
steady_state_ms           18.799
encode_drain_ms           619.625
finalize_ms               0.028
attributed_ms              4146.570
unaccounted_ms             40.714
accounted_percent          99.028
cuda_host_upload_bytes     0 (prewarm and steady-state)
```

The authoritative report is `/tmp/chronon-gpu-baseline-cfd178b59/gpu-baseline.json`.
This is a current-tip smoke baseline, not a claim that the complete B00–B11
and GPU_TEXT corpus is certified; those fixtures still require the registered
content pack and the NVENC certification host.
