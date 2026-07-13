# Competitor Matrix — Chronon3D vs After Effects + Remotion + Pipeline Precedente

> **SCAFFOLD-ONLY · FASE 7.1 (TICKET-BENCH-MARKET-V1)** — zero-placeholders for all
> commercial metrics.  Real measurements forward-pointed to
> [`TICKET-BENCH-MARKET-V1-MEASURE`](docs/tickets/TICKET-BENCH-MARKET-V1.md#forward-points)
> per the established `DEFERRED-WBH` pattern (this VPS lacks vcpkg glm/magic_enum
> bootstrap; `cmake --build` end-to-end is unavailable locally).

## Scope

Single source of truth for Chronon3D's commercial positioning on
`costo_per_minuto_output`.  Scaffolds a per-scene cost/ram comparison between
**Chronon3D** and **3 commercial competitors** (A / B / C) across Chronon3D's
**12-scene strategic corpus** (B00–B11) on the canonical **CPU-Low** reference
machine (`configs/benchmark_machines.yaml`).

> User spec verbatim: *"stessa CPU, stesso numero thread, stessa risoluzione
> (NO Chronon 4K vs concorrente 1080p), stesso codec, stesso contenuto, stessa
> durata, warm/cold comparabili"*

## Methodology — uniform comparison conditions

For EVERY (Chronon3D | competitor × scene) combination, the SAME baseline applies:

| Variable | Setting | Source |
|---|---|---|
| CPU class | `cpu-low` | `configs/benchmark_machines.yaml` (4-core VPS economical) |
| Threads | `4` | `thread_counts.cpu-low[2]` (max) |
| Resolution | `1920×1080` (1080p) | `configs/benchmark_machines.yaml.resolutions[0]` |
| Codec | `libx264` CPU-only | `VideoCodec::H264` (no NVENC/QSV/AMF hardware) |
| Container | MP4 | `VideoContainer::Mp4` |
| Content | 12-scene corpus B00–B11 | `examples/bench_corpus/corpus_v1.json` |
| Duration | per-scene `duration_seconds` | corpus_v1.json (typical: 60s) |
| Warmup policy | 3 warm runs + discard 1st frame + median of last 2 | `configs/benchmark_machines.yaml.common.warmup` |
| Cold measurement | NOT measured (warm-cache is canonical comparison) | `common.warmup.discard_first_frame: true` |
| OS / Kernel / Compiler | Ubuntu 22.04 LTS / 5.15+ / Clang 16 (or GCC 13) | `configs/benchmark_machines.yaml.machines[0]` |
| CMake preset | `linux-fast-dev` | `configs/benchmark_machines.yaml` |
| CPU governor | `performance` | `configs/benchmark_machines.yaml` |

**Forbidden mismatches** (per user spec):
- Chronon at 4K vs competitor at 1080p
- GPU codecs (NVENC/QSV/AMF) — Chronon is CPU-only
- Inconsistent thread counts (always `4`)
- Mixed warm/cold (warmup policy is mandatory)

## Cost formula (canonical, unit-correct)

```
costo_per_minuto_output =
    costo_orario_macchina × secondi_render / 3600 / minuti_generati
```

Unit-check: `(€/h) × (s) / (3600 s/h) / (min)` simplifies to `€/min_output`. ✅

**Pricing model** = on-demand cloud VPS pricing (AWS c5.xlarge / Hetzner standard /
equivalent), `costo_orario_macchina ≈ €0.04/h`.  Spot pricing is **deliberately
excluded** per volatility concerns (a reproducible rate is critical for the
`Pubblica report finale` gate).

**Example (B06 VideoOverlay1080p, 60s output, 30s wallclock, cpu-low)**:

```text
costo_orario_macchina  = €0.04 / h
secondi_render         = 30 s
minuti_generati        = 60 s / 60 s/min = 1 min

costo_per_minuto_output = 0.04 × 30 / 3600 / 1
                        = €0.000333 / min_output
```

(Real data DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`.)

## Competitors A / B / C

| Letter | Product | Class | Rationale |
|---|---|---|---|
| **A** | After Effects (Adobe, industry-standard) | Visual compositor | Head-to-head that commercial positioning cares about (AE parity tests + `docs/FEATURES.md`) |
| **B** | Remotion v4 (open-source, MIT) | Cloud-native programmatic video | Open-source React-based competitor per `docs/product-tests/TEST-17-COMPARISON.md` |
| **C** | Pipeline Precedente (ad-hoc shell + FFmpeg + manuale AE import/export) | In-house pre-Chronon baseline | Regression-prevention baseline per `docs/product-tests/TEST-17-COMPARISON.md` row 1 |

## 12-scene strategic corpus grid (template)

For each scene (`BenchB00_EmptyFrame` … `BenchB11_Portrait1080x1920`) per
`examples/bench_corpus/corpus_v1.json`, the matrix records:

| Scene | ID | Description | Dur (s) | Threads | Resolution | Codec |
|---|---|---|---|---|---|---|
| B00 | `BenchB00_EmptyFrame` | Smoketest baseline (min CPU) | 60 | 4 | 1920×1080 | libx264 |
| B01 | `BenchB01_StaticText1080p` | Pure static text | 60 | 4 | 1920×1080 | libx264 |
| B02 | `BenchB02_Typewriter200Glyphs` | Typewriter 200 glyphs | 60 | 4 | 1920×1080 | libx264 |
| B03 | `BenchB03_CinematicGlow1080p` | 5-level cinematic glow | 60 | 4 | 1920×1080 | libx264 |
| B04 | `BenchB04_Layers100` | 100 layer stacking | 60 | 4 | 1920×1080 | libx264 |
| B05 | `BenchB05_Blur4K` | 4K blur stress (NB: skipped at 1080p only) | 60 | 4 | 1920×1080 | libx264 |
| B06 | `BenchB06_VideoOverlay1080p` | Decode + composite + encode | 60 | 4 | 1920×1080 | libx264 |
| B07 | `BenchB07_NestedPrecomps` | Nested pre-comps | 60 | 4 | 1920×1080 | libx264 |
| B08 | `BenchB08_DirtyRectSmallMotion` | Dirty rectangle + small motion | 60 | 4 | 1920×1080 | libx264 |
| B09 | `BenchB09_LongForm10Min` | Commercial 10-min render | 600 | 4 | 1920×1080 | libx264 |
| B10 | `BenchB10_RandomFrameAccess` | Random frame access | 60 | 4 | 1920×1080 | libx264 |
| B11 | `BenchB11_Portrait1080x1920` | Vertical 9:16 | 60 | 4 | 1080×1920 | libx264 |

> Per-scene schemas live at `configs/benchmarks/corpus/b00..b11.yaml` (each
> already carries a `cost_per_minute_eur` metric field — the per-scene
> commercial anchor for this matrix).

## Result template (12 scenes × 4 columns)

`[DEFERRED-WBH]` markers indicate rows awaiting real data from
`TICKET-BENCH-MARKET-V1-MEASURE` (working build host macchina-verifica).

| Scene | Chronon costo/min | A=AE costo/min | B=Remotion costo/min | C=Pipeline costo/min | Chronon Peak RAM (MB) | RAM ≤110% × A | Win? |
|---|---|---|---|---|---|---|---|
| B00 EmptyFrame       | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | n/a |
| B01 StaticText1080p  | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | n/a |
| B02 Typewriter200    | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | n/a |
| B03 CinematicGlow1080p | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | n/a |
| B04 Layers100        | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | n/a |
| B05 Blur4K (1080p)   | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | n/a |
| B06 VideoOverlay1080p | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | n/a |
| B07 NestedPrecomps   | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | n/a |
| B08 DirtyRectSmallMotion | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | n/a |
| B09 LongForm10Min    | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | n/a |
| B10 RandomFrameAccess | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | n/a |
| B11 Portrait1080x1920 | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | [DEFERRED-WBH] | n/a |

**Per-scene Win criterion** (Chronon3D row): wins iff
`Chronon.costo_per_minuto_output < competitor.costo_per_minuto_output` **AND**
`Chronon.peak_ram ≤ 1.10 × competitor.peak_ram` (joint gate on cost AND RAM).

## 4 commercial gates (acceptance envelope)

Per user spec verbatim *"vincere ≥8 scene su 12 nel corpus strategico,
geometric mean speedup ≥1.25x, Peak RAM non superiore di >10%, risultati
riproducibili da terzi"*:

| # | Gate | Threshold | Pass criterion | Forward-point |
|---|---|---|---|---|
| 1 | Per-scene wins | ≥ 8 / 12 wins | row-level Win = `cost < competitor cost ∧ RAM ≤ 110% × competitor RAM` | `TICKET-BENCH-MARKET-V1-MEASURE` |
| 2 | Geometric mean speedup | ≥ 1.25× | `geomean(competitor.frame_p50 / chronon.frame_p50) ≥ 1.25` | `TICKET-BENCH-MARKET-V1-MEASURE` |
| 3 | Peak RAM envelope | ≤ 110% × competitor | `max(chronon.peak_ram) / max(competitor.peak_ram) ≤ 1.10` | `TICKET-BENCH-MARKET-V1-MEASURE` |
| 4 | Third-party reproducibility | 100% of scenes | external user runs published CLI commands on standard `cpu-low` hardware achieved equal results within noise envelope | `TICKET-BENCH-MARKET-V1-MEASURE` |

## Reproducibility protocol (third-party — gate 4)

A third party with no Chronon3D source access reproduces the matrix by:

### Chronon3D row

```bash
git clone https://github.com/<owner>/Chronon3d.git && cd Chronon3d
cmake --preset linux-fast-dev && cmake --build --preset linux-fast-dev -j4

# Per-scene (canonical recipe, e.g., B03):
THREADS=4; WARMUP_FRAMES=10  # discard_first_frame per common.warmup
/usr/bin/time -v ./build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli \
    video BenchB03_CinematicGlow1080p \
    --output /tmp/b03.mp4 --fps 30 --codec libx264 \
    --threads $THREADS
# wallclock_s = (real seconds from /usr/bin/time -v); peak_rss_mb stderr column.
# costo_per_minuto_output = 0.04 × wallclock_s / 3600 / (duration_seconds/60)
```

### Competitor A (After Effects)

Adobe Media Encoder or AE-extendscript pipeline exports the same composition to
ffmpeg-compatible MP4 ≈ `aecmdcli -project <name> -output <path> -codec libx264`
(Adobe CLI specifics tracked in `TICKET-BENCH-MARKET-V1-AE-RUNNER`).

### Competitor B (Remotion v4)

```bash
npx remotion render BenchB03_CinematicGlow1080p \
    --concurrency 4 --codec libx264 --output /tmp/remotion_b03.mp4
```

### Competitor C (Pipeline Precedente)

Ad-hoc bash pipeline: shell render → PNG sequence → ffmpeg encode libx264.  Exact
recipe inside `TICKET-BENCH-MARKET-V1-PIPELINE-RUNNER`.

## Per-scene cost-per-minute (existing per-corpus anchors)

Each `configs/benchmarks/corpus/b0X_<scene>.yaml` already publishes a
`cost_per_minute_eur` metric field — these are the per-scene Chronon3D
"ground-truth" anchors the matrix cells fill in.  Spot-check:

- `b06_video_overlay_1080p.yaml`: `"# primary commercial metric on this scene"`
- `b09_long_form_10min.yaml`: `"# commercial: 10min @ VPS rate"`
- All others: `"# cost_per_minute_eur"`

## Cross-references

| Resource | Path |
|---|---|
| Canonical machine spec | [`configs/benchmark_machines.yaml`](../configs/benchmark_machines.yaml) (cpu-low class + thread_counts + common.warmup) |
| 12-scene manifest | [`examples/bench_corpus/corpus_v1.json`](../examples/bench_corpus/corpus_v1.json) |
| Per-scene schematic | [`configs/benchmarks/corpus/b00..b11.yaml`](../configs/benchmarks/corpus/) |
| Output schema | [`bench/benchmark_schema.json`](benchmark_schema.json) (chronon3d.bench.v3 — 16 required flat fields) |
| Existing precedent | [`docs/product-tests/TEST-17-COMPARISON.md`](../docs/product-tests/TEST-17-COMPARISON.md) (8-metric Chronon3D vs Pipeline Precedente vs Remotion; matrix formalizes + extends) |
| Perf-baseline | [`docs/baselines/main-131119fe-baseline.md`](../docs/baselines/main-131119fe-baseline.md) (FASE 1.5 zero-placeholder, DEFERRED-WBH) |

## §honesty closure

This matrix is **SCAFFOLD-ONLY** with deliberate `[DEFERRED-WBH]` placeholders for
every commercial metric row.  Per `AGENTS.md v0.1 §honesty` discipline (zero inaccuracy
of state on disk), the cells are NOT pre-filled with guessed numbers — they are
explicitly empty (`[DEFERRED-WBH]` is a semantic row marker, not a number).

Real measurement requires working build host (`cmake --build` + `cmake --preset
linux-fast-dev` + `chronon3d_cli` binary linking).  Each `[DEFERRED-WBH]` cell is
backed by ticket `TICKET-BENCH-MARKET-V1-MEASURE` in
[`docs/tickets/TICKET-BENCH-MARKET-V1.md`'s forward-points section](../docs/tickets/TICKET-BENCH-MARKET-V1.md#forward-points).

Cross-link: `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` precedent + the established
`DEFERRED-WBH` pattern documented in `AGENTS.md v0.1`.

## Versioning

| Version | Date | SHA | Notes |
|---|---|---|---|
| 1.0 (scaffold) | 2026-07-13 | `<commit-after-this-pr>` | First-author scaffold per FASE 7.1; `[DEFERRED-WBH]` placeholders; real data forward-pointed |
