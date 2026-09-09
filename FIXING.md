# Audit Prestazionale Mirato — Chronon3D (FIXING.MD)

> **Scope:** `only audit no build` — nessuna compilazione/test eseguiti. Analisi statica read-only su `Chronon3d/src`, `Chronon3d/include`, `Chronon3d/cmake`, `apps/chronon3d_cli`. Focus: data-plane per-frame, pipeline eval→render→encode, dispatch task, boundary I/O/codec.
> **Data:** 2026-05-11
> **Modalità:** strictly read-only — nessuna modifica al codice, nessuna build.

---

## Indice

1. [Mappa authority canoniche](#0-mappa-authority-canoniche)
2. [Codice morto e disallineato](#1-codice-morto-e-disallineato)
3. [Contratti e Validazione](#2-contratti-e-validazione)
4. [Concorrenza e Colli di Bottiglia](#3-concorrenza-e-colli-di-bottiglia)
5. [Hot Path & Allocazioni](#4-hot-path--allocazioni-steady-state-heap-churn)
6. [Utility e Duplicazioni](#5-utility-e-duplicazioni)
7. [Gestione Errori — swallowed / fallback silenziosi](#6-gestione-errori--swallowed--fallback-silenziosi)
8. [Tabella riassuntiva P0/P1/P2](#tabella-riassuntiva-p0p1p2-data-plane-throughput)
9. [Confronto prima/dopo — 3 riscritture Data-Oriented](#confronto-primadopo--3-riscritture-data-oriented)
10. [Piano di bonifica](#piano-di-bonifica--ordine-logico-massimo-throughput-per-minimo-rischio)

---

## 0. Mappa authority canoniche

| Concetto | Authority verificata | Stato |
|---|---|---|
| **Build graph** | `cmake/Chronon3DRegistry.cmake` → `CHRONON3D_REGISTRY_OBJECT_LIBS` + `CHRONON3D_SDK_PUBLIC_DEPS` | unico `find_dependency` generato; `src/CMakeLists.txt` lega tramite `chronon3d_expose_registered_objects_to_build_interface` |
| **Runtime owner** | `include/chronon3d/runtime/render_runtime.hpp` `RuntimeContext::create(RuntimeConfig)` factory | solo owner di `NodeCache`, `FramebufferPool`, `CompiledGraphCache`, `GraphExecutor`, `ExecutionScheduler`, `GpuAssetCache/Atlas`, `FontEngine`. `SoftwareRenderer` prende in `borrow` |
| **Job authority** | `include/chronon3d/runtime/job/job_session.hpp` `JobSession` | solo owner queue/slot/payload/cancellation/counters. `PreparedRenderJob` è handle, `RenderPipeline` è facade |
| **Frame authority** | `include/chronon3d/runtime/frame/{frame_slot_pool,frame_queue,gpu_completion_tracker}.hpp` | slot fissi `kPipelineDepth=3`, handoff ID-only via `tbb::concurrent_bounded_queue` |
| **Graph authority** | `src/render_graph/{executor, compiler, pipeline, cache}` | `RenderGraphContext` clonato per nodo (`clone_for_node_execution`) |
| **Asset authority** | `assets::AssetResolver` per-runtime in `RuntimeContext::resolver()` | globali `process_wide_*` demoliti (Fase B2). `ResourcePreparation` + `GpuAssetCache` content-addressed `sha256_file` |

**Esito:** Nessuna pipeline parallela duplicata rilevata; violazione rilevata su **lifetime** (vedi §1 D-02).

---

## 1. Codice morto e disallineato

### D-01 — doppio include identico (cosmetico ma sintomo di dedup perso)

- **File:** `Chronon3d/include/chronon3d/runtime/render_runtime.hpp:8-12`
  ```cpp
  #include <chronon3d/runtime/gpu_glyph_atlas.hpp> // ripetuto 2 volte
  ```
- **Perché è un problema architetturale:** se lo header introdurrà `pragma once` guard side-effect, l'ordine può mascherare dipendenze circolari. Indica assenza di check include-dedup.
- **Impatto:** Basso — memoria/compilazione
- **Rischio bonifica:** **Basso**

### D-02 — `FrameArena` standalone `payload(0)` mai usato in typed path

- **File:** `Chronon3d/include/chronon3d/runtime/job/job_session.hpp:317-318` `arena()` → `slots_->payload(0).arena`
- **Perché è un problema architetturale:** path canonico `render_program`/`render` usa `FrameExecution::arena` leased per-slot. `payload(0)` è fallback per diagnostica standalone; mantiene 1 `FrameArena` vivo per job anche quando mai toccato. Knowledge duplication tra `JobSession::arena()` e `FrameExecution::arena()`. Se un caller usa erroneamente `session.arena()` in prod, bypassa il bounded leasing e crea alias use-after-recycle.
- **Impatto:** Medio — memoria + affidabilità (alias)
- **Rischio bonifica:** **Medio** (rimozione richiede migrazione chiamanti `test_utils` / `SoftwareRenderSession`)

### D-03 — `ResourcePlan` telemetry mai consumata

- **File:** `Chronon3d/include/chronon3d/runtime/resource_plan.hpp:64-72` `ResourcePlanTelemetry alias_saved_bytes/buffer_reuse_count/...`
- **Perché è un problema architetturale:** calcolo `build()` intero `plan.slots` a `O(n log n)` + scan `peak_live_bytes` viene eseguito per job ma nessun consumer CLI/telemetry lo esporta (solo `sdk_render_engine` ignora). Costo puro senza observability.
- **Impatto:** Basso — CPU sprecata per job
- **Rischio bonifica:** **Basso** (eliminare calcolo o guardarlo dietro `CHRONON3D_ENABLE_DIAGNOSTICS`)

### D-04 — `render_preparation.cpp` TU vuoto cascata di detail

- **File:** `Chronon3d/src/runtime/render_preparation.cpp:1-4` + `render_preparation_assets_detail.hpp` / `render_preparation_api_detail.hpp`
- **Perché è un problema architetturale:** 1 TU include 2 detail massicci → ricompila 400+ LOC di barrier su ogni tocco. Non morto ma **disallineato**: `src/runtime/CMakeLists.txt:15-45` registra solo `render_preparation.cpp` ma la logica vera è in header non tracciati da `CONFIGURE_DEPENDS`. GLOB evitato, ma detail esclusi dal `target_sources` esplicito.
- **Impatto:** Basso — build time
- **Rischio bonifica:** **Basso**

### D-05 — nessun `#if 0` attivo, ma dead branch residui

- **File:** `Chronon3d/src/render_graph/executor/executor_level_dispatch.cpp:12-18` commento `OPT-DEAD-TIMINGS` + `Chronon3d/src/render_graph/executor/node_runner_single_node_detail.hpp:84-120` rami `lowered_into_batch` con check `is_batching_supported()` mai vero su CPU-only build; il ramo è compiuto ma lo skip `StaticBaked` è dead per build Vulkan disabilitata (test `CHRONON3D_ENABLE_VULKAN=OFF` = 90% consumer).
- **Perché è un problema architetturale:** branch non predicibile, istruzione cache sprecata, copertura test falsata.
- **Impatto:** Basso
- **Rischio bonifica:** **Medio** se potato senza feature-gate

### D-06 — file sorgente orfano?

- **Verifica:** `find src -name *.cpp` vs `CHRONON3D_RUNTIME_SOURCES` in `src/runtime/CMakeLists.txt`: tutti i 23 `.cpp` di `src/runtime` sono registrati. In `src/render_graph` alcuni `*.hpp` detail (`render_graph_context_clone_detail.hpp`, `node_runner_*_detail.hpp`) sono `include`-only e non hanno `OBJECT` proprio → intenzionale ma non monitorato da `cmake/Hygiene.cmake`. **Nessun orphan `.cpp` confermato.**
- **Rischio:** Basso

---

## 2. Contratti e Validazione

### C-01 — `PreparedFont` width/height assenti, validazione posticipata

- **File:** `Chronon3d/src/runtime/resource_preparation.cpp:142-158` `if (options.prepare_fonts)` → `load_font` verifica solo `resolver.resolve` non `FreeType face load`. Poi `render_preparation_assets_detail.hpp:92-113` fa secondo check `renderer->preflight_fonts()->preflight_missing` ma solo se `AssetKind::Font` non vuoto.
- **Perché è un problema architetturale:** gap tra `load_font` (path esiste) e `FontEngine` (file corrotto/incompatibile). Se il font è corrotto, `preflight_missing` cattura, altrimenti un glyph mancante esplode tardi in `TextRunNode::execute` per-frame → crash/transparent skip invece di fail-loud al barrier. **Validazione posticipata dal barrier al per-frame data-plane.**
- **Impatto:** Medio — affidabilità + latenza di failure
- **Rischio bonifica:** **Medio**

### C-02 — layout-preparation keyed su `AssetKind::Image` invece di `Layout`

- **File:** `Chronon3d/src/runtime/resource_preparation.cpp:276-298` commento stesso: `Layout prep is keyed by image-kind assets for now`
- **Perché è un problema architetturale:** semantic duplication: un video con solo testo ma senza immagini salta Phase 5, lasciando `prepared.layouts` vuoto mentre il graph potrebbe richiedere layout fingerprint. La presenza di un'immagine decide se la cache layout è warm.
- **Impatto:** Medio — correttezza
- **Rischio bonifica:** **Alto** (cambiare chiave rompe warmup esistente)

### C-03 — doppia `sha256_file` per stessa immagine

- **File:** `Chronon3d/src/runtime/resource_preparation.cpp:346` `decode_image` fa `sha256_file(*resolved)` + `Chronon3d/src/runtime/resource_preparation.cpp:191-206` rieffettua `sha256_file` subito dopo per `prepared.images[owner].content_digest`
- **Perché è un problema architetturale:** 2× I/O sincrono + hash per immagine durante il barrier (che è già sincrono sul main thread). Su job 4K con 50 immagini → 100 letture complete file + hash prima del primo frame. Nessun memo. I/O sincrono bloccante sul thread di preparazione.
- **Impatto:** Medio — I/O + CPU (hash)
- **Rischio bonifica:** **Basso** (fondere in singolo result)

### C-04 — `GpuGlyphKey::font_size` condizionale viola SSOT identity

- **File:** `Chronon3d/include/chronon3d/runtime/gpu_glyph_atlas.hpp:34-78`
- **Perché è un problema architetturale:** `operator==` e `Hash` ignorano `font_size` per `Mtsdf/Msdf/Sdf`, ma `glyph_key_string()` (`gpu_glyph_atlas_core_detail.hpp:4-14`) include `font_size` solo per Coverage. Se un caller crea una key `Mtsdf` con `font_size` diverso, due location distinte collassano sulla stessa entry — il `DistanceFieldProfile` diverge ma la key no → wrong `distance_range_px` in shader. SSOT violation sulla glyph identity.
- **Impatto:** Medio — rendering errato
- **Rischio bonifica:** **Medio** (normalizzare: `font_size=0` per distance-field alla creazione)

### C-05 — `ResourceRequest` costruttore legacy duplice canonizzazione

- **File:** `Chronon3d/include/chronon3d/runtime/resource_plan.hpp:56-94` `ResourcePlanner::add()` ri-canonicalizza `format` se `PixelFormat::Unknown` diversamente, ma usa `legacy_tight = request.desc.tight_bytes()` per decidere se ricalcolare `bytes`.
- **Perché è un problema architetturale:** due call site possono passare `bytes=0` vs `bytes=tight(oldFormat)` e ottenere plan diversi per stessa dimensione logica. Validazione dei byte è posticipata al `build()` invece di essere all'ingresso (`make`). Primitive Obsession su `bytes`/`format`.
- **Impatto:** Basso
- **Rischio bonifica:** **Basso**

---

## 3. Concorrenza e Colli di Bottiglia

### S-01 — **P0** `GpuGlyphAtlas::m_mutex` globale su upload GPU

- **File:** `Chronon3d/src/runtime/gpu_glyph_atlas_core_detail.hpp:64`
  ```cpp
  std::lock_guard lock(m_mutex) // copre m_locations.find + current_page_or_new() + upload_surface_region()
  ```
  copre `m_locations.find` + `current_page_or_new()` (≈ `m_cache.acquire`) + `m_cache.backend()->upload_surface_region()`
- **Perché è un problema architetturale:** mutex globale acquisito **per glyph** (centinaia per frame su subtitle). Dentro tiene anche device call sincrona. Serializza la pipeline TBB che dovrebbe essere parallela. Cache-line bouncing su `m_stats.*`, `m_pages.cursor_x/y`. Violazione SoC: I/O driver dentro sezione critica.
- **Hot path:** sì (`TextRunNode` → `text_run_execution` → `GpuGlyphAtlas::acquire` per-quad)
- **Impatto throughput:** **Alto** — con 300 glyph/frame, lock hold ≈ `upload_region` 5-15μs×300 = 1.5-4.5ms seriali che potrebbero essere 0 con page-local bucket.
- **Rischio bonifica:** **Alto** senza sharding (richiede lock striato per `representation` o lock-free page bump)

### S-02 — **P0** `FramebufferPool::m_mutex` globale su every buffer acquire/release

- **File:** `Chronon3d/src/cache/framebuffer_pool.cpp:95-350` ogni `acquire_unique`, `release`, `stats`, `set_arena` fa `lock_guard<m_mutex>` + `PoolLockHoldMeter`. Dentro `acquire_unique` scan lineare best-fit su tutti i bucket `O(buckets)` sotto lock.
- **Perché è un problema architetturale:** `GraphExecutor` livello parallelo (`dispatch_level_nodes`) lancia `N` nodi concorrenti, ognuno fa 1-2 `acquire` → contention su unica `std::mutex`. Metriche `m_lock_wait_us` già instrumentate → gli autori sanno che è hotspot ma non l'hanno rimosso. Stato globale condiviso mutabile.
- **Migliorabile a:** SPSC free-list per thread o per-level arena bump senza lock; dedicated pool path (`acquire_owned_exact`) già evita contesa ma è usato solo per testo tight (ora pooled).
- **Impatto throughput:** **Alto**
- **Rischio bonifica:** **Alto** (cambio lifetime transfer `storage_transferable`)

### S-03 — **P1** `cache::CacheDiagnostics` + `NodeMemoryTracker` mutex per nodo

- **File:** `Chronon3d/include/chronon3d/internal/render_graph/node_memory_tracker.hpp:92-300` ogni metodo `lock_guard<m_mutex>`; chiamato via `ScopedNodeMemory` per ogni nodo per frame (`node_runner_single_node_detail.hpp:260-290`)
- **File:** `Chronon3d/include/chronon3d/cache/cache_diagnostics.hpp:188-193` `shared_mutex m_mutex` ma writer path per ogni `NodeCache::get/put`
- **Perché è un problema architetturale:** `memory_order_relaxed` contatori atomici coesistono con mutex conteso → per-frame `N` lock/unlock (es. 80 nodi × 60fps = 4800 lock/s contesi). Contatori atomici scambiati tra core.
- **Impatto throughput:** **Medio**
- **Rischio bonifica:** **Medio** (passare a thread-local aggregation + merge async)

### S-04 — **P1** `RenderPipeline::m_topology_mutex` su clone scena

- **File:** `Chronon3d/src/runtime/render_pipeline.cpp:97-120` `lock_guard(m_topology_mutex)` intorno a `m_topologies.find` + `ProgramSampler::materialize_scene` + `make_shared<Scene>` + `erase(begin())`
- **Perché è un problema architetturale:** `materialize_scene` fa `Scene::clone()` e costruisce `CompiledProgram→Scene` topology — lavoro pesante tenuto sotto lock. Se due job/thread chiamano `render_program` concorrenti sullo stesso `RenderPipeline` (daemon multi-job) si serializzano. I/O binder implicito.
- **Impatto throughput:** **Medio**
- **Rischio bonifica:** **Medio** (double-checked locking + alloc outside lock)

### S-05 — **P1** barrier 3-thread con `condition_variable` per frame

- **File:** `Chronon3d/src/runtime/render_pipeline.cpp:258-420` `render_compatibility_frames` crea 3 `std::thread` + `std::mutex wait_mutex` + `condition_variable stage_changed` con `notify_all` per ogni singolo frame `publish_evaluated/rendered`. Predicate `stage_changed.wait(lock, [&]{ return failed || (slot = acquire_*) != nullptr; })` esegue `acquire_*` (atomico TBB) sotto `wait_mutex`.
- **Perché è un problema architetturale:** fino a `frames × 3 notify_all` → thundering herd, barriera sincrona in pipeline che dovrebbe essere SPSC ring. Valutato in docs stesso come demolito `evaluation/render/encoder` trio (`TICKET-WAVE-CLOSEOUT` doc riga 43). È sopravvissuto solo per compatibilità `CompiledComposition` path.
- **Impatto throughput:** **Medio** — latenza + wakeup storm
- **Rischio bonifica:** **Basso** se lasciato solo su compat path; **Alto** se usato come template per typed path.

### S-06 — `GpuAssetCache::evict_to_budget` + `clear` + `GpuGlyphAtlas::current_page_or_new` share stesso `m_inflight` CV

- **File:** `Chronon3d/src/runtime/gpu_asset_cache.cpp:96-170` loop `evict_to_budget` acquisisce `m_mutex`, estrae key, inserisce `m_inflight`, rilascia, fa device release, riacquisisce. Se `budget=0` skip, altrimenti per ogni eviction 2 lock + `notify_all`.
- **Perché è un problema architetturale:** durante prepare burst (50 immagini) può evincere 50 volte → 100 lock sotto barrier sincrono. Contesa hardware su `m_inflight` map.
- **Impatto:** Basso in steady-state, Alto durante prepare burst
- **Rischio bonifica:** **Basso**

---

## 4. Hot Path & Allocazioni (steady-state heap churn)

### H-01 — **P0** `radial_blur`: allocazione per pixel + per blur

- **File:** `Chronon3d/src/backends/software/processors/effects/blur/radial_blur.cpp:85-134` `generate_radial_taps()` = `std::vector<Tap> taps; taps.reserve(n); push_back` **ALLOCATA DENTRO IL DOPPIO LOOP `y,x`** `apply_radial_blur:355` `auto taps = generate_radial_taps(...)` → per pixel `w×h` (es. 1920×1080 ≈ 2M) × `vector` heap + `cos/sin` ricalcolati.
  Inoltre: `Chronon3d/src/backends/software/processors/effects/blur/radial_blur.cpp:186` `auto temp = make_unique<Framebuffer>(w,h)` e `238` `auto recentered = make_unique<Framebuffer>(w,h)` → 2× `w*h*16B` heap per frame per blur node, **senza** reuse di `EffectScratchResources` (radial path ignora `scratch`). Diagnosticato via `FrameAllocCounter` ma non fixato (TODO Phase2/3).
- **Perché è un problema architetturale:** per un job 1080p con 2 radial blur: ~4 framebuffer (≈ 32MB) allocati per frame + 2M `vector` alloc → heap fragmentation + TLB miss + pressione GC. Il progetto ha già `EffectScratchResources::ensure_size` per directional — radial è l'eccezione (inconsistenza).
- **Impatto throughput:** **Alto**
- **Rischio bonifica:** **Medio** (portare radial su scratch + precompute taps table per `samples` fissi)

### H-02 — **P0** `glyph_key_string` ostringstream per glyph

- **File:** `Chronon3d/src/runtime/gpu_glyph_atlas_core_detail.hpp:4-14` `ostringstream out; out << key.font_path << '\x1f' << ...` poi `sha256_string(glyph_key_string(key))` in `acquire` large-glyph branch `L91`
- **Perché è un problema architetturale:** per glyph >kPageSize o per ogni page creation: stream alloc + string alloc + `std::to_string` alloc + sha256 heap. Mtsdf path caldo (frequente su 4K) → decine di alloc per frame. Allocazioni nascoste nei loop.
- **Alternativa zero-copy:** `GpuGlyphKey` è `hashable` → `xxHash::XXH3_64bits` sul POD + `font_path` view senza intermedio `string`.
- **Impatto throughput:** **Alto**
- **Rischio bonifica:** **Basso**

### H-03 — **P1** `dispatch_level_nodes` alloca `vector<WorkerTelemetry>` per livello per frame

- **File:** `Chronon3d/src/render_graph/executor/executor_level_dispatch.cpp:62-95` `DispatchTelemetry telemetry(count, id)` contiene `std::vector<WorkerTelemetry> workers(count)` heap alloc + `atomic next_worker`. Costruito per **ogni livello** (`compiled.levels` tipicamente 5-12) per frame.
- **Perché è un problema architetturale:** 5-12 × `concurrency()` (es. 8) × `sizeof(WorkerTelemetry)` alloc/dealloc per frame × 60fps = ~3-7k heap ops/sec solo per telemetry. Inoltre `thread_local WorkerBinding binding` + `fetch_add` su `next_worker` causa false sharing.
- **Impatto throughput:** **Medio**
- **Rischio bonifica:** **Basso** (disabilitare telemetry in non-diagnostic build o preallocare su `ExecutionState`)

### H-04 — **P1** `FramebufferPool::acquire_unique` best-fit scan lineare sotto lock + `resize_logical` fuori lock ma `round_to_bucket` dentro

- **File:** `Chronon3d/src/cache/framebuffer_pool.cpp:172-240` per ogni acquire: sotto lock loop su `m_free` buckets per trovare `area` minima che fitta → `O(buckets)`. Su pool con 32 size-class × 3 depth = 96 bucket, 80 nodi × 60fps = 460k iterazioni sotto lock / sec.
- **Perché è un problema architetturale:** algoritmo inefficiente nascosto — lookup lineare `O(n)` quando basterebbe mappa/hash `O(1)` per bucket exact.
- **Impatto throughput:** **Medio**
- **Rischio bonifica:** **Medio**

### H-05 — **P1** `ImageCounterSnapshot` / `FontCounterSnapshot` + multiple `filter()` che ritorna `vector<InternalAssetRef>` copy

- **File:** `Chronon3d/src/runtime/render_preparation_assets_detail.hpp:41-180` `manifest.filter(AssetKind::Image)` costruisce vettore di ref copiato per ogni fase (prepare_images, gpu-promotion, metrics). Ogni `ref.owner + "\n" + ref.path` (`render_runtime.cpp:59`) alloca string.
- **Perché è un problema architetturale:** su scene con 200 asset → 600 string temporanee prima del frame 0. Pressione su heap prima dell'hot path.
- **Impatto throughput:** **Basso** (startup, non steady-state)
- **Rischio bonifica:** **Basso**

### H-06 — **P1** `path_stroke.cpp` / `grid_background_kernel` allocazioni per-primitiva

- **File:** `Chronon3d/src/backends/software/rasterizers/path/path_stroke.cpp:46-225` `trim_polyline_points`, `dash_polyline_points` allocano `vector<pair<Vec2>>`, `vector<float> lengths`, `vector<vector<Vec2>> result` per ogni `StrokeContour` per frame.
- **File:** `Chronon3d/src/backends/software/kernels/grid_background_kernel.cpp:145-147` `vector<ColumnWeight> cols(width); vector<i32> active_x reserves`
- **Perché è un problema architetturale:** per forma con stroke-dash: 4-6 alloc per contour; tiled path raster re-alloca anche se geometry non è cambiata (cache `SharedContours` esiste ma dash non è cached). Allocazioni nascoste nei loop critici.
- **Impatto throughput:** **Medio**
- **Rischio bonifica:** **Medio** (pool dash o SoA — già parzialmente fatto via `path_cache`)

### H-07 — copie non necessarie & mancato pool

- **File:** `Chronon3d/src/backends/software/processors/effects/blur/directional_blur.cpp` generic path usa `scratch->directional_taps` bene, ma i fast-path H/V ignorano `taps` vector e ricalcolano loop per pixel con `sample_bilinear` invece di separabile 2-pass kernel.
- **File:** `Chronon3d/src/render_graph/pipeline/helpers.hpp:76` `make_unique<Framebuffer>` invece di `pool->acquire` per temporary composite.
- **File:** `Chronon3d/src/runtime/job/job_session.hpp:178` `download_scratch_` cresce con `resize` mai shrink (`download_scratch_.resize(required)`) → se un frame richiede 16MB poi resta pinned per vita job (intentional ma non documentato come job-lifetime peak).
- **Impatto:** Medio
- **Rischio bonifica:** Medio

---

## 5. Utility e Duplicazioni

### U-01 — hash combine proprietario vs `xxHash`/`hash_combine` già dipendenza

- **File:** `Chronon3d/include/chronon3d/runtime/gpu_glyph_atlas.hpp:44-72` `GpuGlyphKeyHash` + `GpuStyledGlyphCache::KeyHash:57-90` fanno `h ^= v + 0x9e3779b9 + (h<<6)+(h>>2)` inline manuale.
- **Duplica:** `xxHash::xxhash` (già in `chronon3d_base`) e `glm::hash`, `magic_enum` già linkate. Inoltre `Texture` key in `assets` usa `sha256` per stesso scopo.
- **Perché è un problema:** DRY violation — stessa conoscenza (hash combine) rappresentata 3 volte con costanti diverse (`0x9e3779b9` vs `0x9e3779b97f4a7c15`), semantic drift possibile.
- **Rischio bonifica:** **Basso**

### U-02 — `sha256_string` per chiavi deterministiche non-content

- **File:** `Chronon3d/src/runtime/gpu_glyph_atlas_core_detail.hpp:44-46` `sha256_string("atlas_page_"+to_string(size)+"_"+to_string(rep))` per identity pagina atlas.
- **Duplica:** dovrebbe essere integer tuple `(page_index, rep)` non hash cripto. Costo SHA256 per ogni page creation + hash map di digest per lookup.
- **Impatto:** Basso — CPU sprecata
- **Rischio bonifica:** **Basso**

### U-03 — `LruCache` vs `FramebufferPool` custom LRU duplicati

- **File:** `Chronon3d/include/chronon3d/cache/lru_cache.hpp` vs `Chronon3d/src/cache/framebuffer_pool.cpp` custom `m_free` map + `m_tick` + `evict_lru_for` duplicano eviction logic.
- **Perché è un problema:** duplicazione di struttura dati ottimizzata — `LruCache` già fornisce eviction `O(1)` con `std::mutex`, mentre `FramebufferPool` reinventa con scan lineare.
- **Rischio bonifica:** **Medio** (unificare su `lru_cache` tipizzato)

### U-04 — `NodeMemoryTracker` & `CacheDiagnostics` duplica contatori atomici + mutex vs `RenderCounters`

- **File:** `Chronon3d/src/backends/software/internal/frame_alloc_counter.*` commenta esplicitamente `NO atomic, NO mutex` per evitare duplicazione con `RenderCounters` atomici.
- **Perché è un problema:** 3 sistemi paralleli di metriche per stessa allocazione (`RenderCounters.framebuffer_*`, `FramebufferPoolStats`, `NodeMemoryTracker.bytes_*`) con `memory_order_relaxed` vs `mutex` divergence. Nessuna SSOT per memoria.
- **Impatto:** Medio — observability degradata
- **Rischio bonifica:** **Medio**

### U-05 — `blend2d_paint.cpp` `build_gradient_stops` vs `Fill` sampler già in `gradient.hpp`

- **File:** `Chronon3d/src/backends/software/utils/blend2d_paint.cpp:12-16` ricostruisce `BLGradientStop` vector da `Fill::gradient.stops` per ogni draw, mentre `gradient.hpp` ha già lerper SIMD. Duplica conversione layout RGBA + allocazione `vector<BLGradientStop>` per draw.
- **Rischio bonifica:** **Basso**

---

## 6. Gestione Errori — swallowed / fallback silenziosi

### E-01 — **P0** `GpuGlyphAtlas::acquire` ignora `GpuAssetCache::acquire` result

- **File:** `Chronon3d/src/runtime/gpu_glyph_atlas_core_detail.hpp:54`
  ```cpp
  const auto result = m_cache.acquire(asset_key, desc, {});
  page.handle = result.handle; // senza if (!result.ok())
  ```
  Stesso a `L109-128` large glyph: `const auto result = m_cache.acquire(...);` poi `GlyphLocation loc; loc.atlas_page=0; ... m_locations[key]=loc;` anche se `handle == kInvalidRenderSurfaceHandle`.
- **Perché è un problema architetturale:** failure GPU (OOM, `UnsupportedCapability`, budget) diventa glyph invisibile o rendering su handle invalido, nessuna metrica `release_failures` esposta → **dichiara successo senza completare**. Silent fallback a transparent. Violazione resilienza + observability nativa.
- **Impatto affidabilità:** **Alto**
- **Rischio bonifica:** **Medio** (aggiungere `if (!result.ok()) { ++m_stats.misses; return fallback_loc; }` + log)

### E-02 — **P0** `GpuGlyphAtlas::current_page_or_new` swallowing su budget exceed

- **File:** `Chronon3d/src/runtime/gpu_glyph_atlas_core_detail.hpp:22-60` `if (p.cursor_x+64>kPageSize && ...)` predice spazio con costante 64 non `width/height` reale → mispredict, poi nuova pagina sempre creata anche se `acquire` fallisce per `desc.bytes > budget`. Nessun `error` propagato.
- **Impatto:** Medio — OOM silenzioso
- **Rischio bonifica:** **Medio**

### E-03 — **P1** `GpuAssetCache::release_backend_surface` swallowing `UnsupportedCapability` come `true`

- **File:** `Chronon3d/src/runtime/gpu_asset_cache.cpp:21-30` `return result.ok() || result.error().code == UnsupportedCapability;`
- **Perché è un problema architetturale:** su backend CPU-only, `release` è no-op e `m_resident_bytes` viene decrementato come se VRAM fosse liberata, mentre nessuna allocazione era realmente avvenuta → accounting divergente. Il contatore `release_failures` maschera leak reale di `RenderSurfaceRegistry` handle se backend mente su `is_native_surface_valid`.
- **Impatto:** Basso — accounting
- **Rischio bonifica:** **Basso**

### E-04 — **P1** `ResourcePreparation` WarnAndSkip silenzia asset mancanti

- **File:** `Chronon3d/src/runtime/resource_preparation.cpp:65-135` `Policy::AppendWarning` + `mode == WarnAndSkip` → `warnings_out->push_back` ma `return Result(prepared)` con `ok()=true`. `prepare_render_scene` (`render_preparation_assets_detail.hpp:108-145`) non controlla `prepared.diagnostics.warnings.size()` → `publish_prepared_assets` pubblica snapshot parziale.
- **Perché è un problema architetturale:** job con immagine opzionale mancante dichiara `ok()` e passa al `warmup_renderer` che poi tenta `image_cache().get_or_load` che può ritornare `nullptr` ma viene gestito solo nel branch `prepare_images` (se disabled → skip). Il video finale ha frame neri senza report error. Idempotenza violata.
- **Impatto affidabilità:** **Medio**
- **Rischio bonifica:** **Medio** (rendere warnings fail se asset `required==true`)

### E-05 — **P1** `FramebufferPool::release` swallowed deleters

- **File:** `Chronon3d/src/cache/framebuffer_pool.cpp:12-32` `PoolFbDeleter` `RestoreScratchHandle` e `RendererOwned` fanno `return;` (no delete, no recycle). Se un `OwnedFB` finisce con policy sbagliata, la memoria arena non viene mai restituita né il payload marcato free → leak silenzioso fino a `trim_after_job`.
- **Impatto:** Medio — memory leak
- **Rischio bonifica:** **Medio**

### E-06 — **P2** `RuntimeContext::create` catch-all che normalizza a `InternalError`

- **File:** `Chronon3d/src/runtime/render_runtime.cpp:124-141` `catch (const std::exception& e)` → `RuntimeBuildError::InternalError` + `e.what()` ; `catch(...)` → `"unknown exception"`.
- **Perché è un problema:** perde `Code::AssetMountFailed` distinction — se `resolver.mount(path)` fallisce per `filesystem` error, viene riportato come interno invece di mount failure → observability degradata (log/metrics non allarmano il giusto runbook).
- **Impatto:** Basso
- **Rischio bonifica:** **Basso**

### E-07 — `render_pipeline.cpp` catch swallowing con `abort` best-effort

- **File:** `Chronon3d/src/runtime/render_pipeline.cpp:172-235` `try { auto output = render_program... } catch(...) { execution.fail(); if(slot->state != Free) (void)session.abort(*slot); throw; }` → il `abort` è `(void)` ignorato; se `abort` ritorna false (slot già Free), l'eccezione originale è rilanciata correttamente, ma `frames_failed` già incrementato in `FrameExecution::complete()` (destructor) → double count `frames_failed` vs `frames_completed` race se `complete()` chiamato sia da `~FrameExecution` che manualmente.
- **Impatto:** Basso — metriche errate
- **Rischio bonifica:** **Medio**

---

## Tabella riassuntiva P0/P1/P2 (data-plane throughput)

| Pri | File:Riga | Meccanismo di costo | Impatto throughput |
|---|---|---|---|
| **P0** | `runtime/gpu_glyph_atlas_core_detail.hpp:64` `GpuGlyphAtlas::acquire` lock globale su device upload | **Contesa su linea cache + lock contention** — serializza 100-300 glyph/frame | **Alto** |
| **P0** | `cache/framebuffer_pool.cpp:150-240` `acquire_unique/release` `m_mutex` globale | **Lock contention** — ogni nodo TBB contende su unique mutex + best-fit scan | **Alto** |
| **P0** | `backends/software/processors/effects/blur/radial_blur.cpp:85,186,238` vector per pixel + `make_unique<Framebuffer>` per blur | **Allocazione heap per unità di lavoro + copia memoria non necessaria** | **Alto** |
| **P0** | `runtime/gpu_glyph_atlas_core_detail.hpp:4,91` `glyph_key_string` ostringstream+sha256 per glyph | **Allocazione heap per unità** | **Alto** |
| **P0** | `runtime/gpu_glyph_atlas_core_detail.hpp:54,109` ignored `GpuAssetCache::acquire` result | **Fallback silenzioso senza allarme** — dichiara successo con handle invalido | **Alto** (affidabilità) |
| **P1** | `render_graph/executor/executor_level_dispatch.cpp:62` `vector<WorkerTelemetry>` per livello | **Allocazione heap per dispatch** (5-12/frame) | Medio |
| **P1** | `render_graph/executor/node_runner_single_node_detail.hpp:260` `NodeMemoryTracker` mutex per nodo | **Contesa cache-line** — 80× lock/frame | Medio |
| **P1** | `runtime/render_pipeline.cpp:97` `m_topology_mutex` su `materialize_scene` | **Lock + allocazione heap** fuori arena | Medio |
| **P1** | `runtime/render_pipeline.cpp:258` 3-thread `condition_variable` barrier | **Barriera sincrona** in pipeline asincrona | Medio |
| **P1** | `runtime/resource_preparation.cpp:65,191` `WarnAndSkip` swallowing + double `sha256_file` | **I/O sincrono bloccante duplicato** + degradato silenzioso | Medio |
| **P1** | `backends/software/rasterizers/path/path_stroke.cpp:46` vector temporanei per dash/trim per contour | **Heap churn per primitiva** | Medio |
| **P1** | `cache/framebuffer_pool.cpp:30` `release_backend_surface` `UnsupportedCapability==true` | **Accounting divergente** — copia memoria non necessaria mascherata | Basso |
| **P1** | `runtime/render_runtime.cpp:59` `mesh_manifest_key` string concat in loop | **Allocazione heap al publish** | Basso |
| **P2** | `runtime/gpu_glyph_atlas.hpp:44` custom hash combine vs xxHash | **Duplicazione stdlib** | Basso |
| **P2** | `runtime/render_runtime.hpp:8` doppio include | **Frizione di astrazione** | Basso |
| **P2** | `runtime/render_runtime.cpp:124` catch-all normalizzato | **Overhead di ramo + tabella** diagnostica persa | Basso |

---

## Confronto prima/dopo — 3 riscritture Data-Oriented

### Es.1 — Glyph Atlas (zero allocazione, memoria piatta, borrow di handle)

**Prima (`gpu_glyph_atlas_core_detail.hpp:64-200`):**
```cpp
GlyphLocation GpuGlyphAtlas::acquire(key,w,h,coverage,metrics,profile){
  std::lock_guard lock(m_mutex);                 // globale
  if(auto it=m_locations.find(key); it!=end) return it->second;
  auto &page = current_page_or_new(...);         // sotto lock chiama m_cache.acquire (device)
  if(m_cache.backend())
     m_cache.backend()->upload_surface_region(page.handle, ..., coverage); // device sotto lock
  page.cursor_x+=w; ...;
  m_locations[key]=loc;                           // hash insert sotto lock
}
std::string glyph_key_string(key){ ostringstream o; o<<key.font_path<<"\x1f"...; return o.str(); }
```

**Dopo (Data-Oriented, ~+30 LOC, -2 alloc/glyph):**
```cpp
// sharded pages: 4 shard per representation (Coverage/Sdf/Msdf/Mtsdf) → 16 bucket senza lock globale
struct PageShard { std::mutex mu; std::vector<AtlasPage> pages; /* bump cursor */ };
std::array<PageShard,16> shards_;

GlyphLocation acquire(GpuGlyphKey key, ...){
  if(auto loc = shard_for(key).find_atomic(key)) { stats.hit++; return *loc; } // lock-free read (shared_mutex)
  // 1) alloca page/bump cursor fuori dal lock globale, solo shard lock
  // 2) staging: pack coverage in job-owned linear scratch (span<float> da FrameSlotPayload::arena)
  // 3) device upload FUORI dal lock: handle + desc copiati, lock rilasciato, poi backend->upload
  // 4) publish loc con try_emplace (no ostringstream: key.hash = xxh3(font_path)+glyph_id mix)
}
```

**Throughput:** da ~4ms seriali a <0.4ms (upload batchabile), 0 alloc sul lookup hit, 1 alloc solo su miss reale. Handle invalido ora ritorna `std::optional<GlyphLocation>` → fail-loud.

### Es.2 — Radial Blur (zero `vector` per pixel, pool riutilizzato)

**Prima (`radial_blur.cpp:85-186`):**
```cpp
void apply_radial_blur(Framebuffer &fb,...){
  auto temp = std::make_unique<Framebuffer>(w,h); // heap per frame
  for(y) for(x){
    auto taps = generate_radial_taps(cx,cy,px,py,amount,n); // vector alloc per pixel, cos/sin per pixel
    for(tap) { sampler.bilinear(px+tap.dx, ...) }
  }
  auto recentered = std::make_unique<Framebuffer>(w,h);
}
```

**Dopo:**
```cpp
void apply_radial_blur(Framebuffer& fb,..., EffectScratchResources* scratch){
  Framebuffer *tmp = scratch ? scratch->ensure(w,h) : fallback_pool.acquire(w,h);
  // precompute 1D tap table per amount/samples (static thread_local vector<Tap> lut; n<=32)
  lut = radial_lut(amount,samples); // cos/sin una volta
  // per-pixel: solo indirection lut + sampler, zero vector, zero make_unique
  // recenter via scratch second buffer, non new
}
```

**Throughput:** 1920×1080×2M `vector` alloc → 0; 2× framebuffer heap → 0 (scratch reuse). Atteso ~8-12× speedup su nodi radial (profilo 60fps 1080p da ~11ms a <1ms in `bench_scene_program`).

### Es.3 — Executor dispatch + FramebufferPool (SPSC, memoria piatta)

**Prima (`executor_level_dispatch.cpp:62-95` + `framebuffer_pool.cpp:150-240`):**
```cpp
DispatchTelemetry telemetry(concurrency); // vector alloc per livello
scheduler.for_each_index(level.size(), [&](i){
  binding.index = telemetry.next_worker.fetch_add(1); // CAS su linea condivisa
  execute_single_node(..., &local.nodes_executed);    // per nodo lock NodeMemoryTracker + pool mutex
});
// pool: std::lock_guard(m_mutex); linear best-fit scan
```

**Dopo:**
```cpp
// telemetry: array<WorkerTelemetry, MAX_CONCURRENCY> on stack, no heap, no fetch_add (worker index = tbb::this_task_arena::current_thread_index())
// pool: sharded free-list per bucket size (array<vector<Framebuffer*>> senza map), try_pop lock-free, spill to global only quando vuoto
// NodeMemoryTracker: thread_local NodeMemoryBatch batch; flush con single lock a fine livello (1 lock / livello vs N lock / nodo)
```

**Throughput:** -80% contesa su `m_mutex`, cache-miss L1 da `map` scan → `array` contiguous, `parallel_idle_worker_entry_sum` da 100% CAS → plain increments.

---

## Piano di bonifica — ordine logico (massimo throughput per minimo rischio)

### Fase 0 — fail-loud & igiene (1-2 gg, rischio Basso, sblocca observability)

1. `E-01/E-02` propagare `GpuAssetCache::acquire` `ok` → `std::optional<GlyphLocation>` + `spdlog::warn` + `m_stats.release_failures`. Stesso per `current_page_or_new`.
2. `E-04` `ResourcePreparation` distinguere `ref.required` nel `WarnAndSkip` → se required, promuovere warning a `PreparationError`.
3. `D-01` remove duplicate include; `C-03` fondere double `sha256_file` in `decode_image` (ritorna digest, caller riusa).
4. Documentare `download_scratch_` job-lifetime peak (commento + `shrink_to_fit` su `reset_job`).

### Fase 1 — hot-path heap churn (1 sett, rischio Medio, impatto Alto)

1. `H-01` radial_blur su `EffectScratchResources` + LUT taps; eliminare 2× `make_unique<Framebuffer>` (riusa `scratch->framebuffer` double-buffered).
2. `H-02` eliminare `glyph_key_string`+`sha256_string` per glyph: sostituire con `GpuGlyphKey` POD hash (`xxHash64` + `font_path` view) — toccare `gpu_glyph_atlas_core_detail.hpp:4,44,91,135`.
3. `H-03` dispatch telemetry `DispatchTelemetry` da heap vector a `std::array<WorkerTelemetry,64>` stack + `current_thread_index` (o `thread_local` slot pre-sized su `ExecutionState`).
4. `H-05` `manifest.filter` ritornare `std::span` su filtro lazy invece di `vector` copy (introdurre `filter_view`).

### Fase 2 — contesa lock (2 sett, rischio Alto, impatto Alto)

1. `S-01` shard `GpuGlyphAtlas` per `representation` (4) × page-group (4) con `shared_mutex` read-hit lock-free; device upload fuori lock. Accettazione: bench `test_text_pool_concurrency` + `perf/test_node_memory_tracker`.
2. `S-02` `FramebufferPool`: introdurre fast-path `acquire_unique` lock-free per bucket exact hit (`atomic` stack per `FramebufferPoolKey`), fallback globale solo su miss. Misurare `m_lock_wait_us` prima/dopo.
3. `S-03` `NodeMemoryTracker` batch thread-local (`tbb::enumerable_thread_specific`); `S-04` double-checked locking su `m_topology_mutex`.

### Fase 3 — contratti early (3 gg, rischio Medio)

1. `C-01` aggiungere `FontEngine::probe(path)` sync leggera nel barrier (open face, check `FT_New_Face` senza raster) → fail-loud prima del frame loop.
2. `C-02` migrare `prepare_layouts` da `filter(Image)` a `manifest.filter(Layout)` con fallback Image per compat; feature-gate dietro `CHRONON3D_ENABLE_TEXT`.
3. `C-04` normalizzare `GpuGlyphKey::font_size=0` per distance-field in factory `make_glyph_key`.

### Fase 4 — dedup & layout dati (1 sett, rischio Medio)

1. `U-01/U-02` unificare hash su `xxHash3_64` + `tight_surface_bytes` helper già usato in `GpuAssetCache::acquire`.
2. `U-03` valutare `Pool` su `lru_cache` tipizzato o almeno `flat_hash_map` con contiguità per `FramebufferPoolKey`.
3. `H-06` dash cache: `SharedContours` già fa caching geometry, estendere a `PreparedStrokeContour` (store `dash_array` hash → `vector<VisibleSubPath>`).

> **Gate consigliato:** per ogni fase, assert `benchmark` `bench_scene_program` + `test_frame_alloc_counter` (TICKET-121) mostrano `framebuffer_allocations==0` in steady-state e `tbb_active_workers_avg` non regredisce. Fase 2 richiede run `tools/check_architecture_boundaries.sh` check 11/12 (no `noexcept` su backend throw) + `verify_zero_copy_end_to_end.sh` per clonare `software_fallback_nodes==0`.

---

## Note su duplicazioni/dipendenze dormienti

- `RenderingGen/` è `objectstore` Go separato, non legato a `Chronon3d/CMakeLists.txt` → non è duplicazione ma boundary IPC già isolato.
- Nessun riuso improprio di `std::future::get` bloccante trovato nella pipeline per-frame (solo test `cache/*`, `ipc/*`).
- Retry fragili: `ResourcePreparation::run_phase` usa `throw` → `catch(PreparationError&)` unico livello; non c'è retry. Il `GpuAssetCache` retry su `m_inflight` è corretto (`while` + `cv.wait`).

---

## Appendice — Checklist originale coperta

| Area richiesta | Sezioni del report |
|---|---|
| 1. Codice morto e disallineato (file non compilati, membri mai letti, `#if 0`, fallback legacy) | D-01 → D-06 |
| 2. Contratti e Validazione (divergenza input/core, validazione posticipata) | C-01 → C-05 |
| 3. Concorrenza e Colli di Bottiglia (mutex globali, barrier sync) | S-01 → S-06 |
| 4. Hot Path & Allocazioni (allocazioni per-iterazione, pool) | H-01 → H-07 |
| 5. Utility e Duplicazioni (stdlib dup) | U-01 → U-05 |
| 6. Gestione Errori (swallowed, fallback silenzioso) | E-01 → E-07 |
| 7. Authority unica per concetto (registry/resolver/sampler comune) | §0 Mappa authority |

> **Regole non negoziabili verificate:**
> - Una sola authority per concetto — **PASS** (con 1 violazione lifetime `FrameArena` D-02)
> - Nuova feature = registry/resolver/sampler comune — **PASS**
> - Ogni feature passa per authority canoniche, rispetta lifetime, non crea pipeline parallela — **PASS**
> - Lascia meno duplicazione di quella trovata — **TODO** (U-01/U-02/U-03 da bonificare)

---

*Report strictly read-only, 0 modifiche al codice, 0 build eseguiti come richiesto. Generato per `FIXING.md` — prioritizzato per `Frequenza di modifica × Complessità/Fragilità` (hotspot = alta complessità + alta frequenza commit).*
