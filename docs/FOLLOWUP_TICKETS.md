# Follow-up Tickets — Open Blockers Index

> Stato corrente: [`CURRENT_STATUS.md`](CURRENT_STATUS.md).
> Questo file è un indice sintetico dei blocker attivi; il dettaglio vive nelle schede `tickets/`.

## Open Blockers (≤10)

| Epic / Area | Pri | Stato | Scheda |
|---|---:|---|---|
| Test aggregator | P0 | OPEN | [TICKET-125](tickets/TICKET-125-test-aggregator.md) |
| Deprecated API cleanup (remaining symbols) | P1 | OPEN | [DEPRECATED-API-REMOVAL](tickets/TICKET-DEPRECATED-API-REMOVAL.md) |
| Test harness + certification | P1 | OPEN | Golden visuali focalizzati e camera visuale PASS su `main@4dabf6e3`; restano baseline globale, font e certificazione WBH: [TEST-FONT-ASSET-PATH](tickets/TICKET-TEST-FONT-ASSET-PATH.md), [CERT-SEQUENCE-WBH](tickets/TICKET-CERT-SEQUENCE-WBH-PROTOCOL.md) |
| Core systems / ADR gaps | P1 | OPEN | [NODE-MEMORY-METRICS](tickets/TICKET-NODE-MEMORY-METRICS.md) |
| Benchmark + CPU budget | P1 | OPEN | [BENCHMARK-CORPUS-OFFICIAL](tickets/TICKET-BENCHMARK-CORPUS-OFFICIAL.md), [P1E-CPU-BUDGET](tickets/TICKET-P1E-CPU-BUDGET-MEASUREMENT.md) |
| Cache key rot | P2 | OPEN | [NODE-CACHE-KEY-COLLAPSE-ROT](tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md) |
| Sequential graph cache parity | P1 | DONE (verified path) | `CHRONON3D_BUILD_DIAGNOSTICS=OFF` + `settings.diagnostics.enabled=false`: verifier PASS su `main@377995d4` (3/3 deterministic + 14/14 scene, marker `CHRONON_SEQUENTIAL_GRAPH_CACHE_PASS`); `CHRONON3D_ENABLE_DIAGNOSTICS=ON` resta fuori dalla conclusione: [SEQUENTIAL-CACHE-DIVERGENCE](tickets/TICKET-SEQUENTIAL-CACHE-DIVERGENCE.md) |
| Tools / lint debt | P2 | OPEN | [TOOLS-ORPHAN-AUDIT](tickets/TICKET-TOOLS-ORPHAN-AUDIT.md); [ANALYZE-FRAMES-ORPHAN](tickets/TICKET-TOOLS-ANALYZE-FRAMES-ORPHAN-V1.md) DONE; remaining watch-list open |
| OpenType feature coverage | P1 | OPEN | [OPENTYPE-FEATURES-PASS](tickets/TICKET-OPENTYPE-FEATURES-PASS.md) |
| CLI project UX | P2 | OPEN | [ADD-LOADER-FOR-CHRONON-JSON](tickets/TICKET-ADD-LOADER-FOR-CHRONON-JSON.md) |

## Recently Closed

- Render layer timing removed from the global job in `ecf183f5`; typed plan decoding and pre-frame preparation landed in `27dc34d6` and `67f7f00b`.
- Canonical CLI plan execution and shared audio mux landed in `fee74549`; plan state no longer mutates the engine and ABI validation landed in `0ef3b517`.
- The direct base/animation CMake cycle was removed in `c61a1a3a`; the selectable non-modular path was retired in `6fc72940`.
- Legacy C API/context adapters were removed in `8f43d29d`; architecture failures became blocking in `20a102f3`.
- Transition cleanup master tracker closed as DONE/ARCHIVED on 2026-08-01; TRN-03–TRN-07 remain the detailed evidence sheets.
- The executor-side persistent-cache bridge and retired cache-mode API are absent from source; `TICKET-PERSISTENT-CACHE-ADR-GAP` is DONE/ARCHIVED, while `PersistentFramebufferStore` remains active.
- `ShapedGlyphLine` legacy constructor surface is absent; `TICKET-PUB-DEPRECATE-REMOVAL` and `TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL` are DONE/ARCHIVED. Remaining deprecations are tracked only by `TICKET-DEPRECATED-API-REMOVAL`.
- CompositionDescriptor registration is canonical: the legacy string/factory overload and duplicate factory map are absent; remaining deprecation work stays open under the narrowed API-cleanup tickets.
- Markdown conflict-marker gate now scans all `docs/**/*.md` and closes `TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX` (`bf413d58`).
- Sequential graph cache parity is DONE after diagnostics-OFF verification on `main@377995d4`; details in [TICKET-SEQUENTIAL-CACHE-DIVERGENCE](tickets/TICKET-SEQUENTIAL-CACHE-DIVERGENCE.md).
