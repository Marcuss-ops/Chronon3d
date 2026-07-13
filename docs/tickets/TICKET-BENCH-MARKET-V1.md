# TICKET-BENCH-MARKET-V1 — Competitor matrix scaffold + 4 commercial gates

## Stato: OPEN-APERTURA (2026-07-13, FASE 7.1 first-commit scaffold)

## Scope (per user spec verbatim)

> *"FASE 7.1 — Crea matrice competitor per `costo_per_minuto_output`: documento
> `bench/competitor_matrix.md` con tabella comparativa (Chronon vs concorrenti
> A/B/C) su: stessa CPU, stesso numero thread, stessa risoluzione (NO Chronon
> 4K vs concorrente 1080p), stesso codec, stesso contenuto, stessa durata,
> warm/cold comparabili. Misura `costo_per_minuto_output = costo_orario_macchina
> × secondi_render / 3600 / minuti_generati`. Gate commerciale: vincere ≥8 scene
> su 12 nel corpus strategico, geometric mean speedup ≥1.25x, Peak RAM non
> superiore di >10%, risultati riproducibili da terzi. Pubblica report finale
> dopo che tutte le fasi 1-6 sono PASS. Ticket TICKET-BENCH-MARKET-V1;
> commit+push."*

This ticket SCAFFOLDS the per-scene cost-per-minute comparison between
Chronon3D and 3 commercial competitors (A / B / C).  The matrix is
**SCAFFOLD-ONLY** with deliberate `[DEFERRED-WBH]` placeholders for every
commercial metric row, per `AGENTS.md v0.1 §honesty` discipline.

## Uniform comparison conditions (methodology)

Per user spec verbatim *"stessa CPU, stesso numero thread, stessa risoluzione
(NO Chronon 4K vs concorrente 1080p), stesso codec, stesso contenuto, stessa
durata, warm/cold comparabili"*:

| Variable | Setting | Canonical reference |
|---|---|---|
| CPU class | `cpu-low` | `configs/benchmark_machines.yaml` (4-core VPS economical) |
| Threads | `4` (cpu-low thread_counts[2]) | `configs/benchmark_machines.yaml.thread_counts.cpu-low` |
| Resolution | `1920×1080` (1080p) — NOT 4K vs 1080p mismatches | `configs/benchmark_machines.yaml.common.resolutions[0]` |
| Codec | `libx264` (CPU-only, NO NVENC/QSV/AMF) | `VideoCodec::H264` |
| Container | MP4 | `VideoContainer::Mp4` |
| Content | 12-scene corpus B00–B11 | `examples/bench_corpus/corpus_v1.json` |
| Duration | per-scene `duration_seconds` from corpus_v1.json (typical 60s, B09 = 600s) | corpus_v1.json |
| Warmup | 3 warm runs + discard 1st frame + median of last 2 | `configs/benchmark_machines.yaml.common.warmup` |
| OS / Kernel / Compiler | Ubuntu 22.04 LTS / 5.15+ / Clang 16 (or GCC 13) | `configs/benchmark_machines.yaml.machines[0]` |
| CMake preset | `linux-fast-dev` | `configs/benchmark_machines.yaml.machines[0]` |
| CPU governor | `performance` | `configs/benchmark_machines.yaml.machines[0]` |
| Cold measurement | NOT measured (warm-cache comparison is canonical) | `discard_first_frame: true` |

**Forbidden mismatches**: 4K-vs-1080p, GPU codecs (Chronon is CPU-only),
inconsistent thread counts, mixed warm/cold.

## Cost formula (canonical, unit-correct)

```
costo_per_minuto_output =
    costo_orario_macchina × secondi_render / 3600 / minuti_generati
```

Unit-check: `(€/h) × (s) / (3600 s/h) / (min)` simplifies to `€/min_output`. ✅

`costo_orario_macchina ≈ €0.04/h` (on-demand cloud VPS pricing — AWS c5.xlarge
Linux, Hetzner standard, or equivalent).  **Spot pricing deliberately excluded**
per volatility (a reproducible rate is critical for `Pubblica report finale`).

## Competitors A / B / C

| Letter | Product | Class | Rationale |
|---|---|---|---|
| **A** | After Effects (Adobe, industry-standard) | Visual compositor | Head-to-head that commercial positioning cares about (AE parity tests + `docs/FEATURES.md` "Backend software CPU-first") |
| **B** | Remotion v4 (open-source MIT) | Cloud-native programmatic video | Open-source React-based competitor per `docs/product-tests/TEST-17-COMPARISON.md` |
| **C** | Pipeline Precedente (ad-hoc shell + FFmpeg + manuale AE import/export) | In-house pre-Chronon baseline | Regression-prevention baseline per `docs/product-tests/TEST-17-COMPARISON.md` row 1 |

## 4 commercial gates

Per user spec verbatim *"vincere ≥8 scene su 12 nel corpus strategico,
geometric mean speedup ≥1.25x, Peak RAM non superiore di >10%, risultati
riproducibili da terzi"*:

| # | Gate | Threshold | Pass criterion |
|---|---|---|---|
| 1 | Per-scene wins | ≥ 8 / 12 | row-level Win = `Chronon.costo_per_min < competitor.costo_per_min ∧ Chronon.peak_ram ≤ 1.10 × competitor.peak_ram` |
| 2 | Geometric mean speedup | ≥ 1.25× | `geomean(competitor.frame_p50 / chronon.frame_p50) ≥ 1.25` |
| 3 | Peak RAM envelope | ≤ 110% × competitor | `max(chronon.peak_ram) / max(competitor.peak_ram) ≤ 1.10` |
| 4 | Third-party reproducibility | 100% of scenes | external user runs published CLI commands on standard `cpu-low` hardware → equal results within noise envelope |

## Criteri di accettazione (8)

1. **Matrix scaffold exists** as `bench/competitor_matrix.md` (NEW file).
2. **12-scene grid template** (B00–B11) with `[DEFERRED-WBH]` placeholder rows × competitor columns + Win column.
3. **Cost formula correct unit-check** + worked example calculation on B06 VideoOverlay1080p.
4. **Competitor identity explicit** (A=After Effects, B=Remotion v4, C=Pipeline Precedente) with rationale per row.
5. **4 commercial gates documented** as an acceptance envelope with Pass-criterion formulas.
6. **Cross-refs to canonical sources**: `configs/benchmark_machines.yaml` + `examples/bench_corpus/corpus_v1.json` + `bench/benchmark_schema.json` + `docs/product-tests/TEST-17-COMPARISON.md`.
7. **Reproducibility CLI commands** cited verbatim from `chronon3d_cli video` + ffmpeg-canonical-invocations for B03 CinematicGlow1080p each competitor.
8. **§honesty closure**: zero commercial-metric cells pre-filled (all `[DEFERRED-WBH]`); no guessed numbers on disk; scaffold-only scope explicit at the top of the matrix.

## Forward-points

### (a) TICKET-BENCH-MARKET-V1-MEASURE

Working build host macchina-verifica session to populate the 12-scene grid
columns with real values from `chronon3d_cli bench` execution on CPU-Low
hardware.  This is the END-TO-END macchina-verifica scope.  Pre-requisite:
FASE 1.6 PERF_GATE PASS (forward-point already in `docs/CHANGELOG.md`).
Requires: `cmake --preset linux-fast-dev` + `cmake --build` + `chronon3d_cli`
binary linkable.  Default closure: when working build host unavailable, report
per-scene `[DEFERRED-WBH]` cells back to this ticket's scaffold (no false green).

### (b) TICKET-BENCH-MARKET-V1-AE-RUNNER

After Effects automation runner for competitor A.  Adobe CLI specifics + AE
extendscript per-scene pipeline to ffmpeg-compatible MP4 libx264.  Track to
Adobe Media Encoder CLI documentation.

### (c) TICKET-BENCH-MARKET-V1-REMOTION-RUNNER

Remotion v4 CLI runner for competitor B.  Reference:
`npx remotion render <comp> --concurrency 4 --codec libx264`.  Track to
`remotion.dev/docs/cli/benchmark`.

### (d) TICKET-BENCH-MARKET-V1-PIPELINE-RUNNER

Pipeline Precedente (in-house pre-Chronon baseline) runner for competitor C.
Ad-hoc bash + FFmpeg + manuale AE import/export — exact recipe tracked in
this sub-ticket.

### (e) Pubblica report finale dopo che tutte le fasi 1-6 sono PASS

Per user spec verbatim.  Current F1–F6 PASS state:
- FASE 1.1 corpus ✓ PASS (12-scene B00-B11, `TICKET-BENCH-CORPUS-V1`)
- FASE 1.2 schema ✓ PASS (`TICKET-BENCH-SCHEMA-V1`)
- FASE 1.5 perf-baseline ✓ PASS (zero-placeholder scaffold, DEFERRED-WBH per
  `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`)
- FASE 3.1 fusion pass ✓ PASS (`TICKET-FUSION-PASS-COMPILER-V1`)
- FASE 3.2 glow full-frame audit ✓ PASS (`TICKET-GLOW-FULLFRAME-AUDIT-V1`)
- FASE 4.2 cpu-budget-unified ✓ PASS (`TICKET-CPU-BUDGET-UNIFIED-V1`)
- FASE 4.3 kernel-tiling ✓ PASS (`TICKET-KERNEL-TILING-V1`)
- FASE 5.1 SIMD-registry ✓ PASS (`TICKET-SIMD-REGISTRY-V1`)
- FASE 5.2 AVX2-blend ✓ PASS (`TICKET-SIMD-VECTORIZE-KERNEL-SET-V1`)
- FASE 6.3 PGO+ThinLTO+BOLT ✓ PASS (`TICKET-BUILD-PGO-PIPELINE-V1`, landed locally in this session)
- F1.6 PERF_GATE — forward-point, not yet landed

Forward-point to `TICKET-BENCH-MARKET-V1-MEASURE` (a) which depends on F1.6 PERF_GATE
PASS.  This ticket (F7.1 scaffold) is unaffected.

## Cross-references

| Resource | Path |
|---|---|
| Canonical machine spec | [`../configs/benchmark_machines.yaml`](../configs/benchmark_machines.yaml) |
| 12-scene manifest | [`../examples/bench_corpus/corpus_v1.json`](../examples/bench_corpus/corpus_v1.json) |
| Per-scene schematic | [`../configs/benchmarks/corpus/b00..b11.yaml`](../configs/benchmarks/corpus/) |
| Output schema | [`../bench/benchmark_schema.json`](../bench/benchmark_schema.json) |
| Existing precedent | [`../docs/product-tests/TEST-17-COMPARISON.md`](../docs/product-tests/TEST-17-COMPARISON.md) (matrix formalizes + extends the 8-metric Chronon3D vs Pipeline Precedente vs Remotion table) |
| Perf-baseline | [`../docs/baselines/main-131119fe-baseline.md`](../docs/baselines/main-131119fe-baseline.md) |
| Scaffold deliverable | [`../bench/competitor_matrix.md`](../bench/competitor_matrix.md) |
| F1-F6 closed tickets | (each link in CHANGELOG.md; see forward-points above) |
| AGENTS.md §Workflow | [`../AGENTS.md`](../AGENTS.md) — Cat-3 minimal-surface + per-branch rebase invariant |

## §honesty closure

Matrix doc + canonical ticket contain **NO** guessed commercial metrics.  Every
commercial data row is explicitly `[DEFERRED-WBH]` with a forward-point ticket
DNA-link.  This implements `AGENTS.md v0.1 §honesty` discipline ("no inaccurate
state on disk") and parallels the existing `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`
precedent (`docs/baselines/main-131119fe-baseline.md`).

Per AGENTS.md, F7.1 first-commit scaffold lands:
- `bench/competitor_matrix.md` (NEW, scaffold)
- `docs/tickets/TICKET-BENCH-MARKET-V1.md` (NEW, canonical ticket)
- `docs/CHANGELOG.md` (EDIT: prepended CITA-only entry, subject + ticket link)

NO new SDK API, NO new public header, NO new CLI flag, NO new schema version.
Cat-3 minimal-surface verified.
