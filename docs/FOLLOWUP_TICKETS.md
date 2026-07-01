# Follow-up Tickets — Open Blockers Index

> Stato corrente: [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md).
> Dettaglio completo di ogni ticket: [`docs/tickets/TICKET-NNN.md`](docs/tickets/).
> Cronologia completa ticket chiusi + deferred: [`docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`](docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md).

## Open blockers (top 10 — prioritari per baseline verde)

| ID | Priorità | Area | Stato | Blocca | Scheda |
|---|---|---|---|---|---|
| TICKET-077 | P0 | gate-3 I2 (LOC) | 🟢 Done | check_software_renderer_boundary | header LOC 223→182, gate-3 I2 PASS |
| TICKET-079 | P0 | gate-3 I5 (API surface) | 🟢 Done | check_software_renderer_boundary | SWRenderer& → SWRenderer*, gate-3 I5 PASS |
| TICKET-036 | P0 | camera architecture gate | PLANNED | arch-boundary gate 5/6 | [TICKET-036.md](tickets/TICKET-036.md) |
| TICKET-011 | P0 | mainline build rot | PLANNED | arch-boundary gate 1–8 | [TICKET-011.md](tickets/TICKET-011.md) |
| TICKET-044 | P1 | selftest hardcoded paths | PLANNED | arch-boundary gate 5 | [TICKET-044.md](tickets/TICKET-044.md) |
| TICKET-046 | P1 | filename drift stale refs | PLANNED | arch-boundary gate 5 | [TICKET-046.md](tickets/TICKET-046.md) |
| TICKET-005 | P1 | post-cascade cleanup | PARTIAL | arch-completeness gate 5 | [TICKET-005.md](tickets/TICKET-005.md) |
| TICKET-022 | P1 | camera double look-at | PARTIAL | arch-boundary gate 5/6 | [TICKET-022.md](tickets/TICKET-022.md) |
| TICKET-064 | P1 | ExecutionScope error model | PARTIAL | arch-boundary gate 5 | [TICKET-064.md](tickets/TICKET-064.md) |
| TICKET-051 | P1 | per-preset visual diagnostic | PLANNED | A4.3 visual gate | [TICKET-051.md](tickets/TICKET-051.md) |

Ordinamento: priorità gate-impact desc (P0 > P1), poi per ID.

## Altri ticket aperti

Tutti i ticket aperti non nella top-10 (TICKET-024, TICKET-026, TICKET-066, TICKET-078-deferred, TICKET-081a–116) sono tracciati in [`docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`](docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md) e nella history git pre-Step-6. Le specifiche di accettazione complete sono nei file [`docs/tickets/`](docs/tickets/) individuali.

## Recently closed (atomic `main` commits, giugno–luglio 2026)

| ID | Area | Commit |
|---|---|---|
| TICKET-077 | gate-3 I2 software_renderer.hpp LOC 223→182 | Done |
| TICKET-079 | gate-3 I5 attach_software_backend SWRenderer& → SWRenderer* | Done |
| TICKET-078 | gate-3 I3 non-local includes 7→6 | Done |
| TICKET-087 | gate-3 I3 build-fix preflight_fonts | Done |
| TICKET-106 | gate-3 I5 path-list-parity regression test | Done |
| TICKET-080 | unicode extraction utf8_decoder + whitespace | Done |
| TICKET-086 | thread_local cache → static + mutex | Done |
| TICKET-088 | HarfBuzz buffer pool in FontEngine | Done |
| TICKET-090 | draw_text_run private stages refactor | Done |
| TICKET-090b | draw_text_run stage-marker comments | Done |
| TICKET-100 | eliminate legacy materialize_text_run_shape | Done |
| TICKET-101 | compile_text_layout (TextDocument, paragraph_index) | Done |
| TICKET-092 | per-paragraph error accumulator | Done |
| TICKET-103a | TextLayoutRequest direction/language/features | Done |
| TICKET-104 | PendingTextRun consumed real-decrement | Done |
| TICKET-105 | identity/preservation regression test suite | Done |
| TICKET-107 | per-category register helpers promoted | Done |
| TICKET-108a | aliases.hpp 7 typedefs adopted | Done |
| TICKET-109 | TextRunBuilderInspector + migrate 53 callsites | Done |
| TICKET-098 | text_preset_registry per-category helpers | Done |
| TICKET-099a | internal-only domain type aliases | Done |
| TICKET-077b | std::call_once elimination (6 files) | Done |
| TICKET-076 | wrap_push.sh auto-FF merge before gate | Done |
| TICKET-075 | GATE-MNT-01 strict-SHA → ancestor-relation | Done |
| TICKET-069 | FreeTypeFaceCache destruction contract | Done |
| TICKET-074 | remove dead glyph_selector_v2 pipeline | Done |
| TICKET-070 | SoftwareBackendServices validation | PARTIAL |
| TICKET-068 | crossfade+stroke regression test | Done |
| TICKET-067 | GATE-MNT-01 divergence fix | Done |

Cronologia completa: [`docs/CHANGELOG.md`](docs/CHANGELOG.md) e [`docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`](docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md).
