# Follow-up Tickets — Open Blockers Index

> Stato: [`CURRENT_STATUS.md`](CURRENT_STATUS.md) — dettaglio in `docs/tickets/`.

## Open Blockers (≤10)

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| Engine certification aggregator | P0 | OPEN | [TICKET-125](tickets/TICKET-125-test-aggregator.md) — Test 13 = Camera brutal, distinto da Test 11; product validation in PipelineGen |
| Deprecated API | P1 | OPEN | [DEPRECATED-API-REMOVAL](tickets/TICKET-DEPRECATED-API-REMOVAL.md) |
| Test harness + WBH cert | P1 | OPEN | [TEST-FONT-ASSET-PATH](tickets/TICKET-TEST-FONT-ASSET-PATH.md) + [CERT-SEQUENCE-WBH](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md) |
| Benchmark + CPU budget | P1 | OPEN | [BENCHMARK-CORPUS-OFFICIAL](tickets/TICKET-BENCHMARK-CORPUS-OFFICIAL.md) + [P1E-CPU-BUDGET](tickets/TICKET-P1E-CPU-BUDGET-MEASUREMENT.md) |
| Perf baseline v1 + SW composite blur SEGV | P1 | DONE | [PERF-BASELINE-V1](tickets/TICKET-PERF-BASELINE-V1.md) — baseline lockata; blur_band riabilitato in BENCH-3 (SEGV non più riproducibile, §4a RESOLVED 2026-08-28) |
| Cache key rot | P2 | OPEN | [NODE-CACHE-KEY-COLLAPSE-ROT](tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md) |
| Tools / lint | P2 | OPEN | [TOOLS-ORPHAN-AUDIT](tickets/TICKET-TOOLS-ORPHAN-AUDIT.md) |
| OpenType features | P1 | OPEN | [OPENTYPE-FEATURES-PASS](tickets/TICKET-OPENTYPE-FEATURES-PASS.md) |
| CLI project UX | P2 | OPEN | [ADD-LOADER-FOR-CHRONON-JSON](tickets/TICKET-ADD-LOADER-FOR-CHRONON-JSON.md) |
| Node memory metrics | P1 | OPEN | [NODE-MEMORY-METRICS](tickets/TICKET-NODE-MEMORY-METRICS.md) |

## Demolition Debt (exit-condition driven)

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| FFmpeg subprocess video sink | P1 | OPEN | [FFMPEG-PIPE-SINK-DEMOLITION](tickets/TICKET-FFMPEG-PIPE-SINK-DEMOLITION.md) — delete solo dopo native release/SDK/codec/zero-caller certification |

## Backlog P2/P3 (non bloccante)

| Area | Pri | Stato | Scheda |
|---|---|---|---|
| Video compiler arch | P2 | PLANNED | [VIDEO-COMPILER-ARCH-V1](tickets/TICKET-VIDEO-COMPILER-ARCH-V1.md) |
| Text clip warn/regression | P3 | OPEN | [TEXT-OVERSIZED-CLIP-WARN](tickets/TICKET-TEXT-OVERSIZED-CLIP-WARN.md) |

## Recently Closed (ultimi 3)

- IPC heap-buffer-overflow in `decode_reply` — fuzzer (libFuzzer) → fix (Verifier) in `eabb6713a` → [TICKET-130](tickets/TICKET-130-IPC-HEAP-OVERFLOW-DECODE-REPLY.md)
- `has_compiled_recorder` + `fully_recorded` fast-path (TICKET-VIDEO-COMPILER-ARCH-V1 Fase D/E) — 2026-08-22
- Telemetry dead layer removed (TICKET-TELEMETRY-STORE-CONSUMER-AUDIT) — 2026-08-22
- `Sequential graph cache parity` DONE con `DIAGNOSTICS=OFF` (TICKET-SEQUENTIAL-CACHE-DIVERGENCE)

> Storico completo: `docs/tickets/archive/` (the historical `docs/CHANGELOG.md` was retired in commit 9758751f8).  <!-- drift-class: historical -->
