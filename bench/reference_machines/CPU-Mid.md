# CPU-Mid — Reference Machine (8–16 core desktop/server)

> **Class id**: `cpu-mid`
> **Canonical spec**: [`configs/benchmark_machines.yaml`](../../configs/benchmark_machines.yaml) → `machines[id=cpu-mid]`
>
> This document is the **narrative companion** to the YAML canonical spec.
> The YAML is the **single source of truth** for hardware sizing, OS toolchain,
> CMake preset, governor, and per-class thread counts. Do **NOT** duplicate
> those values here — update the YAML and this doc will follow via citation.

---

## What this class represents

CPU-Mid models the **developer workstation / small production server** tier
(8 physical cores, 16 logical cores via SMT, 32 GB RAM, ≤75 °C ambient). It
is the **primary reference machine** for Chronon3D regression gates and for
public performance reports: the most representative single-machine target
across the entire Chronon3D user base.

The 8–16-core range straddles the **SMP-friendly TBB threshold**: this is
where intra-frame parallelism begins to yield non-trivial speedups, but the
system is still memory-bandwidth-bound rather than NUMA-bound.

## Hardware profile (cite SSoT)

| Concern | Where to find it |
|---|---|
| CPU model / vendor range | `machines[cpu-mid].cpu.model` |
| Physical cores (8) | `machines[cpu-mid].cpu.physical_cores` |
| Logical cores (16) | `machines[cpu-mid].cpu.logical_cores` |
| RAM (32 GB) | `machines[cpu-mid].ram_gb` |
| Operating temperature (75 °C) | `machines[cpu-mid].operating_temperature_c` |
| Compiler (Clang 16 / GCC 13) | `machines[cpu-mid].compiler` |
| CMake preset (`linux-fast-dev`) | `machines[cpu-mid].cmake_preset` |
| Thread counts to sweep | `thread_counts.cpu-mid` in YAML |

## Taskset mode

An 8-physical-core workstation exposes **SMT siblings** on most CPUs. The
canonical pattern is to sweep two task-mask shapes:

```bash
# Physical cores only (avoid SMT contention)
taskset -c 0-7  "$CLI_BIN" bench <scene> --frames <N>

# SMT-enabled (full logical count)
taskset -c 0-15 "$CLI_BIN" bench <scene> --frames <N>
```

The runner (`bench/run_perf_bench.sh`) defaults to the SMT-enabled mask for
`cpu-mid` (since `linux-fast-dev` preset is enabled even at high thread
counts). The default can be overridden via the `CHRONON3D_BENCH_TASKSET`
environment variable.

## Governor policy (`cpu_governor: performance`)

CPU-Mid is the **cleanest environment** for governor testing:

- `intel_pstate` driver exposes the `performance` governor cleanly
  (no WARN, no degradation).
- `acpi-cpufreq` is acceptable but may require `sudo cpupower frequency-set`.
- AMD Zen 3/4 with `amd_pstate` driver behaves identically to `intel_pstate`.

If the runner reports `[WARN] cpupower not available` on cpu-mid, this is
**strong evidence that the host is mis-configured** (env-block, not a
measurement issue). Surface this in the bench report, do not silently
continue (or do — but record the WARN in the JSON output).

## Why this class matters

CPU-Mid is the **ground truth** for Chronon3D performance: every other
class is a derivative. CPU-Low tests cost-per-minute; CPU-High tests scaling
ceiling; CPU-Mid is where regression gates run every day, where the team's
own workstation matches the published numbers, and where bug repros are
**most likely** to land within an hour of being filed.

## Tooling conventions

- **Pre-flight**: `tools/benchmark_host_info.sh --yaml` first; verify
  `cpu_governor: performance` is reachable.
- **Runner**: `bash bench/run_perf_bench.sh --machine cpu-mid --scene <name>`.
- **Output**: `/tmp/bench/perf_cpu-mid_<sha>.json` plus stacked perf-stat logs.
- **Regression gate**: `bash tools/verify_performance_linux.sh` reuses this
  profile implicitly (the canonical PERF scenarios run on cpu-mid by default).
- **Cross-validation**: when adding new scenes to F1.1, run them on cpu-mid
  BEFORE other classes (low cost, fastest turnaround).

## Cross-link canonici

- [`configs/benchmark_machines.yaml`](../../configs/benchmark_machines.yaml) — YAML SSoT.
- [`tools/benchmark_host_info.sh`](../../tools/benchmark_host_info.sh) — host attribute collector.
- [`tools/verify_performance_linux.sh`](../../tools/verify_performance_linux.sh) — reuses this profile.
- [`examples/bench_corpus/run_corpus_v1.sh`](../../examples/bench_corpus/run_corpus_v1.sh) — F1.1 12-scene runner.
- [`bench/run_perf_bench.sh`](../run_perf_bench.sh) — perf-stat + warm-up wrapper.
- [`docs/PERFORMANCE_BOTTLENECKS.md`](../../docs/PERFORMANCE_BOTTLENECKS.md) — historical bottleneck analysis pinned to this class.
- [`docs/tickets/TICKET-BENCH-MACHINES-V1.md`](../../docs/tickets/TICKET-BENCH-MACHINES-V1.md) — cronaca.
- AGENTS.md §regole ("primary reference machine for regression gates").
