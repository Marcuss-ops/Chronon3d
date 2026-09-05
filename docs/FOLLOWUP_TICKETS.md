# Follow-up Tickets — Open Blockers Index

> Stato: [`CURRENT_STATUS.md`](CURRENT_STATUS.md) — dettaglio in `docs/tickets/`.

## Open Blockers (≤10)

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| Main truth / Stage 1-2 closeout | P0 | VERIFY | [MAIN-TRUTH-STAGE-1-2](tickets/TICKET-MAIN-TRUTH-STAGE-1-2.md) — frame-slot structural closeout + Vulkan pipeline-cache consistency + docs reconciliation landed; close only after same-SHA `Chronon CI` green and main-tip verification |
| Stage 1-5 stabilization campaign | P0 | VERIFY | [TICKET-200](tickets/TICKET-200-stage-plan.md) — Stabilize→Certify→Accelerate stages 1-5: build GREEN (756 targets), cache/compositor/render_job_contract/software suites PASS, arch gate 120/120 PASS (all 10 pre-existing fails cleared, see ticket); close after CI green on same SHA |
| Font asset demolition follow-through | P1 | OPEN | tests/text + render_graph suites fail on missing assets/fonts/* deleted in 3bed1ea0e — dependents never migrated |
| Engine certification aggregator | P0 | OPEN | [TICKET-125](tickets/TICKET-125-test-aggregator.md) — Test 13 = Camera brutal, distinto da Test 11; product validation in PipelineGen |
| Deprecated API | P1 | OPEN | [DEPRECATED-API-REMOVAL](tickets/TICKET-DEPRECATED-API-REMOVAL.md) |
| Test harness + WBH cert | P1 | OPEN | [TEST-FONT-ASSET-PATH](tickets/TICKET-TEST-FONT-ASSET-PATH.md) + [CERT-SEQUENCE-WBH](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md) |
| Benchmark + CPU budget | P1 | OPEN | [BENCHMARK-CORPUS-OFFICIAL](tickets/TICKET-BENCHMARK-CORPUS-OFFICIAL.md) + [P1E-CPU-BUDGET](tickets/TICKET-P1E-CPU-BUDGET-MEASUREMENT.md) |
| Tools / lint | P2 | OPEN | [TOOLS-ORPHAN-AUDIT](tickets/TICKET-TOOLS-ORPHAN-AUDIT.md) |
| OpenType features | P1 | OPEN | [OPENTYPE-FEATURES-PASS](tickets/TICKET-OPENTYPE-FEATURES-PASS.md) |
| CLI project UX | P2 | OPEN | [ADD-LOADER-FOR-CHRONON-JSON](tickets/TICKET-ADD-LOADER-FOR-CHRONON-JSON.md) |
| Node memory metrics | P1 | OPEN | [NODE-MEMORY-METRICS](tickets/TICKET-NODE-MEMORY-METRICS.md) |

## Demolition Debt (exit-condition driven)

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| FFmpeg subprocess video sink | P1 | OPEN | [FFMPEG-PIPE-SINK-DEMOLITION](tickets/TICKET-FFMPEG-PIPE-SINK-DEMOLITION.md) — delete solo dopo native release/SDK/codec/zero-caller certification; Wave 0 closeout classifies this debt but does not bypass its exit conditions |

## Backlog P2/P3 (non bloccante)

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| Video compiler arch | P2 | PLANNED | [VIDEO-COMPILER-ARCH-V1](tickets/TICKET-VIDEO-COMPILER-ARCH-V1.md) |
| Text clip warn/regression | P3 | OPEN | [TEXT-OVERSIZED-CLIP-WARN](tickets/TICKET-TEXT-OVERSIZED-CLIP-WARN.md) |

## Recently Closed (ultimi 3)

- `PERF-BASELINE-V1` — DONE; baseline software/CPU lockata e blur_band riabilitato in BENCH-3 → [ticket](tickets/TICKET-PERF-BASELINE-V1.md)
- `NODE-CACHE-KEY-COLLAPSE-ROT` — DONE 2026-09-04 after re-census: `run_node`, `emit_node_records` and `cache_evaluator` all carry the canonical `NodeCacheKey`; historical forward-points already landed → [ticket](tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md)
- IPC heap-buffer-overflow in `decode_reply` — fuzzer (libFuzzer) → fix (Verifier) in `eabb6713a` → [TICKET-130](tickets/TICKET-130-IPC-HEAP-OVERFLOW-DECODE-REPLY.md)

> Storico completo: `docs/tickets/archive/` (the historical `docs/CHANGELOG.md` was retired in commit 9758751f8).  <!-- drift-class: historical -->
