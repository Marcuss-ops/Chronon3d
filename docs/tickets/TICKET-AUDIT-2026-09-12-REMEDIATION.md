# TICKET-AUDIT-2026-09-12-REMEDIATION — Bonifica dell'action list "Technical Audit Report"

> **Stato:** CODE DONE — build verde su `linux-video-test`; 1 blocker pre-esistente emerso (§7).
> **Origine:** `Chronon3D Technical Audit Report` (2026-09-12), action list ordinata.
> **Scope:** P0-1, P0-2, P1-1, P1-3, P2-1, P2-2, P2-3, P2-4, P2-5.
> **Vedi anche:** `docs/AUDIT-2026-09-12-VIDEO-CODEC-BOUNDARY.md` (audit diverso, stesso giorno).

Ground truth dichiarata dall'audit: `build/chronon/linux-video-test/compile_commands.json`
(1.040 TU) incrociata con i 489 `src/**/*.cpp` su disco. Stato vocabolario:
`PASS` / `FAIL` / `PARTIAL` / `NOT RUN`.

---

## 1. P0-1 — Feature-gated TU senza copertura in presets osservati

**Verifica (riprodotta):** i 17 TU citati sono **assenti** da
`linux-video-test/compile_commands.json`. Mappatura per guard CMake e lane:

| TU | Guard | Lane che lo compila | Su PR? |
|---|---|---|---|
| `src/c_api/chronon3d_c_api.cpp` | `CHRONON3D_BUILD_C_API` | `linux-ci-full-validation` | **sì (dopo questo fix)** |
| `src/assets/svg_importer.cpp`, `svg_path_loader.cpp`, `svgpp_parser_impl.cpp` | `CHRONON3D_ENABLE_SVG` | `linux-ci-full-validation` | **sì (dopo)** |
| `src/backends/image/exr_mmap.cpp` | `CHRONON3D_ENABLE_EXR` | `linux-ci-full-validation` | **sì (dopo)** |
| `src/backends/software/processors/software_mesh_processor.cpp`, `specialized/mesh_renderer.cpp` | `CHRONON3D_ENABLE_MESH` | `linux-ci-full-validation` | **sì (dopo)** |
| `src/daemon/daemon_runtime.cpp` | `CHRONON3D_BUILD_DAEMON` (+ IPC) | `linux-ci-full-validation` | **sì (dopo)** |
| `src/core/crash/crash_handler.cpp` | `CHRONON3D_ENABLE_CRASH_HANDLER` | **nessuna lane lo abilitava** → `linux-ci-full-validation` | **sì (dopo)** |
| `src/core/dev/watch_service.cpp` | `CHRONON3D_ENABLE_DEV_WATCH` | **nessuna lane lo abilitava** → `linux-ci-full-validation` | **sì (dopo)** |
| `src/ipc/{composition_session,contract_validator_registry,ipc_codec}.cpp`, `ipc/transport/memfd_shared_memory_transport.cpp` | `CHRONON3D_ENABLE_IPC` | `linux-ci` (build-matrix) | già sì |
| `src/ipc/fuzz/ipc_codec_fuzzer.cpp` | `CHRONON3D_BUILD_FUZZERS` (+ Clang/libFuzzer) | `fuzz-regressions` / `linux-fuzz` | già sì |
| `src/media/video/direct_yuv/direct_yuv_program_stub.cpp` | ramo `else` di `CHRONON3D_ENABLE_NATIVE_FFMPEG` | `linux-ci-render-video-contract` | già sì |
| `src/core/color/ocio_color_transform.cpp` | `CHRONON3D_ENABLE_OCIO` | `ocio.yml` (settimanale) | **no — dependency pesante** |

**Rettifica rispetto all'audit:** la lane `linux-ci-full-validation` **esisteva
già**, ma il job CI `full-validation` girava solo su `push`
(`if: github.event_name == 'push'`). Inoltre due TU
(`crash_handler.cpp`, `watch_service.cpp`) non erano abilitati da **nessun**
preset: il fix suggerito dall'audit (solo SVG/EXR/IPC/MESH/C-API) non li
avrebbe coperti.

**Fix implementato (solo infrastruttura, zero sorgenti di engine):**

1. `cmake/presets/ci.json` → `linux-ci-full-validation` abilita
   `CHRONON3D_ENABLE_CRASH_HANDLER=ON`, `CHRONON3D_ENABLE_DEV_WATCH=ON` e le
   feature vcpkg `crash-handler;dev-watch`. Il preset è ora la *compile-coverage
   authority* per i TU feature-gated (description aggiornata).
2. `.github/workflows/ci.yml` → il job `full-validation` non è più push-only:
   su PR esegue configure + build (copertura di compilazione); la `ctest` full
   resta push-only perché i fixture richiedono LFS
   (`lfs: ${{ github.event_name == 'push' }}`).

**OpenColorIO** resta sulla lane dedicata settimanale: l'audit la include nei
17, ma `opencolorio` è una dipendenza pesante che non giustifica l'install su
ogni PR. È l'unico TU feature-gated non compilato ad ogni PR; tracciato qui.

**Estensione della stessa classe (scoperta durante la verifica):** la suite
`chronon3d_backend_registry_tests` è compilata **solo** con
`CHRONON3D_ENABLE_VULKAN=ON` (default `OFF`); nessun workflow in
`.github/workflows/` usa un preset Vulkan (`native-video.yml` lo forza `OFF`).
Di conseguenza la suite non è mai stata compilata in CI, e al 2026-09-10 ha
smesso di compilare senza che nessuno se ne accorgesse (§7).

**Regression risk:** Low (nessuna modifica ai sorgenti di engine). **NOT RUN:**
la compilazione effettiva di `crash_handler.cpp`/`watch_service.cpp` — vedi §6.

---

## 2. P0-2 — Fallback GPU→CPU testo silenzioso

**File:** `src/render_graph/nodes/text_run/gpu_text_run.cpp`
(`prepare_mtsdf_text_run`, catch su `resolve_handle`).

- `catch (...)` → `catch (const std::exception&)` + `catch (...)`: entrambi
  incrementano `gpu_text_font_resolve_failures` (nuovo contatore in
  `CHRONON_COUNTERS_TEXT`) ed emettono `spdlog::warn` con `what()`. Il throw di
  risoluzione handle non è più indistinguibile da "GPU text unsupported";
  contratto fail-closed (`return nullptr`) invariato.
- **De-duplicazione intra-funzione:** il loop font non ri-verifica più
  `glyph_begin > glyph_end` / `glyph_end > placed.glyphs.size()`: il loop
  canonico precedente (quello su `covered_glyphs`) le ha già garantite. Resta
  il solo check nuovo (`font_path.empty()`). Byte-equivalente; nessuna
  rimozione della validazione canonica di `text/compiler/text_compile_validation.cpp`.

---

## 3. P1-3 — `fallback_session` per-frame non osservabile

**File:** `src/render_graph/pipeline/scene.cpp`.

Il ramo di compatibilità che costruisce `std::optional<runtime::JobSession>`
ora incrementa `fallback_session_constructions` (nuovo contatore in
`CHRONON_COUNTERS_CORE`). La guardia resta un branch runtime: un delta non-zero
su un export di produzione è ora il segnale misurabile che un caller ha perso il
lease (regressione ~70% memset in arena teardown). Nessun warning per-frame
(evita log spam); il contatore è la superficie di misura.

---

## 4. P1-1 — MTSDF `face_mutex` process-wide

**Stato: ACTIVE (nessun refactor speculativo).** Il ticket
[`TICKET-HOTPATH-TEXT-MTSDF-CONTENTION`](TICKET-HOTPATH-TEXT-MTSDF-CONTENTION.md)
è già implementato nella parte strutturale: la sezione critica contiene solo il
lavoro FreeType-owned, il resto è fuori dal lock. La granularità per-face è
gated dalla Demolition Debt con exit condition misurabile
(`mtsdf_lock_wait_us` che non cresce con i worker, su corpus multi-font).
L'audit stesso la colloca a "eseguire quando la telemetria lo mostra"; i
contatori esistono. **Nessuna modifica** — un refactor senza corpus è
indistinguibile da una regressione.

---

## 5. P2 — Manutenzione

| # | Item | Azione | Stato |
|---|---|---|---|
| P2-1 | `JobSession::arena()` deprecata, caller CLI `command_bake_layer.cpp:95` | Il contratto di deprecazione (`job_session.hpp`) nomina `ExecutionScope::make_root(session, session.arena(), ...)` come percorso *pre-frame* sanzionato. Il comando è one-shot (nessun frame loop, nessun owner concorrente dello slot 0) quindi l'hazard di aliasing D-02 non è applicabile. Aggiunto commento che cita il contratto: niente lease con nulla da proteggere. | DOCUMENTED (no behaviour change) |
| P2-2 | `graph_cache_coordinator.cpp` dtor → `std::terminate()` | La policy resta terminate (contratto di ownership), ma ora è **osservabile**: `spdlog::critical` con il messaggio + `graph_cache_restore_failures` (nuovo contatore in `CHRONON_COUNTERS_GRAPH`) prima del terminate. | CODE DONE |
| P2-3 | Commenti CMake stale | `src/backends/text/CMakeLists.txt` non cita più `resources/glyph_atlas.cpp` (inesistente in qualunque revisione git); `src/render_graph/CMakeLists.txt` usa `executor/node_runner.cpp` / `executor/executor_levels.cpp` (path reali). | CODE DONE |
| P2-4 | Due famiglie hash (`core::hash::HashBuilder` vs `graph::hash_string/hash_combine`) | Entrambe wrapper XXH64, deterministiche, documentate. Consolidamento = churn > beneficio. | NOTED (no action) |
| P2-5 | Artefatti locali non tracciati | `test_svg_path_loader.cpp` alla root **non esiste più** (verificato: `git ls-files --error-unmatch` fallisce, file assente). I 3 `chronon3d-2026-09-10-*.log` sono coperti da `.gitignore` (`*.log`), quindi zero impatto sul repo. | ALREADY RESOLVED / NOISE |

---

## 6. Verifica

Build: `linux-video-test` (`-j8`, RAM controllata, mai oltre ~25 GB/costantemente
sotto la soglia OOM). Il cambio all'X-macro dei contatori invalida ogni TU che
include `counters.hpp`, quindi la ricompilazione è ampia per costruzione:
`509/509` step sui test target, `331/331` sui target registry/render_graph.

| Suite (ctest canonico) | Esito |
|---|---|
| `chronon3d_zero_copy_capability_tests` (include `test_encoder_capability`) | **PASS** 14/14 |
| `chronon3d_compositor_tests` | **PASS** 69/69 |
| `chronon3d_render_job_contract_tests` | **PASS** 11/11 |
| `chronon3d_render_graph_tests` | **PASS** 376/376 (81.7s) |
| `chronon3d_media_video_tests` | **PASS** 29.9s |
| `chronon3d_core_tests` — filtro `*RenderCounters*` | **PASS** 8/8 (gate naming/timing OK) |
| `chronon3d_backend_registry_tests` | **FAIL** 46/58 — pre-esistente, §7 |

I 4 TU modificati (`gpu_text_run.cpp`, `scene.cpp`,
`graph_cache_coordinator.cpp`, `command_bake_layer.cpp`) sono anche passati da
un `-fsyntax-only` isolato con gli include/define reali del preset.

**NOT RUN:** compilazione effettiva di `crash_handler.cpp` /
`watch_service.cpp` (richiedono `cpptrace`/`efsw` non installati localmente);
la prima esecuzione della lane CI `full-validation` su PR è la verifica
autoritativa. Se uno dei due TU è marcio, la lane lo scopre — è l'intento di
P0-1.

---

## 7. Blocker emerso: suite Vulkan-gated rossa (pre-esistente)

**Sintomo.** `chronon3d_backend_registry_tests`: 58 casi, 46 PASS, **12 FAIL**
(14 assertion). Dominante:
`std::logic_error("Vulkan planned surface physical slot is already reserved")`
da `src/backends/vulkan/vulkan_surface_authority.hpp::bind_planned_exact`,
che fa fallire i casi di aliasing (`physical slots back several logical handles
with one VkImage`). Secondari: `Vulkan transform references incompatible
surfaces` e i pixel-check del text-run Vulkan.

**Perché non è causato da questo lavoro.**

1. Il check che lancia è stato introdotto da `077a44275` (2026-09-05,
   "prevent plan from aliasing reserved video/NVENC slots") — nessuno dei TU
   toccati in questa bonifica lo sfiora.
2. Il TU del test non compilava dal commit `4a6b58ce1` (2026-09-10), quando
   `GpuGlyphAtlas::acquire` è passato a `Result<GlyphLocation, ...>` senza
   aggiornare `test_backend_registry_parity.cpp`. La suite era quindi
   *impossibile da eseguire*, non "verde".
3. Riparare il compile (§ sotto) fa emergere i 12 fallimenti: sono lo stato
   reale che il compile error mascherava.

**Fix di sblocco incluso in questo ticket** (necessario: `backend_registry` è
nella suite minima pre-push di `AGENTS.md`): in
`tests/registry/test_backend_registry_parity.cpp` il caso
`VRAM glyph atlas keeps glyphs resident and preserves placement metrics` ora
fa `REQUIRE(x.has_value())` e legge i campi via `x->` invece di `x.`. Prima:
12 errori di compilazione; dopo: `-fsyntax-only` pulito.

**Aggiornamento 2026-09-13 — aliasing ripristinato (code fix).** L'aliasing
pianificato non è più vietato dal check: in `begin_plan_batch` il plan registra
i propri occupant per slot (`register_planned_occupant`) prima dei bind, e
`bind_planned_exact` (così come la collision validation "with initialized
content") ammette un secondo handle sullo stesso slot solo se il plan lo ha
registrato come alias (`is_registered_alias`). Un occupant **non** pianificato
(il ring NVENC/encode creato fuori dal plan) resta hard-fail: il "steal" case
resta fail-by-design, che era lo scopo di `077a44275`.

**Verifica 2026-09-13 (preset `linux-video-test`, Vulkan ON, GPU host).**
Costruito `build/chronon/linux-video-test/tests/chronon3d_backend_registry_tests`
e la suite è ora **49/59** (era 46/59). Due cause reali rimosse:

1. **Aliasing pianificato** (sopra) — l'eccezione `physical slot is already
   reserved` non compare più nel log.
2. **Retirement transiente al primo batch**: `retire_completed_frame_transient_surfaces()`
   chiamava `release_frame_transient_handles()` incondizionatamente, anche
   quando nessun frame era in volo. Al primo `begin_plan_batch` questo azzerava
   `initialized` sulle surface transitorie pre-caricate dall'host, che il plan
   poi non ri-materializzava (stesso desc) → `transform` falliva con
   `src-uninitialized`. Ora il retirement avviene solo se almeno un frame è
   effettivamente completato (`retired_any`).

Inoltre `bind_planned_exact` (e la validation in `begin_plan_batch`) ora
ammettono che il plan ri-posizioni una surface legata dinamicamente **finché il
vecchio slot non ha contenuto live** — il plan è l'authority di placement.

**Restano 10 rossi** (`46→49`): le surface pre-caricate dall'host con
`create_surface()` ottengono slot dinamici che collidono con lo slot namespace
del plan quando il planner fa aliasing; servono la mappa
`plan_slot → physical_slot` (con consolidamento degli slot dinamici) e due fix
di harness testo/atlas (styled-cache `ctx.runtime`, `atlas.stats().hits`,
parità pixel text-run). Tracciati in `docs/FOLLOWUP_TICKETS.md`; non fanno
parte dello scope di questo ticket.

**Coverage gap correlato:** aggiungere una lane CI che compili un preset
Vulkan (`linux-video-test`) è il passo che avrebbe scoperto *entrambi* i
problemi; oggi nessun workflow lo fa.
