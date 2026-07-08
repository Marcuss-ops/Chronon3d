# Chronon3D — Current Status

> **Snapshot:** `main@3a6163c3` — baseline verde certificata `main@7eb5c2ba` **11/11 PASS** ✅. Feature freeze V0.1 revocato. Linux-only.

## Active Blockers (top 6)

| ID | Area | Stato | Blocca | Scheda |
|---|---|---|---|---|
| TICKET-011 | mainline build rot (chronon3d_core_tests) | PARTIAL | gate 1–8 | [TICKET-011](tickets/TICKET-011.md) |
| TICKET-036 | SceneBuilder::animated_camera() in test files | PLANNED | gate 6 | [TICKET-036](tickets/TICKET-036.md) |
| TICKET-120 | 17/24 scene test failures | PARTIAL | Camera V1 cert | [TICKET-120](tickets/TICKET-120.md) |
| TICKET-AE-CAM-MULTI-NODE-SWEEP | 3 unreached call sites + cache-key camera fp | PLANNED | gate 2 | [Scheda](tickets/TICKET-AE-CAM-MULTI-NODE-SWEEP.md) |
| TICKET-GATE-4-LEAK-CHANGELOG | abs-path leak in CHANGELOG.md | OPEN | gate 4 | — |
| TICKET-GATE-10-PHASE-4-BLACK-FU5 | PNG mean-RGB metric rot | OPEN | gate 10 | [FU5](tickets/TICKET-GATE-10-PHASE-4-BLACK-FU5.md) |

Per il dettaglio completo: [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) + [`docs/tickets/`](docs/tickets/).
Cronologia ticket chiusi: [`docs/CHANGELOG.md`](docs/CHANGELOG.md).

## Stato per area

| Area | Stato | Note sintetiche |
|---|---|---|
| Camera V1 | PASS | AE-parity 35/35 PASS; hash collision (AE_CAM_02/04) resolved via Fase 6 cache-key camera fingerprint. |
| Text Production V1 | PARTIAL | Golden capture pipeline funzionante; 144 PNG tracked. M1.5#1–#13 refactor completati. |
| SDK C++ installabile | PARTIAL | gate #10: sub-blocks A+B PASS, sub-block C (FU5 mean-RGB) OPEN. |
| SDK cross-language | NOT RUN | C ABI e formato `.chronon` da progettare. |
| Render runtime | PASS | ImageCache + RenderSession::layout_cache landed. |
| Composition pipeline | PASS | Canonical pipeline documented; Sequence V2 + Asset Readiness code-complete. |
| Sistemi meta (Expressions V2 / V3) | PLANNED | Expressions V2 OFF di default; V3 subordinato a V1. |

## Gate Audit — ultima baseline certificata

**`main@7eb5c2ba` — 11/11 PASS** (2026-07-06). Feature freeze V0.1 revocato.
Baseline: [`docs/baselines/main-7eb5c2ba-baseline.md`](docs/baselines/main-7eb5c2ba-baseline.md).

| # | Gate | Esito |
|---|------|-------|
| 1 | `check_architecture_boundaries.sh` | ✅ PASS |
| 2 | `check_architecture_boundaries_selftest.sh` | ✅ PASS |
| 3 | `check_software_renderer_boundary.sh` | ✅ PASS |
| 4 | `check_gitignored_dirs.sh` | ✅ PASS |
| 5 | `audit_software_renderer.sh` | ✅ PASS |
| 6 | `check_camera_architecture.sh` | ✅ PASS |
| 7 | `check_doc_sync.sh` | ✅ PASS |
| 8 | `check_filename_drift.sh` | ⚠️ PASS* |
| 9 | `test_architectural.sh` | ✅ PASS |
| 10 | `install_consumer_test.sh` | ✅ PASS |
| 11 | `check_backend_sanitization.py` | ✅ PASS |

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
