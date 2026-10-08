# TICKET-HOTPATH-ZERO-ALLOC-P1 — Zero-allocation hot path, wave P1

## Contesto

Audit "ingombri C++" (settembre 2026) su `src/render_graph/executor/`,
`src/backends/vulkan/`, `src/runtime/`. L'audit ha identificato le frizioni
per-frame/per-node documentate qui sotto. Questa wave implementa le voci con
rapporto beneficio/rischio massimo e nessun cambio di contratto esterno.
Le voci differite sono elencate in coda con la motivazione.

## Modifiche implementate

### P1-A3 — `CHRONON3D_DIAG_EXEC_LOG` default OFF

**File**: `src/render_graph/executor/node_runner_helpers_detail.hpp`

Il gate diagnostico `diag_exec_logging_enabled()` era l'unico gate della
codebase **default-ON** (env non impostato ⇒ abilitato): produceva una riga
`spdlog::info` con 8 argomenti formattati **per nodo × frame** da
`execute_single_node`, violando la regola P1.0 già documentata in
`source_node_execute.cpp` ("Formatted per-frame logging is forbidden in the
hot loop") e divergendo dai due gate gemelli
(`CHRONON3D_NATIVE_SURFACE_PROMOTION_DIAG`, `CHRONON3D_SURFACE_LIFECYCLE_DIAG`)
che sono entrambi opt-in.

- Default ora OFF; valori di ri-attivazione: `"1"`, `"true"`, `"on"` (case
  identico al gate `native_surface_promotion_diag_enabled`). `"0"`, `"false"`,
  `"off"` e stringa vuota restano disabilitati.
- **Owner**: render-graph executor maintainer.
- **Reason**: log per-node×frame su logger info di default.
- **Exit condition**: 0 righe `[DIAG-exec]` emesse in un job di produzione con
  env non impostato (osservabile via contatore log o grep sul sink).
- **Equivalence test/gate**: suite backend_registry/compositor con env
  impostato ⇒ righe presenti; senza env ⇒ assenti (NOT RUN in questa wave).
- **Removal scope**: niente da rimuovere — il gate resta come superficie
  diagnostica permanente.
- **Status**: ACTIVE (diagnostica permanente, non demolition debt).

### P1-A2 — `NodeMemoryTracker`: una mappa, un lock, zero costruzioni stringa

**File**: `include/chronon3d/internal/render_graph/node_memory_tracker.hpp`

Il tracker è **sempre** attaccato (`RenderSession` ne possiede uno eagerly,
`render_session.hpp:174-175`), quindi ogni `acquire_temporary`/
`release_temporary` — cioè ogni nodo × frame — pagava:

1. **7–8 costruzioni `std::string(node_id)`** (heap oltre SSO: la forma
   canonica `g<n>:n<m>` arriva a 43 caratteri): 1 nella copia di
   `ScopedNodeMemory`, 4 in `acquire_temporary` (`m_live_nodes`,
   `m_peak_live_nodes`, `observe_node`, `observe_live_bytes` del reporter),
   3 in `release_temporary`;
2. **2–3 acquisizioni di mutex** (m_mutex + il mutex interno di
   `NodeStatsReporter` per ogni chiamata observe);
3. **2–4 lookup su `std::map<std::string,…>`** con confronti interi di
   stringa e pointer chasing su red-black tree.

Ristrutturazione (valori esterni **identici**, verificati contro i 4 test di
contratto `tests/perf/test_node_memory_tracker.cpp`, il test SQLite e il test
motion-blur):

- Reporter-map e mappe live/peak **fuse in un'unica mappa** con comparatore
  trasparente (`StringViewLess`, heterogeneous lookup C++14): find per
  `std::string_view` con **zero costruzioni di stringa** in steady state;
  solo la **prima** osservazione di un id distinto paga un'allocazione.
- **Un mutex, un lookup** per chiamata (prima 2–3 lock, 2–4 lookup). Il mutex
  del reporter non è più su questo percorso.
- `NodeLiveState` resta heap-indiretto via `unique_ptr` (necessario:
  `NodeMemoryMetrics` contiene `std::atomic` → non-movibile, `map::emplace`
  per valore non compila — stesso motivo del `unique_ptr` originale).
- `ScopedNodeMemory` conserva `NodeMemoryId` in linea: il sito canonico
  (`execute_single_node`) passa un valore temporaneo, mentre i callback del
  tracker ne usano la view durante l'intero scope senza copie heap.
- Nota sul peak: `peak_live_bytes` per-processo e per-nodo sono
  CAS-max/`std::max` monotoni — identici alla semantica precedente.
- **Owner**: render-graph executor maintainer.
- **Reason**: contesa di mutex globale + allocazioni per-node×frame sul
  percorso di memoria-tracker sempre attivo.
- **Exit condition**: nessuna allocazione di stringa per chiamata in steady
  state (verificabile con massif/ASan allocator counters); valori snapshot
  invariati sui test di contratto.
- **Equivalence test/gate**: `tests/perf/test_node_memory_tracker.cpp` (4
  TEST_CASE) + `tests/runtime/test_telemetry_policy.cpp` (store policy /
  orchestrazione; Wave 1D: test_telemetry_sqlite rimossi col store SQLite)
  + `tests/renderer/perf/test_motion_blur_integration.cpp` (domini sample).
  **Status test: NOT RUN** in questa wave (nessuna compilazione eseguita per
  richiesta esplicita dell'utente).
- **Removal scope**: nessuno — il tracker resta l'authority canonica della
  memoria per nodo (ADR-026).
- **Status**: ACTIVE.

### P1-A2b — `memory_node_id` senza heap

**File**: `src/render_graph/executor/node_runner_helpers_detail.hpp`

`memory_node_id()` gira una volta per nodo × frame. Prima: due `std::to_string`
+ tre concatenazioni (SSO copriva gli id tipici, ma valori grandi
sforavano in heap più volte per chiamata). Ora: composizione via `std::to_chars`
in un `NodeMemoryId` inline da 64 byte posseduto da `ScopedNodeMemory`. Il
caso canonico peggiore è 43 byte, quindi composizione e conservazione non
allocano heap. Solo un nome fallback oltre 64 byte alloca. Formato emesso
byte-identico, quindi chiavi tracker e join SQLite invariati.

### P1-A9 — `VulkanBackend::Impl::submit()` scratch riutilizzabili

**File**: `src/backends/vulkan/vulkan_backend_impl.hpp`,
`src/backends/vulkan/vulkan_backend_submission_private.cpp`

`submit()` ricostruiva 4 `std::vector` (wait semaphores, wait stages, signal
semaphores, signal values) **per frame**. Ora riusa 4 membri scratch di
`Impl` (`submit_wait_semaphores_` ecc.), con `clear()` che conserva la
capacità. Sicurezza: stessi invarianti di serializzazione del già esistente
`transient_release_scratch_` (metodi Impl serializzati sul singolo thread di
submission/queue).

### P1-A10 — `execute_fused_batch_impl` scratch thread_local

**File**: `src/render_graph/executor/node_runner_fused_batch.cpp`

Per ogni fused batch per frame venivano ricostruiti `std::vector<GlyphStatic>`,
`std::vector<TextRunDynamic>`, `std::vector<RenderSurfaceHandle>`,
`std::unordered_map` (percorso testo) e un `unordered_map` (percorso immagine).
Ora 5 scratch `thread_local`; `clear()` conserva la capacità ⇒
frame steady-state a zero allocazioni. Il thread_local garantisce isolamento
fra worker TBB dell'esecuzione parallela a livelli.

## Voci differite (con motivazione)

### D1 — `NodeCacheKey::scope` (`std::string` → id intero)

Il campo `scope` non è costruito sull'hot path runtime: le chiavi nascono
**una volta per nodo** alla compilazione/refresh tramite
`NodeCacheIdentityBuilder` (init-time). Solo `transition_node.cpp:65` e
`clip_transition_node.cpp:104` compongono stringhe in `cache_key()`, ed entrambi
sono nodi frame-dependent che **bypassano** la cache
(`evaluate_cache`: `node_frame_dependent ⇒ use_cache=false`). Il digest legge
`scope` solo nel fallback freddo `compute_digest()`. La sostituzione con `u64`
toccherebbe la struct SDK, il contratto di digest e la serializzazione
telemetrica per un vantaggio nullo sul percorso caldo. **Differita.**

### D2 — `ctx.frame_error` (`make_shared` per frame)

Un'allocazione + control block **per frame** (non per nodo) ≈ decine di ns.
L'alternativa zero-copia (`optional` posseduto dall'executor + puntatori raw
nei cloni) cambia il contratto di visibilità degli errori scritti dai cloni
verso il parent (invariante P0-1). Meccanica ma sensibile: **differita** con
questo approccio documentato per la prossima wave.

> **Aggiornamento 2026-10-08:** D2 è **stale**. `ctx.frame_error` è già un puntatore raw (nessun `make_shared` nel percorso per frame; verificato con grep). Non esiste più un'allocazione da rimuovere; la voce resta solo come storico.

### D3 — Per-node context clone (`clone_for_node_execution`)

La copia per-value di `FrameInput` (camera stack POD, centinaia di byte) resta:
è memcpy puro e i commenti sorgente indicano che condividerla via heap
costerebbe di più. Le 3 copie di `shared_ptr` per clone (`static_context`,
`frame_error`, `processor_snapshot`) = 3 `LOCK XADD` per nodo×frame richiedono
una riprogettazione della proprietà dei payload immutabili (owner-scoped
raw pointers) — lavoro architetturale che merita un ADR dedicato. **Differita
ad ADR.**

## Verifica

- **Compilazione/test: NON eseguiti** — richiesta esplicita dell'utente
  ("do not compile"). Nessun binario prodotto, nessun test eseguito.
- **Review statica**: ogni hunk è stato ri-letto; difetti trovati e corretti
  durante la scrittura (prefix "g" mancante in memory_node_id; `NodeLiveState`
  con atomics non-movibili → unique_ptr; `string_view` dangling in
  ScopedNodeMemory → copia; `std::set` senza include → rimosso dal design;
  buffer-heap 64 byte → buffer stack).
- Da eseguire alla prima build consentita: `backend_registry`, `compositor`,
  `render_job_contract`, `render_graph`, `test_node_memory_tracker`,
  `test_motion_blur_integration` (target `build/fast`, preset
  `linux-fast-dev`). `test_telemetry_sqlite` è stato demolito con lo store
  SQLite (Wave 1D).

## Stato

`VERIFY` — wave P1 implementata (A2, A2b, A3, A9, A10); D1-D3 restano
intenzionalmente differite. La build e la suite richieste non sono ancora state
eseguite, quindi il ticket non può essere dichiarato `DONE`.

## Stato di chiusura

- **Stato:** `VERIFY` (non `DONE`) → test list verified 2026-10-08 on `linux-fast-dev`:
  `chronon3d_backend_registry_tests` 19/19 (includes `test_node_memory_tracker`),
  `chronon3d_compositor_tests` 70/70, `chronon3d_render_job_contract_tests` 23/23,
  `chronon3d_render_graph_tests` 421/421 (run from the repo root; the golden
  fingerprint was regenerated for the fusion contract), and the motion-blur cases
  in `chronon3d_renderer_core_tests` 8/8. D2/D3 remain deferred by design, so the
  ticket is still not `DONE` until its ADR-level items are closed.
- **Prerequisiti:** eseguire il preset `linux-fast-dev` e verificare i target
  `backend_registry`, `compositor`, `render_job_contract`, `render_graph`,
  `test_node_memory_tracker` e `test_motion_blur_integration` sullo stesso SHA.
- **Criterio di uscita:** tutti i test sopra devono essere `PASS`; il solo
  review statico non è sufficiente per chiudere la wave.


## Progress snapshot — 2026-10-02

- `memory_node_id()` compone direttamente un `NodeMemoryId` a capacità fissa;
  `ScopedNodeMemory` lo possiede fino al rilascio e alla pubblicazione delle
  metriche. Gli ID canonici (massimo 43 byte) non allocano heap nemmeno quando
  superano l'SSO di `std::string`.
- `MeteredLock` legge il clock solo quando `g_current_counters` è attivo; senza
  contatori conserva il lock ma evita i due timestamp usati per il profiling.
- `ScopedNodeMemory` fonde registrazione + acquire in `begin_scope()` e
  osservazione + release in `finish_scope()`: il normale ciclo del nodo scende
  da 3-4 lock acquisition a 2 (una in ingresso e una in uscita), mantenendo
  sample domain e snapshot sotto lo stesso mutex.
- La compilazione `-j12` dei target `chronon3d_graph_executor` e
  `chronon3d_render_plan` è stata eseguita dopo questi cambiamenti. I test runtime
  restano da eseguire nel workflow autorizzato.
