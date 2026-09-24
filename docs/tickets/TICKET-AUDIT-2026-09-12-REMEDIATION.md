# TICKET-AUDIT-2026-09-12-REMEDIATION — Bonifica dell'action list "Technical Audit Report"

> **Stato:** VERIFY — il codice dello scope audit è done e il build locale è verde; il blocker Vulkan storico di §7 non appartiene a questo scope, ma la chiusura same-SHA resta da certificare.
> **Origine:** `Chronon3D Technical Audit Report` (2026-09-12), action list ordinata.
> **Scope:** P0-1, P0-2, P1-1, P1-3, P2-1, P2-2, P2-3, P2-4, P2-5.
> **Vedi anche:** `docs/AUDIT-2026-09-12-VIDEO-CODEC-BOUNDARY.md` (audit diverso, stesso giorno).

## Stato di chiusura

- **Stato:** `VERIFY`, non ancora `DONE`.
- **Fatto:** feature-gated TU, fallback GPU→CPU del testo, osservabilità di
  `fallback_session` e la bonifica P2 sono implementate; il build locale
  `linux-video-test` è verde.
- **Blocco storico fuori scope:** la suite Vulkan-gated è arrivata a
  `59/59 PASS (847/847 assertion)` nel follow-up del 2026-09-16. I due problemi
  di placement descritti in §7 restano tracciati separatamente e non devono
  essere usati per dichiarare verde questa bonifica.
- **Prerequisiti di chiusura:** eseguire la lane `linux-ci-full-validation` e
  la suite canonica sullo stesso SHA del commit di closeout; riconciliare la
  snapshot storica `50/59` con l'evidenza `59/59`; poi sostituire `VERIFY`
  con `DONE` e citare lo SHA certificato.

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

**Restano rossi** (`46→49` al 2026-09-13; **50/59** dopo il fix di harness
sotto): le surface pre-caricate dall'host con `create_surface()` ottengono slot
dinamici che collidono con lo slot namespace del plan quando il planner fa
aliasing; servono la mappa `plan_slot → physical_slot` (con consolidamento degli
slot dinamici) e fix di harness testo/atlas (styled-cache `ctx.runtime`,
`atlas.stats().hits`, parità pixel text-run). Tracciati in
`docs/FOLLOWUP_TICKETS.md`; non fanno parte dello scope di questo ticket.

**Aggiornamento 2026-09-13 (seconda sessione) — 49→50/59 + diagnosi del
placement.** Il fix di harness `atlas.stats().hits` è atterrato: il caso
`VRAM glyph atlas batch residency matches per-glyph lookups` asseriva
`hits == 1` dopo due `acquire()` di chiavi nuove e un `resolve_resident()` (pura
proiezione), e si aspettava che un batch vuoto azzerasse l'output. Il contratto
canonico in `include/chronon3d/runtime/gpu_glyph_atlas.hpp:254-256` dice invece
che le entry oltre `min(keys.size(), out.size())` restano **intatte**: le due
assertion sono state corrette contro il contratto → suite **50/59**.

È stato poi **tentato** il consolidamento degli slot descritto sopra, con esito
negativo e **revert**. Esito dell'esperimento (traccia
`CHRONON3D_TRACE_PLAN_BIND=1`):

- Il plan assegna `allocation.physical_slot` come indice **locale al plan**
  (es. `slots=2` → slot 0/1) e `bind_planned_exact` lo usa come slot fisico
  globale. Piani distinti nello stesso `command_batch`, o surface pre-legate con
  `create_surface()`, collidono quindi nello stesso namespace: da qui le 5
  eccezioni (`... attempted to change physical slot` / `... collision with
  initialized content`).
- Un rimedio "retirement dei transient nello slot namespace del plan" fa
  **sparire le eccezioni** ma azzera anche i binding di surface che il plan
  corrente non referenzia: nel caso a 3 overlay indipendenti gli output del
  primo piano restano unbound e `download_surface` fallisce. Revertito.

**Design richiesto** (non un fix di una riga): il backend deve mantenere una
mappa `plan_slot → physical_slot` **per istanza di plan**, così che (a) piani
distinti nello stesso batch ottengano slot fisici distinti e (b) la
riesecuzione dello stesso plan riusi la mappa, preservando l'aliasing
intrapiano (`gpu_surface_copy_frames == 0`). Serve un'identità di plan stabile
più una policy di lifecycle per la mappa; il desc/`initialized` del nuovo slot va
riconciliato col contenuto precedente quando la mappa è nuova.

**Aggiornamento 2026-09-13 (terza sessione) — due difetti distinti, con
conflitto di contratto.** La ricognizione ha mostrato che i 9 rossi non sono un
solo problema.

*Difetto 1 — `ResourcePlanner` aliasa contenuto esterno.* In
`include/chronon3d/runtime/resource_plan.hpp` la decisione di aliasing è
`physical.last_use < request.first` + `compatible()`. Due risorse **entrambe
solo-lette** e **caricate dall'host** (es. `background` e `overlay` in
"minimal background + 1 overlay") hanno intervalli disgiunti, quindi finiscono
nello **stesso slot**: una delle due perde le proprie texel e la composizione
legge dati sbagliati. `ResourceResidency` ha già il vocabolario giusto
(`requires_dedicated_allocation() == exportable || domain == External`,
`allows_transient_aliasing()`), ma i test dichiarano le surface caricate come
residenti normali, quindi `allows_transient_aliasing()` è `true` e l'aliasing
scatta. Il planner **potrebbe** inferire il contenuto esterno dal flusso dei
pass (un `first` che è una lettura), ma per `medium` il `first` di `background`
è una `StorageReadWrite` (destinazione di `composite`) il cui contenuto è
comunque richiesto dall'oracolo CPU.

*Difetto 2 — slot locali al plan.* `allocation.physical_slot` è un indice locale
al plan usato come slot fisico globale; con più plan nello stesso batch o
surface pre-legate con `create_surface()` gli indici collidono (le 5 eccezioni).

**Conflitto.** Il caso `medium CommandPlan` asserisce
`physical_slots.size() == 2` **e** parità pixel, dove `background` (contenuto
dall'host, letto da `composite`) è aliasato con `transformed` (prodotto
dentro il plan). Le due cose sono compatibili solo se il backend (a) tiene gli
occupanti portatori di contenuto in slot fisici distinti — cioè il plan dice
`2` e il backend ne usa di più — **oppure** (b) migra i byte dell'import nello
slot condiviso. Un fix al solo planner porta a 3–4 slot e fa fallire
l'asserzione `== 2`; un fix al solo backend non salva `minimal background`. La
scelta tra "il conteggio di aliasing del plan è la verità, il backend
disaccoppia" e "l'aliasing deve preservare il contenuto importato" è una
decisione di contratto del maintainer, non un fix meccanico.

**Stato:** parcheggiato per decisione di contratto. Codice di produzione
invariato (nessuna modifica non verificata lasciata sul percorso certificato).

**Coverage gap correlato:** aggiungere una lane CI che compili un preset
Vulkan (`linux-video-test`) è il passo che avrebbe scoperto *entrambi* i
problemi; oggi nessun workflow lo fa.

**Aggiornamento 2026-09-13 (quarta sessione) — retirement per frame-slot,
reintrodotto in forma ristretta con regression test.** Il rimedio revertito
nella seconda sessione era un **flush completo** dei binding transienti nello
slot namespace del plan (iterava *tutti* i `surface_bindings` transienti): per
questo azzerava surface che il plan corrente non referenziava. Il codice adotta
ora un meccanismo **diverso e ristretto**, in
`VulkanSurfaceAuthority::record_frame_slot_transient()` /
`retire_frame_slot_transients()`:

- `ensure_surface()` registra un handle **solo** quando un frame batch è attivo
  (`frame_batch.active`), nel vettore del **frame-slot corrente**
  (`frame_slot_transient_handles[next_slot]`).
- Il retirement avviene quando quel frame-slot è effettivamente completato
  (riuso dello slot in-flight in `begin_frame_batch`, o `wait_for_pending()`) e
  cancella **solo** i binding registrati per quello slot.

Guardia: una surface caricata dall'host fuori da un batch (es. `create_surface()`
in preparazione) **non viene mai registrata**, quindi nessun confine di frame la
ritira. È esattamente la failure mode del rimedio revertito, ed è ora coperta da
regression test in `tests/backends/test_vulkan_surface_materializer.cpp`:
"a recorded frame-slot transient is retired while a host surface survives",
più l'isolamento cross-slot e il bound del pool caldo.

Questo **non** chiude i 9 rossi di placement: dipendono dalla mappa
`plan_slot → physical_slot` per istanza di plan (Difetto 2) e dal conflitto di
contratto sul caso `medium`, entrambi aperti. Il retirement per-slot riguarda la
crescita VRAM, non il placement.

**Bound VRAM corretto.** Il warm pool trattiene al massimo
`kMaxRetainedTransientSlots = 8` slot transienti non usati: a 1080p RGBA32F
(~33 MB per immagine) sono **~265 MB**, non "8 MB". È quel tetto a impedire
l'accumulo per-frame fino all'esaurimento della VRAM.

**Fix collaterale — assertion stale su `valid()`.** Due casi di
`test_vulkan_surface_materializer.cpp` assumevano che `valid()` riflettesse
`initialized`. `d1f43877a` (fix dell'hang Vulkan↔CUDA) ha **deliberatamente**
cambiato il contratto a "immagine materializzata (non-null)", quindi i due
`CHECK_FALSE(valid(...))` erano stale e il target falliva **14/16 anche su
`HEAD`**. Le assertion ora verificano `image.initialized` direttamente.

**Aggiornamento 2026-09-13 (quinta sessione) — la lane Vulkan-gated esiste.**
Chiuso il gap di copertura che questo ticket nomina: nessun workflow compilava
le suite Vulkan-gated (`native-video.yml` configura lo *stesso* preset
`linux-video-test` con `-DCHRONON3D_ENABLE_VULKAN=OFF`). Aggiunti:

- `scripts/ci/verify_vulkan_gated_unit_tests.sh` — configura (se serve),
  compila ed esegue le 8 suite headless;
- job `vulkan-gated-unit` in `.github/workflows/native-video.yml`.

Esito locale: **8/8 PASS** (`shader_abi`, `vulkan_debug_context`,
`vulkan_descriptor_arena`, `vulkan_kernel_store`,
`vulkan_surface_materializer`, `vulkan_interop_generation`,
`vulkan_submission_ring`, `vulkan_upload_ring`).
`chronon3d_vulkan_surface_materializer_tests` è ora **17/17**.

**Rosso pre-esistente, NON escluso per comodità:**
`chronon3d_text_tile_raster_canary_tests` fallisce 13 assertion di pixel
(`full.r/g/b`, `half.*`, `mtsdf.*`, `counts[*] >= 1` tutte a 0 → il dispatch
compute non scrive). È **provato non correlato** a questo change set:
neutralizzando completamente il retirement per-slot
(`record_slot_transient_surface`/`retire_frame_slot_transient_surfaces` no-op)
le 13 failure si riproducono **identiche**. Inoltre il canary non usa
`VulkanBackend`/la surface authority: fa dispatch SPIR-V raw. Per questo è
escluso dalla lane — includerlo renderebbe verde/rosso il gate per una ragione
che non è un contratto di backend. Va diagnosticato a parte (driver/GPU).

**Terza assertion stale chiusa:** `test_vulkan_debug_context.cpp` si aspettava
`stats.passes_executed >= 6` per 6 `draw_node`, ne otteneva 5. Causa: il nodo
Path con `cubic_to` è **deliberatamente** respinto dal gate di CPU-exactness di
`try_native_path_stroke` (solo un singolo segmento retto opaco è
native-equivalente; una cubica sfumerebbe i giunti e intaccherebbe i cap).
L'expectation `>= 6` precedeva quel gate: ora è `>= 5` con la motivazione, e il
target è **8/8**.
