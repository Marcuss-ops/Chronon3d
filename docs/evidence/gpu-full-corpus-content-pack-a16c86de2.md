# GPU corpus availability check — `a16c86de2`

Date: 2026-09-12  
Runner: `tools/benchmark_gpu_baseline.sh`  
Manifest: `docs/benchmarks/gpu-corpus-v1.json`  
Build: `build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli`

The canonical manifest was invoked with one shared configuration over all 17
entries (B00–B11 and GPU_TEXT_01–05), using 12 frames and 2 warmup frames.

```text
cases requested = 17
cases completed = 0
cases failed    = 17
error_class     = command_failed_before_sidecar (17/17)
```

The build is configured with `CHRONON3D_BUILD_CONTENT=OFF`; the repository
does not contain the external content module that registers the manifest scene
names. Therefore no timing sidecar exists for these entries and no baseline
number is recorded. The built-in `DarkGridBackground` smoke remains the only
local GPU case until the content pack is supplied.

The runner report is:
`.tmp/gpu-baseline-a16c86de2-full-corpus-run2/gpu-baseline.json`

