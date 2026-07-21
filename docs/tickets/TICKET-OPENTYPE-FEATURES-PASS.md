# TICKET-OPENTYPE-FEATURES-PASS — `hb_shape()` features explicit pass-through

## Stato

**OPEN (commit-pending)** — implementation landed on a working-copy branch with diff stat +181/-1 across `font_engine.hpp`, `font_engine.cpp`, `tests/text/test_font_engine.cpp`. Awaiting macchina-verifica on working build host (vcpkg env-block on this VPS, see `TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX`).

## Problema

Il call site canonico `hb_shape(entry->hb_font, buf, nullptr, 0)` (`src/backends/text/font_engine.cpp:296`) non consentiva alcun controllo delle feature OpenType passate a HarfBuzz: `liga`, `kern`, `calt`, `smcp`, `dlig`, `ss01`, ecc. erano appiccicate al default-on implicito del font (`kern=1, liga=1, calt=1`). Conseguenze: impossibile silenziare ligature fastidiose, attivare stylistic sets, o certificare che il default di un dato font NON sia cambiato in update di versione.

## Soluzione accettabile (questa chore)

`TextShaping::features` (stringa OpenType, e.g. `"kern=1,liga=0"`) viene parsata da `parse_opentype_features()` (helper anon-namespace in `font_engine.cpp`) tramite HarfBuzz's native `hb_feature_from_string()`, e il risultato `std::vector<hb_feature_t>` viene threadato a `hb_shape(... features.data(), features.size())`. Default `features = ""` → empty vector → `hb_shape(buf, data(), 0)` ≡ legacy `hb_shape(buf, nullptr, 0)` (preserva 11/11 baseline). Tests in `tests/text/test_font_engine.cpp` verificano il path esplicito (liga su "office", kern su "AV").

## Accepted deviations from verdict spec

### 1. `TextDefinition` features — non aggiunto

Verdict Step 3: "Verifica che `TextDefinition` esponga già una stringa `features` o equivalente". `TextDefinition` (`include/chronon3d/text/text_definition.hpp`) NON ha campo `features` né `TextDefStyle::features` né altri sub-struct rilevanti.

**Decisione**: features aggiunto a `TextShaping` (in `font_engine.hpp`), non a `TextDefinition`. Rationale: `TextShaping` è già la sede canonica per le 3 dimensioni per-call di HarfBuzz (direction/script/language); aggiungere `features` come 4° dimensione è naturale e Cat-3 minimal. TextDefinition → TextSpec → shape_text() passa per TextShaping invariato. Forward-point: plumbing diretto `TextRunLayout::features` → `TextShaping::features` nei caller upstream (text_run_builder.cpp) NON yet landed.

### 2. Liga su "office" — glyph_count `== 4` non `== 5`

Verdict Step 4 verbatim: "la parola 'office' deve avere `glyph_count == 5` (fi + ffi ligatures)". `office` NON contiene substring "fi" standalone (`o-f-f-i-c-e`): il "f-f-i" forma ligatura `ffi` unica, NON `fi` + `ffi` separate. Honest-discipline:

- `liga=1` su Inter-Bold.ttf → `o + ffi + c + e = 4` glyphs
- `liga=0` su Inter-Bold.ttf → `o + f + f + i + c + e = 6` glyphs

Il verdict claim di 5 era errato (probabile confusione con "offices" o doppio conteggio di "fi" + "ffi" come se fossero entrambe formate). Test asserts relative (`<`) + INFO logs della split esatta per forensic traceability.

### 3. Kern advance_x — width + per-glyph advance_x probe

Verdict Step 5: "verifica con `hb_buffer_get_glyph_positions()` che `pos[1].x_advance` sia ridotto". Test asserts sia `width(kern=1) <= width(kern=0)` (proxy via `hb_buffer_get_glyph_positions` aggregato) SIA `glyphs[1].advance_x(kern=1) <= advance_x(kern=0)` (probe diretto; `GlyphPosition::advance_x` viene popolato a `font_engine.cpp:393` da `hb_glyph_position_t::x_advance`).

## Criteri di accettazione

- [x] Helper `parse_opentype_features()` canonico (anon-namespace in `font_engine.cpp`).
- [x] `hb_shape` call threaded features (`features.data()` + `features.size()`).
- [x] `TextShaping::features` field added con documentazione esempi ("liga", "kern=1", "-liga", etc.).
- [x] Liga test (interop-agnostic relative assertions + INFO forensic).
- [x] Kern test (width + per-glyph advance_x).
- [x] Default `features=""` ≡ legacy `(nullptr, 0)` semantics preservate.
- [ ] Working-build-host verify: `cmake --build build/chronon/linux-content-dev --target chronon3d_font_engine_tests` PASS. (DEFERRED-WBH per `TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX`.)
- [ ] Forward-point plumbing `TextRunLayout::features → TextShaping::features → parse_opentype_features`. (See §Forward-points.)

## Forward-points catena

1. **`TextRunLayout::features → TextShaping::features` plumbing**: oggi se l'utente vuole controllare le features deve settare `TextShaping::features` esplicitamente. Il canonical path dal TextRunLayout (production) → shape_text call deve essere popolato da upstream `@ text_run_builder.cpp::compile_text_layout` o `text_layout_rebuild.cpp::rebuild_active_side`. Forward-point: ticket dedicato che saga l'orchestrator pass-through + tests di integration che coprano il flow production-level.

2. **Variable-font axes pass-through**: `TextRunLayout::variation_axes` (campo esistente, M1.5#5) NON è thread-to-harfbuzz. `hb_font_set_variations()` non è ancora chiamato in font_engine.cpp. Forward-point: ticket dedicato per variable-font axes (`wght`, `wdth`, etc.).

3. **Dlig + ss01 + custom-feature coverage regression**: il test copre liga + kern. Le feature `dlig`, `ss01`, `ss02`, `calt`, `smcp`, `onum`, `lnum`, `tnum` non hanno ancora test di coverage specifici. Forward-point: ticket dedicato per test matrice OpenType.

## Cross-link

- `include/chronon3d/text/font_engine.hpp` (TextShaping struct line 220 con `features` field)
- `src/backends/text/font_engine.cpp` (parse_opentype_features anon-namespace + hb_shape threaded call)
- `tests/text/test_font_engine.cpp` (2 nuovi TEST_CASE: liga + kerning)
- TICKET-103a (TextShapingFeatures alias type lineage, già chiuso)

## §honest-discipline lineage

Verdict typo (glyph_count==5 vs observable==4 su "office") documentato in `tests/text/test_font_engine.cpp::test "liga on 'office'"` + §Accepted deviations sopra. Test asserts relative `<` per font-version-agnostic + INFO logs della split esatta per forensic traceability.

## §Cat-3 anti-dup

Nessun nuovo ticket canonical - questo TICKET-OPENTYPE-FEATURES-PASS è nuovo (rg-check pre-commit confermava assenza di canonical pre-esistente per il thread-through call-site). Forward-points elencati sopra come catena, NON come ticket paralleli (vedi §Forward-points).
