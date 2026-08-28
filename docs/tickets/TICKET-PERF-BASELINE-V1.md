# TICKET-PERF-BASELINE-V1 — Chronon Performance Baseline ufficiale (BENCH-1..5)

## Stato

**DONE** (2026-08-25). Baseline CPU (software backend) misurata e archiviata su
`main@eb871240`; gate `tools/check_perf_baseline.sh` PASS 10/10 sui budget
LOCKED. GPU-side budgets deliberatamente nulli: il backend Vulkan non renderizza
in questa sandbox (device visibile, render FAIL — env-block DEFERRED-WBH),
quindi le metriche GPU (gpu_execute_us, VMA, submissions) restano da misurare
su una macchina GPU funzionante.

## Priorità

P0 — Phase 1 del programma Performance & Smart Rendering: **performance truth
prima di ottimizzare**. Ogni futura modifica grande deve rispondere con numeri
prima/dopo contro questa baseline, non con "sembra più veloce".

## Problema

Chronon non aveva una baseline prestazionale ufficiale: esistevano il corpus
B00-B11 (TICKET-BENCH-CORPUS-V1), lo schema bench.v3, il runner
`run_perf_bench.sh` e il gate Mann-Whitney, ma mancavano:

1. le **5 composition canoniche** richieste (BENCH-1 Static / BENCH-2 Text /
   BENCH-3 Motion Graphics / BENCH-4 Video / BENCH-5 Heavy) — esisteva solo il
   corrispettivo di BENCH-4 (B06);
2. un report JSON **machine-readable completo** delle metriche GPU
   (submissions, uploads, readback, wait/execute us, VMA, physical surfaces);
3. un **runner di suite** che esegue le 5 benchmark, registra host + governor +
   CPU%/GPU% e produce suite JSON/TXT;
4. **regression budgets** LOCKED e un gate che li verifica a ogni commit.

## Soluzione adottata

### File change-set (5 NEW + 5 EDIT)

| File | Tipo | Ruolo |
|---|---|---|
| `bench/corpus/bench_corpus_scenes.{hpp,cpp}` | EDIT | +`bench_canon1_static`, `bench_canon2_text`, `bench_canon3_motion_graphics`, `bench_canon5_heavy` + registrazione; BENCH-4 = factory esistenta `bench_b06_video_overlay_1080p` (Cat-3 anti-dup, nessuna nuova comp) |
| `apps/chronon3d_cli/commands/basic/command_benchmark_saturation.cpp` | EDIT | +flag `--backend auto\|software\|vulkan`; +blocco JSON `gpu` (26 metriche per-frame delta + VMA assoluti) |
| `apps/chronon3d_cli/commands.hpp` + `commands/group_core.cpp` | EDIT | firma + flag `--backend` |
| `tests/bench_corpus/test_bench_corpus_scenes.cpp` | EDIT | +4 sane TEST_CASE (name/width/height/duration + shape-count a 3 frame) |
| `bench/perf_baseline_v1.json` | NEW | manifest canonico: 5 benchmark, composizioni, metric spec, **budget LOCKED** |
| `bench/run_perf_baseline.sh` | NEW | runner suite: saturation per bench (3 reps), host_info, suite JSON/TXT, GPU%/annex |
| `tools/check_perf_baseline.sh` | NEW | gate budget 3-exit (0=PASS, 1=FAIL, 2=BLOCKED); chiavi budget `*_max` ↔ metric suite |
| `bench/baselines/main-eb871240-perf-baseline.{json,txt}` | NEW | baseline archiviata (primo run onesto) |

## 2. Metriche registrate per benchmark

frame_ms (p50/p95/p99 + fps), gpu_execute_us, gpu_wait_cpu_us, uploads/frame,
upload_bytes/frame, readback_bytes/frame, vk submissions/frame, VMA allocs
after warmup, physical_surfaces_peak, vma_usage_bytes, CPU%/GPU% (annex host),
encode fps (sidecar `.timing.json` video export — assente nel run CPU).

## 3. Numeri baseline (prima run onesta — Intel Xeon E3-12xx v2, 24 core, software backend, Debug)

| Bench | p50 ms | p95 ms | fps | upload B/frame | note |
|---|---|---|---|---|---|
| BENCH-1 Static | 0.93 | 1.41 | 926 | 0 | bg+logo, zero anim |
| BENCH-2 Text | 2.56 | 3.35 | 303 | 98.6 KB | shaping+layout+fade |
| BENCH-3 Motion | 1.34 | 2.11 | 627 | 0 | 24 shapes+glow (blur escluso) |
| BENCH-4 Video | 0.88 | 1.39 | 925 | 21.5 KB | B06 (video surrogate) |
| BENCH-5 Heavy | 1.55 | 1.76 | 112 | 948 KB | video+text+effects |

Budget LOCKED nel manifest: p50/p95 × 1.4 (headroom), es. BENCH-2 p50 ≤ 3.6ms.

## 4. Decisioni abbinate

### 4a. Software-backend composite SEGV (blur) — followup necessario

`CompositeNode::execute → ensure_native_surface → Framebuffer::is_opaque()` su
framebuffer NULL quando un layer con **blur** viene composto in una scena
multi-layer con molti primitivi (24 dots + blur band): SIGSEGV su backend
software. B05 (blur su scena a 2 layer) rende senza problemi → trigger isolato
= blur + composito con N>2 layer animati.

- **Owner**: runtime team (software backend).
- **Reason**: baseline v1 non può includere blur in BENCH-3.
- **Exit condition**: `chronon3d_cli benchmark --scene <scena-con-blur-multi-layer>` non segfaulta; BENCH-3 può riabilitare `blur_band`.
- **Equivalence**: BENCH-3 con e senza blur_band, stessi budget.
- **Removal scope**: commento in bench_corpus_scenes.cpp + manifest note + questo paragrafo.
- **Status**: **RESOLVED** (2026-08-28). Il SEGV non riproduce più su `main@d134e214`: la propagazione dei fallimenti upstream attraverso i compositi (`d1475f2c`) elimina il dereference di framebuffer mancanti in `CompositeNode::execute`. Verifica su `linux-fast-dev`: `blur(32)` band + 24 dots animati renderizza still + sequenza 0-10 exit 0; saturation 5s con blur_band incluso → p50 1.52ms (budget 1.9) / p95 1.60ms (budget 3.0) PASS, 3243 frame, 0 crash. `blur_band` riabilitato in `bench_corpus_scenes.cpp` (BENCH-3) e nota manifest aggiornata.

### 4b. GPU path non verificabile in sandbox

`--backend vulkan` → device RTX A4000 rilevato ma render FAIL; metriche GPU
restano 0 (honest: `gpu` block presente ma zero). Budget GPU (gpu_execute_us,
readback) = null nel manifest: il gate li skippa esplicitamente (`[INFO]`)
senza mai nascondere un FAIL. Da bloccarsi su macchina GPU quando il percorso
Vulkan renderizza.

## 5. Demolition Debt — FrameDeltaCompiler

- **Owner**: Render Graph / runtime team.
- **Reason**: il refactor introduce `FrameDeltaCompiler` come authority canonica, ma alcuni percorsi legacy conservano ancora adattatori e decisioni locali per compatibilità con dirty bounds, dirty tiles, riuso superficie e telemetria. Questi percorsi restano necessari finché l'intera pipeline non consuma esclusivamente il delta compilato.
- **Exit condition**: tutti i consumer di dirty bounds, dirty tiles e reuse ricevono il risultato di `FrameDeltaCompiler`; non esistono più decisioni locali di cambiamento o riuso fuori dal percorso `FrameDeltaCompiler → ExecutionResolver`; i test di render graph e tile execution passano senza flag o fallback di compatibilità dedicati.
- **Equivalence gate**: suite `FrameDeltaCompiler` completa, test `Dirty Tiles`, `TileParallel` e suite render-graph; confronto deterministico tra percorso legacy e percorso compilato su struttura, trasformazioni, testo, colore, immagini, effetti, video, dirty bounds, dirty tiles e decisione di riuso, con output e contatori equivalenti.
- **Removal scope**: adattatori legacy in `src/render_graph/pipeline/scene_dirty.cpp`, policy duplicate in `src/render_graph/pipeline/tile_execution_policy.cpp`, wiring di compatibilità in `src/render_graph/pipeline/scene.cpp`/`scene_internal.hpp`, contatori e rami di fallback non più necessari in `tile_execution_coordinator.cpp`, oltre ai test e flag dedicati esclusivamente al percorso legacy.
- **Status**: **ACTIVE**.

## Gate di verifica

```bash
bash bench/run_perf_baseline.sh                    # suite completa (5 bench)
bash tools/check_perf_baseline.sh --suite <out>/perf_baseline_<sha>.json
# → PASS (10 checks) sulla baseline archiviata
```

E i 4 new TEST_CASE del bench corpus passano (16/16).

## Forward point

- ~~TICKET-FIX: software-backend composite blur SEGV (4a) → poi riabilitare
  `blur_band` e aggiornare manifest+BUDGET con il blur incluso.~~ DONE 2026-08-28:
  blur_band riabilitato, §4a RESOLVED (vedi sopra).
- Bloccare budget GPU (gpu_execute_us/readback/submissions/VMA) su macchina
  Vulkan certificata.
- CPU%/GPU% sampling end-to-end nel runner (annex oggi a fine run singola).
- Encode fps: agganciare il suite runner al video export `.timing.json`.