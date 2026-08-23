# TICKET-SUBTITLE-PRODUCTIVE-FOUNDATION — Productive subtitle authoring

## Stato: DONE (2026-07-20)

## Problema

Orphan productive-subtitle WIP parked in stash@{0}/stash@{1} after the matrix
cinematic+reveal recovery session (2026-07-12). The orchestrator
(`SubtitleTrackBuilder` + `Layer::subtitles(track)` + 4 new Subtitle-category
presets + 10 TEST_CASEs in `test_subtitle_productive.cpp`) was complete on
disk but uncommitted. `subtitle_from_vtt` test failed until a850303d patched
VTT parser end_part leading-whitespace truncation bug.

## Soluzione

Cherry-pick the full 11-file chore (7 MOD + 4 NEW) onto a850303d:

- include/chronon3d/authoring/subtitle_track_builder.hpp — NEW: fluent `SubtitleTrackBuilder(...)...build()`.
- src/scene/subtitle_track_builder.cpp — NEW: `build()` impl. Implementation recovered from `.git/objects/` blob 4e377170d49adebbc42bf42546ceb53caaa384ae (dangling blob; content intact).
- include/chronon3d/presets/text/subtitle.hpp — UPGRADE: 547-byte old version → `using SubtitleTrack = TimedTextDocument;` aliases + `WordStyleState` + 6 inline helpers.
- include/chronon3d/authoring/layer.hpp — adds `Layer::subtitles(track)`.
- include/chronon3d/authoring/text_span_builder.hpp — refactor to span_index_ + `Text::current_span()` (friend).
- src/registry/text_preset_factories_basic.cpp — 4 new Subtitle presets: karaoke_fill / active_word_pop / subtitle_card / lower_third_safe.
- cmake/Chronon3DAuthoringPublicHeaders.cmake — register headers.
- src/scene/CMakeLists.txt — adds subtitle_track_builder.cpp to OBJECT lib.
- tests/text/test_subtitle_productive.cpp — NEW: 10 TEST_CASEs.
- tests/subtitle_productive_tests.cmake — NEW.
- tests/manifests/test_definitions.cmake — adds subtitle_productive_tests.cmake entry.

Inline-fix: removed `for (word) (void)word;` no-op loop in `SubtitleTrackBuilder::build()`; forward-point to TICKET-SUBTITLE-WORD-KARAOKE.

## §honest-discipline Process notes

Shipped via `git commit <14-paths>` explicit-paths bypass for a phantom-staging UI quirk in this session.

Stale `build/tests` binary deleted pre-ship. `build/` is the canonical configured build dir.

## Evidenza

- 10/10 TEST_CASEs pass + 58/58 assertions pass (this chore, this SHA, post-rebuild on `build/`).
- 14 paths shipped.
- Rebuild from `build/` (top-level CMakeCache.txt + build.ninja).

## Forward-points (Cat-3 anti-dup)

- TICKET-SUBTITLE-WORD-KARAOKE — implement per-word `TextSpanOverride` attach in `SubtitleTrackBuilder::build()` (deferred this chore).
- TICKET-GOLDEN-MATRIX-CINEMATIC-REVEAL — matrix tests in stash@{1} (separate chore per §Fare PR piccole e mirate).
- TICKET-PHANTOM-STAGING-INVESTIGATION — diagnose the `git ls-files --stage` vs `git status -sb` UI quirk (forward-point for next session).
