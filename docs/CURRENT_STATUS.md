# Chronon3D — Current Status

> **Snapshot:** `main@50e36a04` — baseline verde certificata `main@7eb5c2ba` **11/11 PASS** ✅. Feature freeze V0.1 revocato. Linux-only. Gate #10 PASS. CI status artifacts uploaded per run (90-day retention).

## Active Blockers (top 3)

| ID | Area | Stato | Blocca | Scheda |
|---|---|---|---|---|
| TICKET-011 | mainline build rot (chronon3d_core_tests) | PARTIAL | gate 1–8 | [TICKET-011](tickets/TICKET-011.md) |
| TICKET-036 | SceneBuilder::animated_camera() in test files | PLANNED | gate 6 | [TICKET-036](tickets/TICKET-036.md) |
| TICKET-120 | 17/24 scene test failures | PARTIAL | Camera V1 cert | [TICKET-120](tickets/TICKET-120.md) |
| TICKET-GATE-10-PHASE-4-BLACK-FU5 | PNG mean-RGB metric rot | DONE | gate 10 | [FU5](tickets/TICKET-GATE-10-PHASE-4-BLACK-FU5.md) |

> Note: `TICKET-TEXT-CLIP-PREDICTED-BBOX` (Clip 06 diagnostic) was active at HEAD~2; **closed in this session** via `fix(text): close TICKET-TEXT-CLIP-PREDICTED-BBOX — restore bbox on contract violation` (FU01 of `TICKET-TEXT-VISIBILITY-PIPELINE`). See `docs/CHANGELOG.md` entry below + `docs/FOLLOWUP_TICKETS.md` §Recently Closed row. Counter `text_bbox_contract_violations` atomically tracks pre-clip degenerate bboxes for `/api/runs` telemetry surface; regression locks deferred to FU03/FU06.

Per il dettaglio completo: [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) + [`docs/tickets/`](docs/tickets/).
Cronologia ticket chiusi: [`docs/CHANGELOG.md`](docs/CHANGELOG.md).

## Stato generale per area

| Area | Stato | Note sintetiche |
|---|---|---|
| Camera V1 | PASS | AE-parity 35/35 PASS; hash collision (AE_CAM_02/04) resolved via Fase 6 cache-key camera fingerprint. |
| Text Production V1 | PARTIAL | **Text Export V1 certificato** ✅: check_text pipeline passa ([TEXT-OK]). Clip 06 diagnostic **CHIUSA in this session** (TICKET-TEXT-CLIP-PREDICTED-BBOX, FU01 di TICKET-TEXT-VISIBILITY-PIPELINE). I 5 goldens Clip 01–05 vanno re-seeded (P1, TICKET-TEXT-CLIP-GOLDENS-01-05). |
| SDK C++ installabile | PASS | gate #10: sub-blocks A+B PASS, sub-block C (FU5 mean-RGB) DONE. |
| SDK cross-language | NOT RUN | C ABI e formato `.chronon` da progettare. |
| Render runtime | PASS | ImageCache + RenderSession::layout_cache landed. |
| Composition pipeline | PASS | Canonical pipeline documented; Sequence V2 + Asset Readiness code-complete. |
| Video pipeline | PASS | Structured error reporting (VideoSinkError enum, 13 codes). Memory budget: max dimension 16384, overflow guards. Atomic output (.partial + ffprobe). media_video_tests linker fixed (`chronon3d_pipeline` added to LINK_TARGETS). 98 video tests pass. |
| CI infrastructure | PASS | Sanitizers nightly/weekly (P2-A). Renderer-boundary gate blocking (P0-C). Test hygiene gate: 3 invariants (P2-E). CI status JSON artifact (P3-A). |
| Test coverage | PASS | Frame rate edge cases: 55 tests (P2-D). Determinism matrix: 16 tests, 5×5 matrix (P2-C). Test hygiene: 3 invariant checks. |
| Sistemi meta (Expressions V2 / V3) | PLANNED | Expressions V2 OFF di default; V3 subordinato a V1. |
| 10-point friction audit | DONE (2026-07-08) | Lineage closed. Pre-existing rot tickets tracked in §Active Blockers. |

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

## Text Simplicity Plan (M1.8 — PLANNED)

Piano dettagliato per raggiungere l'ergonomia di Remotion (17 commit in 5 fasi).
Documenti: [`docs/TEXT_SIMPLICITY_ACTION_PLAN.md`](docs/TEXT_SIMPLICITY_ACTION_PLAN.md) (piano operativo) + [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) §M1.8 (17 ticket PLANNED) + [`docs/ROADMAP.md`](docs/ROADMAP.md) §M1.8 (milestone).

---

## Hygiene

Main-sync hygiene enforced da GATE-MNT-01 ([`tools/wrap_push.sh`](../tools/wrap_push.sh) + [`tools/check_main_clean.sh`](../tools/check_main_clean.sh)). `branch.main.rebase = true`.

### docs cleanups recenti (2026-07-10)

- Commit [`bfb2ca4b`](https://github.com/.../bfb2ca4b) — `docs(hygiene): remove 7 obsolete+forbidden docs, sync drift whitelist`:
    - **7 deletions**: `docs/NEXT_STEPS.md` (VIETATO pattern per AGENTS.md §Pattern di filename vietati), `MAINTENANCE_INVENTORY.md`, `CERTIFICAZIONE_PRODOTTO.md`, `CODE_IMPROVEMENTS.md`, `CAMERA_REGIA_AE_PLAN.md`, `TEXT_SELECTOR_SINGLE_PIPELINE_PLAN.md`, `video_pipeline.md`. <!-- drift-allow: archived-doc-pattern -->
    - **3 whitelist removals** in `tools/check_filename_drift.sh` (entry per i 3 file rimossi).
    - **2 inline `drift-allow:` markers**: `AGENTS.md:58` (continuazione del doc-sync bullet) + `docs/baselines/main-9ef0fe33-dod-fail-matrix.md:115` (riga che cita NEXT_STEPS.md rimosso).
- Commit `8464c771` — `docs(changelog): record docs(hygiene) cleanup commit bfb2ca4b`: prepended CHANGELOG entry documenting bfb2ca4b (audit trail, mirrors 8464c771 pattern).
- Commit `2c5a6be` — `chore(cleanup): remove obsolete tools, postmortem, fix baseline governance`:
    - **4 deletions**: `tools/wp0_archive_audit.sh` (WP-0 era, 0 ref), `tools/audit_aggregate_archive.sh` (audit, "not part of mandatory gate pipeline"), `tools/chronon-watch.sh` (replaced by `chronon3d_watch.sh`), `docs/postmortems/pixel-ink-centering.md` (29-line postmortem, 0 ref). <!-- drift-allow: archived-doc-pattern -->
    - **1 baseline rename**: `main-HEAD-baseline.md` → `main-acf7d1de-baseline.md` (governance fix: HEAD → SHA specifico per DOCUMENTATION_GOVERNANCE.md §baselines format). <!-- drift-allow: archived-doc-pattern -->
    - **2 reference updates**: `tests/sdk/test_sdk_archive_manifest.cpp:211` (rimossa referenza al audit script rimosso) + `docs/adr/ADR-016-sequence-asset-canonical-contract.md:238` (aggiornata referenza al baseline rinominato).
    - **1 CHANGELOG entry** prependata per il cleanup parallelo.
- **Kept intentional**: `main-16319557f-baseline.md` (typo 'f' extra ma cited da altre 2 baseline immutabili); 16 altri "0-ref" tools (false-negative: molti CI-invoked o wrap_push gate helpers).

_Limite raccomandato: 150 righe (vedi `DOCUMENTATION_GOVERNANCE.md` DoD §10)._
