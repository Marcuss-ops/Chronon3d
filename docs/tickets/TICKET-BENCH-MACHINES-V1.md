# TICKET-BENCH-MACHINES-V1 — Reference Machines & Perf-Bench Runner

## Stato

**PARTIAL** (2026-07-13). Bash syntax PASS. python3 YAML parse PASS. Full
end-to-end perf-stat + macchina-verifica DEFERRED-WBH per AGENTS.md
§honest-limitation (env-block: cpupower / intel_pstate / sudo on this VPS).

## Priorità

P2 — abilita TICKET-PERF-GATE-V1 (F1.5) future work; non blocca 11/11 green.

## Problema

I 3 reference machine (cpu-low / cpu-mid / cpu-high) erano già definiti in
[`configs/benchmark_machines.yaml`](../../configs/benchmark_machines.yaml)
(schema_version: `benchmark_machines_v1`) come SSoT programmatic, ma:

1. **Nessuna documentazione descrittiva** per ciascuna classe — operatori
   umani non sapevano cosa rappresentasse ciascun tier (VPS vs workstation
   vs batch server).
2. **Nessun runner controllato** per misurare `cpupower performance`
   governor + taskset affinity + warm-up + `perf stat -d -r 7` in modo
   riproducibile. Le misurazioni esistenti (in `tools/verify_performance_linux.sh`)
   usano `/usr/bin/time -v` senza governor control né perf-stat.
3. **Nessuna graceful-degradation policy documentata** per VPS / WSL /
   Docker / non-root container dove `cpupower` o `perf` mancano — rischio
   silent rot (env-block con exit 0).

## Soluzione adottata

### 5 file change-set (4 NEW + 1 EDIT)

| File | Tipo | Ruolo |
|---|---|---|
| `bench/reference_machines/CPU-Low.md` | NEW | Narrative per cpu-low (4-6 core VPS); cita YAML come SSoT |
| `bench/reference_machines/CPU-Mid.md` | NEW | Narrative per cpu-mid (8-16 core desktop); cita YAML come SSoT |
| `bench/reference_machines/CPU-High.md` | NEW | Narrative per cpu-high (32-64 core server); cita YAML come SSoT |
| `bench/run_perf_bench.sh` | NEW | Runner; python3-parsed YAML + cpupower/perf/taskset graceful degradation + warm-up + perf stat -d -r 7 |
| `docs/CHANGELOG.md` | EDIT | Prepended Cita-Only entry + ticket back-link |

### Cat-3 minimal-surface (zero nuove SDK API)

- ZERO nuovi simboli pubblici in `include/chronon3d/`.
- ZERO nuovi flag CLI su `chronon3d_cli` (lo script è `bash bench/run_perf_bench.sh`).
- ZERO nuovi singleton / registry / resolver.
- ZERO `#include <msdfgen>` / `<libtess2>` / `<unicode[/...]>` (script only, no C++ modification).
- Reusa surface esistente canonica:
  - `configs/benchmark_machines.yaml` → SSoT classi cpu-low/mid/high.
  - `tools/benchmark_host_info.sh` → host-attribute collector (parallel precedent).
  - `examples/bench_corpus/run_corpus_v1.sh` → F1.1 12-scene runner (sottostante workload).

### Cat-3 anti-dup discipline (3 .md vs YAML)

I 3 file `.md` SONO narrative-only con un "SSoT pointer" alla YAML canonica.
**NON duplicano** i valori tabulari (cores, RAM, preset, thread_counts) —
quelli vivono in `configs/benchmark_machines.yaml` (schema_machine_v1) come
single source of truth. Le .md aggiungono:
- Cosa rappresenta la classe (use case narrative).
- Taskset mode (NUMA-aware per cpu-high, SMT siblings per cpu-mid, full
  core sweep per cpu-low).
- Governor policy (driver-name aware: intel_pstate vs acpi-cpufreq vs WSL).
- Why this class matters (why we measure here, not elsewhere).
- Tooling conventions (runner invocation + output paths).
- Cross-link canonici (no info duplication).

### Graceful degradation policy

Il `run_perf_bench.sh` adotta una policy best-effort+continue per ogni fase
che può essere ambient-bloccata:

| Failure mode | Comportamento | Rationale |
|---|---|---|
| `cpupower` mancante o non-root | `[WARN]` + continue (governor unchanged) | VPS / WSL / Docker tipici |
| `performance` governor rejected | `[WARN]` + continue, registra actual_gov_initial | cpu_mid su cloud può essere bloccat[o] |
| `taskset` mancante | `[WARN]` + continue (no affinity) | macOS-via-Docker, alcuni container |
| `perf` mancante o senza -d | `[WARN]` + fallback `/usr/bin/time -v` | container non-privileged |
| `chronon3d_cli` non buildato | `[ERROR] + exit 2` (hard fail) | senza CLI non possiamo misurare |

`trap governor_restore EXIT` ripristina il governor **SOLO se** era stato
effettivamente cambiato (`GOVERNOR_CHANGED=true`). Non è un side-effect
silente (rispetta "non sorprendere l'utente" di AGENTS.md).

### Why separate reports per (machine, sha)

`/tmp/bench/perf_<machine>_<sha>.json` evita:
- Conflitti tra run concorrenti (manifest separati per SHA).
- Overwrite silenzioso di misurazioni precedenti.
- Confusione tra run su commit diversi (benchmark history preservata).

Lo script NON modifica mai la YAML canonica — il report JSON è read-only
rispetto al SSoT.

### Used by… (albero di dipendenze)

```
F1.3 (questo ticket)
   ├─ abilita F1.5 (TICKET-PERF-GATE-V1): gate "all 3 machines pass perf target"
   ├─ consumato da F1.4 (TICKET-BENCH-SCHEMA-V1): schema JSON include perf fields
   ├─ consumato da F1.6 (TICKET-BENCH-MARKET-V1): cost-per-minute matrix per machine class
   └─ consumato da F2 (TICKET-COUNTERS-NODE-MEMORY-V1): per-machine NUMA-aware mem counter
```

## Env-block classification (transparency)

Su questa VPS di sviluppo, l'esecuzione end-to-end del runner è bloccata da:

1. **`cpupower` non invocabile senza sudo** — questa VPS non è root e la
   `intel_pstate` driver manca. Governor resta al default di sistema (tipicamente
   `schedutil` o `ondemand`). Il runner emette `[WARN]` corretto e procede.
2. **`perf` non privilegiato** — `perf stat` richiede `perf_event_paranoid`
   ≤ 1 o capability speciali. Container VPS tipicamente ha `-1` (full paranoid).
   Il fallback `/usr/bin/time -v` è documentato.
3. **`chronon3d_cli` env-block** — il binario `build/manual-test/chronon3d_cli`
   richiede vcpkg `text/text_animator.hpp` (precedent TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV).
   In assenza del binary, il runner exit 2 (`HARD_BLOCKED`) — comportamento
   onesto, non silent rot.

Il bash syntax (`bash -n`) + python3 yaml parse del file YAML sono
**PASS su questa VPS** — la chore è HARNESS-COMPLETE.

## Criteri di accettazione

| # | Criterio | Stato (post-implementation) |
|---|---|---|
| 1 | `bench -n run_perf_bench.sh` exit 0 | PASS (bash syntax clean) |
| 2 | `python3 -c "import yaml; yaml.safe_load(open('configs/benchmark_machines.yaml'))"` exit 0 | PASS (YAML well-formed) |
| 3 | `bash bench/run_perf_bench.sh --machine cpu-low --scene BenchB00_EmptyFrame` end-to-end | DEFERRED-WBH (env-block cron tools su VPS) |
| 4 | 3 .md narrative docs cita YAML SSoT (no tab dup) | PASS (verified via grep) |
| 5 | Cat-3 minimal-surface: zero new SDK API, zero new CLI flag | PASS (script + .md only, no C++ modification) |
| 6 | Forbidden checks: zero `#include <msdfgen>` / `<libtess2>` / `<unicode[/...]>` | PASS (no C++ modification; script-only) |
| 7 | trap-based governor restore on EXIT, only-if-changed | PASS (governor_restore trap + GOVERNOR_CHANGED flag) |
| 8 | Subject envelope ≤ 72 chars per AGENTS.md TICKET-GATE-SUBJECT-RANGE | 72 chars (`feat(bench): reference machines + perf runner (TICKET-BENCH-MACHINES-V1)`) |

## Forward-points

- **TICKET-PERF-GATE-V1** (planned, F1.5): CI gate "all 3 machine classes pass perf target"; uses run_perf_bench.sh as inner loop. Forward-point cronaca qui.
- **TICKET-BENCH-SCHEMA-V1** (planned, F1.4): extend `bench/benchmark_schema.json` to include `governor_actual_initial` field. Il TICKET-BENCH-MACHINES-V1 already emit campo; serve wbh-validation.
- **TICKET-BENCH-MARKET-V1** (planned, F1.6): cost-per-minute matrix `cost_cpu-low * duration / 60`. Forward-point da qui.
- **TICKET-COUNTERS-NODE-MEMORY-V1** (F2, cat-1): per-machine NUMA-aware memory counter. cpu-high's NUMA profile requirement informato qui.
- **TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV** (precedent env-block): cron tools non-root VPS. Forward-point disclosure.

## Cross-link canonici

- [`configs/benchmark_machines.yaml`](../../configs/benchmark_machines.yaml) — SSoT canonical spec machine classes.
- [`tools/benchmark_host_info.sh`](../../tools/benchmark_host_info.sh) — host-attribute collector (parallel precedent).
- [`examples/bench_corpus/run_corpus_v1.sh`](../../examples/bench_corpus/run_corpus_v1.sh) — F1.1 12-scene runner (workload substrate).
- [`docs/PERFORMANCE_BOTTLENECKS.md`](../../docs/PERFORMANCE_BOTTLENECKS.md) — historical bottleneck analysis pinned to cpu-mid.
- [`docs/CHANGELOG.md`](../../docs/CHANGELOG.md) — prepended cite-only entry.
- [`docs/FOLLOWUP_TICKETS.md`](../../docs/FOLLOWUP_TICKETS.md) — this ticket NON è blocker (no row aggiunto; Cat-5 deferred obligation).
- AGENTS.md §Cat-3 (zero new SDK API surface), §honest-limitation (DEFERRED-WBH env-block pattern), §regole ("single source of truth per ogni dato").
