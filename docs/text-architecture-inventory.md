# Chronon3D — Text Architecture Inventory

> **Snapshot:** `main@a469c8cb` — 2026-06-29. Linux-only.
> **Branch:** `main`. **HEAD == origin/main** (post Agenzia-3 Step 6 amend).
> **Working tree:** clean.
> **Trigger:** TEXT-INV-01 — Agent 3 baseline inventory before kinetic-typography roadmap execution.

---

## 1. Executive summary

Chronon3D possiede un **sottosistema testo maturo ma pre-stabile** rispetto alla parità After Effects:

- **Pipeline canonica**: `TextDocument → resolve_document → ResolvedTextRun → PlacedGlyphRun → TextRunProcessor → raster`.
- **Shaping**: HarfBuzz + FreeType + FriBidi; nessuna dipendenza **ICU** — grapheme cluster hand-rolled in `text_unicode_utils.hpp`.
- **Preset registry** consolidato in `TextPresetRegistry` (22+ preset), ma `AnimatorResolver` mantiene ancora una **tabella parallela** (cleanup TXT-01 ancora aperto).
- **7 architectural debts** rilevati, mappati 1-a-1 sui 14 ticket del piano canonico `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`.

### Verdetto di parità AE (gap analysis)

Il sistema è **ben fondato per la kinetic typography 2D**, ma **non AE-parity**. Le 14 issues del verdetto AE (correzione Character pre-shaping, AnimatedValue coverage completa, separazione Character/Grapheme/Glyph, single registry + retire legacy TextAnimator, Wiggly/Expression selector, paragraph layout produttivo, variable-font axes, Source Text expressions, per-character 3D, golden suite bloccante) sono nella **roadmap `TEXT-`** ma **non** tutte risolte.

---

## 2. Architectural debts (vs 13-step kinetic-typography plan)

| # | Architectural debt | Source location | Follow-up |
|---|---|---|---|
| **D1** | `Character == Grapheme` semantics (no distinct code-point map) | `include/chronon3d/text/glyph_selector.hpp:107-108` | **TEXT-UNM-01** |
| **D2** | `CharacterOffsetProperty` static, written in `GlyphInstanceState` post-shaping (against documented "pre-shaping" intent) | `include/chronon3d/text/text_animator_property.hpp:46,105`; `src/text/text_animator_property.cpp:198` | **TEXT-COR-01** |
| **D3** | Properties **NOT** in `AnimatedValue`: Rotation (X/Y/Z), Skew, Skew Axis, Anchor Point, Fill Color/Opacity, Stroke Color/Width/Opacity, BaselineShift, Font Size, Line Spacing, Line Anchor, Blur X, Blur Y | `text_animator_property.hpp:46-49` (comment) | **TEXT-ANM-01** |
| **D4** | `AnimatorResolver` keeps parallel id→spec table alongside `TextPresetRegistry` (cleanup #4 TXT-01 outstanding) | `include/chronon3d/registry/animator_resolver.hpp:42,100,152,286,322`; `docs/ANTI_DUPLICATION_RULES.md:27` | **TEXT-RES-01** |
| **D5** | Legacy `TextAnimator` (class) ancora produttivo in tests + content | `include/chronon3d/text/text_animator.hpp`; `tests/text/test_text_animator.cpp` (12+ TEST_CASEs); `content/showcases/cinematic/cinematic_text_camera.hpp:8` | **TEXT-RET-01** |
| **D6** | No `PropertyStage` plumbing (TXT-03 ancora PLAN) | assente (vedi `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md:702`) | **TEXT-STG-01** |
| **D7** | Selector variants limited to **Range**; Wiggly + Expression missing | `include/chronon3d/text/glyph_selector.hpp:67-88` (range-only fields) | **TEXT-SEL-01** |

### Properties con `AnimatedValue<T>` (post Step 4 spec Agent 3, commit `c6b9b99`)

| Property | Type | Note |
|---|---|---|
| Position (Vec3) | `AnimatedValue<Vec3>` | doc + `text_animator_property.hpp:51` |
| Scale (Vec3) | `AnimatedValue<Vec3>` | doc + `text_animator_property.hpp:55` |
| Opacity (f32) | `AnimatedValue<f32>` | doc + `text_animator_property.hpp:72` |
| Blur radius (f32) | `AnimatedValue<f32>` | `text_animator_property.hpp:76` |
| Tracking (f32 pixels) | `AnimatedValue<f32>` | `text_animator_property.hpp:92` |
| Selector start/end/offset/amount | `AnimatedValue<f32>` (in `GlyphSelectorSpec`) | `glyph_selector.hpp:75-78` |

### Properties **statiche** da convertire (oggetto di TEXT-ANM-01)

Rotation (X/Y/Z), Skew (X/Y + axis), Anchor Point (Vec3), Fill Color/Opacity, Stroke Color/Width/Opacity, Baseline Shift (f32 pixels), Font Size, Line Spacing, Line Anchor, Blur X e Blur Y separati. Per ciascuna servono: `AnimatedValue<T>` field + branch in `apply_property_value_to_glyph` (`src/text/text_animator_property.cpp`) + override `evaluate(time)` coerente con le altre.

---

## 3. TU map

### 3.1 Public headers — `include/chronon3d/text/`

| File | Surface | Note |
|---|---|---|
| `text_document.hpp` | `TextDocument`, `TextStyleSpan`, `ParagraphRange` | TU base del documento |
| `glyph_selector.hpp` | `GlyphSelectorSpec`, `TextUnitMap` | selector IR + unit map builder |
| `text_animator.hpp` | `TextAnimator` (legacy class) | **target TEXT-RET-01** |
| `text_animator_property.hpp` | properties bag (`AnimatedValue<T>` + variante static) | contiene D2 + D3 |
| `text_animator_property.hpp:TextAnimatorStack` | typedef canonico risolve doc-only stage | aggiunto da PR-A2 ticket 002 |
| `text_resolver.hpp` | `resolve_document()` → `ResolvedTextRun`, `ShapedTextTree` | entry-point pipeline |
| `text_run.hpp` | `TextRunLayout`, `TextRunShape` + `TextUnitMap units` | per-frame shape |
| `text_span.hpp` | per-span `GlyphSelectorSpec` selector list (Fase 3 reuse) | nuovo |
| `composer_types.hpp` | typographic unit (grapheme/ligature) | internal |
| `font_engine.hpp` | `FontEngine` facade (Hb ↔ FT bridge) | shaper entry |
| `text_path_spec.hpp` | `TextPathSpec` (perpendicular_to_path, baseline_offset, margin, force_alignment) | parziale |
| `text_path_composer.hpp` | `TextPathComposer` | parziale (margini, reverse path anim non ancora) |

### 3.2 Public headers — `include/chronon3d/backends/text/`

| File | Surface |
|---|---|
| `text_layout_engine.hpp` | `TextLayoutEngine` core (line break, paragraph layout, **grapheme cluster hand-rolled**) |
| `bidi_segmenter.hpp` | RTL/LTR via FriBidi (single header) |
| `text_layout_types.hpp` | shared types (FT/Hb measurement) |
| `text_unicode_utils.hpp` | UTF-8 codec + `is_grapheme_extend`, `grapheme_cluster_count`, `grapheme_byte_offset_at` |

### 3.3 Builders

| File | Layer | Note |
|---|---|---|
| `include/chronon3d/scene/builders/text_run_builder.hpp` + `src/scene/builders/text_run_builder.cpp` | builder | `append_animator(spec)`, `selector(spec)`, `make_global_glyph_selector(id)` |
| `include/chronon3d/scene/builders/builder_params.hpp` | params | `hb_buffer_set_script` passthrough (lines 196, 198) |
| `include/chronon3d/authoring/animator.hpp` | façade | `BaselineShiftProperty {pixels}` al lines 139, 143; `CharacterOffsetProperty {offset}` al 147, 151 |
| `include/chronon3d/authoring/selector.hpp` | façade | `Selector` → `GlyphSelectorSpec release()` |
| `include/chronon3d/authoring/text.hpp` | façade | `HB_DIRECTION_*` passthrough |

### 3.4 Implementation — `src/`

| File | Layer | Note |
|---|---|---|
| `src/text/text_document.cpp` | core | validation + paragraph split + hash |
| `src/text/glyph_selector.cpp` | selector | unit_map builder + selector evaluation; `build_text_unit_map` line 14 |
| `src/text/text_animator_property.cpp` | animator | `apply_property_value_to_glyph`, branch su `AnimatedValue<T>`; line 49-78 Position/Scale; line 127-140 Opacity/Blur; line 198 CharacterOffset post-shaping |
| `src/text/text_run_driver.cpp` | runtime | per-frame shape + transitions (scramble, morph, crossfade) |
| `src/text/text_path_composer.cpp` | path | line 163 `if (spec.perpendicular_to_path)` |
| `src/text/animated_text_document.cpp` | runtime | mutated `TextDocument` bridge + custom grapheme cluster (line 39-40) |
| `src/backends/text/font_engine.cpp` | shape | **Hb ↔ FT bridge** (lines 15, 16 includes; line 157 `hb_ft_font_create`; line 239 `hb_shape`) |
| `src/backends/text/bidi_segmenter.cpp` | bidi | dense UTF-32 + `get_bidi_types` + `get_par_embedding_levels_ex` |
| `src/backends/text/text_rasterizer_render.cpp` | render | **direct FT_Load_Glyph path** + uses pre-shaped when available (line 765) |
| `src/backends/software/processors/text_run/text_run_processor.cpp` | render | TU per spec; **direct FT path again** (lines 25-28, 119-176) |
| `src/scene/builders/text_run_builder.cpp` | builder | DSL implementation (`TextRunBuilder::append_animator` line 32) |
| `src/runtime/motion_graph_prewarm.cpp` | runtime | `text_layers_touched` prewarm stub (lines 8, 25, 48, 52) |
| `src/render_graph/CMakeLists.txt` | render graph | `nodes/TextRunNode.cpp` listed in comment "PR 3 (TextAnimator V2 batched text node)" |

### 3.5 Tests — `tests/text/*` + `tests/test_text_preset_registry.cpp` + `tests/backends/software/text_run_processor_tests.cpp`

| File | Coverage |
|---|---|
| `tests/text/test_text_animator.cpp` | Legacy `TextAnimator` (split by char/word/line/glyph, FontEngine interaction, 12+ TEST_CASEs) — **target TEXT-RET-01** |
| `tests/text/test_text_document.cpp` | validation + paragraph split + hash |
| `tests/text/test_text_unit_map.cpp` | word/line mapping + exclude_spaces (lines 9, 21, 32, 76) |
| `tests/text/text_run_tests.cpp` | `layout.units.grapheme_count` (line 91) |
| `tests/text/text_animator_property_tests.cpp` | BaselineShift, Scale Add vs Multiply, sub-cases 333-357 |
| `tests/text/test_text_run_driver.cpp` | per-frame driver; `make_global_spec()` helper line 60 |
| `tests/text/test_selector_combine.cpp` | selector composition (multi-spec combine) |
| `tests/text/test_selector_evaluate.cpp` | selector evaluation pipeline |
| `tests/text/test_text_quality_tracking.cpp` | tracking integration + grapheme cluster count |
| `tests/text/test_text_quality_glyph.cpp` | UAX #29 string examples (e + combining acute, ZWJ family, skin tone, RI pair, ZWJ sequence) — lines 144-228 |
| `tests/text/test_text_bidi.cpp` | bidi run segmentation (LTR/RTL mixed) |
| `tests/text/test_text_path_composer.cpp` | perpendicular / baseline_offset (lines 95, 111, 245) |
| `tests/text/test_text_preset_visual.cpp` | preset visual test bed, registry-driven; **8 sentinels per preset** |
| `tests/test_text_preset_registry.cpp` | registry tiers A-F (32+ sub-cases) — line 76: TICKET-012 header-lifted AnimatorResolver; line 123: Stage-4 AnimatorResolver cinematic_text_camera; line 669: Stage-5 coverage all 22 presets |
| `tests/authoring/test_animator_dsl.cpp` | authoring façade AnimatorTestAccess |
| `tests/backends/software/text_run_processor_tests.cpp` | per-glyph bbox / blur buckets / safe-clipping |

---

## 4. Dependency map (HarfBuzz / FreeType / FriBidi / ICU-equivalent)

### 4.1 HarfBuzz (`hb_*`, `hb-ft`)

| TU | Role |
|---|---|
| `src/backends/text/font_engine.cpp` | `hb_buffer_create`, `hb_buffer_add_utf8`, `hb_buffer_set_direction`, `hb_buffer_set_script`, `hb_buffer_set_language`, `hb_buffer_guess_segment_properties`, `hb_shape`, `hb_buffer_get_glyph_infos`, `hb_buffer_get_glyph_positions`, `hb_ft_font_create`, `hb_ft_font_changed`, `hb_buffer_destroy` |
| `include/chronon3d/authoring/text.hpp` | doc reference a `hb_buffer_guess_segment_properties` (lines 319, 470) |
| `include/chronon3d/scene/builders/builder_params.hpp` | script passthrough (lines 196, 198) |
| `include/chronon3d/text/font_engine.hpp` | API surface lines 110, 112, 126 (script + lang passthrough) |

### 4.2 FreeType (`FT_*`)

| TU | Role |
|---|---|
| `src/backends/text/font_engine.cpp` | `FT_Init_FreeType`, `FT_Load_Glyph`, `FT_Load_Char`, FT_Face management |
| `src/backends/text/text_rasterizer_render.cpp` | direct glyph path (`FT_Load_Glyph`, `FT_Load_NO_BITMAP`); shared `FT_Library` static |
| `src/backends/software/processors/text_run/text_run_processor.cpp` | direct glyph path; `TextRunPathBuilder` (lines 25-28, 119-176) |
| `include/chronon3d/text/font_engine.hpp` | doc: FT_Library non-copyable (line 237) |

### 4.3 FriBidi (`fribidi_*`, `FRIBIDI_*`)

| TU | Role |
|---|---|
| `src/backends/text/bidi_segmenter.cpp` | bidi run extraction: `fribidi_charset_to_unicode` (UTF-32 conversion), `fribidi_get_bidi_types`, `fribidi_get_par_direction`, `fribidi_get_par_embedding_levels_ex` |
| `include/chronon3d/backends/text/bidi_segmenter.hpp` | public API; `FRIBIDI_PAR_DIR_*` enum |
| `tests/text/test_text_bidi.cpp` | mixed LTR/RTL test (lines 108, 117) |

### 4.4 ICU / "grapheme cluster"

**ICU NON è una dipendenza attiva.** Codice zero `icu_*`, `<unicode/utf8>`, `U_*` outside dei file di documentazione/citazione.

L'implementazione grapheme è **hand-rolled** in:

- `include/chronon3d/backends/text/text_unicode_utils.hpp:107-513` — `is_grapheme_extend`, `grapheme_cluster_count`, `grapheme_byte_offset_at` + regole GB9/GB11/GB12 parziali (ZWJ emoji, RI flag pair).
- `src/text/animated_text_document.cpp:39-40` — versione inline per hot path.
- `include/chronon3d/text/text_animator.hpp:128, 133, 155, 174` — usa `grapheme_cluster_count` per misura unit.

**Copertura UAX #29**: confermata per test in `tests/text/test_text_quality_glyph.cpp:144-228` (e+acute, ZWJ family, skin tone, RI pair) — **ma manca**:
- Emoji keycaps (GB6 wrapping).
- Indic conjunct cluster (GB7-GB9c partial).
- Tag sequences (GB11/GB13/GB999).
- Bidi class embedding forces.

Estensione hand-roll o integrazione `<unicode/brkiter.h>` rimandata a **TEXT-EXP-01** (Source Text expressions + variable fonts include ICU come pre-requisito opzionale).

---

## 5. TICKETS (text-area)

Estratto da `docs/FOLLOWUP_TICKETS.md` snapshot corrente e `docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`:

### Chiusi (archive / current index)

| Ticket | Stato | Commit | Note |
|---|---|---|---|
### Aperti (active index, top-10 blockers table)

| Ticket | Stato | Commit/Source | Note |
|---|---|---|---|
| TICKET-051 | `🔵 OPEN` | n/a (unica OPEN testo nell'active index) | area `A4.3 per-preset diagnostic` (source `tests/text/test_text_preset_visual.cpp`, target `A4.3 visual`) |

### Chiusi (archive / current index narrative)

| Ticket | Stato | Commit/Source | Note |
|---|---|---|---|
| TICKET-006 | 🟢 Done | `09997570` (PR-A archive narrative) | gen-exp guard in `tests/renderer_tests.cmake` + `tests/scene_tests.cmake`; sblocca link `chronon3d_backend_text` |
| TICKET-029 | 🟢 Done | `fb1b7e97` | sblocca link di `chronon3d_scene_tests` |
| TICKET-038 | 🟢 Done | `91debc36` | lambda capture / auto deduction rot in `tests/text/test_text_preset_visual.cpp` |
| TICKET-039 | 🟢 Done | `68c3e0f0` | `SoftwareRenderer::settings()` access regression |
| TICKET-040 | 🟢 Done | — | Taskflow rimosso (no SHA in `CURRENT_STATUS.md` narrative; verificare `git log --grep='TICKET-040'` se serve pinning) |

### Aperti

| Ticket | Stato | Note |
|---|---|---|
| TICKET-051 | `🔵 OPEN` | Introdotta come sinonimo di `🔵 Planned` da Step 4 spec Agent 3 (unica OPEN testo nell'header di `FOLLOWUP_TICKETS.md` snapshot corrente) |
| TICKET-012/013/014/015/016 | status non estratto da grep snapshot | storici TXT deliverables (cleanup series) — da verificare puntualmente in `git show main:docs/FOLLOWUP_TICKETS.md` |

### Tickets **da aprire** (follow-up inventory → roadmap)

Questa inventory identifica 7 nuovi ticket (uno per ogni follow-up `TEXT-`) da registrare in `docs/FOLLOWUP_TICKETS.md` via **TEXT-TRK-01**:

- `TICKET-TXT-STG` (PropertyStage plumbing — pre-shaping/pre-layout/post-layout/post-raster).
- `TICKET-TXT-COR` (Character Offset + Value + Range pre-shaping con invalidazione).
- `TICKET-TXT-ANM` (static properties → AnimatedValue per 12+ properties).
- `TICKET-TXT-RES` (single TextPresetDescriptor + AnimatorResolver queries registry only).
- `TICKET-TXT-RET` (retire legacy TextAnimator).
- `TICKET-TXT-SEL` (Range + Wiggly + Expression selectors).
- `TICKET-TXT-UNM` (TextUnitMap reale: byte UTF-8, code point, grapheme cluster, glyph, word, line, paragraph, span).

---

## 6. Render aggregator touchpoints

### 6.1 Fan-in points

| Touchpoint | Source | Ruolo |
|---|---|---|
| `TextRunBuilder::append_animator(spec)` | `src/scene/builders/text_run_builder.cpp:32` | append TextAnimatorSpec |
| `TextRunBuilder::selector(spec)` | `src/scene/builders/text_run_builder.cpp:162` | append GlyphSelectorSpec |
| `TextRunBuilder::make_global_glyph_selector(id)` | `src/scene/builders/text_run_builder.cpp:20-21` | factory weight-1 selectors |
| `TextRunBuilder::selector(GlyphSelectorSpec)` | `include/chronon3d/scene/builders/text_run_builder.hpp:179-183` | DSL API |
| Render graph node `TextRunNode` | `src/render_graph/CMakeLists.txt:51` | comment "PR 3 (TextAnimator V2 batched text node)" — non ancora implementato |
| Builder params script passthrough | `include/chronon3d/scene/builders/builder_params.hpp:196, 198` | `hb_buffer_set_script` propagation |
| Motion graph prewarm stub | `src/runtime/motion_graph_prewarm.cpp:8, 25, 48, 52, 75` | `text_layers_touched` counter |
| Authoring facade `Selector`, `Animator` | `include/chronon3d/authoring/animator.hpp:139, 143, 147, 151` (BaselineShift/CharacterOffset); include/chronon3d/authoring/selector.hpp | user-facing builder API |
| `TextAnimatorStack` typedef (canon) | `include/chronon3d/text/text_animator_property.hpp` (added by PR-A2) | resolves doc-only stage from `TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` "Pipeline canonica" |
| `TextAnimator{d}` transient layer-builder | `tests/text/test_text_animator.cpp` + `src/scene/builders/layer_builder.cpp:293` | legacy path — target TEXT-RET-01 |

### 6.2 Render pipeline fan-in (testo → render)

```
TextDocument (src/text)
    -> ResolvedTextRun/ShapedTextTree (src/text/text_resolver.cpp)
    -> PlacedGlyphRun (text_layout_engine.hpp)
    -> TextRunSpec (scene/builders/text_run_builder.cpp)
    -> builder.text_param(...).animator(BaselineShiftProperty{8.0f}).selector(...)
    -> TextRunProcessor (src/backends/software/processors/text_run)
        -> Boundary generators (FT direct glyph load)
        -> Clip + bounding box + blur bucket
    -> render_aggregator (src/runtime/motion_graph_prewarm.cpp + render_graph nodes)
                            ├── TextRunNode (planned PR 3, non implementato)
                            └── motion_graph_prewarm.text_layers_touched counter
```

### 6.3 Authoring façade fan-in

Surface autoriale canonica (verbatim dall'header docstring — `include/chronon3d/authoring/animator.hpp` line 9-22):

```cpp
#include <chronon3d/authoring/authoring.hpp>
using namespace chronon3d::authoring;

auto a = animator("hero")
             .select(selector(TextSelectorUnit::Word)
                           .start(Frame{0},    0.0f)
                           .start(Frame{30}, 100.0f))
             .position({0.0f, 46.0f, 0.0f})
             .scale(0.94f)
             .opacity(0.0f)
             .blur(12.0f)
             .tracking(10.0f);
// release() è friend-only: consumato da Text::animate() (PR 3)
```

**API curate dal codice (non dedotte)**:
- Factory: `animator(std::string id)` (`animator.hpp:~209-211`) + `selector(TextSelectorUnit unit)` (`selector.hpp:~210`).
- `Animator` builder methods (verbatimmente enumerati a `animator.hpp:~50-178`): `.select(Selector&)`, `.position(Vec3)`, `.scale(Vec3)`, `.scale(f32 uniform)`, `.rotation(Vec3)`, `.skew(f32 angle, f32 axis=0)`, `.anchor(Vec3)`, `.tracking(f32)`, `.baseline_shift(f32)`, `.character_offset(i32)`, `.opacity(f32)`, `.blur(f32 radius_px)`, `.fill_color(Color)`, `.stroke_color(Color)`, `.stroke_width(f32)`.
- Nessun `.property(GenericProp)` esiste: ogni property ha il suo setter dedicato.
- `Selector` builder methods (`selector.hpp`): `.shape(...)`, `.smooth()`, `.order(...)`, `.combine_mode(...)`, `.exclude_spaces(bool=true)`, `.start(Frame, f32, Easing=Linear)`, `.end(Frame, f32, Easing=Linear)`, `.offset(Frame, f32, Easing=Linear)`, `.amount(Frame, f32, Easing=Linear)`, `.ease_low(f32)`, `.ease_high(f32)`.
- Nessun `Selector::range(...)` static factory: il Range selector si costruisce via `.start(...).end(...).amount(...)` keyframes.
- `release()` è `private &&` + friend-only (`animator.hpp:~198-201`, `selector.hpp:~195-198`); consumato da `Text::animate()` (PR 3, pianificato in `authoring.hpp:64-67`).

Le property type (PositionProperty, ScaleProperty, RotationProperty, SkewProperty, AnchorProperty, TrackingProperty, **BaselineShiftProperty**, **CharacterOffsetProperty**, OpacityProperty, BlurProperty, FillColorProperty, StrokeColorProperty, StrokeWidthProperty) sono **struct static senza `AnimatedValue<T>`** in `include/chronon3d/text/text_animator_property.hpp`.

---

## 7. Architectural findings

1. **No `PropertyStage` enum** (D6): documented `TextPropertyStage` da `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md:702` non implementato. Properties trust implicit pipeline ordering.

2. **Double source of truth for presets** (D4): `TextPresetRegistry` ha metadata + builder + motion chain (`TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md:106`); `AnimatorResolver` mantiene parallela id→spec table (`animator_resolver.hpp:42, 100, 152, 286, 322`). Cleanup #4 di TXT-01 dipende dalla consolidazione.

3. **`CharacterOffsetProperty` static** (D2): `text_animator_property.hpp:46` dice "BaselineShiftProperty, CharacterOffsetProperty keep static values". Codice in `text_animator_property.cpp:198` scrive in `GlyphInstanceState` post-shape. Header aspirazione "pre-shaping" non onorata.

4. **`Character == Grapheme` semantics** (D1): `glyph_selector.hpp:107-108`:

   ```cpp
   case TextSelectorUnit::Grapheme:  return glyph_to_grapheme[glyph_index];
   case TextSelectorUnit::Character: return glyph_to_grapheme[glyph_index]; // same for now
   ```

5. **Selector variants limitati a Range** (D7): solo Range (`glyph_selector.hpp:67-88`). Wiggly ed Expression selectors **non presenti**.

6. **`AnimatedValue<T>` coverage parziale** (D3): solo Position, Scale, Opacity, Blur radius, Tracking + fields del GlyphSelectorSpec. 12+ properties restano statiche.

7. **No ICU dependency**: grapheme cluster hand-rolled in `text_unicode_utils.hpp` + `animated_text_document.cpp`. Copertura parziale UAX #29 (GB9/GB11/GB12 ZWJ/RI).

8. **Render graph TextRunNode pianificato, non implementato**: comment in `src/render_graph/CMakeLists.txt:51 "PR 3 (TextAnimator V2 batched text node)"`. La fan-in verso il render aggregator passa oggi solo via `motion_graph_prewarm` stub.

---

## 8. Cross-references

- `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` — 13-step plan + XY map di TXT tickets centrali.
- `docs/TEXT_BOTTLENECKS.md:63` — `PlacedGlyphRun` + `resolve_placed_glyph_run()` intro note (canonical positioning).
- `docs/ANTI_DUPLICATION_RULES.md:27` — Text preset canonical row (`TextPresetRegistry` risolto da `AnimatorResolver::compose_for`).
- `docs/CHANGELOG.md:103` — 2026-06-10/14 `PlacedGlyphRun` unification chronicle.
- `docs/EXPRESSIONS_V2_PROMOTION.md:69` — Gate 3 con `AnimatedValue` integration (TICKET-EXP2-G3).
- `docs/agent-tasks/AGENT_4_VERIFY_VISUAL_INTEGRATION.md` — visual verification gates A4.1..A4.6 (deferred).
- `docs/baselines/main-446a60e2-baseline.md` — ultima baseline macchina-verificata (3/4 ✅).
- `docs/ARCHIVE/CURRENT_READINESS.md:49` + `docs/ARCHIVE/STATUS.md:52` — historical baseline snapshots.
- `docs/FOLLOWUP_TICKETS.md` snapshot corrente — index TICKET-051 (🔵 OPEN, unico OPEN testo).

---

## 9. Action plan forward (sequenza canonica)

1. **TEXT-STG-01** — PropertyStage enum (`PreShaping`, `PreLayout`, `PostLayout`, `PostRaster`) + plumbing.
2. **TEXT-COR-01** — CharacterOffset + CharacterValue + CharacterRange **pre-shaping** con invalidazione su shaping/layout/cache.
3. **TEXT-ANM-01** — Convertire in `AnimatedValue<T>`: Rotation X/Y/Z, Skew, Skew Axis, Anchor Point, Fill Color+Opacity, Stroke Color+Width+Opacity, BaselineShift, Font Size, Line Spacing, Line Anchor, Blur X/Y.
4. **TEXT-RES-01** — Single `TextPresetDescriptor {id, metadata, builder, animator_factory, fixture}`; `AnimatorResolver` interroga solo registry.
5. **TEXT-RET-01** — Retire legacy `TextAnimator` (char/word/line/glyph layer).
6. **TEXT-SEL-01** — Implementare Range + Wiggly + Expression selectors canonicamente.
7. **TEXT-UNM-01** — `TextUnitMap` reale: byte UTF-8, code point, grapheme cluster, glyph, word, line, paragraph, span.
8. **TEXT-PLY-01** — Paragraph layout completo (point/box, kerning, leading, hanging, CJK+RTL).
9. **TEXT-RCH-01** — Rich text end-to-end (font per span, sizes, weight, variable axes per span, ID semantici).
10. **TEXT-EXP-01** — Source Text expressions + variable fonts axes animabili + sandbox deterministica.
11. **TEXT-PTH-01** — Text on Path completo (tutte le opzioni + RTL + degenerate).
12. **TEXT-3DF-01** — Per-character 3D + motion blur (Z per char, Rotation XYZ, anchor grouping multi-livello, perspective).
13. **TEXT-EXT-01** — Testo 3D estruso + materiali (extrusion, bevel, materials, normals).
14. **TEXT-GLD-01** — Golden suite bloccante + baseline certificata su `main-<HEAD>-baseline-text.md`.
15. **TEXT-TRK-01** — Registrare 7 nuovi ticket (D1..D7 mapping) + `git show` cross-link da Step 5 doc.

---

## 10. Closing note

La road-map **kinetic-typography** è già presente in `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` e questa inventory ne misura il **gap residuo**. Le 14 issues del verdetto AE (analizzate nel gap input) coprono l'intero perimetro qui identificato. **TEXT-STG-01 è il primo passo obbligatorio** perché senza PropertyStage plumbing gli step 2-7 non possono essere applicati con invalidazione coerente.
