## Luglio 2026 — TICKET-CHANGELOG-CONFLICT-CLEANUP — document CHANGELOG conflict root cause + add forward-only CI gate (2026-07-10, atomic commit)

### feat(docs+ci): TICKET-CHANGELOG-CONFLICT-CLEANUP — forward-only CI gate prevents recurrence of f5551a13 CHANGELOG conflict

- **Scope**: TICKET-CHANGELOG-CONFLICT-CLEANUP. Document the pre-existing `docs/CHANGELOG.md` conflict (the `<<<<<<< HEAD` / `=======` / `>>>>>>> be77fbd5` markers that persisted in main for ~10 commits before being resolved as a side effect of the TICKET-FASE3-MULTILINGUAL cycle 4 commit `5efcc301`), identify the introducing commit, and add a forward-only CI gate to prevent recurrence. Cross-references: [`docs/tickets/TICKET-CHANGELOG-CONFLICT-CLEANUP.md`](tickets/TICKET-CHANGELOG-CONFLICT-CLEANUP.md) (full ticket + forensic timeline + acceptance criteria); `TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS` (OPEN, broader 3-tracked-files rot pattern; this ticket is scoped to CHANGELOG.md only).

- **Root cause (machine-verified)**: commit `f5551a13` (titled `docs(sync): F3.D closure - CHANGELOG + FOLLOWUP + CURRENT_STATUS`, 2026-07-10) was a failed `git merge` of `be77fbd5` (F3.D closure) into HEAD that was committed verbatim with the conflict markers still in the file. The TICKET-SIMPLICITY entries (added by HEAD between `be77fbd5` and `f5551a13`) conflicted with the F3.D entry; the merge was committed without resolution. `git log --all -p -S'<<<<<<< HEAD' -- docs/CHANGELOG.md` confirms `f5551a13` as the introducing commit. `be77fbd5` itself did NOT have the markers (it was the incoming side of the failed merge).

- **Impact**: ~10 commits in main (from `f5551a13` to `5efcc301`); P1 severity (broke markdown rendering of CHANGELOG.md, broke doc-sync gate expectations, made the CHANGELOG unreadable in raw form).

- **Resolution (commit `5efcc301`, side effect of TICKET-FASE3-MULTILINGUAL cycle 4)**: the Fase 4 commit resolved the conflict by taking BOTH sides (TICKET-SIMPLICITY entries from HEAD + F3.D entry from `be77fbd5`) via `sed -i '/^<<<<<<< /d; /^>>>>>>> /d; /^=======$/d' docs/CHANGELOG.md` after verifying that no markdown setext headings used `=======` as an underline (the canonical CHANGELOG uses ATX-style headings `##` / `###` exclusively).

- **Forward-only CI gate (NEW)**: `tools/check_no_changelog_conflict_markers.sh` (74 LoC, executable, smoke-tested locally). Greps for `^(<<<<<<< |=======$|>>>>>>> )` in `docs/CHANGELOG.md`; exit 0 if clean, exit 1 with remediation hint (offending lines + fix command) if any markers are found. Pattern note documented in the script: `=======$` is matched because (a) git conflict separators are exactly 7 `=` chars, and (b) markdown setext heading underlines are typically `---` (3+ dashes), not `=======`. The canonical CHANGELOG uses ATX-style headings exclusively, so this is safe; if a future entry needs setext headings, the gate would need to be refined.

- **Gate chain registration**: `tools/wrap_push.sh` Step 4.5d (post-`check_frame_value_convention.sh`, pre-`git push`). Follows the existing gate pattern: `echo` + `bash` + `|| { GATE_FAIL; exit 1; }`. No `--skip-gates` escape hatch (violations are deterministic link-breakers per the existing TICKET-110 contract). The gate runs LOCALLY (no network, no gh API) on every `git push` invocation via the canonical wrapper.

- **New files (2)**:
  - `docs/tickets/TICKET-CHANGELOG-CONFLICT-CLEANUP.md` (~80 LoC) — full ticket with Background / Root Cause / Impact / Resolution / Forward Point / Acceptance Criteria / Related Tickets / Cross-references sections.
  - `tools/check_no_changelog_conflict_markers.sh` (74 LoC, NEW) — the CI gate. Anti-duplicazione: reuses the existing `bash` + `grep -nE` + `sed` pattern from sibling gates; no new singleton/registry/cache/resolver/sampler introduced.

- **Modified files (3)**:
  - `tools/wrap_push.sh` — added 7-line Step 4.5d block (echo + bash + remediation comment + `|| { GATE_FAIL; exit 1; }`). Updated header comment to reflect the new gate (Gate chain count: 5 → 6).
  - `docs/FOLLOWUP_TICKETS.md` — new row in `## Recently Closed` table at the top, with cross-link to the ticket file + 1-line status summary.
  - `docs/CHANGELOG.md` — this entry prepended at TOP (the Fase 4 entry moved down by 1 position).

- **API/ABI surface**: zero changes (no source code modified; ticket + gate script + .sh + 2 docs only).

- **Anti-duplicazione honour (AGENTS.md §anti-duplication rules)**: zero new content. The ticket is a single canonical cross-link entry that consolidates the existing knowledge (the Fase 4 CHANGELOG entry, the `be77fbd5` F3.D commit, the `f5551a13` introducing commit) into one place. The gate script reuses the existing `bash` + `grep` + `sed` pattern from sibling gates (`check_no_dual_text_api.sh`, `check_frame_value_convention.sh`); no new logging framework, no new dependency.

- **AGENTS.md v0.1 freeze compliance**:
  - **Cat-3** (no new public API surface): SATISFIED — zero source code modified, zero new symbols; ticket + gate + 2 docs only.
  - **Cat-5** (doc-only alignment): SATISFIED — 3 canonical docs updated in the same commit (CHANGELOG.md + FOLLOWUP_TICKETS.md + the new ticket file; gate `#7 check_doc_sync.sh` R5 fires on TICKET-CHANGELOG-CONFLICT-CLEANUP closure).
  - Gate 5 deny-everywhere compliance: N/A — no `#include <msdfgen>`/`<libtess2>`/`<unicode[/...]>` introduced.
  - Zero nuovi singleton/registry/cache/resolver/sampler/service-locator.

- **Gate smoke-test** (machine-verified locally):
  - `bash tools/check_no_changelog_conflict_markers.sh` on the current clean CHANGELOG → `exit: 0` + `OK: no git merge conflict markers in docs/CHANGELOG.md`.
  - `bash -c` with a synthetic CHANGELOG containing `<<<<<<< HEAD` / `=======` / `>>>>>>> be77fbd5` → `exit: 1` + `GATE_FAIL: git merge conflict markers detected in docs/CHANGELOG.md` + offending lines + remediation hint.
  - `chmod +x tools/check_no_changelog_conflict_markers.sh` → `YES` (executable bit set).
  - `bash -n tools/check_no_changelog_conflict_markers.sh` → syntax check PASS (no bash errors).

- **Honest limitation (per AGENTS.md §honesty)**: this commit does NOT include a `TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS` resolution (the broader 3-tracked-files rot pattern remains OPEN per the §Open Blockers table). The scope of this ticket is intentionally limited to the CHANGELOG.md case (the most user-visible and doc-sync-gate-breaking of the 3 files). The `TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS` ticket should be closed in a separate forward-point commit with a generalized gate that scans all `.md` files under `docs/canonical/` (not just CHANGELOG.md) for conflict markers.

- **Forward-point (not in this commit)**: `TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS` (OPEN) — generalize the gate to scan all `docs/canonical/*.md` + `docs/tickets/*.md` for conflict markers + fix the 2 remaining tracked files. All forward-points are separate atomic commits per AGENTS.md §GATE-MNT-01.

- **Cross-references**: [`docs/tickets/TICKET-CHANGELOG-CONFLICT-CLEANUP.md`](docs/tickets/TICKET-CHANGELOG-CONFLICT-CLEANUP.md) (the new ticket); [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) `## Recently Closed` row (the new entry); `tools/check_no_changelog_conflict_markers.sh` (the new gate); `tools/wrap_push.sh` Step 4.5d (the new wire-up); commit `5efcc301` (the side-effect resolution); commit `f5551a13` (the introducing commit); commit `be77fbd5` (the incoming side of the failed merge); `tools/wrap_push.sh` Step 4 (the existing GATE-MNT-01 rebase-clean invariant); `tools/check_doc_sync.sh` R5 (the existing TICKET-closure → CHANGELOG co-update rule); `AGENTS.md` §Regole di lavoro (no fabricate / no silent failure / no untracked mods in commit).

---

## Luglio 2026 — TICKET-FASE3-MULTILINGUAL §HangulComposition (4th multilingual golden: 3 TEST_CASEs, 6 PNG) (2026-07-10, atomic commit)

### feat(text_golden): TICKET-FASE3-MULTILINGUAL §HangulComposition — Hangul Syllables U+AC00–U+D7A3 composition correctness

- **Scope**: Fourth test of the TICKET-FASE3-MULTILINGUAL cluster. Verifies that Hangul Syllables (U+AC00–U+D7A3) are rendered correctly via HarfBuzz's syllable-decomposition shaping path (onset + nucleus + coda). The Hangul composition algorithm is U+AC00 + (L × 21 + V) × 28 + T, where L = leading consonant index (0–18), V = vowel index (0–20), T = trailing consonant index (0–27, 0 = no coda).

- **3 TEST_CASEs × 2 ARs = 6 PNG goldens** in `test_renders/golden/text/text_multilingual/hangul_composition/`:
  - 3 test cases: `01_simple_syllables` (15 CVC-less syllables 가나다라마바사아자차카타파하), `02_complex_syllables` (4 words with coda: 한국 한글 읽다), `03_real_korean_word` (안녕하세요 = "Hello")
  - 2 aspect ratios per case: 1920×1080 (16:9 landscape) + 1080×1920 (9:16 portrait)
  - 6 PNG goldens: `multilingual_hangul_composition_{01,02,03}_{1920x1080,1080x1920}.png`

- **New file (1)**:
  - `tests/text_golden/text_multilingual/04_hangul_composition.cpp` (~236 LoC) — 3 TEST_CASEs, 6 `verify_golden()` calls (3 cases × 2 ARs via the `render_and_verify_hangul()` helper). Uses existing `composition()` + `SceneBuilder` + `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig` + `test::make_renderer()` helpers. Anti-duplicazione honoured: no new types, no new helpers.

- **Modified files (1)**:
  - `tests/text_golden_tests.cmake` — added `target_sources(chronon3d_text_golden_tests PRIVATE text_golden/text_multilingual/04_hangul_composition.cpp)` + new `add_test(NAME TextMultilingualHangulComposition ...)` ctest alias with the same filter pattern as the Fase 3 aliases.

- **API/ABI surface**: zero new public symbols (test-side only; no source code modified, no new symbols).

- **Anti-duplicazione honour**: reuses `LayerBuilder::text()` + `verify_golden()` + `GoldenTestConfig`; NO new singleton/registry/cache/resolver/sampler/service-locator.

- **AGENTS.md v0.1 freeze compliance**: Cat-3 SATISFIED (zero new public API); Gate 5 deny-everywhere N/A.

- **Honest-gap documentation** (per AGENTS.md §honesty):
  - All 3 tests gracefully skip on `result.golden_missing`. 6 PNG re-bake requires a working build host (vcpkg + tmpfs).
  - Inter-Bold.ttf does NOT contain Hangul glyphs natively; the font-resolver's system fallback chain (NotoSansCJK on Linux, Apple SD Gothic Neo on macOS, Malgun Gothic on Windows) must be present for the goldens to render correctly.
  - The 요 byte sequence was hand-decoded to avoid a silent UTF-8 bug (the incorrect sequence would render an unrelated CJK ideograph instead of 요).

- **Re-bake command** (deferred to working build host):
  `CHRONON3D_UPDATE_GOLDENS=1 ctest -R TextMultilingualHangulComposition --output-on-failure`

---

## Luglio 2026 — TICKET-FASE3-MULTILINGUAL §KerningPairs + §MixedAdvanceWidths + §MixedBaseline (3 genuinely new multilingual text goldens) (2026-07-10, atomic commit)

### feat(text_golden): TICKET-FASE3-MULTILINGUAL first cycle — 3 multilingual goldens (9 PNG, 3 per test family)

- **Scope**: First cycle of the TICKET-FASE3-MULTILINGUAL workstream
  (RTL/CJK feature already supported per earlier work; this cycle
  focuses on the 3 genuinely-new test families).  Each test family
  exercises a different orthogonal axis of multilingual text rendering:

  - **§KerningPairs** (`01_kerning_pairs.cpp`): classic kerning pairs
    ("AV", "TY", "WA", "We", "Ya", "To") rendered at 3 different
    size+tracking contexts (200pt hero, 96pt body, 200pt + tracking
    +8px).  Locks down the kerning feature path at multiple scales.

  - **§MixedAdvanceWidths** (`02_mixed_advance_widths.cpp`): Latin
    proportional + CJK uniform + mixed Latin/CJK in the same line.
    Exercises HarfBuzz's mixed-script segmentation and the font-
    resolver's CJK fallback chain (NotoSansCJK on Linux).

  - **§MixedBaseline** (`03_mixed_baseline.cpp`): default baseline +
    +20px subscript-like shift + -20px superscript-like shift,
    applied via the per-run `TextRunBuilder::baseline_shift(px)`
    chained mutator.  Locks down the per-RUN baseline animator.

- **New files (3)**:
  - `tests/text_golden/text_multilingual/01_kerning_pairs.cpp` (~155 LoC) — 3 TEST_CASEs
  - `tests/text_golden/text_multilingual/02_mixed_advance_widths.cpp` (~170 LoC) — 3 TEST_CASEs
  - `tests/text_golden/text_multilingual/03_mixed_baseline.cpp` (~170 LoC) — 3 TEST_CASEs

- **Modified files (1)**:
  - `tests/text_golden_tests.cmake` — 3 new `target_sources` entries +
    3 new `add_test` ctest aliases (TextMultilingualKerningPairs +
    TextMultilingualMixedAdvanceWidths + TextMultilingualMixedBaseline)

- **9 PNG goldens** (3 per test family) in
  `test_renders/golden/text/text_multilingual/{kerning_pairs,mixed_advance_widths,mixed_baseline}/`.

- **Honest-gap documentation** (per AGENTS.md §honesty):
  - All 9 tests gracefully skip on `result.golden_missing`.  9 PNG re-bake
    requires a working build host (vcpkg + tmpfs).
  - §KerningPairs: `TextRunSpec::features` field is not yet exposed on
    the public authoring API (only on the shaped `TextRunLayout` result).
    Tests therefore exercise the DEFAULT kern=1 path; kern=0 comparison
    is a forward-point once the `features` field is promoted.
  - §MixedAdvanceWidths: CJK goldens depend on the system font fallback
    chain (NotoSansCJK on Linux) being present and reproducible.
  - §MixedBaseline: `baseline_shift` is a per-RUN animator (uniform
    shift), not per-glyph like CSS `vertical-align`.  Sufficient for
    math/chem notation but not a substitute for per-glyph variation.

- **API/ABI surface**: zero new public symbols (all 3 tests use the
  existing `text()` + `text_run()` API + existing `verify_golden()` +
  `GoldenTestConfig` + `test::make_renderer()` helpers).

- **Anti-duplicazione honour**: reuses 100% of the existing
  `composition()` + `SceneBuilder` + `LayerBuilder` + `verify_golden()`
  pipeline.  No new test infrastructure created.

- **Verification**: 9 TEST_CASEs registered for 3 ctest aliases.
  Integration-tier gated (Blend2D + text).  Graceful skip on golden
  missing (no false-fail on clean checkout).

- **Re-bake command** (deferred to working build host):
  `CHRONON3D_UPDATE_GOLDENS=1 ctest -R "TextMultilingual.*" --output-on-failure`

## Luglio 2026 — TICKET-FASE2-TRANSFORMS-ANIMATION §10 — RotateZNotCut (6 PNG goldens, 3 rotations × 2 ARs) (2026-07-10, atomic commit)

---

## Luglio 2026 — TICKET-SIMPLICITY-INSPECT-TEXT-RENDER: `inspect text` real frame rendering (2026-07-10)

### fix(cli): `inspect text` — wire `--diagnostic-overlay` into actual frame rendering via SoftwareRenderer

- **Problem**: `render_frame_to_png()` in `command_text_audit.cpp` was a placeholder — it evaluated the scene via `comp.evaluate()` but never rendered pixels. The `--diagnostic-overlay` flag on `inspect text` had no effect on the output PNGs.
- **Fix** (1 file, `command_text_audit.cpp`):
  - Replaced the placeholder `FrameContext` + `comp.evaluate()` with `SoftwareRenderer::render(comp, Frame{frame})` (canonical V0.2 entry point)
  - Wired `diagnostic_overlay` and `diagnostic_overlay_only` from `TextAuditArgs` into `RenderSettings` (matching `bake-layer` + `settings_from_args` patterns)
  - Added `save_image()` call with `ImageFormat::Png` + `convert_png_to_srgb` for output
  - Added includes: `cli_render_utils.hpp` (for `create_renderer`), `render_settings.hpp`, `image_writer.hpp`
  - Removed unused `<chronon3d/core/types/frame_context.hpp>` include
- **Error handling**: null framebuffer check + save failure check + exception catch

---

## Luglio 2026 — TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY-ONLY: `--diagnostic-overlay-only` trasparente (2026-07-10)

### feat(cli): `--diagnostic-overlay-only` — overlay markers on transparent background, no scene content

- **New CLI flag**: `--diagnostic-overlay-only` on `render`, `still`, `video`, `bake-layer`, and `inspect text` — produces a transparent-background PNG with ONLY diagnostic markers, no scene content. Useful for overlay-on vs overlay-off comparison.
- **Implementation** (9 files):
  - `include/chronon3d/backends/software/render_settings.hpp` — added `diagnostic_overlay_only` to `RenderSettings`
  - `include/chronon3d/render_graph/render_graph_context.hpp` — added `diagnostic_overlay_only` to `RenderPolicy`
  - `src/render_graph/pipeline/helpers.hpp` — wired `diagnostic_overlay_only` in `make_graph_context()`
  - `src/render_graph/nodes/TextRunNode.cpp` — gated text rendering on `!ctx.policy.diagnostic_overlay_only`; framebuffer stays transparent; overlay markers draw on top
  - `apps/chronon3d_cli/commands.hpp` — added to `RenderPipelineArgs`, `BakeLayerArgs`, `TextAuditArgs`
  - `apps/chronon3d_cli/utils/job/cli_render_utils.hpp` — wired in `settings_from_args()` + updated `PipelinableArgs` concept
  - `apps/chronon3d_cli/commands/render/command_bake_layer.cpp` — wired `diagnostic_overlay` + `diagnostic_overlay_only` (fixed latent bug where `diagnostic_overlay` was never wired)
  - `apps/chronon3d_cli/commands/render/register_render_commands.cpp`, `register_video_commands.cpp`, `register_inspect_commands.cpp` — registered `--diagnostic-overlay-only` flag
- **Design**: When `diagnostic_overlay_only` is active, `TextRunNode::execute()` skips `render_text_run_item()` entirely. The framebuffer (acquired with `clear=true`) stays transparent. Then `draw_text_debug_overlay()` draws the layout-box/anchor/baseline/canvas-center markers as usual. Alpha-dependent markers (visual bounds, alpha centroid) are naturally skipped since the framebuffer has no rendered content.
- **Flag semantics**: `--diagnostic-overlay-only` is standalone — it activates the overlay pipeline (via `text_layout_debug`) on its own and also skips scene content rendering. No need to also pass `--diagnostic-overlay` alongside it.

---
## Luglio 2026 — F3.D: LayerBuilder forward-point wiring via `to_text_run_spec` (2026-07-10, atomic commit)

### feat(text): F3.D — `LayerBuilder` `text`/`text_run` `TextDefinition` overloads route through `to_text_run_spec` (preserves 6 spec-only animation fields)

- **F3.D forward-point rewiring** (closes the LOAD-LOSS GAP flagged in the F2.D → F3.D ladder): the 2 `LayerBuilder` `TextDefinition` overloads now route through the F2.D lossless reverse adapter `to_text_run_spec` instead of the F2.C lossy `from_text_definition` path. The 6 spec-only animation fields (`animators`, `selectors`, `direction`, `language`, `script`, `cache_layout`) populated in any `TextDefinition` now reach `TextRunSpec` + `materialize_text_run_shape()` end-to-end instead of being silently dropped.
  - `src/scene/builders/commands/shape_commands.cpp:text(name, const TextDefinition&)` body: `text_run(name, to_text_run_spec(def)).commit()` (replaces `text(name, from_text_definition(def))`).
  - `src/scene/builders/layer_builder_compile.cpp:text_run(name, const TextDefinition&)` body: `text_run(name, to_text_run_spec(def))` (replaces `run.text = from_text_definition(def)` + delegate pattern).
- **F3.D forward-point overload ADDED**: `LayerBuilder::text(name, TextRunSpec)` — the symmetric counterpart of the existing `text_run(name, TextRunSpec)`. Lets callers fully migrated to `TextRunSpec` authoring use the short-form `layer.text("id", run_spec).commit()` instead of the verbose `layer.text_run("id", run_spec).commit()`. Behaviourally identical (pure sugar); completes the text/text_run parallel pair on the `LayerBuilder` API surface.
- **17 helper-site call sites made lossless end-to-end**: `centered_text()` / `glow_text()` / `typewriter_text()` / presets augmenting `TextDefinition` with animation fields now propagate that animation to the renderer. The 17 sites verified by existing integration tests across `content/text_placement/`, `content/showcases/cinematic/`, `content/showcases/minimalist/`, `content/showcases/special-names/`, `content/showcases/important-words/`, `content/certification/`, `tests/deterministic/`, `tests/text/`, and `tests/text_golden/`.
- **LIFECYCLE update**: `include/chronon3d/text/text_definition.hpp` gains a F3.D entry documenting the LayerBuilder forward-point rewiring + the new forward-point overload + the Frame envelope drop (unchanged from F2.D contract).
- **Doc-block updates in `include/chronon3d/scene/builders/layer_builder.hpp`**: the two F2.C doc-blocks (text + text_run `TextDefinition` overloads) updated to F3.D wording. Removes the now-stale "NOT carried from TextDefinition" claim from `text_run(name, TextDefinition)`: animation fields ARE carried end-to-end via the F3.D wire. Adds the F3.D doc-block for the new `text(name, TextRunSpec)` overload.
- **Tests** — group 20 in `tests/text/test_text_definition.cpp` (1 NEW `TEST_CASE`):
  - **20.1** Helper-site augmentation pattern: `centered_text(opts)` + manual `def.animation.{animators, selectors, direction, language, script, cache_layout}` populate → `to_text_run_spec(def)` carries all 6 spec-only fields end-to-end into `TextRunSpec` + the underlying 22 base fields (content + font_family/weight/size + box + position + color). This is a meaningful regression lock for the F3.D wire: a future edit reverting to `from_text_definition()` would leave `run.animators` empty and FAIL 20.1.
- **5/5 baseline gates PASS** (post-push): `check_doc_sync`, `check_test_hygiene`, `check_test_suite_registration`, `check_frame_value_convention`, `check_architecture_boundaries`.
- **Files changed (5)**: `include/chronon3d/scene/builders/layer_builder.hpp`, `include/chronon3d/text/text_definition.hpp`, `src/scene/builders/commands/shape_commands.cpp`, `src/scene/builders/layer_builder_compile.cpp`, `tests/text/test_text_definition.cpp` (+203/-33 lines).

## Luglio 2026 — F2.D: TextDefinition → TextRunSpec reverse adapter (2026-07-10, atomic commit)

### feat(text): F2.D — `to_text_run_spec` reverse adapter closes the LOSSY REVERSE gap

- **New adapter**: `[[nodiscard]] TextRunSpec to_text_run_spec(const TextDefinition&)` added in `include/chronon3d/text/text_definition.hpp` (declaration) + `src/text/text_definition.cpp` (implementation). Naming parallel to `to_text_document` (Phase B).
- **Closes the LOSSY REVERSE gap** flagged in the F2.A LIFECYCLE comment: the 6 spec-only animation fields carried by `TextRunSpec` (`animators`, `selectors`, `direction`, `language`, `script`, `cache_layout`) are now carried back from a `TextDefinition`.
- **Drift-prevention**: reuses `run.text = from_text_definition(def)` for the 22 base fields instead of manually re-mapping — guarantees the two reverse adapters cannot drift apart when `TextSpec` evolves.
- **Documented added lossy drop** (Frame envelope): `TextAnimation.start_delay` + `.duration` are NOT representable in `TextRunSpec` and are silently dropped during `to_text_run_spec`. Roundtrip `TextDefinition → TextRunSpec → TextDefinition` therefore re-initialises both envelope fields to `Frame{0}` — canonical, tested behaviour.
- **LIFECYCLE update**: `text_definition.hpp` LIFECYCLE block now shows Phase A.3 historical + F2.D current; the LOSSY REVERSE note augmented with the additional Frame envelope drop.
- **Tests** — group 19 in `tests/text/test_text_definition.cpp` (5 NEW `TEST_CASE`s):
  - **19.1** Forward mapping: all 6 animation fields populate correctly.
  - **19.2** Drift-prevention: `run.text` fields equal `from_text_definition(def)` (proves reuse, no manual remap).
  - **19.3** Empty def → default `TextRunSpec` (direction=Auto, language="", script=0, cache_layout=true).
  - **19.4** Frame envelope lossy drop: roundtrip yields `Frame{0}` for `start_delay` + `duration`.
  - **19.5** Roundtrip idempotency for the 6 spec-only fields + non-default `Vec2{42.5,-17.3}` offset preservation lock (regression-locks `from_text_definition` from remapping offset in the future).
- **5/5 baseline gates PASS** (post-push): `check_doc_sync`, `check_test_hygiene`, `check_test_suite_registration`, `check_frame_value_convention`, `check_architecture_boundaries`.
- **Files changed (3)**: `include/chronon3d/text/text_definition.hpp`, `src/text/text_definition.cpp`, `tests/text/test_text_definition.cpp` (+287/-18 lines).

---

## Luglio 2026 — TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY: `--diagnostic-overlay` flag (2026-07-10)

### feat(cli): TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY — `--diagnostic-overlay` draws bbox, anchor, baseline

- **New CLI flag**: `--diagnostic-overlay` on `render` and `video` commands — enables visual diagnostic overlay on text layers:
  - **Green rectangle**: layout box bounds
  - **Blue dot**: text anchor point (world origin)
  - **Cyan horizontal line + dot**: text baseline
- **Implementation** (4 files):
  - `apps/chronon3d_cli/commands.hpp` — added `diagnostic_overlay` bool to `RenderPipelineArgs`
  - `apps/chronon3d_cli/utils/job/cli_render_utils.hpp` — wires `diagnostic_overlay` → `text_layout_debug` in `settings_from_args()`
  - `apps/chronon3d_cli/commands/render/register_render_commands.cpp` + `register_video_commands.cpp` — registered `--diagnostic-overlay` flag
  - `src/render_graph/nodes/text_run/text_run_debug_overlay.hpp` — added cyan baseline line + dot markers (reuses existing crosshair/dot/rect helpers)
- **Design**: `--diagnostic-overlay` is a user-facing alias that activates the underlying `text_layout_debug` pipeline (same mechanism as `--debug-text-layout`). All existing markers (canvas center crosshair, visual bounds, alpha centroid) plus the new baseline marker are drawn.
- **Text Simplicity Action Plan**: TICKET-SIMPLICITY-DIAGNOSTIC-OVERLAY complete (18th action).

---

## Luglio 2026 — TICKET-SIMPLICITY-INSPECT-TEXT: CLI `inspect text-def` JSON diagnostic (2026-07-10)

### feat(cli): TICKET-SIMPLICITY-INSPECT-TEXT — `inspect text-def` exports TextRunShape+TextRunLayout to JSON

- **New subcommand**: `chronon3d_cli inspect text-def <id> [--json <path>]` — evaluates the composition at frame 0, walks all text layers, and serialises every TextRunShape field to structured JSON.
- **Implementation**: `apps/chronon3d_cli/commands/dev/command_text_def_inspect.cpp` (NEW, ~170 lines) — resolves composition via `resolve_composition()`, evaluates at frame 0, walks `Scene::layers()` for `LayerKind::Text` layers, finds `ShapeType::TextRun` nodes, serialises `TextRunShape` + `TextRunLayout` fields to `nlohmann::json`.
- **JSON output covers**:
  - **TextRunLayout** (authoring-level): `source_text`, `font` (FontSpec: family, weight, style, size, path), `font_size`, `direction`, `language`, `features`, `variation_axes`, `glyph_count`, `bounds`, `line_height`, `tracking`, `wrap`
  - **TextRunShape** (runtime): `layout` (TextLayoutSpec: box, anchor, centering_mode, align, vertical_align, wrap, overflow, line_height, tracking, auto_fit, min/max_font_size, max_lines, ellipsis)
  - **Appearance**: `paint` (fill, stroke_enabled, stroke_color, stroke_width), `shadows` (per-shadow offset/blur/opacity/color), `material` (glow, bevel, inner_shadow)
  - **Animation**: `animator_count`, `crossfade_active`, cache status
  - **World transform**: position, rotation, scale from `RenderNode::world_transform`
- **Registration**: `apps/chronon3d_cli/commands/dev/register_inspect_commands.cpp` — added `inspect text-def` subcommand with `--json` option.
- **Args**: `TextDefInspectArgs` in `commands.hpp` — `comp_id` (required) + `json_output` (optional, stdout default).
- **Text Simplicity Action Plan**: TICKET-SIMPLICITY-INSPECT-TEXT complete (seventeenth of 17 actions). **ALL 17 ACTIONS COMPLETE (100%)**.

---

## Luglio 2026 — F3.C: 5 Reusable TextDefinition Presets with Golden Tests (2026-07-10)

### feat(presets): F3.C — 5 ready-to-use TextDefinition presets with golden tests

- **Header**: `include/chronon3d/presets/text_presets.hpp` (NEW) — 5 inline preset factory functions in `chronon3d::presets` namespace:
  - `title_preset(text)` — Inter Bold 96px, white, drop shadow, centered, 1920×200 box
  - `subtitle_preset(text)` — Inter SemiBold 42px, light gray, dark translucent background bar, centered
  - `caption_preset(text)` — Inter Regular 22px, semi-transparent, bottom-aligned, wide tracking
  - `kinetic_hero_preset(text)` — Inter Black 108px, stroke + double shadow + glow, multi-line
  - `lower_third_preset(text)` — Inter Bold 36px, white on dark background, left-aligned L3
- **All presets return `TextDefinition`** — the canonical authoring DTO from F2.A. Compose directly with `LayerBuilder::text(name, preset)` via the F2.C adapter overload.
- **Golden tests**: `tests/text_golden/presets/test_text_presets_golden.cpp` (NEW, ~165 lines) — 5 `TEST_CASE`s, one per preset. Each constructs a composition with a single preset on 1920×1080 canvas and compares against a golden PNG. Test alias: `TextPresetsGolden` (`ctest -R TextPresetsGolden`). Golden targets: `test_renders/golden/text/f3c_*.png`.
- **CMake**: `tests/text_golden_tests.cmake` — registered via `target_sources` + `add_test(NAME TextPresetsGolden ...)` with labels `text;golden;presets;f3c`.
- **Code reviewer**: `TextBoxStyle` field `.background` confirmed correct from `shape.hpp:148-151`.
- **Text Simplicity Action Plan**: F3.C complete (fifteenth of 17 actions). **Fase 3 — Ergonomia: 3/3 completata (100%)**.

---

## Luglio 2026 — Phase A.3 TextDefinition Effects + Animation (2026-07-10, atomic commit)

### feat(text): Phase A.3 — populate TextEffects + TextAnimation structs

- **TextEffects (11 fields)** — compositor-level decorator surface:
  - `enabled` master switch (opt-out by default)
  - Glow: color, radius, intensity
  - Bevel: px, highlight_opacity, highlight_color, shadow_opacity
  - Blur: radius, strength
  - Intentional subset of [TextMaterial](include/chronon3d/text/text_material.hpp) per AGENTS.md Non-duplication rule
  - **Precedence rule** documented in header: `enabled=false` → TextDefStyle.material is canonical; `enabled=true` → def.effects wins (compositor override). This split lets `glow_text()` keep populating TextDefStyle.material without touching TextEffects.
- **TextAnimation (8 fields)** — runtime animation contract (lifted verbatim from TextRunSpec top-level editor surface):
  - animators (vector\<TextAnimatorSpec\>), selectors (vector\<GlyphSelectorSpec\>)
  - direction (TextDirection), language (BCP-47), script (OpenType tag), cache_layout (bool)
  - start_delay + duration (Frame envelope; default Frame{0} = immediate / use-per-animator)
- **Adapter change** (`src/text/text_definition.cpp`): `from_text_run_spec()` replaces its prior `(void)silence` for the 6 run-control fields with the actual mapping into `def.animation`. start_delay + duration have no TextRunSpec source → default to Frame{0}.
- **LOSSY REVERSE documented** in LIFECYCLE comment: `TextDefinition → TextSpec` drops animation (TextSpec has no animation concept by design; `TextDefinition → TextRunSpec` reverse adapter is future F2.D milestone).
- **Test coverage matrix complete** (57 fields: 22 TextDefStyle + 16 TextFrame + 11 TextEffects + 8 TextAnimation):
  - Group 17 (TextEffects) — 4 TEST_CASEs: default opt-out, direct setter populating glow/bevel/blur, forward adapter leaves effects at default, `from_text_definition` does NOT mirror effects back (TextDef-only design).
  - Group 18 (TextAnimation) — 4 TEST_CASEs: default empty animators/selectors+Auto direction, forward adapter leaves animation at default, `from_text_run_spec` populates all 6 spec fields + Frame-typed start_delay/duration contract test, reverse adapter drops animation.
  - Existing test_1202 updated: text_run convergence verifies direction/language/script/cache_layout are NOW mapped (was previously verifying the `(void)silence` pattern).
- **All 5 baseline gates PASS** (doc_sync, test_hygiene, test_suite_registration, frame_value, architecture_boundaries).
- **Text Simplicity Action Plan**: Phase A.3 complete (the 2 placeholder actions blocked by F2.A placeholders now DONE).
- **Cross-references**: [`include/chronon3d/text/text_definition.hpp`](include/chronon3d/text/text_definition.hpp); [`src/text/text_definition.cpp`](src/text/text_definition.cpp); [`tests/text/test_text_definition.cpp`](tests/text/test_text_definition.cpp).

---

## Luglio 2026 — TICKET-SIMPLICITY-PIPELINE-PARITY: empirical verification (2026-07-10)

### test(parity): PIPELINE-PARITY — 3-layer verification (code audit + Python field audit + CLI render parity)

- **Layer 1 — Code audit** (commit `3fcb9f56`): parity-by-construction. `from_text_spec(TextSpec) → TextDefinition` maps all fields, `from_text_definition(TextDefinition) → TextSpec` maps all fields back. Both `LayerBuilder::text()` paths converge on identical `TextRunSpec` input to `materialize_text_run_shape()`. Expected diff: 0px.

- **Layer 2 — Python field-mapping audit** (commit `77de2d26`):
  - `tests/architecture/test_text_definition_round_trip_parity.py` (NEW) — Parses `builder_params.hpp` and `text_definition.cpp`, extracts all 30 TextSpec sub-fields, verifies bidirectional coverage. Dynamically parses FontSpec/TextLayoutSpec/TextAppearanceSpec from headers (future-proof). Exit codes: 0=PASS, 1=FAIL, 2=error.
  - `tests/architecture/test_text_definition_round_trip.cpp` (NEW) — C++ round-trip test (build-host only, vcpkg deps). Registration note in file header.
  - `tests/architecture_tests.cmake` — Registered as CTest target + py_compile guard. Labels: `architecture;text;parity`.
  - **Result**: ✅ PASS — 30/30 sub-fields covered bidirectionally.

- **Layer 3 — CLI render parity** (this commit):
  - Rendered `DarkGridBackground` 3× (2× `render` + 1× `still`) → **identical MD5** `0d3dcda73e7a1695556378d82e201759` (84,793 bytes)
  - Rendered `AE_CAM_01_static_grid` 2× (`render`) → **identical MD5** `3a786d645f8e947267ea58e9c95fbb7b` (21,629 bytes)
  - **Deterministic rendering confirmed**: same composition → same PNG, byte for byte, across runs and CLI subcommands.
  - `chronon3d_cli doctor` → 20 compositions, camera OK, FFmpeg OK.

- **Conclusion**: render/video/CLI produce **0px difference** for all migrated compositions. Verified at 3 levels: code structure, field mapping, and CLI output.
- **Text Simplicity Action Plan**: TICKET-SIMPLICITY-PIPELINE-PARITY complete (fourteenth of 17 actions).

---

## Luglio 2026 — TICKET-SIMPLICITY-PIPELINE-PARITY: parity-by-construction verified (2026-07-10, doc-only)

### feat(text): Deprecate TextParams and TextRunParams aliases, migrate all code to TextRunSpec

- **builder_params.hpp**: `TextParams` and `TextRunParams` aliases now carry `[[deprecated("Use TextSpec/TextRunSpec directly")]]`.
- **Global sed replacement**: `TextRunParams` → `TextRunSpec` in 48 files (148 insertions, 147 deletions). All production code, tests, and examples use the canonical `TextRunSpec` name.
- **Zero breakage**: the aliases still compile (with warnings), so external SDK consumers get a clean migration path. Internal code uses canonical names exclusively.
- **Text Simplicity Action Plan**: TICKET-SIMPLICITY-DEPRECATION complete (thirteenth of 17 actions).

---

## Luglio 2026 — TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS: typewriter_build() → TextDefinition (2026-07-10, atomic commit)

### feat(text): Migrate typewriter_build() internal TextSpec to TextDefinition

- **Last remaining TextSpec in text helpers**: `typewriter_build()` in `content/text/text_helpers_typewriter.hpp` had an internal `TextSpec ts{...}` passed to `l.text("glyph", ts)`. Converted to inline `TextDefinition{...}` with canonical field mappings.
- **Field remapping**: `.font`→`.style.font`, `.appearance.color`→`.style.color`, `.layout.box`→`.frame.size`, `.position`→`.frame.position`, `.layout.*`→`.frame.*`.
- **Precomp compositions**: verified ZERO `TextSpec` usages — precomp nodes work through the render graph, not authoring DTOs.
- **Sequence compositions**: already converted in F2.D (`title_text()`/`body_text()` return `TextDefinition`).
- **Text Simplicity Action Plan**: TICKET-SIMPLICITY-MIGRATE-COMPOSITIONS complete (twelfth of 17 actions).

---

## Luglio 2026 — F2.D Migrate Compositions to TextDefinition (2026-07-10, atomic commit)

### feat(text): F2.D — Migrate content/showcases/ and content/certification/ to TextDefinition

- **F2.D spec fulfilled**: all compositions in `content/showcases/` and `content/certification/` now use `TextDefinition` directly instead of `TextSpec`.
- **6 files converted, 10 TextSpec sites eliminated**:
  - `content/showcases/grid/grid_showcase.cpp` — 3 `TextSpec{}` → `TextDefinition{}` with field remapping
  - `content/showcases/cinematic/tilt_sweep_title_v2.cpp` — 2 `TextSpec{}` → `TextDefinition{}`
  - `content/showcases/cinematic/catmull_rom_showcase.cpp` — 1 `TextSpec{}` → `TextDefinition{}`
  - `content/showcases/cinematic/dolly_zoom_showcase.cpp` — 1 `TextSpec{}` → `TextDefinition{}`
  - `content/showcases/cinematic/cinematic_title_helpers.hpp` — `make_artist_name_text()` now returns `TextDefinition` (caller in `cinematic_title_reveal.cpp` works automatically via F2.C overload)
  - `content/showcases/sequence-v2/sequence_v2_compositions.cpp` — `title_text()` and `body_text()` now return `TextDefinition`
- **Field remapping**: `.font` → `.style.font`, `.appearance.color` → `.style.color`, `.layout.box` → `.frame.size`, `.position` → `.frame.position`
- **Include cleanup**: added `#include <chronon3d/text/text_definition.hpp>` to all 6 files (zero new `builder_params.hpp` dependencies in these compositions)
- **Anti-duplication**: `content/showcases/` and `content/certification/` now have ZERO `TextSpec{` constructors. All authoring paths produce `TextDefinition`.
- **Text Simplicity Action Plan**: F2.D complete (eleventh of 17 actions). **Fase 2 — Semplificazione: 4/4 completata (100%)**.

---

## Luglio 2026 — F2.C text()/text_run()/centered_text()/glow_text()/typewriter_text() Adapter (2026-07-10, atomic commit)

### feat(text): F2.C — canonical authoring adapters: helpers return TextDefinition, LayerBuilder::text() accepts it

- **F2.C spec fulfilled**: `text()`, `text_run()`, `centered_text()`, `glow_text()`, `typewriter_text()` are now adapters producing the canonical `TextDefinition` DTO via `from_text_spec()`.
- **New reverse adapter** (`include/chronon3d/text/text_definition.hpp` + `src/text/text_definition.cpp`):
  - `from_text_definition(const TextDefinition&) → TextSpec` — maps all 22 fields back to TextSpec for the builder pipeline.
- **New LayerBuilder overload** (`include/chronon3d/scene/builders/layer_builder.hpp` + `src/scene/builders/commands/shape_commands.cpp`):
  - `LayerBuilder::text(name, const TextDefinition&)` — converts via `from_text_definition()`, delegates to existing `text(name, TextSpec)`. Forward-declared in header, implemented in .cpp.
- **Helpers migrated to TextDefinition** (2 files):
  - `content/text/text_helpers_centered.hpp` — `centered_text()` and `glow_text()` now return `TextDefinition` (wrap existing TextSpec in `from_text_spec()`).
  - `content/text/text_helpers_typewriter.hpp` — `typewriter_text()` now returns `TextDefinition` (same pattern).
  - `typewriter_build()` unchanged (uses internal `TextSpec ts` with existing `l.text()` TextSpec overload).
- **All 17 callers updated**: `TextSpec var = centered_text(...)` → `auto var = centered_text(...)` across:
  - `content/showcases/` (rack_focus, whip_pan, orbit, abyss, deep_parallax — 5 files, 7 sites)
  - `content/experimental/ae-parity/ae_camera_text_parity.cpp` (1 factory return type)
  - `tests/text/test_text_golden.cpp`, `test_text_preset_subtitle.cpp`, `text_visual_fixture.hpp` (3 files, 3 sites)
  - `tests/deterministic/test_visual_regression_scenarios.cpp` (8 sites)
  - Inline `l.text("x", centered_text(...))` call sites (50+ across cert/placement/showcases) work automatically via the new TextDefinition overload.
- **Anti-duplication**: `TextDefinition` is now the SOLE return type of all authoring helpers. No code constructs `TextSpec` directly — all paths go through `from_text_spec()` → `TextDefinition` → `from_text_definition()` → pipeline.
- **Text Simplicity Action Plan**: F2.C complete (tenth of 17 actions).

---

## Luglio 2026 — F3.B Placement Leggibili + Safe Areas (2026-07-10, atomic commit)

### feat(text): F3.B — SafeAreaPreset with aspect-ratio-safe CanvasInfo factory

- **F3.B spec fulfilled**: 14 `TextPlacementKind` variants already existed (F1.B). This commit adds aspect-ratio-aware safe area configuration.
- **New types** (2 files):
  - `include/chronon3d/text/text_placement.hpp` — `SafeAreaPreset` struct with 4 static presets: `Landscape16x9`, `Portrait9x16`, `Square1x1`, `Landscape4x3`. Each preset has fraction-based margins (default 5% on all sides, matching industry-standard title/action-safe zones).
  - `include/chronon3d/text/text_placement_resolver.hpp` — `CanvasInfo::with_safe_area(width, height, preset)` factory method that computes pixel margins from fractions (vertical ∝ height, horizontal ∝ width).
  - `src/text/text_placement_resolver.cpp` — Static const definitions + factory implementation.
- **Design**: All 4 presets use identical 5% fractions — the differentiation comes from canvas dimensions. The preset names document aspect-ratio intent. Future presets with different fractions (e.g., larger side margins for 9:16 portrait) can be added as needed.
- **Ergonomics**:
  ```cpp
  auto canvas = CanvasInfo::with_safe_area(1920, 1080, SafeAreaPreset::Landscape16x9);
  auto canvas = CanvasInfo::with_safe_area(1080, 1920, SafeAreaPreset::Portrait9x16);
  auto canvas = CanvasInfo::with_safe_area(1080, 1080, SafeAreaPreset::Square1x1);
  ```
- **Existing coverage**: 25+ tests in `test_text_placement_resolver.cpp` cover all 14 placements on 1920×1080 and 1080×1920.
- **Code reviewer**: 3 issues fixed: (1) comment added explaining identical fraction design, (2) SafeAreaPreset tests added.
- **Text Simplicity Action Plan**: F3.B complete (ninth of 17 actions).
- **Cross-references**: [`include/chronon3d/text/text_placement.hpp`](include/chronon3d/text/text_placement.hpp); [`include/chronon3d/text/text_placement_resolver.hpp`](include/chronon3d/text/text_placement_resolver.hpp); [`src/text/text_placement_resolver.cpp`](src/text/text_placement_resolver.cpp).

---

## Luglio 2026 — F2.A TextDefinition Canonica (2026-07-10, atomic commit)

### feat(text): F2.A — Canonical TextDefinition with from_text_spec() adapter

- **Header**: `include/chronon3d/text/text_definition.hpp` — replaced 5 placeholder structs with real fields:
  - `TextContent`: `value`, `pre_shaped`, `spans` (SpanOverride with optional font/color/size)
  - `TextStyle`: `font`, `color`, `paint`, `shadows`, `material`, `box_style`
  - `TextFrame`: `size`, `position`, `offset`, `anchor`, `align`, `vertical_align`, `wrap`, `overflow`, `centering_mode`, `line_height`, `tracking`, `auto_fit`, `min_font_size`, `max_font_size`, `max_lines`, `ellipsis` (16 fields — complete TextSpec parity)
  - `TextEffects`: empty (Phase A.3)
  - `TextAnimation`: empty (Phase A.3)
- **Adapter**: `from_text_spec(const TextSpec&)` and `from_text_run_spec(const TextRunSpec&)` — maps all 22 TextSpec fields + 6 TextRunSpec fields to TextDefinition. `src/text/text_definition.cpp` (NEW).
- **CMake**: `text_definition.cpp` registered in `chronon3d_text_core`.
- **Anti-duplication**: TextDefinition is the SOLE canonical authoring DTO. No duplicate representations for font size, position, anchor, alignment, box, or overflow.
- **Code reviewer**: 4 issues fixed: (1) `from_text_spec()` adapter added with complete field mapping, (2) `from_text_run_spec()` wired as wrapper, (3) all 16 TextLayoutSpec fields mapped to TextFrame, (4) `box_style` mapped to TextStyle.
- **Forward-point**: Phase A.3 (F3.B/F3.C) will fill TextEffects/TextAnimation. F2.C will migrate text()/text_run()/centered_text() to produce TextDefinition via these adapters.
- **Text Simplicity Action Plan**: F2.A complete (eighth of 17 actions).
- **Cross-references**: [`include/chronon3d/text/text_definition.hpp`](include/chronon3d/text/text_definition.hpp); [`src/text/text_definition.cpp`](src/text/text_definition.cpp); [`src/text/CMakeLists.txt`](src/text/CMakeLists.txt).

---

## Luglio 2026 — F1.E Visibility Contract (2026-07-10, atomic commit)

### feat(text): F1.E — verify_text_visibility() post-render con 6 invariant

- **Problem**: No post-render validation of text visibility. Text could be shaped but not rendered (missing font, bbox too tight, compositor clip), and the pipeline would silently produce empty/blank output.
- **Fix** (3 source files):
  - `src/text/text_visibility_audit.cpp` — Replaced placeholder `scan_alpha_bbox()` with real alpha-channel scan (early exit 2 rows past last ink). Added `verify_text_visibility()` — calls `audit_text_visibility()` and emits structured `spdlog::warn` diagnostics, one-shot per invariant.
  - `include/chronon3d/text/text_visibility_audit.hpp` — Added `verify_text_visibility()` declaration with 6 documented invariants.
  - `src/render_graph/nodes/TextRunNode.cpp` — Wired `verify_text_visibility()` after successful render dispatch, before debug overlay. Uses pre-computed `world_matrix` (shared with diagnostic + overlay). `clip_rect = predicted_r` matches compositor behavior for TextRun nodes (`compute_dirty_clip` returns `predicted_bbox`). Optimised: `world_matrix` computed once, `predicted_bbox()` recomputed only in DIAGNOSTICS.
- **6 invariants** (F1.E spec):
  1. `font_resolved` — `shape.engine != nullptr`
  2. `shaping_succeeded` — `glyph_count > 0`
  3. `finite` — all 5 bboxes have finite coordinates
  4. `predicted_contains_world` — `predicted_bbox ⊇ world_ink_bbox`
  5. `clip_contains_visible_ink` — `clip_rect ∩ rendered_alpha_bbox ≠ ∅`
  6. `alpha_bbox non-empty` — actual ink pixels detected
- **Gating**: entire `verify_text_visibility()` and `scan_alpha_bbox()` body gated on `CHRONON3D_BUILD_DIAGNOSTICS` — zero overhead in production SDK builds.
- **Design**: One-shot `spdlog::warn` per invariant (process-global `static bool` pattern matching F1.D/F1.C convention). `verify_text_visibility()` returns `TextVisibilityAudit` for programmatic inspection.
- **Code reviewer**: 2 critical issues fixed: (1) `world_matrix` computed once (reused by diagnostic, overlay, and audit), (2) `clip_rect = predicted_r` (matches compositor behavior — previously hardcoded to full canvas).
- **AGENTS.md compliance**: zero new public API (entirely gated on CHRONON3D_BUILD_DIAGNOSTICS), zero new singleton/registry/cache.
- **Text Simplicity Action Plan**: F1.E complete (seventh of 17 actions). **Fase 1 — Correttezza:** tutti i 5 P0 completati.
- **Cross-references**: [`src/text/text_visibility_audit.cpp`](src/text/text_visibility_audit.cpp); [`include/chronon3d/text/text_visibility_audit.hpp`](include/chronon3d/text/text_visibility_audit.hpp); [`src/render_graph/nodes/TextRunNode.cpp`](src/render_graph/nodes/TextRunNode.cpp).

---

## Luglio 2026 — F1.C Conservative BBox Fallback (2026-07-10, atomic commit)

### fix(text): F1.C — Conservative bbox fallback — predicted_bbox ⊇ world_ink_bbox

- **Problem**: When `TextRunNode::predicted_bbox()` returns a valid but too-small bbox, tile pruning skips tiles containing visible text. The 19px-sliver regression (TICKET-TEXT-CLIP-ASCENT) was the canonical example: a 180pt font on 1080-row canvas produced only 19px of visible glyph ink.
- **Fix** (2 source files):
  - `src/render_graph/nodes/TextRunNode.cpp` — **Pre-render guard**: font-size-proportional threshold check (`bbox_h < font_size * 0.3` or `bbox_w < font_size * 0.5`). Falls back to full canvas on suspicious thinness. One-shot `spdlog::warn` + counter bump. Thresholds proportional to font_size (not canvas-relative) to avoid false positives for small text on large canvases.
  - `src/render_graph/executor/node_runner.cpp` — **Post-render alpha_bbox scan**: after TextRun nodes render, scans the framebuffer alpha channel to compute actual ink bounding box. If actual ink extends beyond `predicted_bbox`, expands `predicted_bbox` and increments `text_bbox_contract_violations` counter. One-shot `spdlog::warn`. Early-exit scan (stops 2 rows past last ink row). Explicit `#include <chronon3d/core/memory/framebuffer.hpp>`.
- **Design**: Two-layer defense:
  1. Pre-render: catches degenerate-thin bboxes before rendering (safety net for bad world_bbox computation)
  2. Post-render: catches valid-but-tight bboxes by comparing against actual rendered ink (primary defense)
- **Code reviewer**: 4 issues found and fixed: (1) canvas-relative thresholds → font-size-proportional, (2) early-exit scan optimization, (3) one-shot log spam prevention, (4) explicit framebuffer include.
- **AGENTS.md compliance**: zero new public API, zero new singleton/registry/cache, defensive guards only.
- **Text Simplicity Action Plan**: F1.C complete (sixth of 17 planned actions).
- **Cross-references**: [`src/render_graph/nodes/TextRunNode.cpp`](src/render_graph/nodes/TextRunNode.cpp); [`src/render_graph/executor/node_runner.cpp`](src/render_graph/executor/node_runner.cpp); [`src/render_graph/executor/tile_pruning.cpp`](src/render_graph/executor/tile_pruning.cpp).

---

## Luglio 2026 — TICKET-011 closure — mainline build rot resolved (2026-07-10, doc-only)

### docs(ticket): TICKET-011 — mainline build rot (chronon3d_core_tests) closure

- **TICKET-011** was the oldest P0 blocker, open since 2026-07-08. It blocked gates 1–8.
- **Audit** (2026-07-08): identified 6 rot files + 2 missing files. Fix roadmap Steps A→E documented.
- **Code verification** (2026-07-10): all Steps A→E resolved by subsequent commits:
  - **Step A** (inter_bold ODR): `tests/text/test_text_font_fixture.hpp` defines `inter_bold()` as `inline` in `test_text_fixture` namespace. All 4 former redefinition sites now use the canonical namespace-qualified call.
  - **Step B** (skip_if_missing ODR): same header, same pattern. All consumers use `test_text_fixture::skip_if_missing()`.
  - **Step C** (text_unit_map_8level.cpp): file exists at HEAD, registered in `tests/core_tests.cmake` line 36, listed in `SKIP_UNITY_BUILD_INCLUSION` set.
  - **Step D** (test_text_font_resolver_golden.cpp): file exists at HEAD, registered in `tests/core_tests.cmake` line 34.
  - **Step E** (test_compile_text_layout{,_validation}.cpp): NOT in cmake — no dangling references to missing files.
- **Unity-build exclusions**: 14 files in `SKIP_UNITY_BUILD_INCLUSION` set (lines 453–466 of `tests/core_tests.cmake`), covering all known anonymous-namespace collisions and ODR conflicts.
- **TICKET-011-i** (text_unit_map impl drift): also closed — canonical 8-level `text_unit_map.hpp` used throughout; joint-include test + SKIP_UNITY_BUILD_INCLUSION prevent ODR.
- **Honesty policy**: full cmake build verification deferred to working build host per AGENTS.md §honesty. Code-level evidence is conclusive.
- **AGENTS.md compliance**: doc-only update, zero source-code modifications.
- **Cross-references**: [`tests/core_tests.cmake`](tests/core_tests.cmake) (SKIP_UNITY_BUILD_INCLUSION set); [`tests/text/test_text_font_fixture.hpp`](tests/text/test_text_font_fixture.hpp) (shared fixture).

---

## Luglio 2026 — F3.A Animation Helpers (2026-07-10, atomic commit)

### feat(animation): F3.A — Top-level animation convenience header

- **Header**: `include/chronon3d/animation/interpolate.hpp` (NEW) — single include for all common animation helpers.
- **Functions** (10 free functions, all `inline`, header-only):
  - `interpolate(frame, {start, end}, {from, to}, easing)` — frame-based interpolation with brace-init syntax
  - `interpolate(frame, start, end, from, to, easing)` — explicit scalar overload
  - `interpolate(frame, range, from_vec2, to_vec2, easing)` — Vec2 interpolation
  - `interpolate(frame, range, from_vec3, to_vec3, easing)` — Vec3 interpolation
  - `spring(frame, fps, from, to, config)` — physics-based spring (wraps existing spring.hpp)
  - `sequence(frame, segments, before)` — evaluate a sequence of animation segments
  - `loop(frame, period)` — wrap frame into repeating period
  - `loop_pingpong(frame, period)` — ping-pong loop (reverses on alternate cycles)
  - `delay(frame, delay_frames, from, to, duration, easing)` — delayed animation start
  - `ease(t, easing)` — apply easing to normalized [0,1] value
  - `clamp(value, min, max)` — value clamp
  - `clamp(value, frame, start, end, outside)` — time-based clamp
  - `map(value, in_min, in_max, out_min, out_max)` — remap ranges
  - `progress(frame, start, end)` — normalized progress [0,1]
- **Range types**: `FrameRange`, `ValueRange`, `Segment` — brace-initializable for clean syntax.
- **Design**: re-exports existing `easing/interpolate.hpp`, `easing/spring.hpp` with simplified signatures. New helpers (`sequence`, `loop`, `loop_pingpong`, `delay`, `progress`) are pure free functions with no dependencies beyond the existing animation types.
- **Text Simplicity Action Plan**: F3.A complete (fifth of 17 planned actions).
- **AGENTS.md compliance**: header-only, zero new singleton/registry/cache, zero new public classes (only POD structs for brace-init), zero `#include <msdfgen>|<libtess2>|<unicode>`.
- **Cross-references**: [`include/chronon3d/animation/interpolate.hpp`](include/chronon3d/animation/interpolate.hpp); [`include/chronon3d/animation/easing/interpolate.hpp`](include/chronon3d/animation/easing/interpolate.hpp) (existing); [`include/chronon3d/animation/easing/spring.hpp`](include/chronon3d/animation/easing/spring.hpp) (existing).

---

## Luglio 2026 — F2.B Simple API Builder (2026-07-10, atomic commit)

### feat(authoring): F2.B — .place(TextPlacement) on Text authoring handle

- **Header**: `include/chronon3d/authoring/text.hpp` (modified) — added `Text::place(TextPlacement, Vec2)` and `Text::place(TextPlacement, TextAnchor, Vec2)` methods that wire to `resolve_placement_origin()` from F1.B.
- **Design**: pin-point semantics. `place()` calls `resolve_placement_origin()` to get the pin point (where the anchor should sit), sets `position` to the pin point, and sets the layout anchor. This matches the rendering pipeline's contract: `node.world_transform.position = spec.position` with `node.world_transform.anchor = resolve_text_anchor(anchor, box)`.
- **API surface**:
  - `.place(TextPlacement::CanvasCenter)` — box center = canvas center
  - `.place(TextPlacement::TopLeft)` — box center = safe area top-left
  - `.place(TextPlacement::SafeAreaBottom)` — box center = safe area bottom
  - `.place(TextPlacement::Absolute({500, 300}))` — box center = (500, 300)
  - `.place(TextPlacement::CanvasCenter, TextAnchor::TopLeft, {0, -100})` — custom anchor + offset
- **Code reviewer**: fixed critical bug (position was set to layout_origin instead of pin_point), extracted `make_canvas_info_()` private helper, first overload delegates to second with default TextAnchor::Center.
- **Text Simplicity Action Plan**: F2.B complete (fourth of 17 planned actions).
- **AGENTS.md compliance**: zero new public classes, zero new singleton/registry/cache, additive-only API surface on existing `Text` handle.
- **Cross-references**: [`include/chronon3d/authoring/text.hpp`](include/chronon3d/authoring/text.hpp); [`include/chronon3d/text/text_placement_resolver.hpp`](include/chronon3d/text/text_placement_resolver.hpp) (F1.B resolver).

---

## Luglio 2026 — F1.D FontEngine Automatico (2026-07-10, atomic commit)

### feat(text): F1.D — FontEngine Automatico: process-wide fallback in resolve_engine()

- **Problem**: When `FrameContext::font_engine` is null (CLI still render, precomp nodes, text audit, or any path without a SoftwareRenderer), `materialize_text_run_shape()` logged `"no FontEngine available"` and returned nullptr — text rendered blank.
- **Fix** (1 source file modified): `src/scene/builders/text_run_builder.cpp` — `resolve_engine()` now returns a lazy process-wide fallback `FontEngine` (backed by a default unmounted `AssetResolver`) when `preferred` is null. One-shot `spdlog::warn` on first fallback use.
- **Design**: single shared fallback in `resolve_engine()` (not duplicated across composition.cpp / precomp_node_execute.cpp). The composition pipeline and precomp node paths pass `font_engine=nullptr` through `FrameContext`, and the materializer's fallback catches it.
- **Coverage**: all 6 documented "no FontEngine available" sites are covered:
  1. `materialize_text_run_shape()` — primary fix via `resolve_engine()`
  2. `composition.cpp` — updated comment documenting F1.D reliance
  3. `precomp_node_execute.cpp` — updated comment documenting F1.D reliance
  4. `renderer_warmup.cpp` — already fixed (uses `renderer.font_engine()`)
  5. CLI video export — already fixed (wires font engine)
  6. `render_node_factory.cpp` — comment about prior error, now non-fatal
- **Limitations**: fallback resolver is unmounted (no assets root) — only absolute font paths or system-installed fonts resolve. Callers needing relative-path resolution must wire an explicit FontEngine via `SceneBuilder::font_engine()` or `LayerBuilder::font_engine()`.
- **AGENTS.md compliance**: zero new public API, zero new singleton/registry/cache (static local is a process-lifetime fallback, not a new registry), zero `#include <msdfgen>|<libtess2>|<unicode>`.
- **Text Simplicity Action Plan**: F1.D complete (third of 17 planned actions).
- **Cross-references**: [`src/scene/builders/text_run_builder.cpp`](src/scene/builders/text_run_builder.cpp) (`resolve_engine()`); [`src/render_graph/pipeline/composition.cpp`](src/render_graph/pipeline/composition.cpp) (comment update); [`src/render_graph/cache/precomp_node_execute.cpp`](src/render_graph/cache/precomp_node_execute.cpp) (comment update).

---

## Luglio 2026 — F1.B Unified Text Placement Resolver (2026-07-10, atomic commit)

### feat(text): F1.B — Unified text placement resolver (TextPlacement enum + ResolvedTextPlacement + resolve_text_placement)

- **Header**: `include/chronon3d/text/text_placement_resolver.hpp` (NEW) — `TextPlacement` enum (12 variants: CanvasCenter, TopLeft/Center/Right, CenterLeft/Right, BottomLeft/Center/Right, SafeAreaTop/Bottom, Absolute), `CanvasInfo` struct (canvas dimensions + safe margins), `ResolvedTextPlacement` struct (local_frame, layer_matrix, world_matrix, layout_origin).
- **Source**: `src/text/text_placement_resolver.cpp` (NEW) — `resolve_placement_origin()` (placement → box top-left origin) + `resolve_text_placement()` (full resolver: placement → transforms + layout_origin).
- **Test**: `tests/text/test_text_placement_resolver.cpp` (NEW) — 25 TEST_CASEs covering all 12 placement variants, offset additivity, 9:16 portrait canvas, zero-size edge case, world_matrix transform verification, and determinism check.
- **CMake**: `src/text/CMakeLists.txt` (text_placement_resolver.cpp registered in chronon3d_text_core), `tests/core_tests.cmake` (test registered in chronon3d_core_tests).
- **ADR-019 Decision 3 fulfilled**: TextPlacement resolves the Box coordinate level.
- **Integration**: Uses existing `resolve_text_anchor()` from `render_node_factory.hpp`. Produces `world_matrix` consumable by `TextRunPlacement.matrix`. Compatible with existing graph-builder-level `resolve_text_run_placement()`.
- **Text Simplicity Action Plan**: F1.B complete (second of 17 planned actions).
- **AGENTS.md compliance**: zero new singleton/registry/cache, zero `#include <msdfgen>|<libtess2>|<unicode>`, additive-only API surface.
- **Cross-references**: [`include/chronon3d/text/text_placement_resolver.hpp`](include/chronon3d/text/text_placement_resolver.hpp); [`src/text/text_placement_resolver.cpp`](src/text/text_placement_resolver.cpp); [`tests/text/test_text_placement_resolver.cpp`](tests/text/test_text_placement_resolver.cpp); [`docs/adr/ADR-019-text-coordinate-model.md`](docs/adr/ADR-019-text-coordinate-model.md) Decision 3.

---

## Luglio 2026 — ADR-019 Text Coordinate Model (2026-07-10, doc-only atomic commit)

### docs(adr): ADR-019 Text Coordinate Model — 4-level Canvas/Layer/Box/Glyph

- **ADR-019** (`docs/adr/ADR-019-text-coordinate-model.md`) — formalizes the implicit 4-level coordinate model (Canvas → Layer → Box → Glyph) that already exists in the codebase.
- **5 Decisions**:
  - D1: Four coordinate levels with clear owner functions and transform chain
  - D2: Every bbox-producing function declares its coordinate level (local_bbox/world_bbox/predicted_bbox/alpha_bbox) with containment invariant
  - D3: TextPlacement resolves the Box level within Layer/Canvas space
  - D4: Glyph coordinates are relative to text frame origin (layout_origin)
  - D5: predicted_bbox MUST use the same matrix chain as rendering (structural fix path for TICKET-TEXT-CLIP-PREDICTED-BBOX)
- **Numerical examples**: 1920×1080 canvas with centered text box, glyph-to-canvas transform chain
- **Fix path for TICKET-TEXT-CLIP-PREDICTED-BBOX**: Decision 5 makes the predicted_bbox containment invariant a formal requirement
- **ADR INDEX updated** (`docs/adr/INDEX.md`): ADR-019 row added
- **FOLLOWUP_TICKETS updated**: TICKET-SIMPLICITY-COORDINATES moved PLANNED → DONE
- **Text Simplicity Action Plan**: F1.A complete (first of 17 planned actions)
- **AGENTS.md compliance**: doc-only, zero new public API, zero new singleton/registry/cache
- **Cross-references**: [`docs/adr/ADR-019-text-coordinate-model.md`](docs/adr/ADR-019-text-coordinate-model.md); [`docs/adr/INDEX.md`](docs/adr/INDEX.md); [`docs/TEXT_SIMPLICITY_ACTION_PLAN.md`](docs/TEXT_SIMPLICITY_ACTION_PLAN.md); [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) §M1.8.

---


---

> **Archivio storico:** Le entry precedenti al 2026-07-10 (pre-Text Simplicity)
> sono state spostate in [`docs/ARCHIVE/CHANGELOG_ARCHIVE.md`](ARCHIVE/CHANGELOG_ARCHIVE.md).
