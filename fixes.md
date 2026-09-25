# Chronon3D — Audit Findings & Fixes (`fixes.md`)

> **Origine:** audit tecnico read-only (static analysis, nessuna build/test eseguita, nessuna modifica al codice).
> **Data:** 2026-09-12.
> **Scope:** data-plane per-frame (`src/runtime`, `src/render_graph`, `src/backends`, `src/media`, `src/cache`), boundary I/O/codec, governance tooling.
> **Classificazione:** P0 = rischio correctness/perf sul path certificato · P1 = costo strutturale · P2 = igiene.
> **Metodo:** le evidenze sono `path:line` del checkout corrente; le frequenze ("per frame") derivano dalla lettura dei call-site, non da profiling runtime. Gli arbitri per la priorizzazione a valle sono i contatori già instrumentati (`m_lock_wait_us`, `node_memory_lock_*`, zero-copy sidecar `*.timing.json`).
> **Verifica (2026-09-12, checkout `5af91243a`):** la Parte B (e la citazione 4.4) è stata ri-controllata riga per riga contro il codice dopo il commit. Verdetto per item in **Parte E**, cosa è stato applicato o differito in **Parte F**. Gli item marcati `[RITIRATO]` non sostenuti dall'evidenza non vanno usati per pianificare lavoro.

**Principio guida:** un fatto deve avere un solo proprietario canonico. Le proiezioni sono consentite. Le seconde Source of Truth no. Un gate non deve contenere una seconda copia della conoscenza che sta cercando di proteggere.

---

## PART 1 — Dynamic allocations on the hot path

Stato generale: disciplina forte (`steady_state_allocations_per_frame=0` gate, `CompiledResourceTable` planned slots, `FramebufferArena`, job-owned `download_scratch_` high-water-mark). Il churn residuo è concentrato in pochi punti.

### [P0] 1.1 — `TypedProgramSampler` rebuilds `SampledLayerParameters` per frame, O(L×A)
- Evidenze:
  - `src/compiler/typed_program_sampler.cpp:42-125`
  - `include/chronon3d/compiler/frame_parameters.hpp:17-45`
- Fatto: `block.layers.clear()` + `push_back` per layer per frame; ogni `SampledLayerParameters` incapsula una `std::variant` i cui payload `SampledImageParameters`/`SampledVideoParameters` portano `std::string asset/source` → copia stringa per layer per frame. Per i text layer: `std::find_if` lineare su `program.texts()`; per ogni layer un secondo `find_if` lineare su `program.animations()` → O(L×A) ogni frame.
- Mitigazione parziale esistente: `RenderPipeline::render_program` riusa il block slot-owned (`src/runtime/render_pipeline.cpp:108-121`) — niente churn di capacità, ma stringhe e scan lineari restano.
- Fix consigliato: pre-risolvere indici text/animation a compile time (tabelle indice in `CompiledProgram`) o lookup sorted-by-id una tantum per programma; `string_view`/indice invece di `std::string` nei payload campionati.
- Rischio intervento: Basso.

### [P1] 1.2 — `gpu_text_run.cpp` prepare path: 9 vettori per prepared run
- Evidenze: `src/render_graph/nodes/text_run/gpu_text_run.cpp:825-846, 888-897`
- Fatto: `keys/widths/heights/metrics` + 5 `pending_*` vettori, `make_shared<GpuPreparedTextRun>`, `std::vector<FontFaceHandle> fonts` per ogni *nuovo* prepared run (cache-miss); loop di re-check richiama `location()`/`metrics()` per glyph. Il vecchio walk selector di compatibilità del testo è stato rimosso.
- Fix consigliato: prepare-scratch riutilizzabile (pattern `download_scratch_`); il vecchio walk selector di compatibilità è stato rimosso.
- Rischio: Basso.

### [P1] 1.3 — `LruCache`: heap work per-op sui warm-path hit
- Evidenze: `include/chronon3d/cache/lru_cache.hpp:471-499`
- Fatto: `get()` = `timed_lock` + `unordered_map::find` + `std::list::splice`; mappa a nodi + lista per-iteratore = 3+ nodi heap sparsi per hit. `NodeCache`, `FrameCache`, `SceneProgramCache` (32 istanze trovate) la usano come struttura più calda dell'engine.
- Fix (wave successiva): mappa open-addressing slab-allocated + LRU a indici intrusivi per entry.
- Rischio: Medio.

### [P1] 1.4 — `render_pipeline.cpp` compat frame path: eccezioni + eviction FIFO
- Evidenze: `src/runtime/render_pipeline.cpp:143-146, 158-162, 186-200, 260-274`
- Fatto: `m_topologies` eviction con `erase(begin())` (FIFO, non LRU — minore). Throw `std::runtime_error` per condizioni *attese* ("no free frame slot", "failed to publish") — vedi Parte 3.1.
- Fix: vedi 3.1; eviction LRU banale.
- Rischio: Basso.

### [P2] 1.5 — `make_shared<Framebuffer>` residuo nei fallback + placeholder 1×1
- Evidenze: `src/render_graph/framebuffer_acquire_shared_detail.hpp:31, 66, 91, 96-103, 153`
- Fatto: fallback cold-path senza pool/planned slot (by design, documentato). Il placeholder 1×1 per lo zero-copy swap fa un'allocazione heap + pool release *ogni frame* per input trasferito.
- Fix: placeholder pre-fatto per slot.
- Rischio: Basso.

**Verdict arena/pool:** il riuso è la norma (FramebufferArena, FramebufferPool, FrameSlotPool, EncoderFramePool, DirectYuvProgram pools). Rischio frammentazione heap basso; churn residuo = 1.1–1.2.

---

## PART 2 — Synchronization, atomicity, hardware contention

### [P0] 2.1 — Doppio `scoped_lock` su ogni batched Vulkan surface op
- Evidenze:
  - `src/backends/vulkan/vulkan_backend_surface_api.cpp:21-40`
  - `src/backends/vulkan/vulkan_surface_authority.hpp:17-23`
- Fatto: `std::scoped_lock(surfaces.mutex, submission_ring.mutex)` per *ogni* op superficie (create/upload/download/composite/blur). Due domini di contesa acquisiti insieme serializzano surface store vs submission ring per tutta l'operazione — il confine "independent contention domain" è di fatto accoppiato da ogni caller. Il commento P1-7 afferma "single-owner unlocked hot path", ma il wrapper esterno prende comunque entrambi i lock.
- Fix consigliato: `surfaces.mutex` solo per lookup di stato, rilascio prima della registrazione nel ring (rivalidazione con generation-token), o snapshot seqlock/RCU dei binding (read-mostly).
- Rischio: Medio.

### [P0] 2.2 — `GpuGlyphAtlas`: `shared_lock` per glyph per frame
- Evidenze:
  - `src/runtime/gpu_glyph_atlas_core_detail.hpp:345-360`
  - `src/render_graph/nodes/text_run/gpu_text_run.cpp:887-897, 905-911`
- Fatto: `location(key)`, `metrics(key)`, `page_handle()` prendono ciascuno `std::shared_lock(m_mutex)`; il draw path chiama `location()` ×2 + `metrics()` + `page_handle()` per glyph. A 60fps × migliaia di glyph = 4 acquisizioni lock per glyph per frame su un `m_mutex` caldo — lo stesso pattern già flaggato dal ticket HOTPATH-TEXT-MTSDF-CONTENTION.
- Fix consigliato: validare il prepared run con un **generation counter** dell'atlas (un atomic load) invece di 4 lock per glyph; le location sono già snapshot in `GlyphStatic`.
- Rischio: Basso.

### [P1] 2.3 — `FramebufferPool` global `m_mutex` + `FramebufferArena::allocate` locked
- Evidenze: `src/cache/framebuffer_pool.cpp:133-164`, `include/chronon3d/core/framebuffer_arena.hpp:46-48`
- Fatto: un mutex per acquire/release/evict; arena locked per un bump-pointer. Sul compiled-path il pool è raro (planned slots) → tassa soprattutto path reference/paralleli. Metering già presente (`m_lock_wait_us`, `m_lock_hold_wall_us`) — GOVERNANCE H-2.
- Fix: cursori arena thread-local o shard lock per size-class quando il parallelismo scala.
- Rischio: Medio.

### [P1] 2.4 — `LruCache`: shard `mutex` + atomics globali per op
- Evidenze: `include/chronon3d/cache/lru_cache.hpp:568-574, 653-662, 391-402`
- Fatto: `m_hits`/`m_misses` bumpati *sempre* → cacheline ping cross-core su ogni accesso; `stats()` locka tutti gli shard. Il facade `SceneProgramCache` fa già per-shard fold — replicare.
- Fix: contatori hit/miss per-shard, fold on read.
- Rischio: Basso.

### [P1] 2.5 — Shared-ownership sul encode path
- Evidenze:
  - `apps/chronon3d_cli/commands/video/common/pipe_export_writer.cpp:242-276` (`make_shared<EncoderGpuCompletion>` per frame)
  - `src/media/video/video_device_runtime_cuda_cache_detail.hpp:92` (`make_shared<CudaImageResource>` + mappa shared_ptr)
  - `include/chronon3d/runtime/job/job_session.hpp:354` (`shared_ptr<JobStatus>` con mutex)
- Fatto: refcount bump per-frame tra thread producer/writer → cacheline bouncing.
- Fix: ownership esclusiva a slot; estendere il pattern `BorrowedEncoderFrame` ai completion record.
- Rischio: Medio.

### [P2] 2.6 — `NativeAvEncoder` `std::recursive_mutex`
- Evidenze: `apps/chronon3d_cli/utils/video/native_av_encoder.hpp:235` + 5 TU (`_native_surface`, `_lifecycle`, `_cuda_queue`, `_direct_yuv`).
- Fatto: recursive lock nasconde il lock graph; il lato Vulkan ha già de-ricorsivizzato (TICKET-VK-LOCK-SHARDING H49-H51).
- Fix: stesso trattamento.
- Rischio: Medio.

### [P2] 2.7 — `slot_in_use()` O(bindings)
- Evidenza: `src/backends/vulkan/vulkan_surface_authority.hpp:96-102`
- Fatto: scan lineare dentro l'autorità locked dai path di reclamation; ≤64 slot transienti lo limita.
- Fix: multimap inverso O(1).
- Rischio: Basso.

**Punti di forza confermati:** `FrameQueue` = TBB bounded queue di soli slot ID (`frame_queue.hpp`); `FrameSlotPool` CAS unico writer di stato; pipeline a 3 stadi con wait-mutex per stadio (no convoy).

---

## PART 3 — Control flow, error handling, dispatch

### [P0] 3.1 — Throw di eccezioni per condizioni attese dentro il per-frame reach
- Evidenze:
  - `src/backends/vulkan/vulkan_backend_surface_api.cpp:42-389` (13 catch; ogni API superficie strutturata su try/throw)
  - `src/runtime/render_pipeline.cpp:158-162, 187-190` (throw per "no free slot", "publish failed" — stati di scheduling recuperabili, non invariant violations)
  - `src/backends/vulkan/vulkan_surface_authority.hpp:104-115` (`bound_slot()` throw con concatenazione stringhe, raggiungibile da loop hot)
  - `src/render_graph/transition/transition_catalog.cpp:459-464` (`resolve` throw su id sconosciuto)
- Fatto: 82 `catch (const std::exception&)` in produzione (non-test). Il modello canonico (`Result<T,E>`, `include/chronon3d/core/types/error.hpp`) esiste ma l'adozione è disomogenea.
- Fix: `Result`/enum per gli stati attesi di `RenderPipeline::render*`; `bound_slot()` → `optional`/error code.
- Rischio: Basso.

### [P1] 3.2 — Message-string → code re-derivation (control flow stringly-typed)
- Evidenza: `src/scene/camera/camera_v1/shot_timeline.cpp:335-355` (`derive_code_from_message` greppa `d.message` per substringhe)
- Fatto: seconda SSOT lossy della tassonomia errori, residente nel testo dei messaggi. Cambio wording → cambio silenzioso dei codici.
- Fix: propagare il codice strutturato all'origine.
- Rischio: Medio.

### [P1] 3.3 — Dispatch: per-node ok; per-glyph/per-pixel no
- Node-level corretto: `RenderGraphNode::execute` virtuale per node per frame (`render_graph_node.hpp:122-127`) + `std::function` `OpacityEvaluator` per node (`:106`) — granularità batch, accettabile.
- Violazione micro-level: `src/backends/vulkan/shaders/text_run.comp:43-48` — loop `for (i < glyph_count)` per *pixel* (scan O(pixels×glyphs) branchy). Il dispatch union-bounds mitiga; fix data-oriented = instance list per-tile o depth-sort + early-out.
- `sample_track` scan lineare per property per frame (`typed_program_sampler.cpp:11-39`) — ok per track corte; binary search per track lunghe.

---

## PART 4 — Data movement / interop boundaries

### [P0] 4.1 — Residuo certificato: D2D Vulkan→CUDA→NVENC per frame
- Evidenze:
  - `docs/ZERO_COPY_GATES.md` (metrics block: `encoder_staging_copy_bytes ≠ 0`, `gpu_surface_copy_frames ≠ 0`)
  - `src/backends/vulkan/vulkan_backend_cuda_export_private.cpp` (blit `copy_surface_to_cuda_encoder`, contatore wired)
  - `src/media/video/compositor/` (`composite_surface_to_nv12` → conversione RGBA→NV12 per frame)
- Fatto: il percorso encode nativo corrente fa una conversione colore per frame. Il percorso DirectYUV (`composite_direct_nv12*`) esiste già e lo azzera; il gate Z3 (`tools/verify_z3_equivalence.sh`) fornisce lo strumento di equivalenza. **Priorità massima già ticketata — confermata.**
- Fix: sostituire in `NativeAvEncoder::write_native_surface_impl` la coppia `copy_surface_to_cuda_encoder` + `composite_surface_to_nv12` con `composite_direct_nv12*`.
- Rischio: Medio (equivalence gate pronto).

### [P1] 4.2 — `Framebuffer` copy/move semantica e `copy_rows` fallback
- Evidenze: `include/chronon3d/core/memory/framebuffer.hpp:111-141`; `src/render_graph/framebuffer_acquire_shared_detail.hpp:126-140`
- Fatto: copy ctor duplica semantica `m_key_digest`/surface-handle comprensibile solo dai commenti; `FramebufferCopyAuthority::copy_rows` (stride-aware, single copy authority F2.4) invoked sul fallback no-pool per frame. Con planned slots ok; tassa i path reference.
- Rischio: Basso.

### [P1] 4.3 — Stride cache-line allineato: corretto
- `framebuffer.hpp:43-53` (`align_stride_to_cache_line`); pool size-class rounding gestito da `copy_rows`; upload CPU→GPU counted alla singola autorità (`video_device_runtime_cuda_cache_detail.hpp:103-118`); layout NV12 planare via `AVFrame` + `hw_frames_ctx` = contratto zero-copy corretto sul lane DirectYUV. Nessun repack ridondante trovato.

### [P2] 4.4 — Contratto C++↔GLSL documentato da commenti (commento errato, guardia a senso unico)
- `src/backends/vulkan/shaders/text_run.comp:13` dichiara 40 byte; il valore reale è 48 ed è già asserito lato C++ (`render_surface.hpp:361`). Le due rappresentazioni dello stesso fatto per-glyph (`GlyphStatic` CPU / `GlyphInstance` GPU) restano senza contratto condiviso: vedi B.5 (`[CORRETTO]`).
- Fix: `[APPLICATO]` commento allineato + `tests/test_shader_layout_contract.py` (slot scalari GLSL vs campi C++, `sizeof` del `static_assert`, `ArrayStride` SPIR-V). Il "test che compili la shader" è ora reale: `glslangValidator` è toolchain già presente nel repo.
- Rischio: Medio (silent visual corruption su rename/add-field) → coperto dal test.

---

## PART 5 — Memory architecture / data locality

### [P1] 5.1 — `Layer` = wide AoS con 6 `unique_ptr` pimpl + 3 `pmr::string`
- Evidenza: `include/chronon3d/scene/model/layer/layer.hpp:50-107`
- Fatto: oggetto grande, deep-copy via custom copy ctor; `std::vector<Layer>` contiguo (buono) ma le indirezioni per-layer fanno chase pointer durante evaluation e fingerprinting (`scene_hasher.cpp`). `std::pmr::vector<RenderNode> nodes` è contenimento SoA-ish buono; memoization hash (`:104-105`) evita lavoro ripetuto.
- Valutazione: complessità di domain model legittima; conversione SoA = refactor grande con payoff basso *se* la scene evaluation non domina i profile.

### [P1] 5.2 — `SampledLayerParameters` ~200+ byte (AoS nel vettore per-frame più caldo)
- Evidenza: `include/chronon3d/compiler/frame_parameters.hpp:17-77`
- Fatto: `Mat4 (64B) + variant + strings` per layer; composizione da 100 layer = strided 20KB+ per frame. L'alternativa compatta pre-sized esiste già (`graph::FrameParameterBlock`, B6) — consolidare (vedi B.1) fixa sia SSOT sia layout.

### [P2] 5.3 — Strutture sparse nei cache
- `LruCache` (nodi mappa+lista+valore — vedi 1.3); `SceneProgramStore` bucket map di `shared_ptr<InstanceEntry>` (`scene_program_store.cpp:117-153`); camera `transitions_` map (bounded a 5 entry, ok).

---

# PART B — SSOT / Governance audit

### [P1] B.1 — Tre contenitori "parametri per-frame" e un nome duplicato
`[CORRETTO — era P0 "seconda SSOT"]` Il nome `FrameParameterBlock` esiste in due namespace con forme diverse, e il fatto "parametri dinamici per-frame" ha in realtà **tre** sedi:

- Owner A — `chronon3d::compiler::FrameParameterBlock` (`include/chronon3d/compiler/frame_parameters.hpp:78`). È quello vivo: prodotto da `TypedProgramSampler::sample` (`src/compiler/typed_program_sampler.cpp:44,122`), consumato da `RenderPipeline::render_program` (`src/runtime/render_pipeline.cpp:111-112`), stoccato in `runtime::FrameSlotPayload` (`include/chronon3d/runtime/frame/frame_slot_payload.hpp:24`) ed esposto da `FrameExecutionContext::parameters()` (`frame_execution.hpp:48-51`) e `RenderGraphContext::frame_parameters` (`render_graph_context.hpp:90,521`).
- Owner B — `chronon3d::graph::FrameParameterBlock` / `LayerParameterBlock` (`include/chronon3d/render_graph/pipeline/frame_parameter_block.hpp:56`, `LayerParameterBlock` a `:35`). Consumatore unico: `PrecompNode::m_param_block` (`include/chronon3d/render_graph/nodes/precomp_node.hpp:165`; `warm_up()` in `src/render_graph/cache/precomp_node_execute.cpp:252`) + la fixture `tests/render_graph/nodes/test_precomp_node_cache.cpp:311-320`. **Non** un bench test, come affermava la prima stesura.
- Owner C — `chronon3d::graph::FrameParameterTable` (`include/chronon3d/render_graph/pipeline/frame_parameter_table.hpp:61`), **ignorato dalla prima stesura**: è lo storage pre-sized per-frame consumato da `CompiledFrameGraph::prepared_parameters` (`compiled_frame_graph.hpp:257`), `GPUCommandPlan::dynamic_parameters` (`runtime/gpu_command_plan.hpp:158`) e `timeline::CompiledComposition::parameters` (`timeline/compiled_composition.hpp:36`).

Perché resta un problema: i tre hanno lo stesso dominio nominale e assi di ownership diversi (A = per-layer variant+stringhe, B = per-binding transform, C = slice per frame-time), quindi dal nome non si deduce chi possiede cosa e il rename di uno non è verificabile dal compilatore.
Perché **non** è il P0 della prima stesura: non è dimostrata una seconda copia dello *stesso* fatto (nessuna conversione, nessun drift osservato). È collisione di nome + overlap di concept.
Fix: decidere l'asse di ownership e nominarlo (es. `PerBindingTransformBlock` per B) + documentare la mappa a 3 in `docs/`. Non è un rename meccanico → **differito** (Parte F.2).
- Rischio: Medio.

### [P0] B.2 — Il gate dell'error-model contiene la seconda copia della tassonomia
`[CONFERMATO — conteggi corretti]`
- Evidenze: `tools/architecture_rules.toml` — `canonical_error_legacy_mapping_inventory` (`presence`, `min_matches = 28` su `\{"chronon3d::`) + **18 regole** `*_error_construction_adapter_only` con allowlist hardcodata (path singolo; l'unico blocco multi-path oltre a `canonical_error_construction_gate` è `sdk_render_error_construction_adapter_only`). Il blocco in più contato nel primo giro è `canonical_error_construction_gate` (riga 49), che è un'altra regola.
- Fatto duplicato: l'elenco dei tipi-errore legittimi e delle loro sedi vive (a) negli header, (b) nel gate come literal, (c) in `kLegacyErrorMappings` come literal. Il gate non legge la conoscenza che protegge: la ricopia.
- Evidenza più forte del primo giro: `min_matches = 28` è **esattamente** la cardinalità di `std::array<LegacyErrorMapping, 28>` (`include/chronon3d/core/types/error.hpp:65`): il gate non legge la conoscenza che protegge, ne tiene il conteggio come literal.
- Perché è pericoloso (meccanismo confermato): `presence` è un *lower bound* → una 29ª mapping passa senza segnalazione, il numero non dice *quali* mapping esistono, e un path move senza cambio semantico rompe il gate; resta la change amplification (3 sedi per un nuovo tipo errore).
- Owner canonico: `include/chronon3d/core/types/error.hpp` + annotazione/convenzione (es. macro `CHRONON_ERROR_ADAPTER`) che lo scanner consumi data-driven.
- Cosa eliminare: le 18 allowlist inline → 1 regola data-driven.
- Rischio: Medio.

### [P1] B.3 — GOVERNANCE punta a un gate inesistente (metà tesi ritirata)
`[CORRETTO]`
- **Confermato:** `docs/GOVERNANCE.md:58` (§4, rituale settimanale) prescrive `bash tools/check_architecture_boundaries.sh`, che **non esiste** (`tools/` non lo contiene). Non è una svista di percorso: lo script è stato deliberatamente consolidato in `tools/check_architecture.py` (`tools/architecture_rules_core.toml:6,28` — "Consolidated from 15+ gate scripts and check_architecture_boundaries.sh"). Resta il fatto che il rituale documentato fa eseguire un comando che fallisce. La nota I-1 del documento invece è già corretta (cita `check_architecture.py`).
- **Ritirato:** `bash tools/gates/manifest.sh` non è un gate orfano — `tools/run_developer_gates.sh:30` lo *source-a*, ne itera `DEVELOPER_GATES` ed è invocato da `.github/workflows/developer-gates.yml:28`. Essere una lista di nomi è il design (SSOT del gate set), non un difetto; `DEVELOPER_GATES` include già `check_architecture.py`.
- Perché è pericoloso: un agente/sviluppatore esegue il rituale e non certifica nulla credendo di farlo.
- Fix: riallineare GOVERNANCE.md alla catena reale — `[APPLICATO]` (Parte F.1).
- Rischio: Basso.

### [P1] B.4 — `steady_state_allocations_per_frame`: SSOT frammentata
`[CONFERMATO — con prova diretta del falso PASS]`
- Il fatto vive in 6 sedi: `docs/baselines/steady-state-allocations-certified.txt:8` (=0.0), `tools/gates/check_steady_state_allocations.sh:10` (default `REPORT` = baseline → certifica un file, non un run), la *definizione* del campo in `include/chronon3d/runtime/telemetry/render_telemetry_record.hpp:181` (**sede non rilevata nel primo giro**), `apps/chronon3d_cli/utils/telemetry/telemetry_record.hpp:61`, `apps/chronon3d_cli/utils/job/report/render_job_report_output.inc:135`, `apps/chronon3d_cli/commands/basic/command_benchmark_saturation_json.inc:152`.
- Prova del "falso PASS storico": la baseline dichiara `source_revision=8bdf0f807` (riga 2) e il checkout corrente è `5af91243a`; il gate legge solo `steady_state_allocations_per_frame` e **non confronta mai** `source_revision`. Il PASS è quindi "un run di un'altra revisione non allocava", presentato come certificazione di questa.
- Owner canonico: il sidecar del run corrente; la baseline solo come riferimento.
- Fix applicato (F.1): il gate ora legge `source_revision`, la confronta con HEAD, la stampa sempre nel PASS e trasforma il mismatch in `GATE_WARN` (default) o in fail fatale con `CHRONON_STEADY_STATE_REQUIRE_REVISION=1`. La policy su "dove vive il report del run corrente" resta **differita** (F.2).
- Rischio: Medio.

### [P1] B.5 — Contratto layout shader: il commento è stale, la guardia esiste solo su un lato
`[CORRETTO — la fix proposta avrebbe rotto la build]`
- Il commento in `src/backends/vulkan/shaders/text_run.comp:13` dichiara "must match ... (40 bytes)". Il valore reale è **48** e il lato C++ **è già guardato**: `static_assert(sizeof(GlyphInstance) == 48, "GlyphInstance must stay 48 bytes to match text_run.comp")` in `include/chronon3d/runtime/render_surface.hpp:361` (la stessa famiglia ora guarda `TextRunDynamic == 128`, `:395-398` (112 era la dimensione storica precedente a rotation/fill-color)). La fix suggerita (`static_assert(... == 40)`) era quindi doppiamente sbagliata: l'assert esisteva già, e il numero proposto è quello del commento stale.
- Difetto reale residuo: la guardia era **asimmetrica** — un cambio della struct GLSL non rompeva nulla lato C++ (il commento ne era la prova: era rimasto a 40 mentre il C++ era a 48) e nessun test legava le due rappresentazioni.
- Contesto che il primo giro non aveva raccolto: il repo ha già un'infrastruttura ABI per le shader — `tools/shaders/spirv_abi_gen.cpp` genera per `text_run` lo header `TextRunShaderAbi` (local size 16/16/1, binding 0/1/3, `kPushConstantSize = 40`). Ma quel generatore emette solo local size, binding e push-constant size: **nessun offset o dimensione delle struct di storage buffer**. Il test `tests/backends/test_shader_abi_consistency.cpp` (§"generated ABI headers match runtime reflection") verifica le local size di 2 sole shader e richiede Vulkan + `spirv-reflect`, quindi non è una guardia utilizzabile su host senza Vulkan.
- Fix applicato e verificato (F.1, 2026-09-25): `tests/test_shader_layout_contract.py` ora confronta sequenza dei campi e size std430 C++/GLSL per `GlyphInstance` e per il record `TextRunDynamic` nei due shader tile, poi verifica gli `ArrayStride` SPIR-V quando è disponibile `glslangValidator`. Nel checkout video-test: layout contract PASS (stride compilato eseguito), shader ABI consistency PASS, text tile canary PASS e render graph PASS. L'estensione corrente del record tile è 128 byte (rotation/fill-color), non i 112 byte storici. Il finding storico G.1 è aggiornato in fondo a questo file.
- Rischio: Medio (silent visual corruption, ora coperto dal test).

### [P2] B.6 — `[RITIRATO]` Enum severity camera: sono già un alias di tipo
- Verifica: esiste **un solo** `enum class Severity` (`include/chronon3d/scene/camera/camera_v1/camera_program.hpp:50`). `CameraResolveDiagnostic::Severity` è `using Severity = CameraProgramDiagnostic::Severity;` (`:94`), con commento che dichiara l'alias come garanzia di non-divergenza. La deduplicazione richiesta dall'audit è già in essere.
- Residuo reale (igiene): il commento in `src/scene/camera/camera_v1/shot_timeline.cpp:370-373` descriveva ancora "i due enum che condividono i 3 valori" come se fossero tipi distinti → `[APPLICATO]` riallineato all'alias (F.1).
- Rischio: nullo.

### B.7 — Salute governance (conferme, ri-verificate)
- Nessun ciclo gate↔gate. `tools/duplicate_basename_allowlist.txt` = 27 basename + 2 righe di commento (29 righe `wc -l`): policy dichiarata nel manifest TOML, non allowlist fantasma. Il gate concorda: `duplicate_source_basenames — 26 duplicate basename(s), all grandfathered` (184/184 PASS).
- `check_no_sqlite.sh` resta vivo post-demolition SQLite come anti-regression: l'header del file (righe 2-8) lo dichiara già esplicitamente come guardia irreversibile → la richiesta "da etichettare come tale" è già soddisfatta.
- Nessun consumer che aggira `AssetResolver`/`RenderSurfaceRegistry`/`EffectCatalog` sul data-plane. `resource_index_` DirectYuv (`src/media/video/direct_yuv/direct_yuv_program.cpp:353-372`, non `355-369`) è una proiezione ordinata interrogata con `std::lower_bound` su `layer_resources_`, non una seconda SSOT.
- Nessuno split-brain: executor unico (`docs/EXECUTOR_OWNERSHIP.md` coerente col codice), NV12 authority unificata (Wave 1D), telemetry SQLite demolita.

---

# A. Top 10 fixes by leverage

Ordinati per rapporto payoff/rischio:

| # | Fix | Riferimento | Rischio |
|---|---|---|---|
| 1 | Eliminare la D2D copy per-frame (Z3) passando a `composite_direct_nv12*` | 4.1 | Medio |
| 2 | Nominare l'ownership dei 3 contenitori per-frame (A/B/C) | B.1 | Medio |
| 3 | `GpuGlyphAtlas`: generation counter invece di 4 shared_lock/glyph/frame | 2.2 | Basso |
| 4 | Scindere i due mutex nel wrapper Vulkan batched | 2.1 | Medio |
| 5 | `Result`/enum per gli stati attesi di `RenderPipeline::render*` | 3.1 | Basso |
| 6 | Indici pre-risolti text/animation in `CompiledProgram` | 1.1 | Basso |
| 7 | Codice errore camera strutturato all'origine (elimina `derive_code_from_message`) | 3.2 | Basso |
| 8 | Contract test di layout per shader `.comp` (✅ applicato, `tests/test_shader_layout_contract.py`) | 4.4 / B.5 | nullo |
| 9 | Per-shard hit/miss counters in `LruCache` | 2.4 | Basso |
| 10 | Allineare GOVERNANCE.md ai gate reali (✅ applicato) | B.3 | nullo |

# B. Canonical ownership map

| Fatto | Owner canonico |
|---|---|
| Per-frame layer parameters | **decisione aperta (B.1)**: `compiler::FrameParameterBlock` (whole-frame, vivo) vs `graph::FrameParameterBlock` (per-binding, `PrecompNode`) vs `graph::FrameParameterTable` (per-frame-time, `CompiledFrameGraph`/`GPUCommandPlan`) |
| Shader layout contract | test eseguibile `tests/test_shader_layout_contract.py` (GLSL AST + `sizeof` C++ + `ArrayStride` SPIR-V) |
| Error taxonomy + legacy mapping | `include/chronon3d/core/types/error.hpp` |
| Frame/encode lifecycle states | `FrameSlotPool` (CAS transitions) |
| Zero-copy counters | timing sidecar `*.timing.json` → `job.gpu` |
| Steady-state allocation budget | sidecar del run corrente; baseline solo riferimento — dal 2026-09-12 il gate dichiara la revisione e avverte sul mismatch (B.4) |
| NV12 composition authority | `media::video::CudaNv12Compositor` |
| GPU glyph residency | `GpuGlyphAtlas` (+ generation counter da introdurre) |
| Compiled graph / allocazione hot-path | `CompiledFrameGraph` + `CompiledResourceTable` |
| Cache taxonomy | `include/chronon3d/cache/cache_taxonomy.hpp` (3 famiglie, rispettata) |
| Architecture invariants | `tools/architecture_rules.toml` (i gate devono consumarla, non duplicarla — B.2) |

# C. Dead code demolition list

Solo elementi con evidenza forte:

1. **`PrecompNode::execute` legacy wrapper** — `src/render_graph/cache/precomp_node_execute.cpp:117-137`; post PR-6.2 lo usa solo il test lattice; demolition già documentata nel file stesso. `[PRECISAZIONE 2026-09-12]` Il file è ancora compilato in produzione (`src/render_graph/CMakeLists.txt:172`) e il wrapper è il path di produzione documentato *fino* a PR-6.2 (commento alle righe 100-118): demolition **condizionata**, non immediata.
2. **Uno dei due `FrameParameterBlock`** — esce dopo il consolidation di A.2 (vedi B.1).
3. **Riferimenti GOVERNANCE a gate inesistenti** — `check_architecture_boundaries.sh` fantasma (B.3): ✅ rimosso da `docs/GOVERNANCE.md` il 2026-09-12 (F.1).

Verifiche consegnate: nessun header zombie nuovo (la Wave 7-header demolition ha già ripulito; `tools/check_public_headers.py` vivo). `JobSession::arena()` deprecated D-02: confine chiaro, mantenersi per test harness.

# D. Governance simplification

- **Fondere**: le 19 regole `*_error_construction_adapter_only` → 1 regola data-driven che derivi la allowlist da un registro nel TOML o da un'annotazione nel codice (B.2).
- **Trasformare**: `canonical_error_legacy_mapping_inventory` da `min_matches = 28` (count-based) a contract che enumeri le mapping attese o le importi dalla fonte.
- **Eliminare**: dopioni di documento GOVERNANCE ↔ AGENTS ↔ manifest sul "gate set"; scegliere AGENTS come indice e far puntare GOVERNANCE al manifest effettivamente consumato.
- **Conservare**: `tools/check_architecture.py` (motore dichiarativo, scanner unico), `tools/gates/check_steady_state_allocations.sh` (ora con revision binding dichiarata, F.1), `tools/check_no_sqlite.sh` (anti-regression, etichettato come tale).

---

# PART E — Verifica della Parte B (2026-09-12, checkout `5af91243a`)

Metodo: rilettura delle sedi citate sul checkout con `grep`/`sed`, più i gate eseguibili senza build host. Nessuna build, nessun GPU, nessun run. Verdetti: **CONFERMATO** (l'evidenza sostiene la tesi), **CORRETTO** (la tesi regge, i dettagli no), **RITIRATO** (la tesi non regge).

| Item | Verdetto | Evidenza verificata | Azione |
|---|---|---|---|
| B.1 | CORRETTO | Due `FrameParameterBlock` esistono (`compiler/frame_parameters.hpp:78`, `graph/frame_parameter_block.hpp:56`). A è vivo (sampler `typed_program_sampler.cpp:44,122`, `render_pipeline.cpp:111-112`, `frame_slot_payload.hpp:24`, `render_graph_context.hpp:521`); B è usato solo da `PrecompNode::m_param_block` (`precomp_node.hpp:165`, `warm_up` in `precomp_node_execute.cpp:252`) + fixture (`test_precomp_node_cache.cpp:311-320`). Terzo contenitore non rilevato: `graph::FrameParameterTable` (`frame_parameter_table.hpp:61`). | Riclassificato P1; differito (design) |
| B.2 | CONFERMATO | 18 regole adapter (non 19), `allow_list` a path hardcodato per regola; `canonical_error_legacy_mapping_inventory` (`architecture_rules.toml:43-46`) usa `min_matches = 28`, esattamente la cardinalità di `std::array<LegacyErrorMapping, 28>` (`error.hpp:65`). | Conteggio corretto nel documento; fix data-driven differita (F.2) |
| B.3 | CORRETTO | `tools/check_architecture_boundaries.sh` assente (consolidato in `check_architecture.py`, `architecture_rules_core.toml:6,28`) mentre GOVERNANCE.md:58 lo prescriveva; `tools/gates/manifest.sh` è invece consumato (`run_developer_gates.sh:30` ← `.github/workflows/developer-gates.yml:28`). | ✅ GOVERNANCE.md corretto; la tesi su manifest.sh è ritirata |
| B.4 | CONFERMATO | Baseline `source_revision=8bdf0f807` (riga 2) ≠ HEAD `5af91243a`; il gate non leggeva `source_revision` (default `REPORT` = baseline, riga 10). 6ª sede trovata: `include/chronon3d/runtime/telemetry/render_telemetry_record.hpp:181`. | ✅ Revision binding nel gate + test dedicato; policy sul report del run differita |
| B.5 / 4.4 | CORRETTO | `static_assert(sizeof(GlyphInstance) == 48)` esiste (`render_surface.hpp:361`); la GLSL compilata vale 48 (`glslangValidator` → `ArrayStride 48`); il commento in `text_run.comp:13` diceva 40. | ✅ Commento corretto + test di contratto (F.1) |
| B.6 | RITIRATO | Un solo `enum class Severity` (`camera_program.hpp:50`); `CameraResolveDiagnostic::Severity` è `using` alias (`:94`). | ✅ Solo il commento stale è stato riallineato |
| B.7 | CONFERMATO | 27 basename + 2 righe di commento; `resource_index_` = `std::lower_bound` su `layer_resources_` (`direct_yuv_program.cpp:353-372`); `check_no_sqlite.sh` auto-etichettato (righe 2-8). | Nessuna azione (era già corretto) |
| C.1 | PRECISAZIONE | `precomp_node_execute.cpp` è ancora in `src/render_graph/CMakeLists.txt:172`; il wrapper `execute()` è il path di produzione fino a PR-6.2 (commento righe 100-118). | Demolition condizionata, non immediata |

Gate eseguiti per questa verifica (senza build host): `python3.11 tools/check_architecture.py` → `GATE_PASS: 184/184`; `bash tools/architecture_gate_selftest.sh` → `PASSED (11 assertions)`; `bash tools/gates/check_steady_state_allocations.sh` → `GATE_PASS`; `bash tools/check_test_hygiene.sh` → `PASS [1/3..3/3]`; `python3.11 tests/test_common_performance_gate.py` e `tests/test_gpu_corpus_manifest.py` → `PASS`; aggiunti `tests/test_shader_layout_contract.py` e `tests/test_steady_state_allocation_gate.py`.

> Nota d'ambiente (non un finding di codice): su questo host `python3` è 3.10 e `check_architecture.py` importa `tomllib` (3.11+), quindi serve l'interprete registrato dal build system (`Python3_EXECUTABLE`). `tools/execute_gate.sh` invoca `python3` letterale: su un host con 3.10 come default la catena può fallire con `ModuleNotFoundError: tomli`.

---

# PART F — Remediation

## F.1 Applicato il 2026-09-12

| Item | Modifica | Verifica |
|---|---|---|
| B.5 / 4.4 | `src/backends/vulkan/shaders/text_run.comp:13`: byte-count corretto (40 → 48) con riferimento al `static_assert`; nuovo `tests/test_shader_layout_contract.py` (slot scalari GLSL vs campi C++, ordine dei membri con nome condiviso, `sizeof` del `static_assert`, `ArrayStride` SPIR-V via `glslangValidator`, degradazione esplicita se il tool manca) — guardia utilizzabile senza Vulkan, da assorbire in `spirv_abi_gen` quando emetterà i member offset | test verde, eseguito con e senza `glslangValidator` nel `PATH`; 4 mutazioni della shader rilevate (commento stale, membro aggiunto, scambio di membri omonimi, retype) |
| B.6 | commento stale in `src/scene/camera/camera_v1/shot_timeline.cpp` riallineato all'alias di tipo | `grep`: una sola dichiarazione di enum |
| B.4 | `tools/gates/check_steady_state_allocations.sh` legge `source_revision`, la confronta con HEAD (prefix-match sullo SHA), la stampa sempre nel `GATE_PASS`; mismatch = `GATE_WARN` di default, fatale con `CHRONON_STEADY_STATE_REQUIRE_REVISION=1` (`CHRONON_STEADY_STATE_REVISION` per la revisione attesa); nuovo `tests/test_steady_state_allocation_gate.py` (7 casi) | gate `GATE_PASS` sulla baseline; test verde |
| B.3 | `docs/GOVERNANCE.md` §4 ora prescrive `bash tools/run_developer_gates.sh` (manifest in `tools/gates/manifest.sh`) invece del gate demolito | `run_developer_gates.sh` presente e invocato dalla CI |

## F.2 Differito (con motivo)

| Item | Motivo |
|---|---|
| B.1 | consolidare/nominare i 3 contenitori per-frame è una decisione di ownership (A/B/C), non un rename meccanico: richiede una scelta sul modello dei parametri e tocca sampler, `PrecompNode`, `CompiledFrameGraph`, `GPUCommandPlan` |
| B.2 | 18 regole inline → 1 data-driven richiede un'API di annotazione nel codice (es. macro `CHRONON_ERROR_ADAPTER`) e una modifica al motore `check_architecture.py`, con la selftest estesa: da pianificare nella sua wave |
| B.4 strict-by-default | la baseline committata è per definizione di un'altra revisione: renderla fatale di default farebbe fallire `run_developer_gates.sh` su ogni checkout. Serve prima una policy su dove vive il report del run corrente |
| C.1 | demolition condizionata a PR-6.2: il file è ancora compilato e il wrapper è documentato come path di produzione fino a quel PR |

---

# PART G — Secondo passaggio (2026-09-12, round 2)

Chiusure raggiungibili della lista di next-step, più un finding nuovo emerso estendendo il contratto di layout.

## G.1 Nuovo finding [P1] — `GpuTextRunDynamic`: due layout, un nome; la pipeline `TextBatch` è irraggiungibile

> Aggiornamento 2026-09-25: il layout tile è 128 byte (rotation/fill-color); la nota storica 112/64 qui sotto descrive il primo audit. `tests/test_shader_layout_contract.py` controlla i due shader tile e il relativo stride SPIR-V contro l'ABI corrente. Nel lane `linux-video-test` sono passati il layout contract con stride SPIR-V compilato, il canary, shader ABI consistency e render graph; il core completo è bloccato dall'assenza degli asset font richiesti. La suite Vulkan debug esegue i casi ma abortisce in teardown con `free(): invalid pointer` e resta aperta. **Caveat sul tree attuale:** è comparso un ramo SDF per stroke shape in `fill_rect.comp` con dispatch dedicato, mentre `test_vulkan_debug_context.cpp` ancora si aspetta il rifiuto `UnsupportedCapability`; le verifiche sopra precedono questa modifica e non la certificano. Parità pixel e stato del test restano aperti: vedere `docs/tickets/TICKET-ROADMAP-LOCAL-CLOSEOUT-2026-09.md`.
- **Due struct omonime con dimensioni diverse**: `text_batch.comp:30` dichiara `GpuTextRunDynamic` con 16 membri (stride std430 **64**, come dice il suo stesso commento *"64 bytes per text run dynamic entry"*); `text_tile_bin.comp:20` e `text_tile_raster.comp:27` dichiarano il record tile completo (ora **128** byte dopo animator rotation/fill-color; era 112 al momento dell'audit iniziale).
- **Il C++ ha un solo alias per quel nome**: `using GpuTextRunDynamic = runtime::TextRunDynamic;` con `static_assert(sizeof(...) == 128)` (`src/backends/vulkan/vulkan_backend_text_ops_private.cpp`).
- **Chi lo usa davvero**: `draw_text_batch` carica `runs.size() * sizeof(GpuTextRunDynamic)` (128 B/run nell'ABI corrente) e batte quel buffer ai kernel tile (`write_text_tile_bin_descriptors(..., run_buffer, ...)`, stesso file `:289`) → coerente con l'ABI corrente a 128 byte. Il percorso `record_text_batch` — l'unico che risolverebbe il kernel `TextBatch`, cioè lo shader a 64 B — **non ha nessun call-site**: la sua unica occorrenza fuori dalla definizione è la dichiarazione in `vulkan_backend_impl.hpp:425`.
- **Conseguenza**: la divergenza 64/128 è **latente, non viva**; ma la pipeline `TextBatch` viene comunque creata e registrata all'avvio (`vulkan_backend_lifecycle_private.cpp:383-403`) e lo shader è compilato e embedded (`src/backends/vulkan/CMakeLists.txt:50`). Chi riattivasse `record_text_batch` otterrebbe una lettura a stride sbagliato **senza alcun errore di build**. La contract test ora convalida stride e campi di entrambi gli shader tile separatamente.
- **Evidenza riproducibile**: `glslangValidator -V <shader>.comp` + lettura delle `OpDecorate ArrayStride` dava 48/64 per `text_batch` e 48/112 per gli shader tile al tempo dell'audit. Dopo l'estensione ABI, `tests/test_shader_layout_contract.py` controlla entrambi gli shader tile contro il record C++ a 128 byte e il loro ArrayStride compilato.
- **Checklist di demolition ancora aperta**: `record_text_batch` (definizione + dichiarazione), `GpuKernelId::TextBatch`, registrazione/pipeline, `text_batch.comp`, entry CMake, embed SPIR-V e relativo test ABI. Non è stata rimossa in questo lavoro: è una pipeline distinta e l'audit iniziale la trovava senza call-site; serve chiudere la verifica dei riferimenti prima della demolizione. La contract test ora protegge gli shader tile correnti, non certifica né riattiva `TextBatch`.



## G.2 Chiusure applicate nel round 2

| Item | Modifica | Verifica |
|---|---|---|
| Next-step 1 | `tools/execute_gate.sh`: selezione dell'interprete che sa leggere il TOML (`tomllib`/`tomli`, override `CHRONON_PYTHON`), come già faceva `architecture_gate_selftest.sh` | prima: `ModuleNotFoundError: tomli`; dopo: `GATE_PASS: 184/184` attraverso il dispatcher; gate non-python invariati; override rispettato nei due sensi |
| Next-step 3 | Nuovo kind dichiarativo `[[doc_tool_reference]]` in `check_architecture.py` + regola `doc_tool_references_resolve` in `architecture_rules.toml`: i doc operativi non possono citare `tools/...` inesistenti. Esclusi per pattern i record datati (`docs/AUDIT-*`, `docs/CCACHE-AUDIT-*`), gli archivi (`adr/`, `baselines/`, `tickets/`, `archive/`) e `ROADMAP.md` (forward-looking) | gate: `[PASS] doc_tool_references_resolve — N tool reference(s) resolved`; controllo negativo su albero reale: iniezione di un riferimento fittizio → `GATE_FAIL` con il riferimento stampato |
| Next-step 7 | **Nessuna azione**: `__pycache__/` è già in `.gitignore`; la directory compariva perché `.gitignore:90` ha `!tests/**` che la ri-abilita per l'intero sottoalbero | `git check-ignore -v` |

## G.3 Doc rot trovata dal nuovo gate (5 riferimenti in 4 doc — tutti a tool demoliti deliberatamente)

| Riferimento inesistente | Doc | Tool demolito in | Fix applicata |
|---|---|---|---|
| `tools/check_wbh_toolchain.sh` + `tools/wbh_toolchain_manifest.env` | `docs/CURRENT_STATUS.md:182` | `4de939887` / `e1716fe63` | la riga ora punta a `tools/gates/manifest.sh` (`WBH_GATES`) + al ticket aperto `CERT-SEQUENCE-WBH` |
| `tools/audit_incomplete_type_pattern.sh` | `docs/reference/AGENTS_GATE_MNT.md:89,95,99` | `5968d148b` | spostato in "Retired from this pipeline", con nota che la capability **non** è coperta dal registry canonico |
| `tools/capture_renderdoc.sh` | `docs/gpu/renderdoc.md:17` | `5602bdb57` | sostituito con l'invocazione diretta `renderdoccmd capture --wait-for-exit --capture-file ...` che il wrapper eseguiva |
| `tools/compare_benchmarks.py` | `docs/schemas/README.md:11` | `4f19b4468` | consumer aggiornato a `tools/check_common_performance_gate.py` |

## G.4 Triage stash (read-only, nessuna azione distruttiva)

| Stash | Data | Contenuto | Raccomandazione |
|---|---|---|---|
| `stash@{0}` `audit-p1p5-wip` | 2026-09-06 | 15 file, +462/-142 (`render_runtime.cpp`, `render_preparation_assets_detail.hpp`) | verificare se è già in `main`; altrimenti **land o drop esplicito** (è lavoro di audit, non refactor) |
| `stash@{1}` WIP su `b1364c39d` | 2026-09-04 | 5 file, +172/-22 (`direct_yuv_program.cpp` 111 righe + preset linux-video-fast-dev) | **land**: tocca il lane DirectYUV, cioè la priorità 4.1; richiede rebase attento (il file è cambiato dopo) |
| `stash@{2}` `wip-image-fade-diag-and-fixes` | 2026-09-01 | 7 file, +173/-51 (`source_node.cpp`, `refresh/source.cpp`) | se il bug del fade immagine è chiuso → **drop**; altrimenti land |
| `stash@{3}` `bug6-wip-check` | 2026-08-26 | 8 file, +405/-46 (116 righe di test su `text_placement_resolver` + `build_dev.sh`) | **land**: fix e test già scritti, minor lavoro residuo |
| `stash@{4}` `WIP-pre-refactor: 3 vulkan + 16 examples` | 2026-08-20 | 4 file, +59/-11 (`vulkan_backend_impl_ops.inc`, `gpu_text_run.cpp`) | **drop probabile**: "pre-refactor", il refactor è atterrato e 59 righe non rappresentano i 16 examples promessi |

## G.5 Resta aperto (non raggiungibile in questo ambiente)

- **Fixture del nuovo kind**: aggiungere `case11` in `tools/architecture_gate_selftest.sh` (doc che cita un tool inesistente → `GATE_FAIL`); oggi la prova è il controllo negativo eseguito a mano sull'albero reale.
- **Contratto di layout generalizzato**: `tests/test_shader_layout_contract.py` copre `text_run.comp`; le coppie `GpuGlyphStatic`/`GlyphStatic` e `GpuTextRunDynamic`/`TextRunDynamic` dei due `text_tile_*` sono già verificate via `glslangValidator` ma non ancora asserite nel test.
- **Vulkan shape-stroke candidate** (aggiornamento 2026-09-25): il codice attuale prova a promuovere stroke solide, ma il test dispatch mantiene le aspettative di rifiuto; non considerare supporto o parity verificati fino a oracle pixel, test riallineato e rebuild del tree corrente.
- **G.1 demolition** e le voci F.2 già differite, tutte da eseguire su un build host.
