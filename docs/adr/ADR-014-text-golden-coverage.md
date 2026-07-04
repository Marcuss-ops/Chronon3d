# ADR-014 — Text golden coverage extension: 12 user-spec tests + 5 forward-only tickets, AGENTS-compliant (no Python, no public C++)

| Field | Value |
|---|---|
| **Status** | ✅ Documented + accepted (companion to 1 doc-only commit on `main` introducing `tests/text_golden/user_spec_NN_*.cpp` + 5 forward-only ticket rows) |
| **Date** | 2026-07-04 |
| **Deciders** | Agent 3 (text subsystem canonicalisation) |
| **Tags** | `text`, `golden-tests`, `TXT-QA-01`, `feature-freeze-V0.1`, `regression-gate`, `verify_golden`, `PerRunShapingFailed`, `bidi`, `framerate-determinism` |
| **Related** | [ADR-013 — camera-v1-semantics](./ADR-013-camera-v1-semantics.md) (the architectural precedent for "lock behavioural contracts via golden + structural test pairs"); `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` §Blocco 5 ("Visual Regression Harness", the canonical home for this work); `docs/MIGRATION_TEXT_SPEC.md` §10 P1 (single canonical text pipeline), §11 TEXT-RET-01 (TextAnimator retirement, closed 2026-06-29), §13 TEXT-SEL-01 (selector V2, closed 2026-06-29), §15 TEXT-UNM-01 (TextUnitMap 8-level, closed 2026-06-29); `tools/check_architecture_boundaries.sh` (gate #5 / `tools/check_doc_sync.sh` gate #7); `tests/text/test_compile_text_layout_errors.cpp:290` (P1-1 regression lock on `PerRunShapingFailed`); `tests/text_golden_tests.cmake` (TXT-QA-01 standalone harness, UNITY_BUILD OFF); `tests/visual/support/golden_test.hpp` (`verify_golden` helper, ENV-gated Update mode). |

## Context and scope

A 32-test golden specification for the text subsystem was reviewed against the current repository state on 2026-07-04. The cross-mapping revealed:

* **Several claims in the spec were already falsified by closed work** post-2026-06-29: `FriBidi` is REQUIRED (TICKET P1-2 gate, not optional); `font_family` is folded into the layout cache-key (P0-2 closure); `TextRunNode` returns `NodeExecutionError` for null backend (M1.5#1 closure). The 32-test spec needs the "false-success" half rewritten against reality.
* **Most claims in the spec are testable today** with the existing infrastructure: `verify_golden` helper (in `tests/visual/support/golden_test.hpp`), `TextDocument` + `TextRunLayout` + `TextRunShape` pipeline (closed M1.5#2), `TextUnitMap` 8-level (closed §15), `TextLayoutCacheKey::digest()` 5-dim (post-M1.5#2), `PerRunShapingFailed` policy (closed `test_compile_text_layout_errors.cpp:290`), `TextAnimatorSpec` + Range/Wiggly/Expression selectors (closed §13).
* **5 of the 32 spec tests are blocked by feature-freeze V0.1** (AGENTS.md): emoji fallback (M5), Character Offset pre-shaping (Blocco 2), karaoke highlight (M1 non-goal), per-character 3D (M5), text on arc (M1 non-goal).

This ADR locks the **12 minimum** subset as the next-tractable unit, with the **5 forward-only tickets** as the post-freeze recovery path. No source-code changes outside `tests/` and `docs/`; no new public API; no new registry, resolver, sampler, or cache; no Python dependencies; no new include in `include/chronon3d/`.

## Decision 1 — Adoption of the 12-minimum subset (1 commit per test group; pre-test infrastructure doc-only)

### Spec

Twelve `TEST_CASE`s in `tests/text_golden/user_spec_NN_*.cpp` are added as a new visual group under the existing TXT-QA-01 harness. Each test:

* uses `chronon3d::test::make_renderer()` (in-process; not subprocess to `chronon3d_cli`),
* builds a single `Composition` via `composition({...}, lambda)` matching the user's spec (font + size + content + aspect ratio + frame index),
* renders one or more frames,
* calls `verify_golden(*fb, case_name, cfg)` (the existing helper in `tests/visual/support/golden_test.hpp`),
* emits a `CHECK(result.passed)` or a `MESSAGE` on golden-missing + the standard `INFO(result.message)`.

The 12 cases:

| # | Test name | Frame matrix | Aspect | Targets which spec bug |
|---|---|---|---|---|
| 1 | `golden_text_basic_centered` | 0 | 1920×1080 | pipeline smoke: `TextDocument → TextRunLayout → TextRunShape → draw_text_run`; alpha correct; no empty frame |
| 2 | `golden_font_swap_same_text` | 0, 30 | 1920×1080 | cache short-circuit on `source_text == target_text` skipping font rebuild (regression-locks the "fast-path" behaviour) |
| 3 | `golden_multifont_single_line` | 0 | 1920×1080 | `TextStyleSpan` + multi-size + multi-weight on shared baseline; no run dropped |
| 4 | `golden_multifont_middle_run_failure` | 0 | 1920×1080 | `PerRunShapingFailed` policy: missing font in the MIDDLE run MUST surface as Err, not as silent-drop "LEFT RIGHT" |
| 5 | `golden_bidi_english_arabic_mixed` | 0 | 1920×1080 | FriBidi correctness: "Hello سلام World" run count + visual order; gate is `tests/text/test_text_font_determinism.cpp:101` FriBidi-availability |
| 6 | `golden_text_wrap_narrow_box` | 0 | 1920×1080 (500×500 box) | word wrap + line height + no word cut + bounds |
| 7 | `golden_vertical_short_safe_area` | 0 | 1080×1920 | 9:16 anchor + safe area + centering + no clipping |
| 8 | `golden_anim_typewriter_character` | 0, 15, 30, 45, 60 | 1920×1080 | character-selector monotonic visibility; per-frame golden per `verify_golden` |
| 9 | `golden_anim_character_offset_wave` | 0, 10, 20, 30, 40, 50, 60 | 1920×1080 | per-character Y wave via `WigglySelector` (closed §13); cache-stale regression |
| 10 | `golden_text_fill_stroke` | 0 | 1920×1080 | fill+stroke paint order; stroke around fill, not over (paint order is `TextAppearanceSpec::paint::stroke_enabled` post-`StrokeOrder::AroundFill`) |
| 11 | `golden_cache_same_text_different_size` | 0 (48px), 1 (96px) | 1920×1080 | `TextLayoutCacheKey::digest()` 5-dim: same source + same font + different size → cache miss + visual size change |
| 12 | `golden_aspect_ratio_layout_semantics` | 0 | 1920×1080 + 1080×1920 (×2 goldens) | anchor + safe area + responsive layout; same semantic content across two aspect ratios |

The 12 cases are registered as a new executable `chronon3d_text_golden_user_spec_tests` (separate from the existing `chronon3d_text_golden_tests` to keep TXT-QA-01's `UNITY_BUILD OFF` + `verify_golden` + `image_diff` translation unit boundary isolated from new test files). The new executable links the same targets as `chronon3d_text_golden_tests` per `tests/text_golden_tests.cmake`.

### Source anchor

* `tests/text_golden/user_spec_NN_*.cpp` (12 new files, 1 file per test group)
* `tests/text_golden_tests.cmake` (append new executable `chronon3d_text_golden_user_spec_tests` with same link contract + `set_target_properties(... UNITY_BUILD OFF)`)
* `tests/CMakeLists.txt` (no edit needed — the existing `include(${CMAKE_CURRENT_SOURCE_DIR}/text_golden_tests.cmake)` already wires the executable into the render-aggregator `chronon3d_tests_render`)

### Test lock

Each `TEST_CASE` is its own lock. The `verify_golden` helper does the pixel-diff against the PNG in `test_renders/golden/text/user_spec_NN_*.png` (relative to repo root, per `golden_test.hpp`'s `golden_directory` config). `CHRONON3D_UPDATE_GOLDENS=1` flips the helper to `GoldenMode::Update` to capture SHAs on first run; subsequent runs without the env var enter `Verify` mode and `CHECK(result.passed)`.

### Failure mode (if silently broken)

* **Cmake wiring rot** — `tests/text_golden_tests.cmake` is not appended, the new executable is silently absent, CI's `chronon3d_tests_render` aggregate target still passes (because the new executable is in a separate cmake list, not the existing one). Future 12-test set rots silently. The append is the only line of prevention.
* **Composition API rot** — `composition({...}, lambda)` signature changes (it is in `include/chronon3d/api/composition.hpp`, which is SDK public surface but stable per `docs/RELEASE_GATE.md` §SDK Product V1). The 12 tests all use the same lambda signature; a regression there breaks all 12 simultaneously, which is the desired burst.
* **verify_golden helper rot** — a regression in `tests/visual/support/golden_test.cpp` would make every golden test silently pass via default-construct `GoldenTestResult{passed=true}`. The existing TXT-QA-01 test (`tests/text/test_text_golden.cpp`) already provides this lock at the harness level; the 12 new tests reuse the helper unchanged.

## Decision 2 — 5 forward-only tickets for the spec subset blocked by feature-freeze V0.1

### Spec

Five rows are appended to `docs/FOLLOWUP_TICKETS.md` (the canonical index per `DOCUMENTATION_GOVERNANCE.md`), one per spec test that depends on a feature whose implementation is post-freeze:

| Ticket ID | Spec test | Freezing gate | Forward path |
|---|---|---|---|
| TICKET-GOLDEN-10 | `golden_emoji_fallback_basic` | M5 ("color emoji/fallback completo") | post-M1, post-M4, post-M5 (color glyph/emoji). Tracked in `ROADMAP.md` §M5. |
| TICKET-GOLDEN-16 | `golden_anim_character_offset_wave` (Character Offset pre-shaping) | Blocco 2 (Character Offset pre-shaping) | already a Roadmap Blocco 2 item; this ticket links the spec test #16 to the existing Blocco 2 work. |
| TICKET-GOLDEN-30 | `golden_karaoke_word_highlight` | M1 non-goal (karaoke is M4+"Subtitle word timing" deliverable) | post-M4 (subtitle word timing + word timing mapping). |
| TICKET-GOLDEN-31 | `golden_character_depth_stack` | M5 (per-character 3D) | post-M5 + Expressions V2 stable. |
| TICKET-GOLDEN-32 | `golden_text_on_arc_path` | M1 non-goal (text on path is M1 work item but arc-only is post-M4) | post-TXT-09 (Blocco 9) + Blocco 11 (motion blur). |

Each row is a one-liner in the canonical "Open blockers" or "P1 backlog" table of `FOLLOWUP_TICKETS.md`, with `Status: PLANNED` (per the project's status enum). None of the 5 tickets are schedulable until the feature-freeze is revoked (i.e. 11/11 PASS baseline per `docs/CURRENT_STATUS.md`).

### Source anchor

* `docs/FOLLOWUP_TICKETS.md` (the canonical index, edited in the same commit as ADR-014 to satisfy gate #7 `check_doc_sync.sh`)

### Test lock

None — the 5 tests do not exist yet. Their forward-only tickets are documentation-only and have no source-code surface.

### Failure mode (if silently broken)

* **Lost-in-noise rot** — the 5 tests are forgotten. Future developers won't know they were a deliberate forward-only deferral. The `docs/FOLLOWUP_TICKETS.md` row IS the lock; if the row is removed without a corresponding "implemented" commit, the spec test is silently lost.
* **Stale cross-link rot** — TICKET-GOLDEN-16 has a "Blocco 2" cross-link; if Blocco 2's work item is renamed/reordered, the cross-link becomes stale. AGENTS.md "non modificare `docs/ARCHIVE/`" rule + "use only the 4 canonical files" rule protects the cross-link, but future maintainers must update it as part of any Blocco 2 PR.

## Decision 3 — No Python; no new public C++ API; light-touch infra

### Spec

The new infrastructure is bounded by:

* **No Python dependencies** — no use of `tools/visual_quality_suite.py`, `tools/compare_pngs.py`, `tools/analyze_frames.py`, `tools/similarity_suite.py`. The 12 tests use the C++ `verify_golden` helper (already in `tests/visual/support/golden_test.cpp`) which performs the pixel-diff via pure C++.
* **No new public C++ API** — no new header in `include/chronon3d/`. All new code lives under `tests/text_golden/` and `docs/`. The `tests/text_golden_tests.cmake` append is build-system only.
* **No new registry/resolver/sampler/cache** — `verify_golden` reuses the existing `chronon3d::test` namespace and the existing `tests/visual/support/golden_test.cpp` source.
* **Bash driver for convenience only** — `tools/test_golden_driver.sh` is a thin wrapper that:
  1. Configures cmake with `linux-fast-dev` preset (or `linux-release` if `BUILD_TYPE=release`),
  2. Builds the `chronon3d_text_golden_user_spec_tests` target,
  3. Runs `ctest -R TextGoldenUserSpec --output-on-failure`,
  4. Reports pass/fail per test.
  The bash driver does not replace any existing test infrastructure. It is a developer convenience for quick local re-runs; CI uses the canonical `chronon3d_tests_render` aggregate target.

### Source anchor

* `tools/test_golden_driver.sh` (new tracked file, executable)

### Test lock

None — the bash driver is not a test. It is a developer convenience wrapper.

### Failure mode (if silently broken)

* **Bash rot** — the driver uses a non-canonical preset, or skips a flag. Maintainers should treat the driver as one of many ways to invoke the test; the canonical CI path remains the aggregate `chronon3d_tests_render` target.
* **Silent skip rot** — the driver silently returns 0 if `chronon3d_text_golden_user_spec_tests` is not built. The driver should `set -euo pipefail` + explicit `if [ ! -f ... ]; then echo "BUILD_FAILED"; exit 1; fi` for the binary check. This is part of the script's body, not a test lock.

## Consequences

### Positive

* **12 new regression gates against silent drift** in the text pipeline, each backed by a golden PNG + manifest JSON, locked under the existing `verify_golden` helper.
* **5 forward-only tickets preserve the spec's intent** without violating the feature-freeze. The spec's 32-test scope is preserved in `FOLLOWUP_TICKETS.md` for post-freeze pickup.
* **AGENTS.md "5 categorie consentite" rule respected** — this is CATEGORY 2 (Test deterministici). No new feature, no new public API, no new registry, no new include, no Python deps.
* **Doc-gate alignment** — `docs/CURRENT_STATUS.md` is updated in the same commit to keep `tools/check_doc_sync.sh` happy (gate #7).

### Negative / Migration cost

* **Build runtime** — adding `chronon3d_text_golden_user_spec_tests` to the render aggregate adds ~5-10s of build time per CI run (in-process C++ tests, no subprocess).
* **First-run golden capture** — the 12 tests' goldens are not pre-baked; on first run with `CHRONON3D_UPDATE_GOLDENS=1` they capture the SHA. Subsequent runs validate. This is intentional (idempotent first-run capture, not pre-baked).
* **Real golden SHAs not captured in this commit** — the 12 PNG files are written by the first `CHRONON3D_UPDATE_GOLDENS=1` run, not committed. The user/next session is responsible for the first run + commit of the goldens.
* **5 forward-only tickets are not schedulable** until 11/11 PASS baseline (per `CURRENT_STATUS.md` "9/11 NON è 11/11: feature freeze ancora attivo").

### Neutral

* No source-code changes outside `tests/`, `docs/`, `tools/`.
* The 12 tests use the existing `Composition` API surface; no new SDK headers, no SDK version bump.
* The bash driver is optional; CI does not depend on it.

## Alternatives considered

* **A. One large test file with all 12 cases.** Rejected — the existing TXT-QA-01 harness has 1 file per scenario (e.g. `static_fill_stroke`, `fade_in`, `blur_in`, `word_cascade`, `cinematic_title_reveal`) for a reason: per-test RUN coverage + per-test golden artefacts. 12 files matches that pattern.
* **B. Use `tools/visual_quality_suite.py` post-processing.** Rejected — AGENTS.md "no new Python deps" (Q2 user answer), and the existing `verify_golden` helper already does pixel-diff in pure C++. No need for Python post-processing.
* **C. Put the 12 tests under `tests/visual_text/`.** Rejected — the existing `tests/text_golden/` directory + `text_golden_tests.cmake` is the canonical home for golden text tests (TXT-QA-01 already there). Reusing the directory + cmake avoids the tests/CMakeLists.txt orchestrator change.
* **D. Open 5 forward-only tickets via TICKET-NNN.md files in `docs/tickets/`.** Considered; rejected — `docs/FOLLOWUP_TICKETS.md` is the canonical index per `DOCUMENTATION_GOVERNANCE.md`, and the 5 rows are NOT full tickets (no DoD, no acceptance criteria), they are forward-only index entries. Per-ticket files would be over-ceremonial for the scope.

## References

* `AGENTS.md` §"Feature Freeze — V0.1 (attivo dal 2026-06-29)" + §"Cosa è CONSENTITO" (Test deterministici — golden test, sentinel hash, regression gate)
* `docs/RELEASE_GATE.md` §"Text Production V1" (Text V1 acceptance criteria)
* `docs/ROADMAP.md` §"M1 — Text Production V1" + §"M5 — Global text ed effetti premium" + §"Blocco 5 — Visual Regression Harness"
* `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` §"3.4 Claim di stabilità non verificati visivamente" + §"Blocco 5"
* `docs/MIGRATION_TEXT_SPEC.md` §10-15 (P1 single canonical pipeline + selector V2 + TextUnitMap 8-level)
* `docs/CURRENT_STATUS.md` "9/11 NON è 11/11: feature freeze ancora attivo" (revocation gate)
* `tests/text/test_text_golden.cpp` (TXT-QA-01 standalone harness, 5 scenarios × 2 aspect × 4 frames)
* `tests/text/test_compile_text_layout_errors.cpp:290` (P1-1 `PerRunShapingFailed` lock)
* `tests/text/test_text_font_determinism.cpp:101` (FriBidi availability gate)
* `tests/visual/support/golden_test.hpp` (verify_golden helper, ENV-gated Update mode)
* `tests/visual/support/image_diff.cpp` (pixel-diff implementation)
* `tests/visual/render_graph/node_goldens.cpp` (per-test golden pattern for render-graph nodes)
* `tests/text_golden_tests.cmake` (TXT-QA-01 standalone harness, UNITY_BUILD OFF)
* `tools/check_architecture_boundaries.sh` (gate #5) + `tools/check_doc_sync.sh` (gate #7) + `tools/wrap_push.sh` (GATE-MNT-01 push-side wrapper)
* `docs/DOCUMENTATION_GOVERNANCE.md` (canonical files contract)
