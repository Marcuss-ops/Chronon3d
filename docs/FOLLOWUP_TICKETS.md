# Follow-up Tickets — Open Blockers Index

> Stato: [`CURRENT_STATUS.md`](CURRENT_STATUS.md) — dettaglio in `docs/tickets/`.

## Open Blockers (≤10)

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| Main truth / Stage 1-2 closeout | P0 | VERIFY | [MAIN-TRUTH-STAGE-1-2](tickets/TICKET-MAIN-TRUTH-STAGE-1-2.md) — frame-slot structural closeout + Vulkan pipeline-cache consistency + docs reconciliation landed; close only after same-SHA `Chronon CI` green and main-tip verification |
| Stage 1-5 stabilization campaign | P0 | VERIFY | [TICKET-200](tickets/TICKET-200-stage-plan.md) — Stabilize→Certify→Accelerate stages 1-5: build GREEN (756 targets), cache/compositor/render_job_contract/software suites PASS, arch gate 120/120 PASS (all 10 pre-existing fails cleared, see ticket); close after CI green on same SHA |
| Font asset fixtures | P1 | RESOLVED (source) | all 21 fixtures re-pinned in `tools/bootstrap_test_fonts.py` (blob SHA-1, local-store first) + `assets/` bootstrap-only policy in `.gitignore`; text/render_graph suites no longer fail on missing assets; residual suite failures are geometry/in-flight work, not asset debt |
| Engine certification aggregator | P0 | OPEN | [TICKET-125](tickets/TICKET-125-test-aggregator.md) — Test 13 = Camera brutal, distinto da Test 11; product validation in PipelineGen |
| Test harness + WBH cert | P1 | OPEN | [TEST-FONT-ASSET-PATH](tickets/TICKET-TEST-FONT-ASSET-PATH.md) + [CERT-SEQUENCE-WBH](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md) |
| Vulkan-gated registry suite | P1 | PARTIAL 50/59 (preset `linux-video-test`) | [AUDIT-2026-09-12-REMEDIATION §7](tickets/TICKET-AUDIT-2026-09-12-REMEDIATION.md) — aliasing pianificato esplicito + fix harness `atlas.stats().hits`/batch vuoto (contro il contratto `gpu_glyph_atlas.hpp:254-256`): suite **49→50**. Restano 9 rossi, due difetti: (1) `ResourcePlanner` aliasa due risorse **caricate dall'host** nello stesso slot (`last_use < first` senza distinguere contenuto importato) — `ResourceResidency::requires_dedicated_allocation()` esiste ma i test non lo dichiarano; (2) `allocation.physical_slot` è un indice **locale al plan** usato come slot fisico globale → serve la mappa `plan_slot → physical_slot` per istanza di plan (il retirement **completo** dei transient è stato provato e revertito; il retirement **per-frame-slot**, ristretto alle surface materializzate dentro un batch attivo e con regression test, è invece atterrato e non chiude da solo questo difetto). I due fix sono in conflitto sul caso `medium` (`physical_slots == 2` **e** parità pixel): decisione di contratto del maintainer. Più 1 conteggio slot fisici, 1 pixel checkpoint batch, 2 testo (styled-cache `ctx.runtime` non cablato, parità pixel text-run kernel) |
| Vulkan-gated unit lane | P1 | LANDED 8/8 | `scripts/ci/verify_vulkan_gated_unit_tests.sh` + job `vulkan-gated-unit` in `native-video.yml` — le 8 suite headless (`shader_abi`, `vulkan_debug_context`, `vulkan_descriptor_arena`, `vulkan_kernel_store`, `vulkan_surface_materializer`, `vulkan_interop_generation`, `vulkan_submission_ring`, `vulkan_upload_ring`) ora vengono compilate ed eseguite in CI: era il gap che ha lasciato passare un HEAD non compilante e tre assertion stale |
| Text tile raster canary | P1 | OPEN (GPU) | `chronon3d_text_tile_raster_canary_tests` rosso pre-esistente: 13 assertion di pixel (`full.*`, `half.*`, `mtsdf.*`, `counts[*] >= 1` tutte a 0 → il dispatch compute non scrive). **Riprodotto identico** azzerando il retirement per-slot, e il canary non usa `VulkanBackend`/la surface authority (dispatch SPIR-V raw) → non correlato al change set. Escluso dalla lane finché non è verde; va diagnosticato su driver/GPU. |
| Skip-rot registry | P2 | OPEN | [DOCTEST-SKIP-ROT](tickets/TICKET-DOCTEST-SKIP-ROT.md) — single authority for every `doctest::skip` (7 rows, deadline 2026-09-30); skip senza riga = lint failure, riga scaduta = CI FAIL |
| Benchmark + CPU budget | P1 | MEASURED / HOST CERT PENDING | [BENCHMARK-CORPUS-OFFICIAL](tickets/TICKET-BENCHMARK-CORPUS-OFFICIAL.md) + [P1E-CPU-BUDGET](tickets/TICKET-P1E-CPU-BUDGET-MEASUREMENT.md) — B00 smoke and same-SHA CPU measurement landed; full corpus still needs the external content pack |
| OpenType features | P1 | OPEN | [OPENTYPE-FEATURES-PASS](tickets/TICKET-OPENTYPE-FEATURES-PASS.md) |
| CLI project UX | P2 | OPEN | [ADD-LOADER-FOR-CHRONON-JSON](tickets/TICKET-ADD-LOADER-FOR-CHRONON-JSON.md) |
| Node memory metrics | P1 | OPEN | [NODE-MEMORY-METRICS](tickets/TICKET-NODE-MEMORY-METRICS.md) |
| CLI/IPC hot-path parity (video build) | P1 | RESOLVED | [CLI-IPC-HOTPATH-PARITY](tickets/TICKET-CLI-IPC-HOTPATH-PARITY.md) — opzione A atterrata in `0db926a47` (`test(cli): pin wire fidelity for resolved encoder selection`): il sottocaso `pure-video GPU job carries its resolved hot path to DirectYUV` autora `gpu_hot_path_mode` dentro `output_spec`, quindi entrambi i transport portano la selezione risolta e i fingerprint coincidono. La riga era rimasta OPEN per mancato aggiornamento dell'indice |

## Deferred by certification precondition

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| Audit remediation residue | P1/P2 | DEFERRED (each with a named precondition) | [AUDIT-REMEDIATION-DEFERRED](tickets/TICKET-AUDIT-REMEDIATION-DEFERRED.md) — F8 surface/lease lock consolidation (needs the N4 regression run + lock-wait evidence), F12 dirty-path string keys (needs pixel-parity + an interned cross-frame key), F13 motion-blur per-sample isolation (determinism-certified pooling), F14 non-pooled framebuffer wrappers (ADR for a new residency primitive) |
| Encoder DTO change amplification | P2 | DOCUMENTED | [ENCODER-DTO-PROJECTION](tickets/TICKET-ENCODER-DTO-PROJECTION.md) — the projection chain and the per-hop extension protocol; consolidating the DTOs would move encoding fingerprints and needs its own ticket |

## Demolition Debt (exit-condition driven)

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| Fingerprint duplicato (FP-1) | P0 | REMOVED | [PRE-SLANG-FOUNDATION](tickets/TICKET-PRE-SLANG-FOUNDATION.md) — `render_settings_fingerprint`/`material()`/`ProgramDigests` duplicati fra `compiler::stages` e `render_plan`; ora una sola authority (`render_plan::compute_render_job_fingerprint`), uguaglianza dei due entry point dimostrata da test |
| Cache taxonomy enforcement | P1 | LANDED | [PRE-SLANG-FOUNDATION](tickets/TICKET-PRE-SLANG-FOUNDATION.md) — AGENTS dichiarava `cache_taxonomy.hpp` + `architecture_rules.toml` come enforcement, ora lo è: 3 regole + 4 casi nel gate selftest (gate 188/188, selftest 15/15) |
| Artefatti GPU fuori da ProgramCache (CUDA/NV12) | P2 | REMOVED | [PRE-SLANG-FOUNDATION](tickets/TICKET-PRE-SLANG-FOUNDATION.md) DD-3 — PTX NV12 ora usa `CompiledArtifactCache::save/load_program_artifact()` con chiave canonica; il CUmodule è una lease locale al compositor, senza `cached_ptx`, `s_modules` o `s_module_mutex` |

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| FFmpeg subprocess video sink | P1 | OPEN | [FFMPEG-PIPE-SINK-DEMOLITION](tickets/TICKET-FFMPEG-PIPE-SINK-DEMOLITION.md) — delete solo dopo native release/SDK/codec/zero-caller certification; Wave 0 closeout classifies this debt but does not bypass its exit conditions |
| SQLite telemetry (store + emitters + CLI readers) | P1 | REMOVED | [TELEMETRY-SQLITE-NORMALIZATION](tickets/TICKET-TELEMETRY-SQLITE-NORMALIZATION.md) — Wave 1D: runtime events-only (NullTelemetryStore default), zero dipendenza sqlite3 in src/include/apps; persistenza/aggregazione engine-external |
| RenderProfiler legacy subsystem | P2 | REMOVED | [TELEMETRY-SQLITE-NORMALIZATION](tickets/TICKET-TELEMETRY-SQLITE-NORMALIZATION.md) — demolito: rimosso graph_profiler.hpp/.cpp e riferimenti da RenderGraphContext |
| PersistentFramebufferStore (CFB4 disk codec) | P2 | REMOVED | codec, env vars, membri RenderRuntime/Config e test rimossi; exit condition: zero produttori/consumatori in produzione (gia verificato); record: ADR-024 + git history |
| Legacy composition adapter | P1 | ACTIVE | [WAVE-CLOSEOUT-DEMOLITION-DEBT](tickets/TICKET-WAVE-CLOSEOUT-DEMOLITION-DEBT.md) — `Composition::SceneFunction` compatibility route remains until typed Composition lowering has corpus equivalence |
| MTSDF FreeType guard granularity | P1 | READY_TO_REMOVE | [HOTPATH-TEXT-MTSDF-CONTENTION](tickets/TICKET-HOTPATH-TEXT-MTSDF-CONTENTION.md) — guardia **per-face** atterrata: `MtsdfFaceGuard` + `FreeTypeFaceLease`/`get_face_lease`, `static std::mutex` rimossa; byte-parity e parità concorrente 8-worker verdi in `chronon3d_mtsdf_generator_tests` (6 case / 114 assertions). Resta solo la conferma numerica `mtsdf_lock_wait_us` su corpus multi-font (host esterno) |
| CPU per-glyph text selector compat | P1 | ACTIVE | [GPU-TEXT-CPU-SELECTOR-COMPAT](tickets/TICKET-GPU-TEXT-CPU-SELECTOR-COMPAT.md) — counters landed (`gpu_text_cpu_selector_compat_*`); delete only after the official corpus proves 0 hits |
| Vulkan surface/submission domain coupling | P1 | REMOVED (recording path) | [HOTPATH-AUDIT-N1-N8](tickets/TICKET-HOTPATH-AUDIT-N1-N8.md) — `SurfaceLease` + per-slot generation token decouple the recording ops: lease sotto lock, rilascio, solo `submission_ring.mutex`, revalidation al hand-off; 4 casi CPU-only + suite minima Vulkan-ON verdi. Resta il doppio lock dove serve davvero (per-op legacy che possiede il batch, `download_surface`) |

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
| Telemetry granular emitter | P2 | SUPERSEDED (Wave 1D) | [TELEMETRY-GRANULAR-CONSUMER](tickets/TICKET-TELEMETRY-GRANULAR-CONSUMER.md) — emettitori granulari gated su SQLite rimossi; resta l'API eventi in-memory (`record_*`) per store host |
| Text clip warn/regression | P3 | OPEN | [TEXT-OVERSIZED-CLIP-WARN](tickets/TICKET-TEXT-OVERSIZED-CLIP-WARN.md) |

## Audit roadmap waves 1-6 (working session)

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| Video execution authority + easy cleanup + surface tests + telemetry gate | P1 | CODE DONE (build/CI pending) | [AUDIT-ROADMAP-WAVES](tickets/TICKET-AUDIT-ROADMAP-WAVES.md) — Waves 1/2/3/5/6 code landed; Wave 4 NV12 unified in Wave 1D into `media::video::CudaNv12Compositor` (contract matrix registered, determinism evidence pending GPU-host run); downgrade reason/count receipt is now emitted by the final resolver decision |

## Recently Closed (ultimi 3)

- `PERF-BASELINE-V1` — DONE; baseline software/CPU lockata e blur_band riabilitato in BENCH-3; evidenza archiviata in `docs/baselines/`, gate vivo come `tools/check_common_performance_gate.py`
- `NODE-CACHE-KEY-COLLAPSE-ROT` — DONE 2026-09-04 after re-census: `run_node`, `emit_node_records` and `cache_evaluator` all carry the canonical `NodeCacheKey`; historical forward-points already landed
- IPC heap-buffer-overflow in `decode_reply` — fuzzer (libFuzzer) → fix (Verifier) in `eabb6713a` (git history)

> Storico completo dei ticket chiusi: `git log --follow docs/tickets/` (archive cartella rimossa 2026-09-05; la git history è l'unico archivio).  <!-- drift-class: historical -->
