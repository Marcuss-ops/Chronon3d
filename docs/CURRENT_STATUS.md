# Chronon3D — Current Status

> **Snapshot:** `main@3fa5880f` — baseline verde certificata `main@7eb5c2ba` **11/11 PASS** ✅. Feature freeze V0.1 revocato. Linux-only. Gate #10 PASS: check_install [BOUNDARY-OK] + check_text [TEXT-OK] (anti-false-green 6100/230400 delta pixels). Both consumers use SceneBuilder public API.

## Active Blockers (top 3)

| ID | Area | Stato | Blocca | Scheda |
|---|---|---|---|---|
| TICKET-TEXT-CLIP-PREDICTED-BBOX | text compositor `predicted_bbox` (Clip 06 diagnostic) | PARTIAL | text golden cert + scene test cluster | [TICKET-TEXT-CLIP-PREDICTED-BBOX](tickets/TICKET-TEXT-CLIP-PREDICTED-BBOX.md) |
| TICKET-011 | mainline build rot (chronon3d_core_tests) | PARTIAL | gate 1–8 | [TICKET-011](tickets/TICKET-011.md) |
| TICKET-036 | SceneBuilder::animated_camera() in test files | PLANNED | gate 6 | [TICKET-036](tickets/TICKET-036.md) |
| TICKET-120 | 17/24 scene test failures | PARTIAL | Camera V1 cert | [TICKET-120](tickets/TICKET-120.md) |
| TICKET-GATE-10-PHASE-4-BLACK-FU5 | PNG mean-RGB metric rot | DONE | gate 10 | [FU5](tickets/TICKET-GATE-10-PHASE-4-BLACK-FU5.md) |

Per il dettaglio completo: [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) + [`docs/tickets/`](docs/tickets/).
Cronologia ticket chiusi: [`docs/CHANGELOG.md`](docs/CHANGELOG.md).

## Stato generale per area

| Area | Stato | Note sintetiche |
|---|---|---|
| Camera V1 | PASS | AE-parity 35/35 PASS; hash collision (AE_CAM_02/04) resolved via Fase 6 cache-key camera fingerprint. |
| Text Production V1 | PARTIAL | **Text Export V1 certificato** ✅: check_text pipeline passa ([TEXT-OK], 6100/230400 delta pixels, anti-false-green). FontEngine auto-wiring verificato end-to-end. Esempio standalone `examples/text_export_consumer/` aggiunto. Golden capture pipeline: 144 PNG tracked. M1.5#1–#13 refactor completati. Clip 06 diagnostic (TICKET-TEXT-CLIP-PREDICTED-BBOX, P0): bug in `predicted_bbox` / compositor. I 5 goldens Clip 01–05 vanno re-seeded (TICKET-TEXT-CLIP-GOLDENS-01-05, P1). |
| SDK C++ installabile | PASS | gate #10: sub-blocks A+B PASS, sub-block C (FU5 mean-RGB) DONE. |
| SDK cross-language | NOT RUN | C ABI e formato `.chronon` da progettare. |
| Render runtime | PASS | ImageCache + RenderSession::layout_cache landed. |
| Composition pipeline | PASS | Canonical pipeline documented; Sequence V2 + Asset Readiness code-complete. |
| Sistemi meta (Expressions V2 / V3) | PLANNED | Expressions V2 OFF di default; V3 subordinato a V1. |
| 10-point friction audit | DONE (2026-07-08) | Lineage officially closed. 7 atomic commits landed (range `0ff8b100`..`8c1e9ddc`) sweeping FIX 1–10 (Camera V1 + SDK C++ + Render runtime + Composition + Test). Forward-only honesty: pre-existing rot tickets tracked in §Active Blockers (`TICKET-011` + `TICKET-036` + `TICKET-120` + `TICKET-GATE-10-PHASE-4-BLACK-FU5`) NON chiusi da questo audit; rimangono nel §Active Blockers. Tickets tracked elsewhere come `TICKET-005` / `TICKET-011-i` / `TICKET-044` (FOLLOWUP_TICKETS §Open Blockers) sono anch'essi out-of-scope. Cross-link: [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) §Recently Closed `TICKET-10-POINT-FRICTION-AUDIT-CLOSURE` row + [`docs/ROADMAP.md`](docs/ROADMAP.md) closure-note above `## M0 — Baseline verificata` + [`docs/CHANGELOG.md`](docs/CHANGELOG.md) "10-point friction audit + fixes" entry. |

## Gate Audit — ultima verifica

**`main@7eb5c2ba` — 11/11 PASS** (2026-07-06, certificata). Feature freeze V0.1 revocato.
Baseline: [`docs/baselines/main-7eb5c2ba-baseline.md`](docs/baselines/main-7eb5c2ba-baseline.md).

**`main@54292ee5` — Fase 7 audit** (2026-07-08):

| # | Gate | Esito | Note |
|---|------|-------|------|
| 1 | `check_architecture_boundaries.sh` | ✅ PASS | |
| 2 | `check_architecture_boundaries_selftest.sh` | ✅ PASS | |
| 3 | `check_software_renderer_boundary.sh` | ✅ PASS | |
| 4 | `check_gitignored_dirs.sh` | ✅ PASS | |
| 5 | `audit_software_renderer.sh` | ✅ PASS | |
| 6 | `check_camera_architecture.sh` | ✅ PASS | |
| 7 | `check_doc_sync.sh` | ✅ PASS | Fixato heading `## Stato generale per area` in CURRENT_STATUS.md |
| 8 | `check_filename_drift.sh` | ⚠️ PASS* | warn-mode; 109 drift findings |
| 9 | `test_architectural.sh` | ✅ PASS | |
| 10 | `install_consumer_test.sh` | ✅ PASS | check_install [BOUNDARY-OK] PASS (SceneBuilder rewrite fixed black PNG). check_text [TEXT-OK] PASS (anti-false-green 6100/230400 delta pixels). Text Export V1 certified. |
| 11 | `check_backend_sanitization.py` | ✅ PASS | |

AE parity golden checker: GATE_PASS (23/23 fresh). Suite AE_CAM: 35/35 PASS, 142/142 assertions.

## Come leggere gli stati

| Stato | Significato |
|---|---|
| PASS | Implementazione verificata contro prova eseguibile osservata. |
| FAIL | Comportamento non corretto osservato. |
| PARTIAL | Implementazione presente ma con limiti o copertura incompleta. |
| NOT RUN | Gate / prova non ancora eseguita. |
| BLOCKED | Bloccato da altro ticket o condizione esterna. |
| PLANNED | Design presente, implementazione non iniziata. |

## Link canonici

- [`docs/ROADMAP.md`](docs/ROADMAP.md) — milestone prodotto
- [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md) — requisiti permanenti di release
- [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) — indice blocker aperti (canonical)
- [`docs/CHANGELOG.md`](docs/CHANGELOG.md) — chiusure recenti e cronologia
- [`docs/CAMERA_FEATURE_MATRIX.md`](docs/CAMERA_FEATURE_MATRIX.md) — feature camera
- [`docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`](docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md) — piano testo
- [`docs/DOCUMENTATION_GOVERNANCE.md`](docs/DOCUMENTATION_GOVERNANCE.md) — contratto documentale
- [`docs/baselines/main-7eb5c2ba-baseline.md`](docs/baselines/main-7eb5c2ba-baseline.md) — baseline verde certificata
- [`docs/ARCHIVE/CURRENT_STATUS_HISTORY.md`](docs/ARCHIVE/CURRENT_STATUS_HISTORY.md) — cronologia estesa (Phase A–H, gate audit pre-`7eb5c2ba`)
- [`docs/ARCHIVE/`](docs/ARCHIVE/) — materiale storico (non operativo)

## Hygiene

Main-sync hygiene enforced da GATE-MNT-01 ([`tools/wrap_push.sh`](../tools/wrap_push.sh) + [`tools/check_main_clean.sh`](../tools/check_main_clean.sh)). `branch.main.rebase = true`.

_Limite raccomandato: 150 righe (vedi `DOCUMENTATION_GOVERNANCE.md` DoD §10)._
