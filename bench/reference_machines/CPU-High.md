# CPU-High — Reference Machine (32–64 core server batch)

> **Class id**: `cpu-high`
> **Canonical spec**: [`configs/benchmark_machines.yaml`](../../configs/benchmark_machines.yaml) → `machines[id=cpu-high]`
>
> This document is the **narrative companion** to the YAML canonical spec.
> The YAML is the **single source of truth** for hardware sizing, OS toolchain,
> CMake preset, governor, and per-class thread counts. Do **NOT** duplicate
> those values here — update the YAML and this doc will follow via citation.

---

## What this class represents

CPU-High models **high-throughput batch rendering servers** (32 physical
cores, 64 logical cores via SMT, 128 GB RAM, ≤80 °C ambient). It captures
the scaling-ceiling scenario: how Chronon3D performs when memory bandwidth
stops being the bottleneck and TBB parallelism finally has room to breathe
across NUMA nodes.

The 32–64-core range is the **stress test for the SMP scheduler**. Frames
that parallelise cleanly here will scale sub-linearly (Amdahl's law), and
the **delta-vs-cpu-mid ratio** tells you how much of the ALG parallelism
is real vs. cache-coherence chatter.

## Hardware profile (cite SSoT)

| Concern | Where to find it |
|---|---|
| CPU model / vendor range | `machines[cpu-high].cpu.model` |
| Physical cores (32) | `machines[cpu-high].cpu.physical_cores` |
| Logical cores (64) | `machines[cpu-high].cpu.logical_cores` |
| RAM (128 GB) | `machines[cpu-high].ram_gb` |
| Operating temperature (80 °C) | `machines[cpu-high].operating_temperature_c` |
| Compiler (Clang 16 / GCC 13) | `machines[cpu-high].compiler` |
| CMake preset (`linux-fast-dev-release`) | `machines[cpu-high].cmake_preset` |
| Thread counts to sweep | `thread_counts.cpu-high` in YAML |

The `linux-fast-dev-release` preset is **deliberate** for cpu-high: the
high-tier machine has cooling headroom to absorb `-O3` regression-test
costs, and the release-mode binaries expose the same scaling characteristics
that production users will see.

## Taskset mode (NUMA-aware)

A 32-physical-core server typically exposes **2 NUMA nodes** (each with
16 cores), or 4 NUMA nodes on the largest EPYC SKUs. The canonical pattern
is to sweep three task-mask shapes:

```bash
# Single NUMA node (best-case cache locality)
taskset -c 0-15 "$CLI_BIN" bench <scene> --frames <N>

# Cross-NUMA (full thread count, NUMA-aware allocator expected)
taskset -c 0-63 "$CLI_BIN" bench <scene> --frames <N>

# SMT-disabled physical cores only (avoid SMT aliasing on cross-NUMA)
taskset -c 0-31 "$CLI_BIN" bench <scene> --frames <N>
```

The runner (`bench/run_perf_bench.sh`) defaults to the full thread count
(64 wide) for `cpu-high` to exercise the NUMA-aware allocator. Custom
masks via `CHRONON3D_BENCH_TASKSET` env override; the mask **must** be
≤ logical_cores (runner logs and aborts otherwise).

## Governor policy (`cpu_governor: performance`)

CPU-High servers are the **easiest** environment for governor policy:
`intel_pstate` or `amd_pstate_efficiency` supports `performance` natively,
**but** the host administrator may have set BIOS-level thermal throttling
that overrides the OS governor. Surface the effective governor in the
JSON output (`effective_cpu_governor` field), not just the requested one.

## Thermal management

At 80 °C ambient the server is operating near the upper limit of most
thermal envelopes. The runner **does not** auto-throttle; the operator
must accept that CPU-High measurements taken without proper cooling will
show thermal-throttling artefacts (CPU MHz oscillation in the perf-stat
output). Either:

- Verify ambient `<= operating_temperature_c` before the run, OR
- Acknowledge thermal-throttling in the bench report's `notes` field.

## Why this class matters

CPU-High is the **competitive matrix row for "we beat the cloud at scale"**.
If CPU-High numbers do not show clean TBB scaling (≥1.5× over CPU-Mid at
matching per-thread workload), the SMP scheduler is the bottleneck and
all other optimisation gains are eroded by it. CPU-High also surfaces
NUMA-related correctness bugs (false sharing, allocator fragmentation)
that CPU-Mid sweeps miss entirely.

## Tooling conventions

- **NUMA check** (always run first on cpu-high):
  ```bash
  lscpu | grep -E '^NUMA|^CPU(s):' 
  numactl --hardware  # if available
  ```
- **Pre-flight**: `tools/benchmark_host_info.sh --yaml`
- **Runner**: `bash bench/run_perf_bench.sh --machine cpu-high --scene <name>`
- **Output**: `/tmp/bench/perf_cpu-high_<sha>.json` (larger; consider ZFS
  compression if your tmp dir is on ZFS).
- **Cooling validation**: `sensors` output goes into the JSON before any
  perf-stat invocation.

## Cross-link canonici

- [`configs/benchmark_machines.yaml`](../../configs/benchmark_machines.yaml) — YAML SSoT.
- [`tools/benchmark_host_info.sh`](../../tools/benchmark_host_info.sh) — host attribute collector (includes NUMA detection).
- [`examples/bench_corpus/run_corpus_v1.sh`](../../examples/bench_corpus/run_corpus_v1.sh) — F1.1 12-scene runner.
- [`bench/run_perf_bench.sh`](../run_perf_bench.sh) — perf-stat + warm-up wrapper (NUMA-aware via taskset).
- [`docs/PERFORMANCE_BOTTLENECKS.md`](../../docs/PERFORMANCE_BOTTLENECKS.md) — TBB peak-workers analysis.
- [`docs/tickets/TICKET-BENCH-MACHINES-V1.md`](../../docs/tickets/TICKET-BENCH-MACHINES-V1.md) — cronaca.
- AGENTS.md §regole ("no new singletons without ADR" — cpu-high NUMA awareness is a measurement concern, not an SDK surface concern).
