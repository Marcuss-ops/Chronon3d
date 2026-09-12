# Chronon3D — Audit Findings & Fixes (`fixes.md`)

> **Origine:** audit tecnico read-only (static analysis, nessuna build/test eseguita, nessuna modifica al codice).
> **Data:** 2026-09-12.
> **Scope:** data-plane per-frame (`src/runtime`, `src/render_graph`, `src/backends`, `src/media`, `src/cache`), boundary I/O/codec, governance tooling.
> **Classificazione:** P0 = rischio correctness/perf sul path certificato · P1 = costo strutturale · P2 = igiene.
> **Metodo:** le evidenze sono `path:line` del checkout corrente; le frequenze ("per frame") derivano dalla lettura dei call-site, non da profiling runtime. Gli arbitri per la priorizzazione a valle sono i contatori già instrumentati (`m_lock_wait_us`, `node_memory_lock_*`, zero-copy sidecar `*.timing.json`).

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
- Fatto: `keys/widths/heights/metrics` + 5 `pending_*` vettori, `make_shared<GpuPreparedTextRun>`, `std::vector<FontFaceHandle> fonts` per ogni *nuovo* prepared run (cache-miss); loop di re-check richiama `location()`/`metrics()` per glyph. Su testo animato (ticket `gpu_text_cpu_selector_compat`) può rientrare nel path.
- Fix consigliato: prepare-scratch riutilizzabile (pattern `download_scratch_`).
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

### [P2] 4.4 — Contratto C++↔GLSL documentato da commenti
- `src/backends/vulkan/shaders/text_run.comp:14` ("Byte layout must match `runtime::GlyphInstance` (40 bytes)"). Due rappresentazioni dello stesso fatto per-glyph (`GlyphStatic` CPU / `GlyphInstance` GPU) senza contratto condiviso.
- Fix: `static_assert(sizeof(GlyphInstance)==40)` + contract test di layout a build time.
- Rischio: Medio (silent visual corruption su rename/add-field).

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

### [P0] B.1 — Due `FrameParameterBlock`, stesso nome, stesso scopo, forme diverse
- Owner A: `chronon3d::compiler::FrameParameterBlock` — `include/chronon3d/compiler/frame_parameters.hpp:78` (usato da sampler + `render_pipeline.cpp`).
- Owner B: `chronon3d::graph::FrameParameterBlock` / `LayerParameterBlock` (B6) — `include/chronon3d/render_graph/pipeline/frame_parameter_block.hpp:33-58` (consumato da `precomp_node.hpp`, bench test).
- Fatto duplicato: storage parametri per-frame per l'esecuzione del grafo. Contratti paralleli (matrix, opacity), zero conversione esplicita documentata.
- Perché è pericoloso: semantic drift (invarianti refresh solo su B; classe di bug residual-value su A), rename unsafe, change amplification.
- Owner canonico raccomandato: **B6 `graph::FrameParameterBlock`** (pre-sized, capacity-stable invariants, testati). Migrare il sampler a scrivere lì; deprecare A.
- Cosa eliminare dopo la migrazione: `include/chronon3d/compiler/frame_parameters.hpp` (o il suo riflesso, se il consolidation avviene in senso inverso).
- Rischio: Medio.

### [P0] B.2 — Il gate dell'error-model contiene la seconda copia della tassonomia
- Evidenze: `tools/architecture_rules.toml` — `canonical_error_legacy_mapping_inventory` (`presence`, `min_matches = 28` su `\{"chronon3d::`) + **19 regole** `*_error_construction_adapter_only` con allowlist hardcodata (19 path).
- Fatto duplicato: l'elenco dei tipi-errore legittimi e delle loro sedi vive (a) negli header, (b) nel gate come literal, (c) in `kLegacyErrorMappings` come literal. Il gate non legge la conoscenza che protegge: la ricopia.
- Perché è pericoloso: falso senso di sicurezza (count-based fragile in entrambe le direzioni), change amplification (3 file per nuovo tipo errore), gate rotto da un path move senza cambio semantico.
- Owner canonico: `include/chronon3d/core/types/error.hpp` + annotazione/convenzione (es. macro `CHRONON_ERROR_ADAPTER`) che lo scanner consumi data-driven.
- Cosa eliminare: le 19 allowlist inline → 1 regola data-driven.
- Rischio: Medio.

### [P1] B.3 — GOVERNANCE punta a gate inesistenti (stale documentation)
- `docs/GOVERNANCE.md` §4 e I-1 prescrivono `bash tools/check_architecture_boundaries.sh` (**inesistente** — glob 0 risultati) e `bash tools/gates/manifest.sh` (è una *lista di nomi*; nessun consumatore in `.github`).
- Perché è pericoloso: un agente/sviluppatore esegue il rituale e non certifica nulla credendo di farlo.
- Fix: allineare GOVERNANCE.md a `DEVELOPER_GATES` via `run_developer_gates.sh`, o creare il wrapper mancante.
- Rischio: Basso-Medio.

### [P1] B.4 — `steady_state_allocations_per_frame`: SSOT frammentata
- Il fatto vive in: `docs/baselines/steady-state-allocations-certified.txt:8` (=0.0), `tools/gates/check_steady_state_allocations.sh` (default REPORT = baseline → certifica un file, non un run), `telemetry_record.hpp:61`, `render_job_report_output.inc:135`, `command_benchmark_saturation_json.inc:152`.
- Perché è pericoloso: falso PASS storico — il gate non lega il valore a un run dello stesso SHA.
- Owner canonico: il sidecar del run corrente; la baseline solo come riferimento.
- Rischio: Medio.

### [P1] B.5 — Contratto layout shader per commento, non per schema
- `src/backends/vulkan/shaders/text_run.comp:14` e famiglia `.comp`. Nessuno schema condiviso o test generato.
- Fix: header condiviso o test che compili la shader e confronti `offsetof`/`sizeof`.
- Rischio: Medio (silent failure).

### [P2] B.6 — Enum severity camera paralleli
- `CameraProgramDiagnostic::Severity` e `CameraResolveDiagnostic::Severity` "share the same 3 values", bit-cast (`shot_timeline.cpp:370-373`). Il commento ammette la duplicazione. Owner canonico: un solo enum condiviso.
- Rischio: Basso.

### B.7 — Salute governance (conferme)
- Nessun ciclo gate↔gate. `tools/duplicate_basename_allowlist.txt` (27 righe grandfathering) è policy dichiarata nel manifest TOML, non allowlist fantasma.
- `check_no_sqlite.sh` resta vivo post-demolition SQLite come anti-regression — legittimo, da etichettare esplicitamente come tale.
- Nessun consumer che aggira `AssetResolver`/`RenderSurfaceRegistry`/`EffectCatalog` sul data-plane. `resource_index_` DirectYuv (`direct_yuv_program.cpp:355-369`) è proiezione indicizzata, non seconda SSOT.
- Nessuno split-brain: executor unico (`docs/EXECUTOR_OWNERSHIP.md` coerente col codice), NV12 authority unificata (Wave 1D), telemetry SQLite demolita.

---

# A. Top 10 fixes by leverage

Ordinati per rapporto payoff/rischio:

| # | Fix | Riferimento | Rischio |
|---|---|---|---|
| 1 | Eliminare la D2D copy per-frame (Z3) passando a `composite_direct_nv12*` | 4.1 | Medio |
| 2 | Consolidare i due `FrameParameterBlock` su B6 | B.1 | Basso-Medio |
| 3 | `GpuGlyphAtlas`: generation counter invece di 4 shared_lock/glyph/frame | 2.2 | Basso |
| 4 | Scindere i due mutex nel wrapper Vulkan batched | 2.1 | Medio |
| 5 | `Result`/enum per gli stati attesi di `RenderPipeline::render*` | 3.1 | Basso |
| 6 | Indici pre-risolti text/animation in `CompiledProgram` | 1.1 | Basso |
| 7 | Codice errore camera strutturato all'origine (elimina `derive_code_from_message`) | 3.2 | Basso |
| 8 | Contract test di layout per shader `.comp` | 4.4 / B.5 | Basso |
| 9 | Per-shard hit/miss counters in `LruCache` | 2.4 | Basso |
| 10 | Allineare GOVERNANCE.md ai gate reali | B.3 | nullo |

# B. Canonical ownership map

| Fatto | Owner canonico |
|---|---|
| Per-frame layer parameters | `graph::FrameParameterBlock` (B6, `render_graph/pipeline/`) |
| Error taxonomy + legacy mapping | `include/chronon3d/core/types/error.hpp` |
| Frame/encode lifecycle states | `FrameSlotPool` (CAS transitions) |
| Zero-copy counters | timing sidecar `*.timing.json` → `job.gpu` |
| Steady-state allocation budget | sidecar del run corrente; baseline solo riferimento |
| NV12 composition authority | `media::video::CudaNv12Compositor` |
| GPU glyph residency | `GpuGlyphAtlas` (+ generation counter da introdurre) |
| Compiled graph / allocazione hot-path | `CompiledFrameGraph` + `CompiledResourceTable` |
| Cache taxonomy | `include/chronon3d/cache/cache_taxonomy.hpp` (3 famiglie, rispettata) |
| Architecture invariants | `tools/architecture_rules.toml` (i gate devono consumarla, non duplicarla — B.2) |

# C. Dead code demolition list

Solo elementi con evidenza forte:

1. **`PrecompNode::execute` legacy wrapper** — `src/render_graph/cache/precomp_node_execute.cpp:117-137`; post PR-6.2 lo usa solo il test lattice; demolition già documentata nel file stesso.
2. **Uno dei due `FrameParameterBlock`** — esce dopo il consolidation di A.2 (vedi B.1).
3. **Riferimenti GOVERNANCE a gate inesistenti** — `check_architecture_boundaries.sh` fantasma (B.3; non-codice ma stesso effetto).

Verifiche consegnate: nessun header zombie nuovo (la Wave 7-header demolition ha già ripulito; `tools/check_public_headers.py` vivo). `JobSession::arena()` deprecated D-02: confine chiaro, mantenersi per test harness.

# D. Governance simplification

- **Fondere**: le 19 regole `*_error_construction_adapter_only` → 1 regola data-driven che derivi la allowlist da un registro nel TOML o da un'annotazione nel codice (B.2).
- **Trasformare**: `canonical_error_legacy_mapping_inventory` da `min_matches = 28` (count-based) a contract che enumeri le mapping attese o le importi dalla fonte.
- **Eliminare**: dopioni di documento GOVERNANCE ↔ AGENTS ↔ manifest sul "gate set"; scegliere AGENTS come indice e far puntare GOVERNANCE al manifest effettivamente consumato.
- **Conservare**: `tools/check_architecture.py` (motore dichiarativo, scanner unico), `tools/gates/check_steady_state_allocations.sh` (ma bindato allo SHA del run), `tools/check_no_sqlite.sh` (anti-regression, etichettato come tale).
