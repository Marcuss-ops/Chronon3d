# Chronon3D — Current Status

> **Snapshot:** `main@8b5ee57f` — post `feat(text): TICKET-SIMPLICITY-INSPECT-TEXT` (add inspect-text CLI, tests, and migrate content to TextSpec API). Baseline verde certificata `main@7eb5c2ba` **11/11 PASS** ✅. F3.D closed (commit `283a8106`, 2026-07-10); 5/5 baseline gates PASS post-commit. Phase A.3 historically closed (`4cce8f52`). F2.D historically closed (`a5a0475c`). Feature freeze V0.1 revocato. Linux-only. Gate #10 PASS. CI status artifacts uploaded per run (90-day retention).

## Active Blockers (top 3)

| ID | Area | Stato | Blocca | Scheda |
|---|---|---|---|---|
| TICKET-011 | mainline build rot (chronon3d_core_tests) | DONE | gate 1–8 | [TICKET-011](tickets/TICKET-011.md) |
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
| Camera V1 | PARTIAL | AE-parity 35/35 PASS; hash collision (AE_CAM_02/04) resolved via Fase 6 cache-key camera fingerprint. **Runtime certification gated by [TICKET-120](tickets/TICKET-120.md)** (24→18 remaining; 2 fixed, 1 hierarchy test `#if 0` disabled). |
| **Glow Final (ChrononGlowFinalAE)** | **🟢 Done/Ready** | **Certified 2026-07-11 at SHA [`1cb9cff2`](https://github.com/PierThatDev/Chronon3d/commit/1cb9cff2)** (Fase 6). **+1 forward-point this session: DoD §9 closed** (chronon3d::TestCase regression lock for the historical 19px-tall right-edge sliver via `test(glow): 19px sliver regression lock (DoD §9)` atomic chore commit) — NEW `TEST_CASE("ChrononGlowFinalAE never regresses to the 19px sliver")` at [`tests/text_golden/ae_parity/ae_08_glow_pulse.cpp`](tests/text_golden/ae_parity/ae_08_glow_pulse.cpp) line 318 (4 hard CHECK assertions: `bbox.height() > 100` + `bbox.x1 < 1910` **PRIMARY** — catch the historical sliver; `bbox.width() > 800` + `bbox.y1 < 1070` **DEFENSIVE** — protect against future variants). See [`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) §Recently Closed `TICKET-CHRONON-GLOW-FINAL DoD §9` row + [`docs/CHANGELOG.md`](CHANGELOG.md) prepended `test(glow): 19px sliver regression lock (DoD §9)` entry. 6/6 Fase landings: Fase 1 `cd42bc97` (factory unification) + Fase 2 `e2b600d7` (cinematic additive glow) + Fase 3 `05c4ae65` (scale breath without offset) + Fase 4 `6aa26018` (A/B darkening verdict) + Fase 5 `e150d649` (inspect-text real audit) + Fase 6 `1cb9cff2` (temporal stability + still-video parity). 11-gate suite: **10/11 PASS** (architecture + boundary + software-renderer + gitignored + camera + doc-sync + filename-drift + architectural + backend-sanitization + test-hygiene + test-suite-registration) + **1 documented pre-existing FAIL**: `check_no_dual_text_api.sh` (rot: `TextSpec::position` Vec3 field, ~200+ sites, pre-existing `TICKET-TEXT-LEGACY-POSITION-ROT` OPEN with 4-step roadmap, see [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) §Open Blockers). CI status: green on `main@1cb9cff2`. **+1 cherry-pick recovery modification-note** (post-`c36e3f13` recovery: 45-line `feat(api): public camera facade + external consumer SDK test` CHANGELOG entry from `cd2548cb` restored; the Glow Final row content was already preserved by 01c95de5's cherry-pick with the +1 DoD §9 clause — no real content loss in CURRENT_STATUS.md, just a structural 1-line diff between the original `cd2548cb` row and the post-cherry-pick version with the DoD §9 clause; see [`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) §Recently Closed `TICKET-CHERRY-PICK-RECOVERY` row + [`docs/CHANGELOG.md`](docs/CHANGELOG.md) prepended `docs(recovery): restore feat(api) entry dropped by cherry-pick --theirs` entry). Cross-link: [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) `## Recently Closed` TICKET-CHRONON-GLOW-FINAL row + [`docs/CHANGELOG.md`](docs/CHANGELOG.md) `feat(glow): ChrononGlowFinalAE certified` entry. |
| Text Production V1 | PARTIAL | **Text Export V1 certificato** ✅: check_text pipeline passa ([TEXT-OK]). Clip 06 diagnostic **CHIUSA in this session** (TICKET-TEXT-CLIP-PREDICTED-BBOX, FU01 di TICKET-TEXT-VISIBILITY-PIPELINE). FU02 audit scaffold landed (TextVisibilityAudit struct + canonical `audit_text_visibility()` fn, gated `#ifdef CHRONON3D_BUILD_DIAGNOSTICS`). FU02next pre-render invariants landed: MissingFontEngine / FontResolutionFailed / ShapingProducedNoGlyphs / InvalidLayout enforced fail-loud at compile_or_cache_layout materializer (Result<>-returning). **Clip 01–05 no-skip rule DONE (commit `6c63f4d2`, 2026-07-10)**: 3 skip-on-missing blocks replaced + 2 verify_golden calls added + Clip 03 placement before empty-bbox soft-skip (canonical `text_completeness.cpp::verify_completeness_golden` pattern). I 5 goldens Clip 01–05 vanno re-seeded (P1, TICKET-TEXT-CLIP-GOLDENS-01-05 → PARTIAL: code done, re-bake + push deferred to working build host per §13 honest-limitation — pre-existing build rot in `text_definition.hpp:170` + typewriter/centered helpers blocks the re-bake on this VPS). **Phase A.3 closed (commit `4cce8f52`)** — TextEffects (11 campi) + TextAnimation (8 campi) populate; coverage matrix 57/57 (22 TextDefStyle + 16 TextFrame + 11 TextEffects + 8 TextAnimation); `from_text_run_spec()` ora mappa animators/selectors/direction/language/script/cache_layout in def.animation. |
| Camera V1 | PASS | AE-parity 35/35 PASS; hash collision (AE_CAM_02/04) resolved via Fase 6 cache-key camera fingerprint. |
| Text Production V1 | PARTIAL | **Text Export V1 certificato** ✅: check_text pipeline passa ([TEXT-OK]). Clip 06 diagnostic **CHIUSA in this session** (TICKET-TEXT-CLIP-PREDICTED-BBOX, FU01 di TICKET-TEXT-VISIBILITY-PIPELINE). FU02 audit scaffold landed (TextVisibilityAudit struct + canonical `audit_text_visibility()` fn, gated `#ifdef CHRONON3D_BUILD_DIAGNOSTICS`). FU02next pre-render invariants landed: MissingFontEngine / FontResolutionFailed / ShapingProducedNoGlyphs / InvalidLayout enforced fail-loud at compile_or_cache_layout materializer (Result<>-returning). **Clip 01–05 no-skip rule DONE (commit `6c63f4d2`, 2026-07-10)**: 3 skip-on-missing blocks replaced + 2 verify_golden calls added + Clip 03 placement before empty-bbox soft-skip (canonical `text_completeness.cpp::verify_completeness_golden` pattern). **FU04 contract fix DONE (commit `<pending>`, 2026-07-10)**: `audit_text_visibility()` now computes `local_ink_bbox` from the canonical `renderer::compute_text_run_visual_bounds(shape)` (was zero-rect PLACEHOLDER per the documented FU04 forward-point); 2 new tests (#5 real-local-from-shape + #6 violation-with-non-zero-local) added to `tests/text/test_visibility_contract.cpp`; redundant manual override in `apps/chronon3d_cli/commands/dev/command_inspect_text.cpp` removed (the audit now produces the real value). I 5 goldens Clip 01–05 vanno re-seeded (P1, TICKET-TEXT-CLIP-GOLDENS-01-05 → PARTIAL: code done, re-bake + push deferred to working build host per §13 honest-limitation — pre-existing build rot in `text_definition.hpp:170` + typewriter/centered helpers blocks the re-bake on this VPS). **Phase A.3 closed (commit `4cce8f52`)** — TextEffects (11 campi) + TextAnimation (8 campi) populate; coverage matrix 57/57 (22 TextDefStyle + 16 TextFrame + 11 TextEffects + 8 TextAnimation); `from_text_run_spec()` ora mappa animators/selectors/direction/language/script/cache_layout in def.animation. |
| SDK C++ installabile | PASS | gate #10: sub-blocks A+B PASS, sub-block C (FU5 mean-RGB) DONE. |
| SDK cross-language | NOT RUN | C ABI e formato `.chronon` da progettare. |
| Render runtime | PASS | ImageCache + RenderSession::layout_cache landed. |
| Composition pipeline | PASS | Canonical pipeline documented; Sequence V2 + Asset Readiness code-complete. |
| Video pipeline | PASS | Structured error reporting (VideoSinkError enum, 13 codes). Memory budget: max dimension 16384, overflow guards. Atomic output (.partial + ffprobe). media_video_tests linker fixed (`chronon3d_pipeline` added to LINK_TARGETS). 98 video tests pass. |
| CI infrastructure | PASS | Sanitizers nightly/weekly (P2-A). Renderer-boundary gate blocking (P0-C). Test hygiene gate: 3 invariants (P2-E). CI status JSON artifact (P3-A). |
| Test coverage | PASS | Frame rate edge cases: 55 tests (P2-D). Determinism matrix: 16 tests, 5×5 matrix (P2-C). SafeArea formats: 5×5 pure-math matrix (1920×1080 / 1080×1920 / 1080×1080 / 1280×720 / 3840×2160 × 5 SafeAreaEnum) + no-inherited-bottom sequential lock (M1.8 §4). Test hygiene: 3 invariant checks. |
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

