# CPU-Low — Reference Machine (4–6 core VPS)

> **Class id**: `cpu-low`
> **Canonical spec**: [`configs/benchmark_machines.yaml](../../configs/benchmark_machines.yaml) → `machines[id=cpu-low]`
>
> This document is the **narrative companion** to the YAML canonical spec.
> The YAML is the **single source of truth** for hardware sizing, OS toolchain,
> CMake preset, governor, and per-class thread counts. Do **NOT** duplicate
> those values here — update the YAML and this doc will follow via citation.

---

## What this class represents

CPU-Low models the **entry-level cloud VPS** scenario (4–6 physical cores,
4 logical cores, 8 GB RAM, ≤70 °C ambient). It captures the cost-per-minute
competitive matrix: how Chronon3D fares against incumbent render services
on the cheapest tier that any developer can spin up in 60 seconds.

The 4–6-core range is deliberate: it is below the SMP-friendly TBB threshold
(8+ cores) and above the single-core starvation floor (2 cores). Frames that
parallelise well on CPU-Mid often serialise here — and that asymmetry is the
point of this class.

## Hardware profile (cite SSoT)

| Concern | Where to find it |
|---|---|
| CPU model / vendor range | `machines[cpu-low].cpu.model` in YAML |
| Physical cores (4) | `machines[cpu-low].cpu.physical_cores` |
| Logical cores (4) | `machines[cpu-low].cpu.logical_cores` |
| RAM (8 GB) | `machines[cpu-low].ram_gb` |
| Operating temperature (70 °C) | `machines[cpu-low].operating_temperature_c` |
| Compiler (Clang 16 / GCC 13) | `machines[cpu-low].compiler` |
| CMake preset (`linux-fast-dev`) | `machines[cpu-low].cmake_preset` |
| Thread counts to sweep | `thread_counts.cpu-low` in YAML |

## Taskset mode

A 4-core VPS surfaces exactly **one** NUMA node, so `taskset` reduces to a
single affinity mask covering all cores:

```bash
taskset -c 0-3 "$CLI_BIN" bench <scene> --frames <N>
```

The runner (`bench/run_perf_bench.sh`) pins to this mask automatically when
`--machine cpu-low` is passed. The mask is **not hard-coded** in the script
— it is read from the YAML at run time via `python3 -c "import yaml..."`.

## Governor policy (`cpu_governor: performance`)

CPU-Low typically runs on a **virtualised cpufreq driver** (`acpi-cpufreq`
or `powernow`). The `performance` governor request is best-effort:

- **Native Linux**: succeeds if `intel_pstate` or `acpi-cpufreq` is loaded.
- **VPS / cloud**: silently rejected by the host kernel — no effect,
  governor stays at the host's default (`schedutil` or `ondemand`).
- **WSL2 / macOS-via-Docker**: no `cpufreq` driver at all → hard fail.

The runner handles all three cases via WARN+continue (no abort). See
`bench/run_perf_bench.sh` for the graceful-degradation pattern.

## Why this class matters

CPU-Low is the **earn-your-place** benchmark. If a scene does not hit its
p95 target here, no amount of CPU-Mid tuning will make it shippable as a
low-tier SaaS. The class also serves as a **regression floor**: if a
formerly-passing scene fails on CPU-Low after a code change, you have a
mandatory fix before any other class is even consulted.

## Tooling conventions

- **Pre-flight**: `tools/benchmark_host_info.sh --yaml` to dump live
  host attributes before declaring the run a CPU-Low measurement.
- **Runner**: `bash bench/run_perf_bench.sh --machine cpu-low --scene BenchB00_EmptyFrame`.
- **Output**: per-machine JSON at `/tmp/bench/perf_cpu-low_<sha>.json`.
- **Warm-up**: 10 discarded frames (`common.warmup.frames` in YAML).

## Cross-link canonici

- [`configs/benchmark_machines.yaml`](../../configs/benchmark_machines.yaml) — YAML SSoT.
- [`tools/benchmark_host_info.sh`](../../tools/benchmark_host_info.sh) — host attribute collector.
- [`examples/bench_corpus/run_corpus_v1.sh`](../../examples/bench_corpus/run_corpus_v1.sh) — F1.1 12-scene runner (the underlying workload).
- [`bench/run_perf_bench.sh`](../run_perf_bench.sh) — perf-stat + warm-up wrapper (this ticket).
- [`docs/tickets/TICKET-BENCH-MACHINES-V1.md`](../../docs/tickets/TICKET-BENCH-MACHINES-V1.md) — cronaca (this ticket home).
- AGENTS.md §honest-limitation (§DEFERRED-WBH posture when env-block).
