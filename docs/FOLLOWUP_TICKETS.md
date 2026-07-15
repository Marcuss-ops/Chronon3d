# Follow-up Tickets — Open Blockers Index

> Stato corrente: [`CURRENT_STATUS.md`](CURRENT_STATUS.md).
> Dettaglio completo: [`tickets/`](tickets/).
> Cronologia ticket chiusi: [`CHANGELOG.md`](CHANGELOG.md).
> Debito differito e cronologia estesa: [`ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`](ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md).

## Open Blockers (≤10)

| Epic / Area | Pri | Stato | Description | Links |
|---|---|---|---|---|
| P0 docs + test aggregator | P0 | OPEN | `TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX` + `TICKET-125-TEST-AGGREGATOR`: marker check extension e catalogo Tests 8-18. | [changelog](tickets/TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX.md) · [125](tickets/TICKET-125-test-aggregator.md) |
| Text API migration | P1 | OPEN | `TICKET-TEXT-SPEC-MIGRATION` + `TICKET-CENTERED-TEXT-MIGRATION` + `TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION` + `TICKET-TEXT-SPEC-FORWARDER-REMOVAL`: deprecazione e migrazione 100+ caller legacy a `TextDefinition` canonica. | [text-spec](tickets/TICKET-TEXT-SPEC-MIGRATION.md) · [centered](tickets/TICKET-CENTERED-TEXT-MIGRATION.md) · [bulk](tickets/TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md) · [forwarder](tickets/TICKET-TEXT-SPEC-FORWARDER-REMOVAL.md) |
| Text shape surface | P2 | TRACKED | `TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL` + `TICKET-PUB-DEPRECATE-REMOVAL` + `TICKET-CHECK-NO-DUAL-TEXT-API-REGEX-IMPROVEMENT`: deprecazione ctor 6-arg, rimozione V0.2 e tightening Gate #25. | [shape-surface](tickets/TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL.md) · [deprecate-removal](tickets/TICKET-PUB-DEPRECATE-REMOVAL.md) · [regex](tickets/TICKET-CHECK-NO-DUAL-TEXT-API-REGEX-IMPROVEMENT.md) |
| CLI project UX | P2 | OPEN | Restano soltanto `TICKET-ADD-LOADER-FOR-CHRONON-JSON` e `TICKET-PREVIEW-CELL-LABELS`. `render` unico, alias, starter, watch props e contact sheet sono chiusi. | [loader](tickets/TICKET-ADD-LOADER-FOR-CHRONON-JSON.md) · [labels](tickets/TICKET-PREVIEW-CELL-LABELS.md) |
| Test harness & certification | P1 | OPEN | `TICKET-TEST-FONT-ASSET-PATH` + `TICKET-COMPLETENESS-GATE-V2-FIX-FORWARD` + `TICKET-CERT-SEQUENCE-WBH-PROTOCOL`: font asset path, video gate v2, protocollo WBH per test first-principles. | [font](tickets/TICKET-TEST-FONT-ASSET-PATH.md) · [completeness](tickets/TICKET-COMPLETENESS-GATE-V2-FIX-FORWARD.md) · [cert-seq](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md) |
| Core systems / ADR gaps | P1 | OPEN | `TICKET-NODE-MEMORY-METRICS` + `TICKET-PERSISTENT-CACHE-ADR-GAP` + `TICKET-SIMD-REGISTRY-CANONICAL`: memory accounting, ADR cache persistent, registry SIMD. | [node-mem](tickets/TICKET-NODE-MEMORY-METRICS.md) · [cache-adr](tickets/TICKET-PERSISTENT-CACHE-ADR-GAP.md) · [simd](tickets/TICKET-SIMD-REGISTRY-CANONICAL.md) |
| Benchmark corpus | P1 | OPEN | `TICKET-BENCHMARK-CORPUS-OFFICIAL` + `TICKET-P1E-CPU-BUDGET-MEASUREMENT` + `TICKET-BENCH-CORPUS-TEST-DEFERRED`: 12 scene YAML B00-B11, misurazione CPU budget e test differito. | [corpus](tickets/TICKET-BENCHMARK-CORPUS-OFFICIAL.md) · [cpu-budget](tickets/TICKET-P1E-CPU-BUDGET-MEASUREMENT.md) · [bench-deferred](tickets/TICKET-BENCH-CORPUS-TEST-DEFERRED.md) |
| Cache rot | P2 | OPEN | `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT`: NodeCacheKey build rot. La chiusura del legacy executor è completata e protetta dal gate anti-regressione. | [node-cache](tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md) |
| Tools / lint debt | P2 | OPEN | `TICKET-TOOLS-ORPHAN-AUDIT`: audit script orfani e tightening regex Gate #25. | [tools-audit](tickets/TICKET-TOOLS-ORPHAN-AUDIT.md) |

---

Debiti differiti e cronologia dei ticket chiusi sono stati spostati in [`ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`](ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md) per rispettare il contratto documentale di ~10 righe sintetiche.
