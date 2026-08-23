# TICKET-TEXT-PROPERTY-PHASES — Formalize 4 property evaluation phases

## Stato: DONE (2026-07-15, commit <this commit>)

## Problema

Il codice era inconsistente sul **quando** ogni proprietà viene valutata nella pipeline testuale. La roadmap `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` Blocco 2 dichiara quattro fasi canoniche (Pre-shaping / Pre-layout / Post-layout per glyph / Post-raster), ma:

* l'`enum class PropertyPhase` era già definito in `include/chronon3d/text/animation/text_animator_properties.hpp:25`, però nessun campo di `TextSpec` o `TextDefStyle` o `TextFrame` portava la propria annotazione di fase;
* la regola "tracking post-layout vs layout-level" era documentata solo nel FASE 2b comment su `TrackingProperty`, senza una bozza unica e grep-discoverabile;
* non esisteva una **`is_reflow_property()`** helper per classificare `PreShaping` ∪ `PreLayout` come "richiede rebuild di shaping/layout/cache" (reflow) e `PostLayout` ∪ `PostRaster` come visual-only;

Il documento `docs/text-architecture-inventory.md` §D2 + `docs/tickets/TICKET-AE-PARITY-KILLER-CHARACTER-OFFSET-VALUE-RANGE.md` attestano che `CharacterOffsetProperty` era già stata spostata in pre-shaping (FASE 2a), ma il refuso residuo era: nessuna **mappa canonica "field → phase"** consultabile da qualunque contributor futuro.

## Soluzione

`enum class PropertyPhase` (già in `text_animator_properties.hpp:25`) rimane **l'unica sede canonica** della tassonomia delle 4 fasi (Cat-3 anti-dup). Sono stati aggiunti, senza nuove sedi di definizione:

1. Helper `inline constexpr bool is_reflow_property(PropertyPhase p) noexcept` accanto all'enum — definisce "reflow" come `PreShaping ∪ PreLayout` e "visual-only" come `PostLayout ∪ PostRaster`.
2. Tabelle di annotazione in cima a `TextLayoutSpec` / `TextAppearanceSpec` / `TextSpec` (in `include/chronon3d/scene/builders/params/text_params.hpp`) che elencano la fase di ogni campo, mantenendo il body dello struct invariato (nessuna API expansion).
3. Tabelle equivalenti in cima a `TextFrame` / `TextDefStyle` / `TextDefinition` (in `include/chronon3d/text/text_definition.hpp`).

Nessuna modifica di behavior: il ticket è documentale + helper-invariante (compile-green dopo commit, gate `tools/wrap_push.sh` 10/10 PASS).

### Tabella di classificazione (verbatim cronaca)

| Fase         | Reflow?     | Categoria                                                | Campi canonici                                                                  |
| ------------ | ----------- | -------------------------------------------------------- | ------------------------------------------------------------------------------- |
| **PreShaping** | **SÌ**     | Source-text mutations                                    | `content.value`, `CharacterOffsetProperty` (animator)                           |
| **PreLayout**   | **SÌ**     | Paragraph composition inputs                             | `TextLayoutSpec.*` (box/anchor/align/wrap/overflow/line_height/tracking/auto_fit/min|max_font_size/max_lines/ellipsis/centering_mode/vertical_align/paragraph); `TextSpec.font` e FontSpec.* (font_path/family/weight/style/size); `TextSpec.placement`; `TextDefinition.style.font`; `TextDefinition.frame.*`; `TextDefinition.paragraph` |
| **PostLayout**  | **NO**     | Per-glyph visual-only mutations                          | `TextAppearanceSpec.*` (color/paint/shadows/material/box_style); `TextDefStyle.{color,paint,shadows,material,box_style}`; tutti i 12 `TextAnimatorProperty::Text*Property` (Position/Scale/Rotation/Skew/Anchor/Tracking/BaselineShift/Opacity/Blur/FillColor/StrokeColor/StrokeWidth); `CharacterOffsetProperty` è già strippato via `evaluate_pre_shaping_source()` (PostLayout è no-op difensivo); `TextDefinition.animation` |
| **PostRaster**  | **NO**     | Post-rasterization effects                               | (Future — alcuni material/glow/blur post-raster; nessun campo corrente)         |

Differenziazione **tracking** (richiesta utente): `TextSpec.layout.tracking` e `TextDefinition.frame.tracking` sono **PreLayout (reflow)** — alimentano `compile_text_layout` e cambiano word-wrapping/justification. `TrackingProperty` (animator) è **PostLayout (visual-only)** — modifica solo l'advance post-shaping di ogni glyph via `detail::apply_tracking_to_glyph`, NON ricalcola HarfBuzz né la cache di layout. Stessa logica per `font_size` (`FontSpec.font_size` PreLayout quando static; `TextAnimator::scale*` PostLayout quando animato).

## Forward-points

1. **`CharacterValue` + `CharacterRange` pre-shaping** (sibling della FASE 2a già chiusa): introdurre i due nuovi `TextAnimatorProperty::Text*Property` variants + estendere `property_phase<T>()` switch + `text_pre_shaping.cpp::evaluate_pre_shaping_source` aggregation. Ticket seed: TICKET-AE-PARITY-KILLER-CHARACTER-OFFSET-VALUE-RANGE.md (la fase 2a del killer 03 già cita CharacterValue come companion).
2. **Source-Text expressions pre-shaping** (`after_effects/sourceText`): aggiungere `SourceTextExpressionProperty` (PreShaping) che valuta il source via `evaluate_expression()` PRIMA di HarfBuzz — ticket: TICKET-AE-PARITY-KILLER-EXPRESSION-SELECTOR.md (Killer 2 sibling).
3. **PostRaster phase field-occupancy**: aggiungere i material/glow post-raster che oggi sono `PostLayout` ma concettualmente appartengono a `PostRaster`. Ticket: TICKET-TEXT-POSTRASTER-TAXONOMY (da aprire).
4. **`is_reflow_property` enforcement in compile_text_document**: aggiungere un check `if (is_reflow_property(phase)) invalidate_layout_cache()` nei punti di mutazione runtime, per garantire staticamente la regola "property reflow → cache invalidate". Ticket: TICKET-TEXT-PROPERTY-PHASE-CACHE-INVARIANT.

## Criteri di accettazione (DONE in questo commit)

* `enum class PropertyPhase` resta in `text_animator_properties.hpp` come unica sede canonica (no duplicazione in nuovi file — Cat-3 anti-dup honoured).
* `inline constexpr bool is_reflow_property(PropertyPhase p) noexcept { return p == PreShaping || p == PreLayout; }` aggiunto accanto all'enum.
* Ogni campo di `TextLayoutSpec`, `TextAppearanceSpec`, `TextSpec`, `TextFrame`, `TextDefStyle`, `TextDefinition` ha una annotazione in-header `// phase: <PreShaping|PreLayout|PostLayout|PostRaster> (reflow|visual-only)` o un blocco di comment raccoglitore all'inizio dello struct.
* `tools/wrap_push.sh` 10 developer gates PASS post-commit + incremental compile green.
* Verifica grep: `rg 'phase: ' include/chronon3d/text/text_definition.hpp include/chronon3d/scene/builders/params/text_params.hpp` restituisce annotazioni su tutti i campi rilevanti.

## Cross-refs

* [docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md](../TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md) Blocco 2 — definizione delle 4 fasi canoniche + acceptance criteria.
* [docs/text-architecture-inventory.md](../text-architecture-inventory.md) §D2 — rot precedentemente identificato: `CharacterOffsetProperty` valutato post-shaping.
* [docs/tickets/TICKET-AE-PARITY-KILLER-CHARACTER-OFFSET-VALUE-RANGE.md](TICKET-AE-PARITY-KILLER-CHARACTER-OFFSET-VALUE-RANGE.md) — FASE 2a closure per CharacterOffset pre-shaping (la parte "fatta", questo ticket aggiunge il "tassello tassonomico" mancante).
* [docs/tickets/TICKET-TEXT-PRODUCTION-STATUS-CORRECTION.md](TICKET-TEXT-PRODUCTION-STATUS-CORRECTION.md) — Text Production V1 PARTIAL: questo ticket NON completa V1 ma aggiunge un tassello di tassonomia documentato in V1 row.
* AGENTS.md §"Regole di lint documentale" — `SHA cite pattern inline-only`: ogni SHA citato nel cronaca del ticket è posizionato inline al suo boundary semantico (es. commit boundary).
* `include/chronon3d/text/animation/text_animator_properties.hpp:25` — sede canonica di `PropertyPhase`.
* `include/chronon3d/text/animation/text_pre_shaping.hpp:8` — sistema pre-shaping + `evaluate_pre_shaping_source`.
* `src/text/animation/text_pre_shaping.cpp:21` — `apply_character_offset_to_source` (FASE 2a già chiusa upstream).
* `src/text/animation/text_property_applier.cpp:167` — no-op difensivo per `CharacterOffsetProperty` nel dispatcher PostLayout.
* `tools/wrap_push.sh` — GATE-MNT-01 + 10-developer-gates (verifica commit).

## Cronologia

* **2026-07-15** chore `docs+lifecycle(text): formalize 4-phase property evaluation` (commit corrente) — aggiunto `is_reflow_property` helper + tabelle `phase:` in `text_params.hpp` + `text_definition.hpp` + questo ticket.
