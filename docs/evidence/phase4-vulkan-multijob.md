# Phase 4 — Vulkan independent-job acceptance evidence

Status: **PASS** for concurrent independent Vulkan jobs on the recorded GPU
host. This is runtime evidence for the authority map; it does not claim that
two processes share one `VulkanBackend::Impl`.

Host:

- NVIDIA RTX A4000
- driver 595.84
- Vulkan instance 1.3.204
- `VK_LAYER_KHRONOS_validation` installed and enabled

Command used for every job:

```text
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
  build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli \
  benchmark --scene DarkGridBackground --duration 1 --backend vulkan \
  --report-json <独立-report-path>
```

The single baseline and the two independent jobs were run from the same
binary. All exit codes were `0`; validation-log scans found `0` occurrences
of `VUID-`, `Validation Error`, or `validation error`.

| run | wall | frames | throughput | lock wait µs | lock hold µs | steady allocations/frame |
|---|---:|---:|---:|---:|---:|---:|
| single baseline | 2.37 s | 4850 | 4849.279 fps | 2 | 10 | 0.0 |
| job 1, concurrent | 2.55 s | 4640 | 4639.597 fps | 1 | 3 | 0.0 |
| job 2, concurrent | 2.55 s | 4775 | 4774.884 fps | 2 | 12 | 0.0 |

Concurrent mean throughput is `4707.240 fps`, or `97.07%` of the single-job
baseline. Both jobs completed while validation layers were active. The
independent-process scope is intentional: same-backend surface scheduling
still requires a dedicated in-process harness before it can be claimed here.

Recorded report directory: `/tmp/chronon-phase4-gpu-evidence.8dRzRr`.
