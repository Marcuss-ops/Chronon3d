# Follow-up Tickets — Open Blockers Index

> Stato: [`CURRENT_STATUS.md`](CURRENT_STATUS.md) — dettagli e prove nelle schede collegate.

## Open blockers (max 10; one line per item)

| Area | Pri | Stato | Scheda |
|---|---:|---|---|
| Main truth / same-SHA CI | P0 | VERIFY — remote job allocation pending | [MAIN-TRUTH-STAGE-1-2](tickets/TICKET-MAIN-TRUTH-STAGE-1-2.md) |
| Stabilizzazione Stage 1–5 | P0 | VERIFY — close after same-SHA CI | [TICKET-200](tickets/TICKET-200-stage-plan.md) |
| Luma/chroma key native GPU | P1 | OPEN | [KEYING-GPU-NATIVE](tickets/TICKET-KEYING-GPU-NATIVE.md) |
| Echo native GPU dispatch | P1 | OPEN | [ECHO-GPU-NATIVE](tickets/TICKET-ECHO-GPU-NATIVE.md) |
| Engine certification aggregator | P0 | OPEN | [TICKET-125](tickets/TICKET-125-test-aggregator.md) |
| Test harness and WBH certification | P1 | OPEN | [TEST-FONT-ASSET-PATH](tickets/TICKET-TEST-FONT-ASSET-PATH.md) · [CERT-SEQUENCE-WBH](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md) |
| Vulkan shape-stroke parity | P1 | OPEN — pixel oracle pending | [ROADMAP-LOCAL-CLOSEOUT](tickets/TICKET-ROADMAP-LOCAL-CLOSEOUT-2026-09.md) |
| Daemon job lifecycle throughput | P1 | OPEN — allocator data gate | [DAEMON-JOB-LIFECYCLE](tickets/TICKET-DAEMON-JOB-LIFECYCLE.md) |
| Benchmark corpus and CPU budget | P1 | MEASURED — host certification pending | [BENCHMARK-CORPUS-OFFICIAL](tickets/TICKET-BENCHMARK-CORPUS-OFFICIAL.md) |
| Node memory metrics | P1 | OPEN — representative corpus pending | [NODE-MEMORY-METRICS](tickets/TICKET-NODE-MEMORY-METRICS.md) |

## Removed from this index

Resolved, landed, and deferred items stay in their owning tickets/evidence; this
index lists only active blockers. The unused spatial motion-path family was
removed under [SPATIAL-MOTION-PATH-REMOVAL](tickets/TICKET-SPATIAL-MOTION-PATH-REMOVAL.md).
