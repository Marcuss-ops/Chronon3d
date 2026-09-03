# Chronon3D — Current Status

> **Single v0.1 release contract:** [`docs/RELEASE_V0_1_CONTRACT.md`](RELEASE_V0_1_CONTRACT.md). This file reports observed status; it does not redefine release identity.

## v0.1 contract status — 2026-09-03

- Release tag: `v0.1`
- Active HEAD SHA: `86c165cbe` (`origin/main`)
- DirectYUV Multi-Source support: **DONE** (`feat(video): compose multiple native YUV sources`)
- CompiledResourceTable cutover: **DONE** (surface API & lifecycle TUs refactored, allocation shims removed)
- Deprecated API Cleansing: **CLOSED** — census-based closure: `TileExecutionPolicy` alias removed (1 test caller migrated), `Camera2_5DProjectionMode` + write-only `projection_mode` fields removed (zero readers), legacy `render_scene`/`materialize_text_run_shape` chains classified ABI-required with evidence in `NAMING_COMPATIBILITY_DEBT.md`.
- Filename drift gate: **PASS 0 operational (strict)** — was 116 findings (2026-08-28); all resolved or re-classified with per-line `drift-class:` evidence (57 historical, 2 false-positive).


## Performance Baseline — 2026-08-25

- **Chronon Performance Baseline ufficiale (BENCH-1..5) DONE**: 5 composition canoniche, runner suite, report GPU/CPU, budget LOCKED (p50/p95 ×1.4) e gate `tools/check_perf_baseline.sh` PASS 10/10 su `main@eb871240` (CPU/software; GPU-side budget da bloccare su macchina Vulkan). Dettaglio: [TICKET-PERF-BASELINE-V1](tickets/TICKET-PERF-BASELINE-V1.md).

## GPU checkpoint — 2026-08-19/20

- GPU-native 1920x1080 960f watermark+subtitle: **4.11s** vs NVDEC→NVENC 2.53s (+62%), CPU 29%, `gpu_readback_bytes=0`, `fallback=0`, `effective_backend=vulkan`.
- Residuo: D2D Vulkan→CUDA→NVENC (`gpu_surface_copy_frames` + `encoder_staging_copy_bytes`), zero-copy NVDEC→NVENC ancora da chiudere (surface importabile).
- **2026-08-28**: strumentati i gate zero-copy 3/4/6 (`nv12_to_rgba_frames`, `rgba_to_nv12_frames`, `gpu_surface_copy_frames`) che prima non esistevano o non venivano mai incrementati; ora il report `*.timing.json` misura le conversioni reali. Il percorso direct-YUV (`composite_direct_nv12*`) azzera i gate ma non è ancora attivo nel pipeline di export.
- CLI/IPC: daemon caldo 10.09s vs CLI 13.17s, 100 job x150f stabili `1059s`, probe CUDA/Vulkan `CUDA_VULKAN_INTEROP_PASS` su RTX A4000, `hwmap=derive_device=vulkan` ancora FAIL.

> Ultima baseline verde storica: `main@7eb5c2ba` 11/11 PASS (2026-07-06). Non è una certificazione same-SHA di `v0.1`; il tag `v0.1` è `BLOCKED / NOT CERTIFIED`. Il worktree corrente è sporco — developer gates 20/20 PASS, architecture 26/26, ma CTest fast 92 test mancano binari, native decoder cache FAIL, no baseline same-SHA. Dettaglio: `docs/baselines/main-7eb5c2ba-baseline.md`.

## Active Blockers

| ID | Area | Stato | Scheda |
|---|---|---|---|
| TICKET-125-TEST-AGGREGATOR | testing | OPEN | [TICKET-125](tickets/TICKET-125-test-aggregator.md) — engine certification (Tests 10-16); Test 13 = Camera brutal, distinto da Test 11; product validation in PipelineGen |
| TICKET-DEPRECATED-API-REMOVAL | API | OPEN | [DEPRECATED-API-REMOVAL](tickets/TICKET-DEPRECATED-API-REMOVAL.md) |
| TEST-FONT-ASSET-PATH / CERT-SEQUENCE-WBH | testing/cert | OPEN | [TEST-FONT-ASSET-PATH](tickets/TICKET-TEST-FONT-ASSET-PATH.md) |

Indice completo: [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md).

## Stato per area (sintesi)

| Area | Stato | Note |
|---|---|---|
| CLI V3 / RenderPlan | PASS | `PreparedRenderPlan → CompiledComposition` + `render --plan` canonico |
| Camera V1 | PARTIAL | 9/9 visual + 64/64 suite + 406/406 scene PASS; manca baseline same-SHA + sanitizer |
| Text Core V1 | PASS | FreeType/HarfBuzz/FriBidi + layout/cache/animator certificati |
| Text Production | PARTIAL | 20+8 preset, 192/192 subtitle PASS, manca corpus CapCut PNG |
| GPU Production V1 | PARTIAL | Vulkan native + CUDA interop probe PASS, D2D copy residua |
| Executor / Tile-prune | P2 OPEN | `GraphExecutor` è la sola authority produttiva; `executor_levels.cpp`, `node_runner.cpp` e `node_executor.cpp` hanno ownership distinta; `command_plan_executor.cpp` è adapter test-only con symbol ABI preservato. Dettaglio: [`docs/EXECUTOR_OWNERSHIP.md`](EXECUTOR_OWNERSHIP.md) |
| SDK C++ / C ABI v2 | PASS (focused) | `install_consumer` 14/14 + `CHRONON_ERROR_ASSET_CHANGED` |
| Video pipeline | PASS | 13 codici errore, atomic output, 98 test |
| CI infrastructure | FAIL | gate WIRED, run recenti non verdi |
| Test coverage | PARTIAL | 39/39 golden + 1423/1423 core --no-skip PASS, manca globale |
| Packaging | PASS (locale) | `verify_packaging_linux.sh` 14/14 |
| Determinism | PASS | seriale/parallelo + cold/warm SHA identici |
| Next Steps | PLANNED | vedi [`docs/CHRONON_PLAN.md`](CHRONON_PLAN.md) |

## Vulkan surface-store extraction — verification report (2026-08-27)

- **CMake/configuration:** preset `linux-fast-dev` configured successfully with Vulkan dependencies and generated shader/ABI metadata.
- **Backend build:** `chronon3d_backend_vulkan` builds with `VulkanBackend::Impl` declared in the private `vulkan_backend_impl.hpp` (not installed) and every implementation fragment compiled as an independent translation unit (`vulkan_backend_lifecycle_private.cpp`, `vulkan_descriptor_arena_private.cpp`, `vulkan_surface_store_private.cpp`, `vulkan_kernel_store_private.cpp`, `vulkan_backend_operations_private.cpp`); the public adapters are split across `vulkan_backend_lifecycle.cpp`, `vulkan_backend_stats.cpp` and `vulkan_backend_surface_api.cpp`. Incremental rebuilds recompile only the touched fragment.
- **Architecture gate:** `tools/check_architecture_boundaries.sh` passes all 26 checks after changing the CLI Vulkan importer include to a relative internal include. This is a gate result, not a complete proof that surface ownership has been fully extracted.
- **GPU runtime:** `vulkaninfo --summary` detects an NVIDIA RTX A4000 (discrete GPU, Vulkan 1.4.329, NVIDIA driver 595.84) and llvmpipe fallback. `nvidia-smi` detects the same RTX A4000 and driver; NVIDIA device nodes are present.
- **CUDA interop:** project configuration finds CUDA headers/driver libraries, while `nvcc` is unavailable. The runtime external-memory probe nevertheless passes on the real GPU: `CUDA_VULKAN_INTEROP_PASS`, device `NVIDIA RTX A4000`, Vulkan/CUDA UUID `66101dcc-7b1c-e277-889c-c62462e0ac8e`, external Vulkan image mapping and semaphore signaling verified. Full toolkit/compiler coverage remains uncertified because `nvcc` is unavailable.
- **Focused tests:** `chronon3d_backend_registry_tests`, `chronon3d_vulkan_debug_context_tests` and `chronon3d_vulkan_descriptor_arena_tests` were registered but their executables were not produced within the available build window. CTest reported `Not Run`; no assertion failure or functional test failure was observed.
- **Build limitation:** targeted test builds timed out at 300 seconds and again at 600 seconds. This is an environmental/build-duration limitation, not a functional failure classification.
- **CTest limitation:** invoking CTest before test binaries existed produced missing-executable errors. These must not be reported as failed Vulkan assertions.
- **Pre-existing warnings:** CMake developer warnings about intentional `LINK_TARGETS` overrides and the filename-drift audit warnings are separate from this extraction. The filename-drift scan reported 787 warnings in warning mode.
- **Uncertified items:** runtime behavior of surface aliasing, plan preallocation, deferred release, frame-transient retirement and final destruction still requires execution of the focused Vulkan test binaries. CUDA/Vulkan external-memory mapping and semaphore signaling are certified by the dedicated probe; broader CUDA toolkit/compiler paths remain uncertified.

## Filename drift — audit progressivo (2026-08-28)

Il gate `tools/check_filename_drift.sh` analizza nuovamente tutti i percorsi
operativi (`cmake/`, `docs/`, `include/`, `src/`, `apps/`, `tests/` e `content/`).
Sono esclusi soltanto output generati/build, dipendenze vendorizzate, fixture e
baseline archiviate esplicitamente, oltre a directory di esempio/template già
classificate.

- **Corretti:** riferimenti a test e sorgenti spostati, inclusi i percorsi della
  pipeline parity, AE parity, text clip, scene hasher, layer builder e camera.
- **Esclusi intenzionalmente:** artefatti di build, fixture/baseline storiche,
  `tests/acceptance/`, `tests/baselines/` e documentazione archiviale. Queste
  esclusioni sono di scope e non vengono presentate come PASS operativo.
- **Aperti:** il gate strict rileva ancora finding operativi da correggere o
  classificare puntualmente; non sono stati ricreati file rimossi e non sono
  stati aggiunti marker `drift-allow` generici.
- **Risultato warn-only aggiornato:** `116` finding operativi rilevati il
  2026-08-28; nessun finding è stato nascosto con un allow generico.
- **Classificazione introdotta:** operational, historical, template,
  third-party, false-positive e planned (`drift-allow` + `drift-reason`).
- **Blocking:** resta disattivato; va riattivato solo sotto 10 finding
  operativi e con tutti i riferimenti CMake/include/test attivi risolti.
- **Regola:** `drift-class: historical`, `template`, `third-party` e
  `false-positive` restano diagnostici; un riferimento pianificato richiede  sia `drift-allow: <id>` sia `drift-reason: <motivazione>`. Nessuna
  classificazione nasconde

  automaticamente sorgenti o test attivi.

## Executor ownership verdict — 2026-08-28

- **Production authority:** `GraphExecutor` (`executor.cpp`) è l'unica autorità che decide il modo di esecuzione del frame: compiled program oppure fallback DAG.
- **Helper ownership:** `executor_levels.cpp` orchestra i livelli; `node_runner.cpp` orchestra il singolo nodo; `node_executor.cpp` esegue il nodo e trasferisce ownership del framebuffer. Nessuna decisione duplicata.
- **Test-only adapter:** `command_plan_executor.cpp` non ha caller produttivi. La dichiarazione è confinata a `tests/helpers/command_plan_executor.hpp`; il simbolo `execute_command_plan` resta presente come ABI-compatibility shim, verificato nella baseline `tools/sdk/chronon3d_c.abi`.
- **Profiling:** syntax-only `executor_levels.cpp` = 15,36s; `node_runner.cpp` = 27,94s. L'accorpamento non è stato applicato perché unire i TU aumenterebbe il coupling e non produce un guadagno misurato.
- Dettaglio: [`docs/EXECUTOR_OWNERSHIP.md`](EXECUTOR_OWNERSHIP.md).

## Test 13 indexing verdict — 2026-08-28

- Audit completo di `AGENTS.md`, `docs/`, `tools/`, test e git history: **Test 13 è distinto da Test 11**.
- Test 11 = fix speed / cronometro / render cost; Test 13 = `Camera brutal` nel first-principles orchestrator.
- Corretto il commento legacy del runner da `Test #9` a `Test #13`; nessuna nuova camera authority introdotta.
- Contratto canonico: [`docs/tickets/TICKET-TEST-13-CORE.md`](tickets/TICKET-TEST-13-CORE.md). TICKET-127 è RESOLVED; runtime Test 13 resta OPEN/PARTIAL fino a esecuzione WBH.

## Main synchronization / F2 divergence — 2026-08-28

- `main` locale è avanti di 5 commit rispetto a `origin/main`; `origin/main` è antenato di `HEAD`, quindi **non c'è divergenza topologica** e non serve rebase.
- `tools/wrap_push.sh` ora richiede il contratto `CHRONON3D_TESTED_SHA` (default = HEAD dopo i gate), vieta force-push su `main` e verifica dopo il push `tested SHA == HEAD == origin/main == @{u}`.
- Selftest: `tools/selftest_wrap_push_sha.sh` PASS.
- Il push non è stato eseguito: il worktree contiene modifiche non committate; prima occorre chiudere/committare il worktree e rieseguire i gate sullo SHA risultante.

## Working Build Host — 2026-08-28

- WBH contract and non-installing preflight: [`docs/WORKING_BUILD_HOST.md`](WORKING_BUILD_HOST.md) + `tools/check_wbh_toolchain.sh`.
- Current host: CMake `3.31.6` available; NVIDIA RTX A4000/driver `595.84` and Vulkan loader detected.
- **BLOCKED:** Ninja `1.13.0.git.kitware.jobserver-pipe-1` is rejected (approved contract requires `>=1.14`); `nvcc` is missing, so the CUDA smoke compilation and full toolkit certification cannot run.
- No host packages were installed or modified by this verification. Clean build, CTest, incremental rebuild and full CUDA/Vulkan/NVENC certification remain deferred to the provisioned WBH.

## Gate Audit

- **2026-08-27 runtime update:** architecture boundary gate `26/26 PASS`; CUDA/Vulkan external-memory probe `PASS` on NVIDIA RTX A4000. The probe confirms device compatibility and external semaphore signaling, but does not replace the unavailable focused Vulkan test suite.

- `main@7eb5c2ba` **11/11 PASS** certificata. `main@ef9c83f1` 14/14+1 BLOCKED. HEAD non certificato.
- Dettaglio storico in `docs/CHANGELOG.md` + `docs/CHANGELOG.archive.md`.

## Link canonici

- [`docs/RELEASE_V0_1_CONTRACT.md`](RELEASE_V0_1_CONTRACT.md) — identità e contratto v0.1
- [`docs/ROADMAP.md`](ROADMAP.md) — milestone
- [`docs/RELEASE_GATE.md`](RELEASE_GATE.md) — requisiti release
- [`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) — blocker
- [`docs/CHRONON_PLAN.md`](CHRONON_PLAN.md) — next steps operativi
- [`docs/baselines/main-7eb5c2ba-baseline.md`](baselines/main-7eb5c2ba-baseline.md) — baseline verde

## Hygiene

`branch.main.rebase=true` + `tools/wrap_push.sh` + `tools/check_main_clean.sh` (GATE-MNT-01).
