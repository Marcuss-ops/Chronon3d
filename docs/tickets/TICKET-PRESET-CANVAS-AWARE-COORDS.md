# TICKET-PRESET-CANVAS-AWARE-COORDS — Canvas-aware v1 text preset verification

## Stato: DONE (2026-07-15, commit <this commit>)

## Problema

La roadmap `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` Blocco 4 (`M2 — Responsive text`) richiede che i preset testuali siano canvas-aware: stesso template deve girare su 16:9, 9:16, 1:1, 4K, custom senza tagliare testo o uscire dalla safe area. Il ticket canonicale `TICKET-TEXT-PRODUCTION-STATUS-CORRECTION.md` §2 elenca come PLANNED: "Preset responsive (zero 1920×1080 hardcoded; `CanvasInfo + SafeArea + TextBoxConstraints + AspectRatioPolicy` per preset)".

Audit pre-chore (2026-07-15):

* I 5 preset v1 in `include/chronon3d/presets/text/text_presets_v1.hpp` (`title_centered`, `subtitle_bottom`, `caption_safe_area`, `kinetic_word`, `lower_third`) **sono già canvas-aware**: accettano `CanvasInfo` + `TextBoxConstraints` + `AspectRatioPolicy`, e usano `TextPlacementKind::{CanvasCenter, SafeAreaBottom, SafeAreaCenter, BottomLeft}` (mai `Absolute`). La migrazione è avvenuta in `TICKET-SIMPLICITY-PRESETS` (DONE 2026-07-10).
* I 4 tipi canonici **esistono**: `CanvasInfo` (in `text_placement.hpp` + `resolve_text_placement.hpp`), `SafeAreaPreset` (4 aspect-ratio presets: Landscape16x9 / Portrait9x16 / Square1x1 / Landscape4x3), `TextBoxConstraints` + `AspectRatioPolicy` (in `preset_constraints.hpp`).
* `tests/text/test_presets_stability.cpp` ha 5 TEST_CASEs × 3 CHECKs = 15 assertions (più i sotto-casi per 9:16 e 1:1), ma **NON è ERT-parameterized** con `DOCTEST GENERATE()` e **NON** verifica safe-area bounds o custom-resolution coverage.
* Coordinate 1920×1080 / 960 / 540 hardcoded **esistono ancora** in content cinematic helpers (`content/showcases/cinematic/cinematic_title_helpers.hpp:43-45`) + content compositions + tests, ma NON nei 5 preset v1 stessi. La copertura scope di questo ticket è limitata ai 5 preset v1.

## Soluzione

Aggiunto un NUOVO file di test `tests/text/test_preset_canvas_aware_presets.cpp` con pattern DOCTEST `GENERATE()` che parametra 5 canvas configurations × 5 preset × 3 invariants (75 CHECK assertions):

| Invariante | Significato                                              | Failure mode locked                                  |
|------------|----------------------------------------------------------|------------------------------------------------------|
| **I1**     | `box.size.x ≤ canvas.width` e `box.size.y ≤ canvas.height` | Box out-of-canvas = reference 1920×1080 escaping the resolver |
| **I2**     | pin (anchor) inside safe-area rectangle                   | Pin at canvas center landing outside safe area       |
| **I3**     | `placement.kind` ∈ {CanvasCenter, SafeArea*, Bottom*}     | Placement drift to `Absolute` (hidden hardcoded coord) |

Le 5 canvas configurations: **1920×1080 / 1080×1920 / 1080×1080 / 3840×2160 / 1280×720 (custom)** — ≥4 aspect ratios come richiesto, e include un custom non-canonical (1280×720 non è 720p standard, è una via di mezzo). Per Square 1:1, il box aspect ratio sotto `FitCanvas` policy è 1:1 (non 5.625:1 reference) — verifica che la policy scali per-axis indipendentemente.

## Criteri di accettazione

- `tests/text/test_preset_canvas_aware_presets.cpp` (~190 LoC) compilato verde; `cmake --build build --target chronon3d_text_canvas_aware_presets_tests -- -j$(nproc)` exit 0.
- `ctest --timeout 30 -R test_preset_canvas_aware_presets` verde (75 assertions PASS, 0 FAIL).
- Nessuna modifica ai file canonici `text_presets_v1.hpp` / `preset_constraints.hpp` / `text_placement.hpp` / `text_placement_resolver.hpp` (i tipi sono già canonici; il chore è test-only).
- `tools/wrap_push.sh origin main` 10/10 gates PASS post-push.
- macchina-verifica: `cmake --build build --target chronon3d_text_core` exit 0 + nuovo test target exit 0.

## Forward-points (FF — to be tracked in FOLLOWUP_TICKETS.md)

1. **FF — Content cinematic_helpers migration**: migrare `content/showcases/cinematic/cinematic_title_helpers.hpp` + 7 file cinematic_showcase + minimalist + important_words a leggere `CanvasInfo` dal composition context (invece di literal `1920.0f, 1080.0f` / `960.0f, 540.0f`). Ticket: TICKET-PRESET-CANVAS-AWARE-CONTENT-MIGRATION (P1, blocked-on TICKET-PRESET-CANVAS-AWARE-COORDS DONE).
2. **FF — Test deprecation della v1 1920×1080-defaulting overloads**: rimuovere le 5 overload `[[deprecated]]` in `text_presets_v1.hpp:345+` dopo che i 100+ callers (in content/ + apps/ + tests/) siano migrati a CanvasInfo. Forward-point già documentato in `TICKET-SIMPLICITY-DEPRECATION`.
3. **FF — Add Subtitle preset canvas-aware coverage**: 4 nuovi preset v2 subtitle (`karaoke_fill`, `active_word_pop`, `subtitle_card`, `lower_third_safe` — vedi stash `wip-subtitle-pre-textspec-removal-2026-07-15`) dovranno accettare CanvasInfo come gli altri 5 preset.
4. **FF — Extend the cross-cut test with `AspectRatioPolicy::FitInside` explicit override**: il `FitInside` policy attualmente non è testato esplicitamente via preset interface (è un parametro opzionale di default a `FitCanvas`). Aggiungere un test che passa `AspectRatioPolicy::FitInside` esplicitamente e verifica l'aspect ratio bloccato.

## Cross-refs

* [docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md](../TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md) Blocco 4 (M2 — Responsive text) — definizione acceptance criteria per preset canvas-aware.
* [docs/tickets/TICKET-TEXT-PRODUCTION-STATUS-CORRECTION.md](TICKET-TEXT-PRODUCTION-STATUS-CORRECTION.md) §2 — row "Preset responsive (zero 1920×1080 hardcoded...)" PLANNED → questo chore ADD-IR al coverage di quella row (5/5 v1 preset canvas-aware verificati ERT; cinematic_showcase content helpers deferred a FF-1).
* [docs/tickets/TICKET-SIMPLICITY-PRESETS.md](../tickets/TICKET-SIMPLICITY-PRESETS.md) — DONE 2026-07-10 commit che ha migrato i 5 preset v1 a CanvasInfo + TextBoxConstraints + AspectRatioPolicy.
* [include/chronon3d/presets/text/text_presets_v1.hpp](../include/chronon3d/presets/text/text_presets_v1.hpp) — 5 inline preset functions, single source of truth per canvas-aware preset authoring.
* [include/chronon3d/presets/text/preset_constraints.hpp](../include/chronon3d/presets/text/preset_constraints.hpp) — `TextBoxConstraints` + `AspectRatioPolicy` + `resolve_text_box_constraints()`.
* [include/chronon3d/text/text_placement.hpp](../include/chronon3d/text/text_placement.hpp) — `CanvasInfo` (line ~187) + `SafeAreaPreset` (Landscape16x9 / Portrait9x16 / Square1x1 / Landscape4x3).
* [tests/text/test_presets_stability.cpp](../tests/text/test_presets_stability.cpp) — complementare a questo chore: copertura per-aspect individuale (no ERT GENERATE()).
* [tests/text/test_safe_area_placement.cpp](../tests/text/test_safe_area_placement.cpp) — pattern di riferimento per safe-area bounds assertion (8 sub-cases 4×2 + 5 SafeAreaEnum exhaustive sweep).
* AGENTS.md §`### Docs canonical update discipline rule` — cronaca estesa lives in ticket-home per Cat-3 anti-dup.
* AGENTS.md §`### Post-push SHA-selfcheck invariant` — `tools/wrap_push.sh origin main` + SHA-triple check post-push.

## Cronologia

* **2026-07-15** chore `test(presets): ERT canvas-aware coverage` (commit corrente) — NEW `tests/text/test_preset_canvas_aware_presets.cpp` (~190 LoC, 75 assertions su 5 preset × 5 canvas × 3 invariants) + NEW `tests/text_canvas_aware_presets_tests.cmake` manifest + questo ticket cronaca-estesa. ZERO source touched in include/ o src/.