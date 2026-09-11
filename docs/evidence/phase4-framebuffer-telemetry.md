# Phase 4 — framebuffer lock telemetry

Status: **PASS — preserve one mutex; do not shard on this corpus**.

The two-job Vulkan run recorded the existing pool telemetry. The measured
wait is below the hold time and is only 1–2 µs per report. There is no
measured contention cost supporting a size-class split on this baseline.

| run | lock wait µs | lock hold wall µs | acquire count | release count |
|---|---:|---:|---:|---:|
| single baseline | 2 | 10 | 2 | 0 |
| job 1, concurrent | 1 | 3 | 2 | 0 |
| job 2, concurrent | 2 | 12 | 2 | 0 |

Decision under ADR-029: **single mutex, measured negligible; preserve**. A
future sharding change requires a corpus where lock wait is a material share
of frame wall time, not merely a non-zero counter.

Source reports: `/tmp/chronon-phase4-gpu-evidence.8dRzRr/*.json`.
