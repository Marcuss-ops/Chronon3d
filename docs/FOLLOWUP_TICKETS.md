# Follow-up Tickets — Open Blockers Index

> Stato corrente: [`CURRENT_STATUS.md`](CURRENT_STATUS.md).
> Questo file è un indice sintetico dei blocker attivi; il dettaglio vive nelle schede `tickets/`.

## Open Blockers (≤10)

| Epic / Area | Pri | Stato | Scheda |
|---|---:|---|---|
| Docs + test aggregator | P0 | OPEN | [CHANGELOG-UPSTREAM-MARKERS-FIX](tickets/TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX.md), [TICKET-125](tickets/TICKET-125-test-aggregator.md) |
| Deprecated API cleanup (remaining symbols) | P1 | OPEN | [DEPRECATED-API-REMOVAL](tickets/TICKET-DEPRECATED-API-REMOVAL.md), [PUB-DEPRECATE-REMOVAL](tickets/TICKET-PUB-DEPRECATE-REMOVAL.md) |
| Transition cleanup (source audit complete; ticket-home sync pending) | P1 | OPEN | [TRN-TRANSITION-CLEANUP](tickets/TICKET-TRN-TRANSITION-CLEANUP.md) |
| Test harness + certification | P1 | OPEN | [TEST-FONT-ASSET-PATH](tickets/TICKET-TEST-FONT-ASSET-PATH.md), [CERT-SEQUENCE-WBH](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md) |
| Core systems / ADR gaps | P1 | OPEN | [NODE-MEMORY-METRICS](tickets/TICKET-NODE-MEMORY-METRICS.md), [PERSISTENT-CACHE-ADR-GAP](tickets/TICKET-PERSISTENT-CACHE-ADR-GAP.md) (source audit complete; ticket-home sync pending) |
| Benchmark + CPU budget | P1 | OPEN | [BENCHMARK-CORPUS-OFFICIAL](tickets/TICKET-BENCHMARK-CORPUS-OFFICIAL.md), [P1E-CPU-BUDGET](tickets/TICKET-P1E-CPU-BUDGET-MEASUREMENT.md) |
| Cache key rot | P2 | OPEN | [NODE-CACHE-KEY-COLLAPSE-ROT](tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md) |
| Tools / lint debt | P2 | OPEN | [TOOLS-ORPHAN-AUDIT](tickets/TICKET-TOOLS-ORPHAN-AUDIT.md) |
| OpenType feature coverage | P1 | OPEN | [OPENTYPE-FEATURES-PASS](tickets/TICKET-OPENTYPE-FEATURES-PASS.md) |
| CLI project UX | P2 | OPEN | [ADD-LOADER-FOR-CHRONON-JSON](tickets/TICKET-ADD-LOADER-FOR-CHRONON-JSON.md) |

## Recently Closed

- Render layer timing removed from the global job in `ecf183f5`; typed plan decoding and pre-frame preparation landed in `27dc34d6` and `67f7f00b`.
- Canonical CLI plan execution and shared audio mux landed in `fee74549`; plan state no longer mutates the engine and ABI validation landed in `0ef3b517`.
- The direct base/animation CMake cycle was removed in `c61a1a3a`; the selectable non-modular path was retired in `6fc72940`.
- Legacy C API/context adapters were removed in `8f43d29d`; architecture failures became blocking in `20a102f3`.
- Transition cleanup (TRN-01–TRN-07) is complete in the current source/gate audit; the master tracker is a closure candidate pending ticket-home synchronization.
- The executor-side persistent-cache bridge and retired cache-mode API are absent from source; the ADR-gap ticket is a closure candidate as obsolete, while `PersistentFramebufferStore` remains active. Ticket-home synchronization is pending.
- CompositionDescriptor registration is canonical: the legacy string/factory overload and duplicate factory map are absent; remaining deprecation work stays open under the narrowed API-cleanup tickets.
