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
| Skip-rot registry | P2 | OPEN | [DOCTEST-SKIP-ROT](tickets/TICKET-DOCTEST-SKIP-ROT.md) — single authority for every `doctest::skip` (7 rows, deadline 2026-09-30); skip senza riga = lint failure, riga scaduta = CI FAIL |
| Benchmark + CPU budget | P1 | MEASURED / HOST CERT PENDING | [BENCHMARK-CORPUS-OFFICIAL](tickets/TICKET-BENCHMARK-CORPUS-OFFICIAL.md) + [P1E-CPU-BUDGET](tickets/TICKET-P1E-CPU-BUDGET-MEASUREMENT.md) — B00 smoke and same-SHA CPU measurement landed; full corpus still needs the external content pack |
| OpenType features | P1 | OPEN | [OPENTYPE-FEATURES-PASS](tickets/TICKET-OPENTYPE-FEATURES-PASS.md) |
| CLI project UX | P2 | OPEN | [ADD-LOADER-FOR-CHRONON-JSON](tickets/TICKET-ADD-LOADER-FOR-CHRONON-JSON.md) |
| Node memory metrics | P1 | OPEN | [NODE-MEMORY-METRICS](tickets/TICKET-NODE-MEMORY-METRICS.md) |

## Demolition Debt (exit-condition driven)

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| FFmpeg subprocess video sink | P1 | OPEN | [FFMPEG-PIPE-SINK-DEMOLITION](tickets/TICKET-FFMPEG-PIPE-SINK-DEMOLITION.md) — delete solo dopo native release/SDK/codec/zero-caller certification; Wave 0 closeout classifies this debt but does not bypass its exit conditions |
| SQLite telemetry (store + emitters + CLI readers) | P1 | REMOVED | [TELEMETRY-SQLITE-NORMALIZATION](tickets/TICKET-TELEMETRY-SQLITE-NORMALIZATION.md) — Wave 1D: runtime events-only (NullTelemetryStore default), zero dipendenza sqlite3 in src/include/apps; persistenza/aggregazione engine-external |
| RenderProfiler legacy subsystem | P2 | REMOVED | [TELEMETRY-SQLITE-NORMALIZATION](tickets/TICKET-TELEMETRY-SQLITE-NORMALIZATION.md) — demolito: rimosso graph_profiler.hpp/.cpp e riferimenti da RenderGraphContext |
| PersistentFramebufferStore (CFB4 disk codec) | P2 | REMOVED | codec, env vars, membri RenderRuntime/Config e test rimossi; exit condition: zero produttori/consumatori in produzione (gia verificato); record: ADR-024 + git history |
| Legacy composition adapter | P1 | ACTIVE | [WAVE-CLOSEOUT-DEMOLITION-DEBT](tickets/TICKET-WAVE-CLOSEOUT-DEMOLITION-DEBT.md) — `Composition::SceneFunction` compatibility route remains until typed Composition lowering has corpus equivalence |

## Backlog P2/P3 (non bloccante)

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| Zero-copy Z-waves | P2 | ACTIVE | [ZERO-COPY-ZWAVES](tickets/TICKET-ZERO-COPY-ZWAVES.md) — Z0 census done; Z1 typed encoder feed capability + Z4 capability matrix + Z6 depth telemetry + Z7 CUDA-lane gate-1 counter landed; Z3/Z4-corpus/Z5 GPU-driven follow-up and depth sweep remain open |
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
