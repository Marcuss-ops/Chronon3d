# Chronon3D build-step audit — 2026-10-18

SHA: `abb528776` (`main`). Lane: `linux-fast-dev` (Debug, unity ON batch 16,
ccache via project-local `.ccache`, tests ON → diagnostics/verification forced
ON by the root `CMakeLists.txt`).

Companion tools added by this audit:

- `tools/ninja_step_census.py` — DAG export, per-target step accounting.
- `tools/source_census.py` — negative census (target/include/consumer) for
  tracked sources and headers.

Nothing was deleted by this audit; every number below is reproducible with the
commands in §Riproduzione.

## 0. DAG Ninja e step per target

`linux-fast-dev` configured at `${sourceDir}/build/chronon/linux-fast-dev`
(symlink to `.tmp/chronon-builds/linux-fast-dev`).

| target                  | edges | compile | link | archive | other |
| ----------------------- | ----: | ------: | ---: | ------: | ----: |
| `chronon3d_cli`         |   379 |     373 |    1 |       4 |     1 |
| `chronon3d_tests_fast`  |   669 |     651 |   12 |       5 |     1 |
| `chronon3d_dev_fast`    |   674 |     655 |   13 |       5 |     1 |
| `chronon3d_tests`       |  1022 |     936 |   80 |       5 |     1 |
| default `all`           |  1027 |     940 |   82 |       5 |     0 |

Readings:

- The queued daily target `chronon3d_dev_fast` is **674 steps** (655 compiles,
  13 links). The figure "1.400 step" is not reproducible on this SHA/lane: the
  default `all` target is 1027 edges and the full test aggregate 1022.
- **The test perimeter is the dominant second half of the build.** The engine
  closure alone (`chronon3d_cli`) is 373 compile edges. The 12 dev-fast suites
  add **282 compile edges + 12 links = 294 steps (~44 % of `dev_fast`)**; the
  full test aggregate adds 563 suite compile edges + 79 links.
- `all` minus `dev_fast` = 353 steps: the 66 test suites that the daily target
  deliberately does not queue.

### Per-target compile edges (`chronon3d_dev_fast`)

| object target            | edges | source LOC |
| ------------------------ | ----: | ---------: |
| `chronon3d_core_tests`   |   144 |     37 623 |
| `chronon3d_backend_softw.` |  82 |     16 315 |
| `chronon3d_scene_tests`  |    65 |     15 954 |
| `chronon3d_render_plan`  |    37 |     15 678 |
| `chronon3d_compiler`     |    35 |      5 030 |
| `chronon3d_graph_pipeline` |  33 |      7 050 |
| `chronon3d_graph_nodes`  |    32 |      9 118 |
| `chronon3d_cli_render`   |    32 |      9 628 |
| `chronon3d_cache_tests`  |    21 |      5 216 |
| `chronon3d_cli_core`     |    20 |      4 166 |

Unity is active for the engine (`UNITY_BUILD ON`, batch 16) and disabled for
every test suite at the `Chronon3DTestSuite.cmake` boundary
(`UNITY_BUILD OFF`) because test files carry same-named local fixtures.

## 1. Cold build, incremental build, ccache

Host: 24 cores / 30 GiB RAM. Build parallel: `-j10` (the script default `-j14`
was deliberately lowered; the memory sampler never observed MemAvailable below
~13.5 GiB and no swap pressure was introduced).

| indicatore              | valore | note |
| ----------------------- | ------ | ---- |
| Cold build `chronon3d_dev_fast` | **674 step, 38m54s wall, exit 0** | `ninja -j10`, tree pulito, ccache fredda (940 miss / 0 hit); max RSS build 2.36 GB; min MemAvailable 4.75 GB (nessun OOM/swap storm) |
| Incremental `.cpp` singolo | **17 step, 279.1s wall** | `src/cache/cache_policy.cpp`: 1 TC (0.2s) + 15 link + archive/SDK re-archive: `libchronon3d_cli_render.a` 120s, `libchronon3d_cli_core.a` 117s, `libchronon3d_sdk_impl.a` 88s |
| Incremental `.hpp` singolo | **168 step, 269.2s wall** | `include/chronon3d/cache/cache_taxonomy.hpp`: 152 TC + 16 link (fanout testimoniato, non stimato) |
| Focused build suite      | **2 step, 4.1s wall** | touch `tests/scene/test_scene_validator.cpp` → RC `chronon3d_scene_tests` only (1 TC + 1 link) |
| ccache hit rate          | 366/1314 direct (27.9%) dopo esperimenti; mtime-only touch → cache miss su 940/1314 primi run (lane prima fredda) | `ccache -s` (CCACHE_DIR progettuale). Secondo run identico: `no work to do` ninja — il patrimonio della lane è il DAG, la cache copre i cambi cross-lane |

Reperto:`ninja -n` post-build = "no work to do" (deps integre); il pattern
corrotto `.ninja_deps` documentato in `docs/CCACHE-AUDIT-2026-09-10.md` non si
è ripresentato su questa lane.

## 2. Quali test pesano

Ranking per compile edge e LOC sorgente (lane `linux-fast-dev`):

| suite (top 10 of 78)       | edges | src-LOC |
| -------------------------- | ----: | ------: |
| `chronon3d_core_tests`     |   144 |  37 623 |
| `chronon3d_scene_tests`    |    65 |  15 954 |
| `chronon3d_render_graph_tests` | 57 | 15 566 |
| `chronon3d_renderer_core_tests` | 26 | 5 257 |
| `chronon3d_effects_tests`  |    26 |   7 577 |
| `chronon3d_cache_tests`    |    21 |   5 216 |
| `chronon3d_cli_tests`      |    17 |   3 378 |
| `chronon3d_c_abi_tests`    |    13 |  10 492 |
| `chronon3d_deterministic_tests` | 12 | 4 343 |
| `chronon3d_backend_registry_tests` | 11 | 6 213 |

## 3. Esperimento: unity build su una suite test isolata

`CHRONON3D_TEST_UNITY_BUILD` (nuova opzione, default OFF) abilita
`UNITY_BUILD ON` per le sole esecuzioni registrate da
`chronon3d_add_test_suite()`.

Esperimento isolato su `chronon3d_scene_tests` (65 sorgenti + link),
**cache disabilitata** su entrambe le varianti, stessi flag, stesso SHA:

| variante       | edges | wall    |
| -------------- | ----: | ------: |
| OFF (baseline) |    66 | 255.5 s |
| ON (unity + whitelist `SKIP_UNITY_BUILD_INCLUSION`) |    14 |  **91.9 s** |
| Δ               | −52 step | **−64 % tempo, 2.8× più veloce** |

Le collisioni fixture (`kCam01Eps`, `kCam01Fps`, `compile_or_die_cam01`,
`eval_at_or_die_cam01`, `make_cam01_base_desc`, `make_test_catalog`,
`make_test_program`, `SingleShotTimeline`) sono state risolte con
`UNITY_EXCLUDE_SOURCES` → `SKIP_UNITY_BUILD_INCLUSION` su 9 sorgenti della
suite: i file esclusi tornano a compilare singolarmente, il resto della suite
resta in batch unity. Risultato runtime: **469/469 test cases PASS,
3632/3632 assertions PASS, exit 0**.

### Revisione completa su tutto `chronon3d_dev_fast`

L'esperimento è stato poi esteso a tutte le 12 suite fast (cache disabilitata
solo sul ramo compila; whitelist applicata suite per suite):

| variante | edges (`chronon3d_dev_fast`) | Δ |
| -------- | ---------------------------: | - |
| ON (baseline)  | 674 | — |
| ON (unity + whitelist definitiva) | **449** | **−225 step (−33 %)** |

Runtime suite al termine: **12/12 suite PASS (exit 0)** including
core/scene/cache/compositor/timeline/animation_helpers/cli/deterministic/
precomp/optimizer/render_job_contract/text_health.

Whitespace-definizione: `UNITY_EXCLUDE_SOURCES` è un parametro opt-in di
`chronon3d_add_test_suite()`; la whitelist definitiva conta 20 sorgenti
(divise su core:11, scene:9, cli:3, compositor:2, timeline:2, deterministic:3,
cache:1 — alcune esclusioni multi-suite).

## 4. Census preset legacy

`tools/source_census.py` e la scansione dei preset mostrano che i nomi
"legacy" non sono orfani:

| preset | stato | consumer |
| ------ | ------ | -------- |
| `linux-content-dev` | legacy name dichiarato in `development.json` | `build-fast.sh` (5), `tools/check_determinism_matrix.sh` (3), `tests/deterministic/test_brute_determinism.cpp`, `tests/deterministic/test_text_warm_cold.cpp`, `docs/adr/ADR-023`, `docs/adr/ADR-022`, `tools/verify_cli_render_surface_linux.sh`, `tools/check_clean_rebuild.sh` |
| `chronon-lean` | 0 consumer eseguibili | citato solo in `TICKET-LOCAL-ACCEPTANCE-CLOSEOUT-2026-09-16.md` |
| `linux-build-profile` | 0 consumer | usato manualmente (`-ftime-trace`) |
| `linux-turbo` | 2 ref | `build-fast.sh` |
| test preset non-CI (`linux-fast-dev-test`, `linux-profile-*-test`, `linux-release-validation-test`, `linux-hardened-release-test`) | 0 consumer | lane `ctest --preset` manuali |

Conclusione: nessun preset è eliminabile senza una migrazione dei consumer.
L'exit condition per il nome legacy `linux-content-dev` è la migrazione dei
consumer elencati (script + 2 test deterministici); finché restano, il nome
resta un alias funzionante.

## 5. Census encoder FFmpeg pipe

Stato fisico al 2026-10-18 (verificato su questo SHA):

- `src/media/video/` **non contiene** più `ffmpeg_pipe_sink.*`,
  `ffmpeg_pipe_args.cpp`, `ffmpeg_frame_submit.cpp`, `process_runner.*`:
  la demolition fisica è già avvenuta.
- `video_sink_factory.cpp` non ha alcun ramo subprocess: per output compresso
  seleziona `NativeAvSink` quando `CHRONON3D_ENABLE_NATIVE_FFMPEG` è ON e
  altrimenti restituisce `nullptr` (fail-closed, nessun fallback).
- Il residuo "pipe" è **vocabolario**, non un secondo percorso: il DTO
  `FfmpegPipeOptions`, il valore `EncoderTransport::Pipe` e
  `--encoder-backend pipe`; `VideoSinkEncoderAdapter::open()` costruisce
  `VideoSinkConfig` e chiama `create_video_sink()` — quindi in un build native
  anche la richiesta "pipe" termina su `NativeAvSink`. Nessuna primitiva
  `popen/fork/exec` esiste nel percorso encoder CLI.
- Exit conditions 4 e 5 di `TICKET-FFMPEG-PIPE-SINK-DEMOLITION` (zero caller di
  `FfmpegPipeSink`/`ProcessRunner`) sono **fattualmente soddisfatte** in codice;
  ciò che resta è la rimozione del vocabolario e l'aggiornamento della scheda,
  che dichiara ancora "compila i componenti pipe/process-runner".
- Reperto separato: `apps/chronon3d_cli/commands/render/command_render_plan_internal.cpp`
  esegue un **ffmpeg esterno** (`run_ffmpeg_child`) per il premix audio del
  render-plan (errori dedicati `audio_ffmpeg_executable_missing` /
  `audio_ffmpeg_premix_failed`). È un percorso ausiliario esplicito, non
  l'encoder; va tenuto tracciato nel census dei consumer esterni ffmpeg.

Nessun file pipe è stato rimosso: la scheda di demolition va prima aggiornata
allo stato reale e l'esecuzione deve restare un lavoro tracciato.

## 6. Census negativo sorgenti/header

`tools/source_census.py` su 3010 file tracciati (2317 nell'universo
`src/ apps/ tests/ include/ bindings/ tools/`):

| verdict | count |
| ------- | ----: |
| REFERENCED (CMake, lane, include o consumer testuale) | 2317 |
| GLOB_COVERED | 0 |
| DOC_ONLY | 0 |
| ORPHAN | **0** |

Controprova indipendente: indice di occorrenza di ogni basename su tutti i file
tracciati (`git grep -F -f`); nessun basename risulta senza occorrenza reale e i
campioni "una sola occorrenza" sono registrazioni CMake o `#include` legittimi.

Target non in `all` (attesi, già `EXCLUDE_FROM_ALL`): `chronon3d_3d_trap_gate`,
`chronon3d_3d_trap_generator`, subdirectory ChrononMotion. 481 target phony
totali nella lane configurata.

## 7. Rimozioni

Nessuna. La negative census non produce candidati: zero sorgenti/header orfani,
zero GLOB, zero DOC_ONLY. Le exit condition della scheda FFmpeg pipe non
autorizzano la rimozione dei simboli/vocabolario senza l'aggiornamento della
scheda stessa. L'unica modifica al build graph è l'opzione opt-in
`CHRONON3D_TEST_UNITY_BUILD` (default OFF, nessun effetto sui build esistenti).

## 8. Gate sullo stesso SHA

SHA `abb5287769c0deb74e5b56275a511ba5521a54d6`:

| gate | esito |
| ---- | ----- |
| `check_conflict_markers.sh` | PASS |
| `check_commit_subject_length.sh` | PASS |
| `check_no_sqlite.sh` | PASS |
| `check_doc_sha_dedup.sh` | PASS |
| `check_frame_value_convention.sh` | PASS |
| `check_architecture.py` | **206/206 rule PASS** |
| `check_test_hygiene.sh` | PASS |
| 12/12 suite `chronon3d_dev_fast` (unity ON) | PASS (exit 0) |

### Ri-verifica sul canone DEFAULT restaurato (stesso SHA, unity OFF)

Dopo il restore del preset di default (`CHRONON3D_TEST_UNITY_BUILD=OFF`,
riconfigure + rebuild: 128 step da ccache, `ninja: no work to do` a build
ferma), sullo stesso SHA `abb528776`:

| gate/unità | esito |
| ---------- | ----- |
| gate statici (conflict/subject/no-sqlite/sha-dedup/frame-convention) | PASS |
| `check_architecture.py` | 206/206 PASS |
| `check_test_hygiene.sh` | PASS |
| rebuild 4 suite minime da zero (`-j10`, 355.7 s, exit 0) | PASS |
| `backend_registry_tests` | 98/98 PASS, exit 0 |
| `compositor_tests` | 651 078/651 078 PASS, exit 0 |
| `render_job_contract_tests` | 368/368 PASS, exit 0 |
| `render_graph_tests` | 19 284/19 284 PASS, exit 0 |
| batteria ctest dell'aggregato `chronon3d_tests` (OFF) | 91/94 PASS |

### 8-bis. Batteria ctest completa della lane (OFF, stesso SHA)

`ctest -j16` sulla lane default-OFF ricostruita da CMakeCache vuoto
(1 022 step, 0 failure di build, ccache pieno):

| esito | conteggio | note |
| ----- | --------- | ---- |
| Passed | 91/94 | incluse scene, core, cache, compositor, timeline, animation, cli, render_job_contract, text_health, precomp, optimizer |
| Skipped | 1 | `chronon3d_dynamic_pyramid_suite` (richiede la GPU native lane) |
| Failed | 2 | **lane-dipendenti, non regressioni** (v. sotto) |

I due FAIL sono entrambi per-costruzione su questa configurazione `dev`:

1. `chronon3d_c_abi_tests` — il decoder `shape.asset` è branchizzato
   `#ifdef CHRONON3D_ENABLE_SVG` (`render_plan_decoder_shape.cpp:85`): con
   `CHRONON3D_ENABLE_SVG=OFF` la decode rigetta esplicitamente una piani con
   asset SVG ("requires SVG importing, which is disabled in this build"), e il
   test case `render plan accepts resolver-backed SVG geometry references`
   fallisce con `decoded.has_value() == false`. Il test case è registrato senza
   guard, quindi girà rosso su ogni lane SVG-OFF; richiede una lane con
   `CHRONON3D_ENABLE_SVG=ON` (es. `linux-svg-fast`).
2. `chronon3d_deterministic_tests` — `text_warm_cold` e `BruteDeterm-17.d`
   eseguono render MP4 nativi (`--backend vulkan --hardware nvenc
   --gpu-hot-path-mode require_gpu_native`) tramite il CLI. Con il CLI dev
   (niente `CHRONON3D_ENABLE_NATIVE_FFMPEG`) il test riportava
   `exit 256`/CLI non discoverabile; puntando `CHRONON3D_CLI_BINARY` al CLI
   della lane `linux-video-release` (costruita ad hoc, `NATIVE_FFMPEG=ON`),
   la suite esita **92/93 test case PASS** (11 784/11 786 assertion):
   `text_warm_cold` passa con render NVENC reali; l'unico residuo è
   `BruteDeterm-17.d` (§16, `cmp -s` tra tre export NVENC: `repeat_1` vs
   `repeat_3` differiscono, rc=256) — confronto byte-a-byte su un encoder
   hardware NVENC: fallimento pre-esistente della GPU lane, fuori dallo scope
   di questo canone (il prerequisito corretto del test è una GPU lane
   certificata replicabile, non la lane dev). Nessuna variazione introdotta
   da questo audit tocca il percorso NVENC/encoder.

Nota operativa: durante la verifica la partizione si è riempita (100 %, SIGBUS
al linker); la lane scratch `/.tmp/chronon-builds/` (49 GB, pura misura
cold-build) è stata rimossa per riportare il disco a >=49 GB liberi — nessun
artefatto di misurazione dipende da quella lane (il DAG del cold-build è
registrato in §0-§2).

## 9. Confronto prima/dopo

| metrica | prima (baseline) | dopo (unity ON, whitelist) | Δ |
| ------- | ---------------: | -------------------------: | - |
| step `chronon3d_dev_fast` | 674 | **449** | −225 (−33 %) |
| step compila test in dev_fast | 282 | ~57 (whitelist TC singole + test_main) | −80 % |
| tempo suite scene (isolato, cache off) | 255.5 s | 91.9 s | −64 % |
| build default `all` | 1027 | (stessa riduzione proporzionale su ~570 step test) | — |

Il canone non è lo scenario DEFAULT: con `CHRONON3D_TEST_UNITY_BUILD=ON`
l'opzione resta sperimentale e ogni suite dichiara esplicitamente le proprie
collisioni nota-per-nota. Il canone DEFAULT (OFF) è invariato al 100 %
(`UNITY_BUILD OFF` rimane il comportamento di ogni suite senza la flag).

## Riproduzione

```bash
# Step census
python3 tools/ninja_step_census.py --json /tmp/step-census.json

# Source/header negative census
python3 tools/source_census.py --json /tmp/source-census.json

# Cold/incremental timing
cd build/chronon/linux-fast-dev
/usr/bin/time -v ninja -j10 chronon3d_dev_fast
```

## Cosa NON è stato toccato

`GraphExecutor`, RenderPlan, property sampler, registry condivisi,
`AssetResolver`, sistema testuale e target GPU: nessuna modifica. La build
continua a certificare rendering software, output video, SDK e ABI.
