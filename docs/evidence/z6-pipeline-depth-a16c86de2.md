# Z6 pipeline depth telemetry — `a16c86de2`

Date: 2026-09-11  
Host: NVIDIA RTX A4000, native Vulkan/NVENC  
Runner: `tools/benchmark_gpu_baseline.sh`  
Case: `B00=DarkGridBackground`, 12 frames, 2 warmup frames

## Result

The existing bounded pipeline now records queue and in-flight depth in the
timing sidecar, without adding a scheduler or a second queue authority:

```text
job.pipeline.queue_depth_average              = 1.0
job.pipeline.queue_depth_peak                 = 1
job.pipeline.render_slots_in_flight_peak      = 2
job.pipeline.encode_frames_in_flight_peak     = 1
```

The short smoke completed successfully; it is telemetry validation, not the
2/3/4/6 × 4/6/8 depth sweep. That sweep remains the next measurement step on
the full official corpus.

Artifact:
`.tmp/gpu-baseline-a16c86de2-z6-run2/B00/render.mp4.timing.json`

## Code contract

- queue depth is sampled after successful publication, so the peak includes
  the newly enqueued frame;
- encode in flight is queue backlog plus the package currently owned by the
  writer;
- render slot peak is observed from the existing `GpuSlotPool`;
- telemetry merge treats `io_queue_peak_depth` as a maximum, not a cumulative
  counter.

