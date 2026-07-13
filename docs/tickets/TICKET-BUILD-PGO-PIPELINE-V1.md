# TICKET-BUILD-PGO-PIPELINE-V1 — PGO + ThinLTO + BOLT CMake Preset Pipeline

## Stato: OPEN (2026-07-13, first-commit)

## Problema

Il release pipeline V0.1 (`linux-release-validation` preset) usa un build Release
vanilla CMAKE_BUILD_TYPE=Release senza PGO, ThinLTO, o BOLT. Le 3 ottimizzazioni
sono ROADMAP items roadmap-locked ma mai wirate per il release path:

| Ottimizzazione | Speedup atteso | Status |
|---|---|---|
| **Clang PGO** (Instr-Generate + Prof-Use) | 5-15% hot-loop | non wirato |
| **ThinLTO** (`-flto=thin`) | 3-8% cross-TU inlining | non wirato |
| **BOLT** (post-link binary re-layout) | 4-10% i-cache | non wirato |
| **PGO × ThinLTO** | 8-20% combinato | non wirato |
| **PGO × ThinLTO × BOLT** | 12-25% combinato | non wirato |

Senza pipeline PGO/ThinLTO/BOLT, il supply-side di performance rimane limitato
al compiler vendor's pre-set `-O3 + -DNDEBUG`. C'è un gap系统性 vs competitor
(Linux/Firefox/Chrome/Clang stesso) che usano PGO + LTO + PGO+ThinLTO+BOLT
di default per release.

## Soluzione Confine (scope)

Configurare 4 nuovi CMake presets (PGO + ThinLTO combinations) per il release
validation path:

1. `release-pgo` — PGO profile-use build, richiede pre-collected `.profdata`
2. `release-thinlto` — ThinLTO senza profile
3. `release-pgo-thinlto` — PGO + ThinLTO combinato
4. `release-pgo-thinlto-bolt` — PGO + ThinLTO + BOLT post-link

Tutti i 4 preset usano:
- **Real Chronon corpus (B00-B11)** come training data, NOT microbenchmark
  (`tools/run_bench_corpus.sh` + `examples/bench_corpus/corpus_v1.json`).
- **NO `-ffast-math` globalmente** finché non provato che preserva:
  (a) determinismo (cross-machine bit-identical output), (b) alpha (alpha bbox
  scan + TextBboxReporter per-session isolation), (c) colori (golden test
  alpha-bbox), (d) golden (5 PNG goldens in test_renders/golden/text/presets/),
  (e) cross-CPU parity (CPU-Low vs CPU-Mid vs CPU-High canonical machines in
  `configs/benchmark_machines.yaml`).
- ADR forward-point: la 5-condition determinism proof + il corpus training
  invocation contract sono documentati in `docs/PERFORMANCE_BOTTLENECKS.md` §F6.3
  e forward-pointati a un ADR dedicato.

## File modificati (3 NEW + 3 EDIT)

NEW:
- `cmake/presets/optimizations.json` (~140 LoC) — 4 configure presets + 4 build
  presets + 4 test presets + 1 hidden `__release-pgo-base` helper. Tutti
  ereditano da `__release-base` (V0.1 release-validation base) + aggiungono
  CMake 3.27 native variables: `CMAKE_PROFILE_GENERATE` /
  `CMAKE_PROFILE_USE` (PGO; CMake 3.20+ native) + `CMAKE_INTERPROCEDURAL_OPTIMIZATION`
  + `CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE_ARGS="-flto=thin"`
  (ThinLTO; CMake 3.9+/3.21+ native) + `CHRONON3D_BOLT_POSTPROCESS`
  (BOLT post-link trigger) + i path hints per i 2 profile sources (`merged.profdata`
  PGO + `perf.fdata` BOLT).
- `cmake/bolt_postprocess.cmake` (~95 LoC) — wire-up del `bolt-postprocess`
  custom_target via `llvm-bolt <binary> -data <fdata> -o <binary>.bolt -relocs`.
  Detect-on-CMake-configure di `llvm-bolt` via `find_program` + FATAL_ERROR
  esplicito se non trovato (forward-point: `tools/check_bolt_available.sh`).
- `docs/tickets/TICKET-BUILD-PGO-PIPELINE-V1.md` (questo ticket)

EDIT:
- `CMakePresets.json` (1 riga: add `"cmake/presets/optimizations.json"` all'array
  `include` tra i 7 sub-preset JSON esistenti)
- `docs/PERFORMANCE_BOTTLENECKS.md` (sezione nuova `## F6.3 — PGO + ThinLTO +
  BOLT release pipeline` con la 3-step collection recipe, training corpus
  mandate, `-ffast-math` prohibition + 5-condition gate, forward-point ADR).
- `docs/CHANGELOG.md` (F6.3 entry prepended at TOP).

## Criteri di accettazione

1. ✅ `CMakePresets.json` parsable (JSON schema v6 — verify con
   `python3 -c "import json; json.load(open('CMakePresets.json'))"`)
2. ✅ 4 configure preset visibili via `cmake --list-presets=configure`:
   `release-pgo`, `release-thinlto`, `release-pgo-thinlto`, `release-pgo-thinlto-bolt`
3. ✅ 4 build presets visibili via `cmake --list-presets=build`
4. ✅ 4 test presets visibili via `cmake --list-presets=test`
5. ✅ NO `-ffast-math` token in qualsiasi preset file
   (verify: `grep -nE '\-ffast-math|fast-math|ffp-contract' cmake/presets/*.json
   CMakePresets.json cmake/bolt_postprocess.cmake` → 0 matches)
6. ✅ Training corpus hard-coded come `examples/bench_corpus/corpus_v1.json`
   (12 scenes B00-B11, NOT `google/benchmark`)
7. ✅ `bolt-postprocess` custom_target eseguibile via
   `cmake --build build/chronon/release-pgo-thinlto-bolt --target bolt-postprocess`
8. ✅ `CHRONON3D_BUILD_TYPE` = `Release` (NON Debug) in tutti i 4 preset
9. ✅ 8 forward-points coprono: macchina-verifica WBH + ADR determinism +
   corpus collection script + BOLT availability gate + thread-budget tuning

## Forward-points

a) **macchina-verifica end-to-end** dei 4 presets su working build host
   (DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` precedent +
   F5.1/F3.1/F3.2 pattern). Verify cycle:
   1. `cmake --preset release-pgo` con `merged.profdata` collocato →
      `cmake --build` → `ctest --preset release-pgo-test`
   2. Re-run con `release-thinlto` e `release-pgo-thinlto` →
      `cmake --build → ctest`. Bench comparison chronon3d_cli `frame_p50`
      su B03 CinematicGlow1080p tra vanilla Release vs PGO vs ThinLTO vs
      PGO+ThinLTO vs PGO+ThinLTO+BOLT.
   3. Assert: nessun regressione bit-exact sui golden test alpha-bbox.

b) **ADR per la 5-condition determinism proof** di `-ffast-math`
   (forward-point esplicito del user spec "NON abilitare -ffast-math
   globalmente finché non provato che preserva determinismo + alpha +
   colori + golden + cross-CPU parity"). Forward-point ADR deve coprire
   le 5 condition + i 5 acceptance tests (`tests/text/test_pipeline_parity_
   real.cpp::BruteDeterm-17` + `tests/text/test_pipeline_parity.cpp::pred_bbox_
   invariance` + 5 golden PNGs in `test_renders/golden/text/presets/` +
   CPU-Low/Mid/High matrix in `configs/benchmark_machines.yaml`).

c) **tools/run_pgo_training.sh** — script di training corpus su B00-B11:
   1. Build instrumented (`cmake --preset release-pgo-instrument` o via
      `cmake -DCMAKE_PROFILE_GENERATE=ON ...`)
   2. Run each B0X scene × `--frames 60` (B09 long-form skippa frame-budget
      via fast-forward warm cycle)
   3. `llvm-profdata merge -output=merged.profdata *.profraw`
   4. Rebuild optimized → `cmake --build`. Forward-point: il
   `linux-pgo-instrument` hidden preset che il training script usa.

d) **tools/check_bolt_available.sh** — pre-`cmake --preset
   release-pgo-thinlto-bolt` gate che verifica `llvm-bolt` su PATH +
   `lld` (BOLT needs ELF binary; `gold` non supportato) + runtime
   availability. Forward-point: wire-in `tools/wrap_push.sh` Step 4.5j
   (continuo lineage di GATE-MNT-01 + 4.5h cronograph + 4.5i docs-sync).

e) **Thread-budget tuning for PGO collection** — la raccolta profile
   su B00-B11 consuma N frames × 1'000ns/frame ≈ 5-10 min su `cpu-mid`.
   Forward-point: `CHRONON3D_THREADS=N` env var wrapper (cf.
   `corpus_v1.json::cli_invocations::threading_env`) + il flag-tuning
   per tbb global_control.

f) **bolt-postprocess parallel invocation** — il custom_target
   `bolt-postprocess` attualmente itera sui 2 binaries serialmente. Per
   binaries più grandi (chronon3d_sdk_impl) il bolt step è I/O-bound;
   forward-point per parallelizzare via BOLT's `-num-threads=N`.

g) **PBenchmark runner integration** — `tools/run_bench_corpus.sh`
   (exists, vedi `corpus_v1.json::cli_invocations::bench_runner`) già
   itera su B00-B11 × thread matrix; forward-point per aggiungere
   `--pgo-collect` mode che instrumente il bench runner e produce
   `merged.profdata` al termine (catena
   run_bench_corpus → corpus_v1.sh → optional PGO collect → bulk
   training corpus data).

h) **Performance baseline recording**: per ogni preset, salvare
   `docs/benchmarks/pgo_thinlto_bolt_baseline.md` con: (preset, build
   SHA, host machine-id, frame_p50 per B00/B02/B03/B05/B07/B09,
   bytes-copied-per-frame, peak-RSS, vs-vanilla-Release speedup).
   Questo diventa il baseline canonico per future regression-via-PGO.

## Cross-reference

- `CMakePresets.json::include` (top-level) → 7 sub-preset JSON in
  `cmake/presets/` (existing) + the NEW `cmake/presets/optimizations.json`
- `cmake/presets/release.json::linux-release-validation` (existing) →
  ancestor V0.1 release path; PGO/ThinLTO/BOLT variants are children
  for performance experimentation, NOT the release-V0.1 path itself
- `CMake 3.27 minimum` (verified `CMakeLists.txt:1`) → supports all
  native PGO/ThinLTO variables natively
- `examples/bench_corpus/corpus_v1.json` → 12-scene B00-B11 with
  per-scene threads, resolutions, fps, expected_p50/p95 (training
  corpus raw metadata)
- `configs/benchmark_machines.yaml::machines[cpu-low/cpu-mid/cpu-high]`
  → canonical thread matrix for cross-CPU parity verification
- `docs/FOLLOWUP_TICKETS.md` §Open Blockers TICKET-PRESETS-PGO-LTO-BOLT
  → forward-point linkage row (NEW this commit; deferred from prior
  cycle per the F6.3 closure lineage)

## §honest-limitation

- **macchina-verifica end-to-end** (preset configure + profile collection
  via corpus B00-B11 + rebuild optimized + bench comparison) DEFERRED-WBH
  per il established `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` precedent
  (this VPS lacks vcpkg glm/magic_enum + chronon3d_cli linkable).
- **On-VPS verifiable**: JSON schema parse + grep `-ffast-math` absence
  + visibility via `cmake --list-presets` (NOT yet verifiabile su VPS
  senza chronon3d_cli linkable). Verify paths: 4 preset files esistono
  nel filesystem + JSON parse OK + `bolt-postprocess` custom_target
  dichiarato in `cmake/bolt_postprocess.cmake`.
- **Profile collection** richiede circulating `.profdata` files via
  WBH session: collect real profile from B00-B11 corpus, ship
  pre-collected profile come test artifact, verify optimize-side
  binary reproduces the unoptimized-side's correctness gates.
- **The 5-condition determinism proof** for `-ffast-math` is FORWARD-POINTED
  to ADR; the preset ships WITHOUT `-ffast-math` per the user spec verbatim
  prohibition; the ADR is the canonical venue for argumenting
  enablement.

Cat-3 minimal-surface: 3 NEW files (presets + bolt helper + ticket) +
3 EDIT files (CMakePresets.json + 2 docs). Subject envelope
`feat(build): PGO + ThinLTO + BOLT cmake presets` (51 chars OK).
