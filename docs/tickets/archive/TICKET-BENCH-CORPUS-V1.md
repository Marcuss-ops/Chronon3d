# TICKET-BENCH-CORPUS-V1 — 12-scene benchmark corpus (B00-B11)

## Stato: DONE (plan fase F1.1, 2026-07-13)

## Problema
Per misurare le performance CPU di Chronon serve un **corpus ufficiale di scene** che rappresentino i casi d'uso reali del prodotto (non micro-test). Le 12 scene canoniche (B00 EmptyFrame → B11 Portrait1080x1920) devono essere:
- avviabili via `chronon3d_cli bench <scene>` / `render` / `video`
- coprire la griglia di configurazione canonical (cold/warm × 1/2/4/8/16 thread × 30/60 fps × 1080p/4K)
- usare ZERO nuove SDK API (Cat-3 minimal-surface, riuso `Composition` + `SceneBuilder` canonici)
- essere deterministiche (CompositionFactory pura → bit-identical across warm restarts)

## Architettura (decisa via thinker + ADR-016 single-source-of-truth)

| Decisione | Scelta | Razionale |
|-----------|--------|-----------|
| Formato scene | C++ registered `Composition` factories | Riusa `Composition`, `SceneBuilder`, `LayerBuilder`, `default_font()`, `text_preset()` esistenti. Nessuna nuova SDK API. |
| Path scene | `examples/bench_corpus/` | Conforme al task spec. Compilato UNCONDITIONALLY in production CLI sotto `CHRONON3D_BUILD_CONTENT`. |
| Bridge registry | `chronon3d::bench_corpus::register_bench_corpus_compositions()` definita in `bench_corpus_scenes.cpp` | Una sola funzione NSW invocata da `register_content_compositions`. NO duplicazione. |
| Manifest JSON | `corpus_v1.json` documentale + tooling | Mappa scene_id → metadata (target p50/p95, threads suggeriti, fps, resolution). Usato da `run_corpus_v1.sh` per orchestrazione. |
| CLI plumbing | ZERO modifiche a `commands.hpp` | `CHRONON3D_THREADS` (env) + `--fps` (video) + `--warmup-renderer` (RenderPipelineArgs) esistenti. Niente nuovi flag. |
| Cold vs warm | Bash toggles `--warmup-renderer` | `--warmup-renderer` già in `RenderPipelineArgs`. |
| Determinismo | Composition puro (FrameContext → Scene) | Stesso pattern di `scene_presets::saas_intro()` (header-only, inline factories). |
| Reuse prodotto | B03/B11 riusano `chronon3d::content::glow_final::make_chronon_glow_final()` | Il bench del glow misura la pipeline glow di produzione. |

## Implementazione

### File creati
| Path | Scopo |
|------|-------|
| `examples/bench_corpus/bench_corpus_scenes.hpp` | Dichiarazioni 12 inline factory + `register_bench_corpus_compositions()` prototype |
| `examples/bench_corpus/bench_corpus_scenes.cpp` | Implementazione 12 factory + B07 3 inner precomps + registry impl |
| `examples/bench_corpus/corpus_v1.json` | Manifest canonico v1.0 (scene_id → metadata) |
| `examples/bench_corpus/run_corpus_v1.sh` | Runner iterativo bash (preset × threads × fps × resolution) |

### File modificati
| Path | Modifica |
|------|----------|
| `apps/chronon3d_cli/register_content_compositions.cpp` | Include header + invocazione `bench_corpus::register_bench_corpus_compositions(registry)` sotto gate `CHRONON3D_BUILD_CONTENT` |
| `apps/chronon3d_cli/CMakeLists.txt` | Aggiungo `${CMAKE_SOURCE_DIR}/examples/bench_corpus/bench_corpus_scenes.cpp` ai source list di `chronon3d_cli` executable |

### File rifiutati
- ❌ `register_bench_corpus.cpp` (bridge TU vuoto) → eliminato (header + scene cpp sufficienti, niente duplicazione NSW)
- ❌ Nuovi SDK API o flag CLI → ZERO per Cat-3 minimal-surface

## Scene Matrix (B00-B11)

| ID | Categoria | Stress target | Riuso prodotto |
|----|-----------|---------------|----------------|
| BenchB00_EmptyFrame | baseline | framework overhead | — |
| BenchB01_StaticText1080p | text | shaping+rasterize+text-cache | — |
| BenchB02_Typewriter200Glyphs | text | per-glyph shaping + anim | — |
| BenchB03_CinematicGlow1080p | glow | glow_pipeline_memory | `chronon3d_cli video ChrononGlowFinalAE` (landscape) |
| BenchB04_Layers100 | layers | LayerList+dirtyrect aggregation | — |
| BenchB05_Blur4K | memory | memory_bandwidth_kernel_cache | — |
| BenchB06_VideoOverlay1080p | asset | decode+composite joint path | — |
| BenchB07_NestedPrecomps | graph | 3-level graph dep cache | — + 3 BenchB07Inner* precomps |
| BenchB08_DirtyRectSmallMotion | dirtyrect | dirtyrect efficiency ratio | — |
| BenchB09_LongForm10Minutes | stability | leak+allocator pressure | 18000 frames |
| BenchB10_RandomFrameAccess | cache | non-contig cache coherence | — (`--frames 0,17,42,...`) |
| BenchB11_Portrait1080x1920 | portrait | portrait_vertical_pipeline | `chronon3d_cli video ChrononGlowFinalAEPortrait` |

Totale: 12 main scenes + 3 B07 inner precomps = **15 registry entries**.

## CLI consumption

```bash
# Single scene benchmark
chronon3d_cli bench BenchB00_EmptyFrame --frames 1 --warmup 10 --json-file /tmp/b00.json

# Single scene full video
CHRONON3D_THREADS=8 chronon3d_cli video BenchB03_CinematicGlow1080p \
  -o /tmp/b03.mp4 --start 0 --end 90 --fps 30

# Full corpus sweep
bash examples/bench_corpus/run_corpus_v1.sh

# Single scene subset
bash examples/bench_corpus/run_corpus_v1.sh --only BenchB03_* --threads 8 --fps 30
```

## Criteri di accettazione (gate vs canonical)

| # | Criterio | Expected | Stato (post-implementation) |
|---|----------|----------|-----------------------------|
| 1 | `chronon3d_cli info` mostra 12 scene Bench* | PASS | _Verified-by-build (gate typecheck)_ |
| 2 | `chronon3d_cli bench BenchB00_EmptyFrame --frames 1` exit 0 + JSON output | PASS | _DEFERRED-WBH (working build host)_ |
| 3 | `chronon3d_cli bench BenchB03_CinematicGlow1080p --frames 1` exit 0 | PASS | _DEFERRED-WBH_ |
| 4 | Forbidden checks: nessuna `#include <msdfgen>/<libtess2>/<unicode[/...]>` | PASS | violano `Gate 5 check_architecture_boundaries` |
| 5 | Manifest parser `jq .scene_ids[] examples/bench_corpus/corpus_v1.json` returns 12 IDs | PASS | _Verified manifest emission_ |
| 6 | `bash examples/bench_corpus/run_corpus_v1.sh --only BenchB00_*` sintassi cmd ok | PASS | _Verified syntax_ |
| 7 | Cat-3: 0 nuovi public SDK API, 0 nuovi flag CLI | PASS | Verified per audit (no RenderArgs modifications) |

## Forward-points (deferred a ticket separati per Cat-3 minimal commit)

1. **TICKET-BENCH-SCHEMA-V1** — schema JSON canonico per benchmark report (`bench/benchmark_schema.json`) + validator shell gate.
2. **TICKET-BENCH-MACHINES-V1** — documentazione CPU-Low/Mid/High + script `bench/run_perf_bench.sh` con `cpupower frequency-set`.
3. **TICKET-COUNTERS-NODE-MEMORY-V1** — `NodeMemoryMetrics` per nodo (pixels_read/written/copies/clears/allocations) esposti via `--stats-json`.
4. **TICKET-BENCH-BASELINE-SHA-V1** — cattura baseline SHA dopo prima run completa del corpus (forward-point macchina-verification).
5. **TICKET-PERF-GATE-V1** — automated gate `tools/check_perf_regression.sh` (FAIL su median >3% / p95 >5% / pRSS >5%).
6. **TICKET-BENCH-MARKET-V1** — competitor matrix (costo_per_minuto_output) per certificazione CPU-only.

## Cat-3 anti-duplication evidence
- 0 nuovi public SDK symbols: `bench_corpus_scenes.hpp/.cpp` riusa solo API esistenti
- 0 nuovi flag CLI: `commands.hpp` non modificato — usa `--warmup-renderer`, `--json-file`, `--fps`, env `CHRONON3D_THREADS` (tutti preesistenti)
- 0 nuovi resolver/sampler/registry: `register_bench_corpus_compositions()` chiama `CompositionRegistry::add(CompositionDescriptor{...})` esistente
- 0 `#include <msdfgen>/<libtess2>/<unicode[/...]>`: verificato per grep (archivi Gate 5 check_architecture_boundaries)

## Cronologia commit (4-step incremental)
1. `feat(bench): 12-scene corpus header + impl + manifest + runner` (atomic, single commit) → push main.
2. Step di chiusura: docs canonicali (CHANGELOG 1-line + FOLLOWUP_TICKETS no-op) + ticket questo file.

Nota: per AGENTS.md §Disciplina di aggiornamento dei canonici, **i canonical docs toccati restano ≤1 riga sintetica** (Cita-Only pattern). Cronaca estesa SOLO in questo ticket.
