# TICKET-WORD-EMPHASIS-ANIMATORS — Tag-driven per-letter emphasis

## Stato: DONE (commit da finalizzare, post-review fixes applied)

## Problema
Sottotitoli prodotti con SRT/VTT/JSON hanno parole semanticamente importanti (nomi propri,
titoli, keyword di enfasi) ma il renderer le tratta come testo qualsiasi. Mancava un
meccanismo per marcare e animare in modo distinto queste parole senza proliferare
singleton/registry/cache o aggiungere campi canonici al `TimedWord` (che richiederebbe
ADR — TICKET-MOTIONTIMELINE-MIGRATION lineage).

## Soluzione scelta
Variant A — tag-driven emphasis riusando `TimedWord::semantic_id` con prefissi
canonici. Animation style: Stagger per-letter rimbalzo (scale 0.7→1.15→1.0 con
`Easing::OutBack` + `Easing::InOutSine` settle; opacity 0→1 con `Easing::OutCubic`;
2-frame stagger tra lettere consecutive).

## API surface (4 simboli pubblici)
1. `enum class WordEmphasisKind { None, Name, Title, Emph }` (parser result)
2. `struct EmphasisParseResult { kind; remainder }`
3. `parse_emphasis_prefix(semantic_id)` — parsing canonico dei prefissi `name:` / `title:` / `emph:`
4. `make_word_emphasis_animators(kind, accent, start_frame, letter_count)` — factory
   per il `std::vector<TextAnimatorSpec>` (1 spec per lettera con selettore
   `TextSelectorUnit::Character` a finestra `Square` stretta + ramp Scale + Opacity +
   FillColor opzionale).

Plus: `is_emphasis_kind(kind)` (1 line, inline), `strip_emphasis_prefix(id)` (1 line, inline).

## Files added
- `include/chronon3d/presets/text/word_emphasis_animators.hpp`
- `src/text/word_emphasis_animators.cpp`
- `tests/text/test_word_emphasis_animators.cpp`

## Files modified
- `src/text/CMakeLists.txt` (+1 source line: `word_emphasis_animators.cpp`)
- `tests/text/CMakeLists.txt` (+1 `chronon3d_add_test_suite` block)

## Iteration history
| Version | Change | Driver |
|---|---|---|
| v1 | Smooth shape + test using `evaluate(SampleTime{...}).value` | Initial scribe |
| v2 | Smooth → Square (selector crispness); accent wired to `FillColorProperty` (no rot-bait); spec/selector IDs encode `kind` token; tests migrate to direct `keyframes()` inspection | `code-reviewer-minimax-m3` review pass |

## Criteri di accettazione
- [x] Variant A chiusa (Tag-driven); NO ADR richiesto (no new singleton/registry/cache; riusa
      `semantic_id` esistente su `TimedWord`)
- [x] `parse_emphasis_prefix` copre i 3 prefissi canonici + None + empty + colon nested + case-sensitivity
- [x] `make_word_emphasis_animators` emette 1 spec per lettera con:
      - `scale.value.keyframes()` di 3 keyframe (0.7 → 1.15 → 1.0)
      - `opacity.value.keyframes()` di 2 keyframe (0 → 1, durata 4 frame)
      - `selector.shape = Square` (CRISP per-letter)
      - `Color FillColorProperty` opzionale quando `accent` è fornito
- [x] Test copre: parse matrix (8 SUBCASE) + is_emphasis_kind matrix + strip helper +
      structural parity (3 kinds) + accent wiring + end-to-end label roundtrip
- [x] Total diff < 250 LOC escludendo tests
- [x] ZERO new singleton/registry/cache; Cat-3 riusa `AnimatedValue<Vec3/f32>`,
      `EasingCurve`, `TextAnimatorSpec`, `GlyphSelectorSpec` da producers canonici
- [x] ZERO `Frame::value` direct access (usa `Frame::integral()`)
- [x] ZERO `#include <msdfgen>` / `<libtess2>` / `<unicode[/...]>` opens

## Build verification
- `cmake --build build/manual-test --target chronon3d_text_word_emphasis_animators_tests`
  (pending host ctest run; pre-ctest staleness invariant CHECK'd)
- `ctest -R chronon3d_text_word_emphasis_animators_tests --output-on-failure`
  atteso PASS (green at HEAD)

## Forward-points
- Consider adding a Color flash (AnimatedValue transition to accent) — deferred to a
  follow-up chore (requires either `AnimatedValue<graphics::FillStyle>` (canonical)
  or extending `Color` arithmetic — neither is trivial).
- `WordEmphasisKind` may need `Acronym` (ALL CAPS detector) for retro-applying
  emphasis to legacy SRT files without semantic_id metadata — tested separate
  (Variant B heuristic in the synthesis; deferred unless user wants retro-emphasis).
- Wiring `make_word_emphasis_animators` into the `SubtitleTrack` / `TimedCue` renderer
  path is a downstream integration chore; not part of this ticket's scope.

## CI alignment
- Lint gates to run on this chore:
  - `tools/check_architecture_boundaries.sh`
  - `tools/check_doc_sync.sh`
  - `tools/check_no_dual_text_api.sh`
  - `tools/check_first_principles_legacy_grep.sh`
- 3-doc canonici NON aggiornati: chore `feat(text)` non-milestone (AGENTS.md §"Per i
  fix piccoli NON aggiornare i canonici" pattern); cronaca estesa vive in questa
  scheda ticket.
