# Hash finalization accounting — `a16c86de2`

Date: 2026-09-12  
Case: `B00=DarkGridBackground`, 12 frames, 2 warmup frames  
Path: native Vulkan/NVENC FullGraph

The timing sidecar now exposes the actual mux/hash behavior:

```text
job.hash.output_write_bytes              = 30420
job.hash.output_seek_count               = 2
job.hash.output_overwrite_bytes          = 4
job.hash.incremental_hash_invalidations  = 1
job.hash.final_reread_bytes              = 30416
job.full_file_reread_count               = 1
```

This confirms the MP4 metadata backpatch invalidates the append-only hash and
causes one explicit final-file reread. The `hashing_ms` phase remains included
in the wall-time attribution (`99.0919%` accounted in this short smoke). No
counter was forced to zero and no new hash pipeline was introduced.

Artifact:
`.tmp/gpu-baseline-a16c86de2-hash-run1/B00/render.mp4.timing.json`

