# Follow-up Tickets — Open Blockers Index

> Stato: [`CURRENT_STATUS.md`](CURRENT_STATUS.md) — dettagli e prove nelle schede collegate.

## Open blockers (max 10; one line per item)

| Area | Pri | Stato | Scheda |
|---|---:|---|---|
| Camera authority / cinematic composition | P0 | ACTIVE — in-workspace producer migration and SDK census are closed; end-to-end optical, temporal, and integrated T16–T18 certification remains | [CAMERA-AUTHORITY-MIGRATION](tickets/TICKET-CAMERA-AUTHORITY-MIGRATION.md) · [CINEMATIC-COMPOSITION-CERTIFICATION](tickets/TICKET-CINEMATIC-COMPOSITION-CERTIFICATION.md) |
| Daemon job lifecycle throughput | P1 | OPEN — one-shape overlap evidence recorded; 3-shape/9-run suite validates in dry-run; GPU execution and internal allocator data remain | [DAEMON-JOB-LIFECYCLE](tickets/TICKET-DAEMON-JOB-LIFECYCLE.md) |
| Benchmark corpus and CPU budget | P1 | MANIFEST VALIDATED (17 unique cases; local test PASS); content-pack/NVENC host certification pending | [BENCHMARK-CORPUS-OFFICIAL](tickets/TICKET-BENCHMARK-CORPUS-OFFICIAL.md) |
| Node memory metrics | P1 | OPEN — representative corpus pending | [NODE-MEMORY-METRICS](tickets/TICKET-NODE-MEMORY-METRICS.md) |

## Removed from this index

Resolved, landed, and deferred items stay in their owning tickets/evidence; this
index lists only active blockers. The unused spatial motion-path family removal
is documented in git history (ticket retired after its scope was removed).
