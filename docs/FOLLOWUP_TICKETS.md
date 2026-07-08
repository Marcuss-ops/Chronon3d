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
| TICKET-AE-CAM-MULTI-NODE-SWEEP | AE-parity multi-node sweep | DONE (2026-07-08) — Sub-cluster A NOT-NEEDED, Sub-cluster B resolved by hash-collision DONE |
| FIX-4-FULLSCREEN-RECT | fullscreen_rect canvas-correct in modular coordinates | DONE (this session) — `pin_to(Anchor::Center) + pos=(-w/2,-h/2,0)` + bbox regression test (`tests/scene/test_fullscreen_rect_modular_bbox.cpp`, 4 TEST_CASEs). Known limitation: macchina-verifica across modular_coordinates=true|false pipeline required (forward note in CHANGELOG). |
| TICKET-§11.1-TEST-SUITE-MIGRATION | close §11.1 / §12.1 test-suite migration backlog (20 raw → 0) | DONE (this commit) — 18 cmake files migrated to `chronon3d_add_test_suite()` (most-tagged TIER per the per-area convention), `tools/check_test_suite_registration.sh` promoted from informational exit-0 to bloccante exit-1 on raw residuo. Final audit: **41 SUITE / 0 RAW**. Build verification deferred to next session with working build host — no PASS fabricated. |
| TICKET-110 | wire hygiene gates into push + CI | DONE (this commit) — `tools/check_test_hygiene.sh` (gate #10b) + `tools/check_test_suite_registration.sh` (gate #10c) wired into `tools/wrap_push.sh` Step 4.5 (post-`check_main_clean.sh`, pre-`git push`) + parallel into `.github/workflows/ci.yml` (per-build) and `.github/workflows/gates-full-validation.yml` (pre-build, paths-scoped). `docs/AGENT_WORKFLOW.md` §6 documents the gate chain + CI parity matrix. NO `--skip-gates` escape hatch (violations are deterministic link-breakers). Both gates exit 1 on violation.
| TICKET-FRAME-VALUE-CONVENTION (1/2) | Frame::value grep-gate WARN mode | DONE (this commit) — `tools/check_frame_value_convention.sh` introduced with refined regex (Frame-typed identifiers + method-call exclusion); 20 real Frame::value hits inventoried across 14 files; 20 `AnimatedValue<T>::value` false-positives in `src/registry/text_preset_factories_{reveal,cinematic}.cpp` documented as expected noise (`.value` is the curve wrapper, `Frame{N}` is the keyframe timestamp arg); 7 `std::expected::value()` / `Result::value()` method-call false-positives excluded via post-filter `grep -vE '\.value\('`. Default mode = `WARN` (exit 0). Commit 2/2 ships the progressive fix + flips default to `FAIL` (exit 1) — gate chain `tools/wrap_push.sh origin main` already includes this script via Step 4.5 once commit 2 lands.
| TICKET-FRAME-VALUE-CONVENTION (2/2) | Frame::value grep-gate FAIL mode + 20-hit progressive fix | DONE (this commit) — All 20 real Frame::value hits fixed across 14 source files via accessor substitution (`frame.value` → `frame.integral()` per the canonical reading-convention table in `include/chronon3d/core/types/frame.hpp`). Gate `tools/check_frame_value_convention.sh` default mode flipped from `WARN` to `FAIL` (override via `FRAME_VALUE_GATE_MODE=WARN` env var). Count-bug fixed in gate (original `grep -c '^'` overcounted empty HITS by 1 due to trailing-newline match). Convention-table docblock in `frame.hpp` updated to remove the historic "core math / time-critical → `frame.value` is OK" concession (the accessor `integral()` is `noexcept` + constexpr, zero perf impact). Post-fix verification: `bash tools/check_frame_value_convention.sh` → `PASS — no out-of-header Frame::value usage` (exit 0), 0 violations. Forward-only: append this gate to `tools/wrap_push.sh` Step 4.5c in a separate commit (wire-up intentionally deferred to ensure clean tree pre-commit, never cause a self-fail). |
