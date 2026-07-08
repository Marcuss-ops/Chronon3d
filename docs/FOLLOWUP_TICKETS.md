# Follow-up Tickets — Open Blockers Index

> Stato corrente: [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md).
> Dettaglio completo: [`docs/tickets/`](docs/tickets/).
> Cronologia ticket chiusi: [`docs/CHANGELOG.md`](docs/CHANGELOG.md).
> Cronologia estesa: [`docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`](docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md).

## Open Blockers (≤10)

Solo ticket realmente aperti (PLANNED / PARTIAL / OPEN).

| ID | Pri | Area | Stato | Blocca |
|---|---|---|---|---|
| TICKET-011 | P0 | mainline build rot (chronon3d_core_tests) | PARTIAL | gate 1–8 |
| TICKET-036 | P0 | camera architecture gate G6 | PLANNED | gate 6 |
| TICKET-120 | P1 | 17/24 scene test failures | PARTIAL | Camera V1 cert |
| TICKET-AE-CAM-MULTI-NODE-SWEEP | P1 | 3 unreached call sites + cache-key camera fp | PLANNED | gate 2 |
| TICKET-GATE-4-LEAK-CHANGELOG | P0 | abs-path leak in CHANGELOG.md | OPEN | gate 4 |
| TICKET-GATE-10-PHASE-4-BLACK-FU5 | P0 | PNG mean-RGB metric rot | OPEN | gate 10 |
| TICKET-005 | P1 | post-cascade cleanup | PARTIAL | gate 5 |
| TICKET-011-i | P0 | text_unit_map impl drift | PLANNED | gate 1–8 |
| TICKET-044 | P1 | selftest hardcoded paths | PLANNED | gate 5 |

---

## Agent3 — Compiler, Session (PLANNED)

| DoD gate | Ticket | Stato |
|---|---|---|
| (a) Metadata | TICKET-A3-METADATA | PARTIAL (1/8) |
| (b) DampedFollow | TICKET-A3-DAMPED-HISTORY | PLANNED |
| (c) Session policy | TICKET-A3-SESSION-POLICY | PLANNED |
| (d) Cache lease | TICKET-A3-CACHE-LEASE | DONE |
| (e) CTX framerate | TICKET-A3-CTX-FRAMERATE | PLANNED |
| (f) Pre-roll FPS | TICKET-A3-PRE-ROLL-FPS | PLANNED |
| (g) LookAt diagnostic | TICKET-A3-LOOKAT-DIAGNOSTIC | PLANNED |
| (h) ADR-013 | TICKET-A3-ADR-013 | PLANNED |

## M1.7 Sequence + Asset Readiness

| ID | Pri | Stato |
|---|---|---|
| [TICKET-SEQUENCE-LOCAL-FRAME](tickets/TICKET-SEQUENCE-LOCAL-FRAME.md) | P1 | PARTIAL (Steps 1-7 + Migration code-complete) |
| [TICKET-ASSET-READINESS](tickets/TICKET-ASSET-READINESS.md) | P1 | PARTIAL (Steps 1-7 + Migration code-complete) |

## Recently Closed

Per la lista completa: [`docs/CHANGELOG.md`](docs/CHANGELOG.md) + [`docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`](docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md).

| ID | Area | Note |
|---|---|---|
| TICKET-ae-cam-hash-collision | AE-parity hash collision (Fase 6) | DONE (2026-07-08) — 35/35 PASS |
| TICKET-P1-09 | RenderRuntime service locator | DONE (Fase 5) |
| TICKET-CONTENT-PCH-ROT | content build pipeline | DONE (2026-07-07) |
| TICKET-M1.5#10-SEQUENCE | rich_text.hpp legacy deletion | DONE (4/4 steps) |
| TICKET-MOTION-BLUR-TEXT | motion-blur text goldens | DONE (2026-07-07) |
| TICKET-AE-PARITY-DRIVER | referee pipeline | DONE (2026-07-07) |
| TICKET-GOLDEN-CAPTURE | capture pipeline verified | DONE (2026-07-06) |
