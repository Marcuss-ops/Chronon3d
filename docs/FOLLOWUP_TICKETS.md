# Follow-up Tickets — Open Blockers Index

> Stato: [`CURRENT_STATUS.md`](CURRENT_STATUS.md) — dettaglio in `docs/tickets/`.

## Open Blockers (≤10)

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| Luma/Chroma Key native GPU dispatch | P1 | OPEN | [KEYING-GPU-NATIVE](tickets/TICKET-KEYING-GPU-NATIVE.md) — software keyer is certified; native shader/capability gate remains |
| Echo native GPU dispatch | P1 | OPEN | [ECHO-GPU-NATIVE](tickets/TICKET-ECHO-GPU-NATIVE.md) — local software processor is certified; GPU-only surfaces fail explicitly until native frame-history storage and dispatch land |
| Main truth / Stage 1-2 closeout | P0 | VERIFY | [MAIN-TRUTH-STAGE-1-2](tickets/TICKET-MAIN-TRUTH-STAGE-1-2.md) — frame-slot structural closeout + Vulkan pipeline-cache consistency + docs reconciliation landed; close only after same-SHA `Chronon CI` green and main-tip verification |
| Stage 1-5 stabilization campaign | P0 | VERIFY | [TICKET-200](tickets/TICKET-200-stage-plan.md) — Stabilize→Certify→Accelerate stages 1-5: build GREEN (756 targets), cache/compositor/render_job_contract/software suites PASS, arch gate 120/120 PASS (all 10 pre-existing fails cleared, see ticket); close after CI green on same SHA |
| Font asset fixtures | P1 | RESOLVED (source) | all 21 fixtures re-pinned in `tools/bootstrap_test_fonts.py` (blob SHA-1, local-store first) + `assets/` bootstrap-only policy in `.gitignore`; text/render_graph suites no longer fail on missing assets; residual suite failures are geometry/in-flight work, not asset debt |
| Engine certification aggregator | P0 | OPEN | [TICKET-125](tickets/TICKET-125-test-aggregator.md) — Test 13 = Camera brutal, distinto da Test 11; product validation in PipelineGen |
| Test harness + WBH cert | P1 | OPEN | [TEST-FONT-ASSET-PATH](tickets/TICKET-TEST-FONT-ASSET-PATH.md) + [CERT-SEQUENCE-WBH](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md) |
| Vulkan-gated registry suite | P1 | **62/62 PASS** (preset `linux-video-test`, 2026-09-24; era 59/59 il 2026-09-16) | [AUDIT-2026-09-12-REMEDIATION §7](tickets/TICKET-AUDIT-2026-09-12-REMEDIATION.md) — **2026-09-24**: riconfermata `chronon3d_backend_registry_tests` **62/62 casi / 920/920 assert** con il tree corrente (rebuild `-j12`, 417 edge, 0 errori) sulla lane Vulkan; l'host logga `CUDA interop extensions unavailable` quindi l'interop Vulkan↔CUDA **non** è esercitata qui. — **CHIUSA 2026-09-16**: l'ultimo rosso (`styled cache miss/hit`) era l'harness del canary che costruiva il `RenderGraphContext` a mano senza cablare `ctx.gpu_styled_glyph_cache` (in produzione lo fa `scene_context_setup.cpp`, autorità unica): cache nulla ⇒ ogni frame MISS e 0 hit. Cablata dal `runtime` nel test → suite **59/59** (847/847 assertion), da 50/59. I due difetti di placement (`ResourcePlanner` che aliasa due risorse caricate dall'host, e `plan_slot → physical_slot` locale al plan) **non** si manifestano più in questa suite; la decisione di contratto sul caso `medium` resta un item separato se riemerge. Storico: aliasing pianificato esplicito + fix harness `atlas.stats().hits`/batch vuoto (contro il contratto `gpu_glyph_atlas.hpp:254-256`): suite **49→50**. Restano 9 rossi, due difetti: (1) `ResourcePlanner` aliasa due risorse **caricate dall'host** nello stesso slot (`last_use < first` senza distinguere contenuto importato) — `ResourceResidency::requires_dedicated_allocation()` esiste ma i test non lo dichiarano; (2) `allocation.physical_slot` è un indice **locale al plan** usato come slot fisico globale → serve la mappa `plan_slot → physical_slot` per istanza di plan (il retirement **completo** dei transient è stato provato e revertito; il retirement **per-frame-slot**, ristretto alle surface materializzate dentro un batch attivo e con regression test, è invece atterrato e non chiude da solo questo difetto). I due fix sono in conflitto sul caso `medium` (`physical_slots == 2` **e** parità pixel): decisione di contratto del maintainer. Più 1 conteggio slot fisici, 1 pixel checkpoint batch, 2 testo (styled-cache `ctx.runtime` non cablato, parità pixel text-run kernel) |
| Vulkan shape-stroke parity | P1 | OPEN / fail-closed pending pixel oracle | [ROADMAP-LOCAL-CLOSEOUT](tickets/TICKET-ROADMAP-LOCAL-CLOSEOUT-2026-09.md) — il dispatch Vulkan rifiuta ancora Rect/RoundedRect/Circle stroked con `UnsupportedCapability`; lo shader SDF resta candidato non eseguito. Le assertion del dispatch sono riallineate, ma nessuna parità pixel è certificata; restano oracle CPU/GPU, rebuild e verifica del teardown Vulkan |
| Vulkan-gated unit lane | P1 | LANDED 8/8 | `scripts/ci/verify_vulkan_gated_unit_tests.sh` + job `vulkan-gated-unit` in `native-video.yml` — le 8 suite headless (`shader_abi`, `vulkan_debug_context`, `vulkan_descriptor_arena`, `vulkan_kernel_store`, `vulkan_surface_materializer`, `vulkan_interop_generation`, `vulkan_submission_ring`, `vulkan_upload_ring`) ora vengono compilate ed eseguite in CI: era il gap che ha lasciato passare un HEAD non compilante e tre assertion stale |
| Text tile raster canary | P1 | **CHIUSA 2026-09-16 — 78/78 VERDE** (3 run consecutivi) | `chronon3d_text_tile_raster_canary_tests`: i 65/78 registrati erano un **binario stantio**, non un difetto driver/GPU. Il `.ninja_log` della lane fissa l'ultima build del target alle **07:00 UTC**, mentre l'ABI fix `5b59cfd48` ("restore text tile canary ABI contract": il mirror a 64 byte di `GpuTextRunDynamic` sostituito dall'ABI di produzione, che allora era 112 byte) atterra alle **16:33 UTC**; il target era escluso da ogni gate, quindi nessuno lo ricostruiva. Ricostruito dallo stesso albero: **78/78, 0 failed, 0 skipped**. Evidenza [`docs/evidence/text-tile-canary-2026-09-16.md`](evidence/text-tile-canary-2026-09-16.md); gate GPU/WBH `tools/verify_text_tile_gpu_canary.sh` (registrato in `tools/gates/manifest.sh`). Il layout dinamico è stato poi esteso da 112 a 128 byte per rotation/fill-color; il canary ora usa il tipo runtime e il test `test_shader_layout_contract.py` confronta entrambi gli shader tile con il C++ e il relativo SPIR-V. Nel closeout locale 2026-09-25 contract layout, shader ABI consistency, canary e render graph sono passati; la prova GPU same-SHA/FullGraph-DirectYUV resta separata. Resta fuori dalla lane headless, che è device-free: lì darebbe uno skip silenzioso. |
| Ciclo di vita job daemon (throughput) | P1 | **OPEN** | [DAEMON-JOB-LIFECYCLE](tickets/TICKET-DAEMON-JOB-LIFECYCLE.md) — resta da completare la suite GPU multi-shape same-SHA e il batch `one runtime + one pool + one preparation → N output`; non rimuovere il lock né aumentare slot senza dati di allocator per shape. Le misure storiche del lifecycle e il receipt unico indicano ancora il costo per-job come leva, ma non certificano il tree corrente. |
| Text Quality V2 (FullGraph) | P1 | **P0-2 CHIUSA 2026-09-17; P0-0b SBLOCCATA** | [TEXT-QUALITY-V2](tickets/TICKET-TEXT-QUALITY-V2.md) — il lowering colore CSS/sRGB → linear è ora unico in `parse_hex_color()` con gate `#808080`; il framehash RGBA8 canonico è `tools/raw_frame_hash.py` (`CHRONON FRAMEHASH v1`). Restano sampling/coverage, shadow canonica, MTSDF/HQ stroke e la campagna DirectYUV; evidence [`color-contract-framehash-2026-09-17.md`](evidence/color-contract-framehash-2026-09-17.md) |
| Native decoder — heap corruption in teardown | P0 | **RISOLTO 2026-09-17 — ASan/UBSan PASS** | Il percorso `frame_to_framebuffer` ora riserva 64 byte di padding SIMD per l'output RGBA di libswscale, mantenendo l'allineamento a 32 byte. Lane `linux-asan-native`, filtro `-tc='decoder*'`: **9/9 casi, 222/222 assertion, 0 leak**, exit 0. Evidenza: [`native-decoder-asan-2026-09-17`](evidence/native-decoder-asan-2026-09-17.md) |
| Framework compiler layer-batch: 3 rossi con 2 cause distinte | P0 | RESOLVED (contratto ri-pinnato) | [SCALE-P0](tickets/TICKET-SCALE-P0-ASSET-VRAM-GOP.md) — causa #1+#2 (invariante `100 Image layers → 1 batch` + golden): gate `frame_bound_image_source` di `00be928ab`, troppo ampio perché `binding_meta.has_binding()` è vero per **ogni** Source raggiungibile → nessuna immagine poteva più essere fusa, mentre il commento dichiarava il contrario. Causa #3 (TextRun `lowered_into_batch`): `members.size() < 2` di `a78a8a29e` sopprime il batch di un solo membro. Decisione (immagini native-only) applicata: invariante riscritta al nuovo contratto, golden rigenerato (`0x65304b41dbf40d8f`) come cambio contrattuale esplicito, proxy del test TextRun sostituito con l'intento reale. Scoperta collaterale: la suite era **troncata** — la `CHECK` su `layer_batches[0]` vuoto faceva SIGSEGV e solo 31/378 casi giravano → ora `REQUIRE`, `chronon3d_render_graph_tests` **378/378 PASS** (era 29/31 su 31 eseguiti) |
| Lane `linux-video-test` (GPU/video) | P1 | **LANDED** | Lane configurata e costruita (648/648, EXIT 0) dopo aver liberato i due alberi `.tmp/chronon-builds/*` (33 GB). I due vuoti video-gated ora girano: `chronon3d_cache_tests` include `test_direct_yuv_text_cache.cpp` (997596 assertion) e `chronon3d_cli_tests` include `test_packet_assembler_contract.cpp` (GOP executor). Il parity gate della demolition hybrid è **eseguito e verde**: `tests/cli/test_gop_direct_mux_parity.cpp` confronta i due percorsi sul corpus congelato e, per la lane copy, l'SHA-256 dell'intero file è **byte-identico** (oltre a payload/PTS/DTS/flags, frame decodificati, durata e audio). **2026-09-16:** la lane è stata **ri-creata** dopo la liberazione degli alberi (era rimasto il solo symlink pendente `build/chronon/linux-video-fast-dev`, ora rimosso): `build/chronon/linux-video-test` → `.tmp/chronon-builds/linux-video-test`, ed è il primo candidato CLI nella discovery di `tools/verify_zero_copy_end_to_end.sh`. Vedi [`CCACHE-AUDIT-2026-09-10.md`](CCACHE-AUDIT-2026-09-10.md) per il rimedio `.ninja_deps` che ha reso incrementali i build |
| Benchmark + CPU budget | P1 | MEASURED / HOST CERT PENDING | [BENCHMARK-CORPUS-OFFICIAL](tickets/TICKET-BENCHMARK-CORPUS-OFFICIAL.md) — B00 smoke and same-SHA CPU measurement landed; full corpus still needs the external content pack |
| CLI project UX | P2 | P1 CODE LANDED (CLI fixture E2E PASS; generated-template host verification pending) | [ADD-LOADER-FOR-CHRONON-JSON](tickets/TICKET-ADD-LOADER-FOR-CHRONON-JSON.md) |
| Daemon Job Runtime (FlatBuffers) | P1 | PARTIAL (FlatBuffers prepared-frame lifecycle + CLI whole-plan job status verified over software IPC; plan-backed FlatBuffers executor and prepared-plan LRU remain) | [DAEMON-JOB-RUNTIME](tickets/TICKET-DAEMON-JOB-RUNTIME.md) |
| Node memory metrics | P1 | OPEN (instrumentation, per-node events and gate landed; representative corpus certification pending) | [NODE-MEMORY-METRICS](tickets/TICKET-NODE-MEMORY-METRICS.md) |

### Cross-checkout contract sign-offs

- **`scale_out` catalog (2026-09-24):** the user approved retiring the
  wire-visible ID and using the compiler-synthesized exit. The catalog and its
  consumers are not present in this checkout; implementation and compatibility
  verification remain pending in the checkout that owns them. Do not add a
  per-motion exit-track schema.
- **I1-CHUNK / DURABLE-DAG:** this checkout has `frame_range` and
  `parallel_disjoint` runtime fields, but no `plan_sha256`, `parent_job_id`,
  `chunk_index`, or producer that submits children. The producer and its
  identical-plan-hash integration test remain pending in the separate checkout.

## Deferred by certification precondition

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| Verità pixel via immagine | P1 | **CHIUSA 2026-09-17 (exit condition 2)** | [IMAGE-PATH-PIXEL-TRUTH](tickets/TICKET-IMAGE-PATH-PIXEL-TRUTH.md) — la via PNG (still e sequenza) non compone il testo e non varia per frame/backend; il **raw sink pre-encode** (`--video-sink raw`, `2a5579985`) è il sostituto atterrato e verificato (91 frame RGBA 1920×1080, testo presente e variabile). Il sanity scan non può vederlo: `scan_framebuffer_sanity` rifiuta **solo** NaN/Inf > 5% e accetta un canvas trasparente per contratto. Evidenza [`docs/evidence/image-path-defect-2026-09-16.md`](evidence/image-path-defect-2026-09-16.md). **Chiusura 2026-09-17:** la lane immagine è **dichiarata non-canonica** con un messaggio esplicito nel CLI (una sola authority, `apps/chronon3d_cli/utils/job/output_lane_pixel_truth.hpp`, emessa dai due punti di decisione della lane) e la via canonica è eseguibile end-to-end (`--video-sink raw` → `tools/raw_frame_to_png.py`, evidenza [`docs/evidence/pre-encode-pixel-truth-2026-09-17.md`](evidence/pre-encode-pixel-truth-2026-09-17.md)); riparare la via still (exit condition 1) resta un lavoro opzionale, non la precondizione di P0-0b |
| Audit remediation residue | P1/P2 | DEFERRED (each with a named precondition) | [AUDIT-REMEDIATION-DEFERRED](tickets/TICKET-AUDIT-REMEDIATION-DEFERRED.md) — F8 surface/lease lock consolidation (needs the N4 regression run + lock-wait evidence), F12 dirty-path string keys (needs pixel-parity + an interned cross-frame key), F13 motion-blur per-sample isolation (determinism-certified pooling), F14 non-pooled framebuffer wrappers (ADR for a new residency primitive) |
| Encoder DTO change amplification | P2 | DOCUMENTED | [ENCODER-DTO-PROJECTION](tickets/TICKET-ENCODER-DTO-PROJECTION.md) — the projection chain and the per-hop extension protocol; consolidating the DTOs would move encoding fingerprints and needs its own ticket |

## Demolition Debt (exit-condition driven)

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| FFmpeg subprocess video sink | P1 | OPEN | [FFMPEG-PIPE-SINK-DEMOLITION](tickets/TICKET-FFMPEG-PIPE-SINK-DEMOLITION.md) — delete solo dopo native release/SDK/codec/zero-caller certification; Wave 0 closeout classifies this debt but does not bypass its exit conditions |
| Legacy composition adapter | P1 | ACTIVE | [WAVE-CLOSEOUT-DEMOLITION-DEBT](tickets/TICKET-WAVE-CLOSEOUT-DEMOLITION-DEBT.md) — `Composition::SceneFunction` compatibility route remains until typed Composition lowering has corpus equivalence |
| MTSDF FreeType guard granularity | P1 | READY_TO_REMOVE | [HOTPATH-TEXT-MTSDF-CONTENTION](tickets/TICKET-HOTPATH-TEXT-MTSDF-CONTENTION.md) — guardia **per-face** atterrata: `MtsdfFaceGuard` + `FreeTypeFaceLease`/`get_face_lease`, `static std::mutex` rimossa; byte-parity e parità concorrente 8-worker verdi in `chronon3d_mtsdf_generator_tests` (6 case / 114 assertions). Resta solo la conferma numerica `mtsdf_lock_wait_us` su corpus multi-font (host esterno) |
| Hybrid GOP segment-file executor | P1 | **DEMOLITO (5A→5F chiusi; resta 5H same-SHA GPU)** | [SCALE-P0-ASSET-VRAM-GOP](tickets/TICKET-SCALE-P0-ASSET-VRAM-GOP.md) — un solo executor: `execute_direct_mux_gop_plan` scrive copie **e** re-encode nella **stessa** `MuxSession` caller-owned, audio una volta, trailer una volta, pubblicazione atomica. Landati: **5A** target di splice encoder-owned (`encoder_bitstream_contract`, l'encoder dichiara il proprio stream a `open()` via `AV_CODEC_FLAG_GLOBAL_HEADER`, la sorgente è solo il lato destro del confronto); **hvcC fix**; **avcC fix** (le lunghezze dei parameter set erano lette con la larghezza dei *sample* invece che a 16 bit come vuole l'`AVCDecoderConfigurationRecord` → ogni sorgente mp4 produceva zero NAL e il gate non poteva certificare nulla); **formato del container prestato** (il container dichiarato dalla sorgente porta già il suo configuration record, quindi libavformat NON inserisce la conversione automatica Annex-B → length-prefixed: il producer ora emette il formato che il container dichiara); **duration dell'ultimo packet** al confine del mux (l'ultimo sample di un GOP re-encodato finale spariva dalla durata/edit list); **5B** comparatore oracle + corpus congelato (`tests/cli/test_gop_direct_mux_parity.cpp`) verde su **tutte e cinque** le forme; **5C** seam `MuxSession` caller-owned; **5D** `open_source_declared_mux` + `encoder_reproduces` + `submit_gop_source_packets` + `submit_source_audio` (finestra in tick interi, origine per video **e** audio); **5E/5F** child render nel mux del chiamante con certificazione dello stream a `close()` (fail-closed); **5G** demolizione (`execute_hybrid_gop_plan`, workspace per segmento, remux: zero residui). Resta **5H**: evidenza same-SHA sulla lane GPU reale |
| Fused storage-image batch (ritirato) | P1 | PARTIAL | [SCALE-P0](tickets/TICKET-SCALE-P0-ASSET-VRAM-GOP.md) — ramo immagine demolito da compiler/executor il 2026-09-17; resta da acquisire la certificazione native-image (parità pixel + zero layer persi sul corpus overlay + una run video) prima di chiudere l’exit condition |

## Hot-path closeout (2026-09-12 working session)

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| Effect scratch wiring regression | P0 | FIXED (build pending) | [EFFECT-SCRATCH-WIRING-REGRESSION](tickets/TICKET-EFFECT-SCRATCH-WIRING-REGRESSION.md) — `effect_scratch`/`depth_buffer_pool`/`dof_scratch` were never wired into `SoftwareProcessorContext`; effects/mesh/DOF allocated a heap framebuffer per frame. Restored in `make_processor_context` + effect scratch is now a required reference |
| FramebufferPool release policy | P2 | LANDED | [POOL-RELEASE-POLICY-DIAGNOSTICS](tickets/TICKET-POOL-RELEASE-POLICY-DIAGNOSTICS.md) — `RestoreScratchHandle` cleanup now invoked, `ReturnToScratch` null-slot no longer leaks, `unexpected_pool_release_policy_count` added |
| Dead headers + orphan contract | P2 | REMOVED | 7 zero-consumer headers deleted (`render_to_media_compiler`, `fusion_support`, `compiled_frame_graph_serializer`, `magic_enum_safe`, `text_box`, `text_spec`, `resolved_render_job`) + `render_to_media_serializer_contract` removed from `tools/architecture_rules_migrated.toml`; census: no include/symbol consumer, not in `Chronon3DPublicHeaders.cmake` |
| `pipe_pixfmt` single authority | P0 | CODE DONE (build pending) | one parser (`runtime::try_parse_pixel_format`) + CLI projection + `InvalidPipePixelFormat` at the job boundary; unknown values now fail loud instead of silently becoming RGBA |
| NodeMemoryTracker lock metering | P1 | INSTRUMENTED | `node_memory_lock_{acquires,wait_us,max_wait_us,hold_wall_us}` added; the restructure is data-gated on those counters |
| Audit 2026-09-12 action list | P0 | CODE DONE (build verde) | [AUDIT-2026-09-12-REMEDIATION](tickets/TICKET-AUDIT-2026-09-12-REMEDIATION.md) — feature-gated TU ora compilati ad ogni PR (SVG/EXR/MESH/C-API/DAEMON/CRASH_HANDLER/DEV_WATCH) + fallback GPU-testo e JobSession di compatibilità ora osservabili via contatori |

## Backlog P2/P3 (non bloccante)

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| Zero-copy Z-waves | P2 | ACTIVE | [ZERO-COPY-ZWAVES](tickets/TICKET-ZERO-COPY-ZWAVES.md) — Z0 census done; Z1 typed encoder feed capability + Z4 capability matrix + Z6 depth telemetry + Z7 CUDA-lane gate-1 counter landed; Z3/Z4-corpus/Z5 GPU-driven follow-up and depth sweep remain open |
| Video codec boundary | P2 | PROTOTYPE LANDED | [VIDEO-CODEC-BOUNDARY](tickets/TICKET-VIDEO-CODEC-BOUNDARY.md) — `VideoExecutionPath` (4 stati) + tabella pura + `ChangedInterval` CLI/IPC + ponte GOP-analysis fail-closed; `TrimPackets` ora ha preflight ed executor packet-copy ai confini GOP, mentre l'esecuzione `BoundaryReencode` e la decisione per-layer restano aperte (§5 di `AUDIT-2026-09-12-VIDEO-CODEC-BOUNDARY`) |
| Video compiler arch | P2 | PLANNED | ROADMAP M7 — Video Compiler Architecture (`SceneIR → CompiledTemplateProgram → hot loop`); scheda da aprire all'avvio milestone |
| Text clip warn/regression | P3 | OPEN | [TEXT-OVERSIZED-CLIP-WARN](tickets/TICKET-TEXT-OVERSIZED-CLIP-WARN.md) |
| CLI strict `require_gpu_native` → parametri canonici | P2 | **CODE LANDED** (host-verify pending) | [CLI-STRICT-GPU-NATIVE-MAPPING](tickets/TICKET-CLI-STRICT-GPU-NATIVE-MAPPING.md) — la derivazione da `resolve_canonical_execution_parameters` è in `register_render_commands.cpp` (strict ⇒ `gpu_required=true`/`cpu_fallback_allowed=false`), override esplicito protetto e parità CLI/IPC verde (`chronon3d_cli_tests` 82/82); restano solo la verifica same-SHA della lane strict e il fail-closed su host incapace |

## Audit roadmap waves 1-6 (working session)

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| Video execution authority + easy cleanup + surface tests + telemetry gate | P1 | CODE DONE (build/CI pending) | [AUDIT-ROADMAP-WAVES](tickets/TICKET-AUDIT-ROADMAP-WAVES.md) — Waves 1/2/3/5/6 code landed; Wave 4 NV12 unified in Wave 1D into `media::video::CudaNv12Compositor` (contract matrix registered, determinism evidence pending GPU-host run); downgrade reason/count receipt is now emitted by the final resolver decision |

## Refactor backlog (non-blocking)

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| Hotspot decomposition: export, mux, render policies, Vulkan e text headers | P1 | ACTIVE | [HOTSPOT-DECOMPOSITION](tickets/TICKET-HOTSPOT-DECOMPOSITION.md) — Wave A–E: contract tests native export, `.inc` production TU, MuxSession/hash seam, node policy seams, Vulkan/CUDA split e header fan-out reduction; primo seam GOP target estratto e build CLI verde |

## Recently Closed and Historical Notes

- **2026-09-24 — DEMOLITION-MESH-SHAPE-COMPAT CHIUSO**: rimosso `MeshShape::mesh`
  (`include/chronon3d/scene/model/shape/shape.hpp`) e l'ultimo consumatore
  (`software_mesh_processor.cpp` draw/bbox + `graph_builder_source_pass.cpp`);
  ogni mesh arriva ora solo come `assets::PreparedMeshSourceRef` via
  `AssetResolver`. Evidenza: `chronon3d_core_tests` verde su mesh/asset
  (86 casi / 332 assert). Scheda rimossa a chiusura terminale.

- **2026-09-24 — DAEMON-IPC-REJECTION-RESILIENCE CHIUSO**: il contenimento era già
  in `main` (`detail::dispatch_request_safely` in `daemon/chronon_ipc.hpp`); la
  chiusura aggiunge il test di **integrazione sul transport reale**
  (`tests/cli/test_daemon_ipc_exception_boundary.cpp`: rigetto tipato → error
  envelope → `STATUS` Ok sulla stessa socket → render valido successivo →
  `Shutdown` pulito, `serve_result == 0`). `chronon3d_cli_tests` 82/82 casi,
  1084/1084 assert sul tree `linux-fast-dev`. Il rigetto resta un risultato di
  job: nessun fallback software. Scheda rimossa a chiusura terminale.

- **2026-09-24 — DOCTEST-SKIP-ROT risolto:** registry bidirezionale vuoto e
  lint/self-test verdi. `SKIP-007` è stato rimosso insieme al placeholder; il
  test di cancellazione C ABI passa nel build video (1 caso / 7 asserzioni;
  cancellazione prima del rendering, senza uso GPU). Il file ticket resta
  l'autorità per nuovi skip.

- **2026-09-24 — VULKAN-ANIMATED-IMAGE-25D-RESIDENCY CHIUSO**: pixel probe
  12/12 su RTX A4000 e strict capability routing certificati
  (`full_graph_native` + `native_surface`, zero readback; animated strict
  respinto esplicito pre-encoder). Fix compilatorio residuo di `d08958efa`
  (Shape lowering) atterrato contestualmente. Scheda rimossa a chiusura
  terminale: la git history è l'archivio.
- **2026-09-24 — purga ticket a chiusura terminale**: rimossi
  `TICKET-VULKAN-ANIMATED-IMAGE-25D-RESIDENCY` (CLOSED 2026-09-24),
  `TICKET-NATIVE-MP4-MUX-FRAME-LOSS` (FIXED 2026-09-23) e
  `TICKET-P1E-CPU-BUDGET-MEASUREMENT` (DONE 2026-09-11) da `docs/tickets/`,
  secondo la policy dell'indice (stato terminale ⇒ rimozione; la git history è
  l'unico archivio). Le regressioni permanenti restano nel codice e nelle
  fixture: `tests/fixtures/animated_image_residency/run_pixel_probe.sh`,
  `tests/registry/test_backend_registry_vulkan_transforms.cpp` e
  `tests/fixtures/text_motion_reference_v2/`. Nessun altro riferimento nel repo.
- **2026-09-24 — chiusi con verifiche CPU e purgati dall’indice**:
  `DAEMON-IPC-REJECTION-RESILIENCE` (Unix socket: rifiuto `UnsupportedCapability`,
  `STATUS`, render software valido e `Shutdown` sulla stessa connessione; 15/15
  asserzioni), `OPENTYPE-FEATURES-PASS` (plumbing fino a `TextRunLayout`; 8/8
  test, 42/42 asserzioni) e `RENDERER-CORE-MASK-SEGV` (backtrace sul sink TLS
  lasciato dalla fixture di campioni sovrapposti; suite renderer completa passata
  7 volte: 167/167 casi e 1744/1744 asserzioni ogni volta). Il crash era
  riproducibile su software CPU e non richiedeva GPU. Le schede terminali sono
  state rimosse secondo la policy dell’indice.
- **2026-09-23 — purga ticket a chiusura terminale**: rimossi `TICKET-GLOW-QUALITY-2026-09-18`
  (CLOSED), `TICKET-GPU-TEXT-CPU-SELECTOR-COMPAT` (RESOLVED 2026-09-21),
  `TICKET-TEXT-INTEROP-CACHE-OVERFLOW` (CLOSED) e `TICKET-TEXT-ROUTE-CONSOLIDATION`
  (CLOSED 2026-09-22) da `docs/tickets/`, secondo la policy dell'indice (stato terminale ⇒
  rimozione; la git history è l'unico archivio). Nessun altro riferimento nel repo.
- **2026-09-16 — schede a chiusura terminale rimosse** (12 ticket + l'audit datato del
  2026-09-09): `AUDIT-2026-09-09-REMEDIATION` (CLOSED),
  `CLI-IPC-HOTPATH-PARITY` (RESOLVED), `ERROR-MODEL-CANONICAL` (COMPLETE),
  `GOAL3-NATIVE-COMPOSITE-COLLISION` (CLOSED),
  `NATIVE-SOFTWARE-SWS-MULTIFRAME-CORRUPTION` (CLOSED),
  `TELEMETRY-SQLITE-NORMALIZATION` (CLOSED), `TELEMETRY-GRANULAR-CONSUMER` (SUPERSEDED),
  `DOD-RUNTIME-CPU-PASS` (verified), `STAGE-CLOSING-ARCH-WAVES-9-13` (IMPLEMENTED),
  `PRE-SLANG-FOUNDATION` (CODE+BUILD+GATE PASS), `HOTPATH-AUDIT-N1-N8` (CODE+BUILD PASS),
  `STATUS-CHRONICLE-ARCHIVE` (record only). Le righe a stato terminale sono state tolte
  da questo indice, che ora contiene solo blocchi aperti, residui deferiti e debt con
  exit condition. Nessuna informazione è persa: la git history è l'unico archivio.
- `PERF-BASELINE-V1` — DONE; baseline software/CPU lockata e blur_band riabilitato in BENCH-3; evidenza archiviata in `docs/baselines/`, gate vivo come `tools/check_common_performance_gate.py`
- `NODE-CACHE-KEY-COLLAPSE-ROT` — DONE 2026-09-04 after re-census: `run_node`, `emit_node_records` and `cache_evaluator` all carry the canonical `NodeCacheKey`; historical forward-points already landed
- IPC heap-buffer-overflow in `decode_reply` — fuzzer (libFuzzer) → fix (Verifier) in `eabb6713a` (git history)

> Storico completo dei ticket chiusi: `git log --follow docs/tickets/` (archive cartella rimossa 2026-09-05; la git history è l'unico archivio).  <!-- drift-class: historical -->
