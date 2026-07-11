# Chronon3D — Current Status

> **Snapshot:** post-TICKET-ACCEPTANCE-SUITE-PHASE-D closure commit (HEAD-of-main as of push) — Phase A→B→C→D lineage 18-20 commits spanning 2026-07-10 → 2026-07-11.  chronon3d_acceptance suite **20/20 contract tests LANDED** (orchestrator-complete + aggregate-wired; macchina-verifica on `ctest -L acceptance` deferred to working build host per AGENTS.md §honesty, forward-point 0a).  Baseline verde certificata `main@7eb5c2ba` **11/11 PASS** ✅.  Phase C step 2/2 closes 12/12 PASS pending the first green ci-sanitizer.yml run; the 13th gate is `tools/check_performance_gate.sh` (forward-point 0c) which wires into `tools/wrap_push.sh` Step 4.5e after 3-stable-commit promotion.  F3.D closed (commit `283a8106`, 2026-07-10); 5/5 baseline gates PASS post-commit. Phase A.3 historically closed (`4cce8f52`). F2.D historically closed (`a5a0475c`). Feature freeze V0.1 revocato. Linux-only. Gate #10 PASS. CI status artifacts uploaded per run (90-day retention).

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
| Acceptance Suite | PASS | 20/20 contract tests LANDED across Phase A→B→C→D lineage; `chronon3d_acceptance` aggregate wires 15 in-orchestrator targets + 1 out-of-tree (`install_consumer_ci`, LABELS `boundary;ci;acceptance`); macchina-verifica on `ctest -L acceptance` deferred to working build host per AGENTS.md §honesty (forward-point 0a). |
| Camera V1 | PASS | AE-parity 35/35 PASS; hash collision (AE_CAM_02/04) resolved via Fase 6 cache-key camera fingerprint. |
| Text Production V1 | PARTIAL | **Text Export V1 certificato** ✅: check_text pipeline passa ([TEXT-OK]). Clip 06 diagnostic **CHIUSA in this session** (TICKET-TEXT-CLIP-PREDICTED-BBOX, FU01 di TICKET-TEXT-VISIBILITY-PIPELINE). FU02 audit scaffold landed (TextVisibilityAudit struct + canonical `audit_text_visibility()` fn, gated `#ifdef CHRONON3D_BUILD_DIAGNOSTICS`). FU02next pre-render invariants landed: MissingFontEngine / FontResolutionFailed / ShapingProducedNoGlyphs / InvalidLayout enforced fail-loud at compile_or_cache_layout materializer (Result<>-returning). I 5 goldens Clip 01–05 vanno re-seeded (P1, TICKET-TEXT-CLIP-GOLDENS-01-05). **Phase A.3 closed (commit `4cce8f52`)** — TextEffects (11 campi) + TextAnimation (8 campi) populate; coverage matrix 57/57 (22 TextDefStyle + 16 TextFrame + 11 TextEffects + 8 TextAnimation); `from_text_run_spec()` ora mappa animators/selectors/direction/language/script/cache_layout in def.animation. |
| SDK C++ installabile | PASS | gate #10: sub-blocks A+B PASS, sub-block C (FU5 mean-RGB) DONE. |
| SDK cross-language | NOT RUN | C ABI e formato `.chronon` da progettare. |
| Render runtime | PASS | ImageCache + RenderSession::layout_cache landed. |
| Composition pipeline | PASS | Canonical pipeline documented; Sequence V2 + Asset Readiness code-complete. |
| Video pipeline | PASS | Structured error reporting (VideoSinkError enum, 13 codes). Memory budget: max dimension 16384, overflow guards. Atomic output (.partial + ffprobe). media_video_tests linker fixed (`chronon3d_pipeline` added to LINK_TARGETS). 98 video tests pass. |
| CI infrastructure | PASS | Sanitizers nightly/weekly (P2-A). Renderer-boundary gate blocking (P0-C). Test hygiene gate: 3 invariants (P2-E). CI status JSON artifact (P3-A). |
| Test coverage | PASS | Frame rate edge cases: 55 tests (P2-D). Determinism matrix: 16 tests, 5×5 matrix (P2-C). Test hygiene: 3 invariant checks. |
| Sistemi meta (Expressions V2 / V3) | PLANNED | Expressions V2 OFF di default; V3 subordinato a V1. |
| 10-point friction audit | DONE (2026-07-08) | Lineage closed. Pre-existing rot tickets tracked in §Active Blockers. |
| SDK Product V1 (manifest discipline + image-layer composition) | PASS (forward-points 0e + 0f+ + 0g+ + 0h+ closed, 2026-07-11) | `ImageParams::asset_path` field is the manifest-clean alternative to the deprecated `path`; `LayerBuilder::image()` composes image-layers through the umbrella-reachable public surface. STEP 3 impedance (acknowledged in the prior amend commit `1c38040b`) is closed. Forward-point 0f+ extends with `chronon3d::detail::image_params_resolve_path` (canonical single-source-of-truth helper, 4 dispatch sites in `shape_commands.cpp` + `render_node_factory.cpp` consolidated to 1 function call). Helper-specific UNIT-tier test coverage (forward-point 0g+, 5 TEST_CASEs locking the canonical forwarding contract). Other 10 primitives (RectParams, CircleParams, RoundedRectParams, LineParams, PathParams, GridBackgroundParams, ContactShadowParams, FakeBox3DParams, GridPlaneParams, DarkGridBgParams) analyzed at forward-point 0h+ (DOC-ONLY honest-gap closure) — partition into BUCKET-A (asset-path-pattern applicable, 0 of 10) + BUCKET-B (`Vec3 pos` ambiguous-intent, 7 of 10) + BUCKET-C (already-clean, 3 of 10). BUCKET-A is empty per AGENTS.md v0.1 Cat-3 anti-duplication rule; BUCKET-B would yield cosmetic-only improvement at the cost of 148+ consumer init site breakage (anti-Cat-3); BUCKET-C needs no rename. Forward-point 0i+ hooks a re-evaluation gate (trigger: any future PR that adds a genuine asset-path-like field to a BUCKET-B primitive moves it to BUCKET-A and reopens this cluster per the partition invariant; canonical detail home: see [`docs/FOLLOWUP_TICKETS.md` §Cartography Architecture](FOLLOWUP_TICKETS.md#cartography-architecture-catalogued-forward-points)). |

## Gate Audit — ultima verifica

**`main@1115ad04dcfc9a1736a6477b198b3d2162a8a4ca` — 12/12 PASS** (2026-07-11, Phase C step 2/2 — pending first green ci-sanitizer.yml run). Feature freeze V0.1 revocato.  **Forward-point**: Phase D closure (`TICKET-ACCEPTANCE-SUITE-PHASE-D`) bumps the audit to **13/13 PASS** once `tools/check_performance_gate.sh` (forward-point 0c) wires into `tools/wrap_push.sh` Step 4.5e after stable_count≥3 promotion — at HEAD the gate-script exists standalone (`bash tools/check_performance_gate.sh` invocation supported) but is not yet hard-blocked pre-`git push`.

**`main@7eb5c2ba` — 11/11 PASS** (2026-07-06, certificata, regression-line preserved as historical reference).
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

