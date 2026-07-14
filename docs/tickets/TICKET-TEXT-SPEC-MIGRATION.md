# TICKET-TEXT-SPEC-MIGRATION — Migrate TextSpec callers to TextDefinition (Blocco 5.1)

## Stato: OPEN (P1, depends on macchina-verifica)

## Problema
L'audit statico (Blocco 5.1) richiede di "eliminare `TextSpec` dai
contenuti".  Il commit `chore(text): deprecate legacy TextSpec overloads
(Blocco 5.1)` (2026-07-14) ha marcato `[[deprecated]]` due entry point
legacy:
  1. `LayerBuilder::text(name, const TextSpec&)` —
     `include/chronon3d/scene/builders/layer_builder.hpp`
  2. `chronon3d::from_text_spec(const TextSpec&)` —
     `include/chronon3d/text/text_definition.hpp`

Entrambi ora emettono un compile warning che punta a questo ticket.  I
callers da migrare:

### Production callers (1)
- `src/scene/builders/layer_builder_shapes.cpp:197` — l'unico caller
  production che ancora usa `from_text_spec` (l'overload
  `text(name, TextSpec)` interno lo chiama).

### Content callers (124+)
- `tests/visual/ae_parity/ae_parity_compositions.cpp` — 5 composition
  factories usano `l.text("...", TextSpec{...})`.
- `src/scene/camera/overlay_spatial_panels.cpp` — ~20 righe.
- `src/scene/camera/overlay_kinematic_panels.cpp` — ~10 righe.
- `src/scene/camera/overlay_hud_panels.cpp` — ~5 righe.
- `src/scene/camera/overlay_diagnostic_panels.cpp` — ~5 righe.
- `content/animation_compositions.cpp`,
  `content/compositions/chronon_glow_final.cpp`,
  `content/text_placement/text_placement_compositions.cpp`,
  `content/common/text/text_reveal.cpp`, ecc.

### Test callers (56+)
- `tests/test_text_preset_registry.cpp`,
  `tests/certification/test_text_production_v1.cpp`,
  `tests/certification/test_cert_text_bbox.cpp`,
  `tests/architecture/test_text_definition_round_trip.cpp`,
  `tests/scene/rendering/test_render_node_factory.cpp`,
  `tests/scene/shapes/test_shape_model.cpp`, ecc.

## Soluzione accettabile
Migrare ogni caller a `text(name, const TextDefinition&)` usando il
field-mapping 1:1:
  `TextSpec.content`     → `TextDefinition.content`
  `TextSpec.font`        → `TextDefinition.style.font`
  `TextSpec.appearance`  → `TextDefinition.style.{color,paint,shadows,box_style,material}`
  `TextSpec.layout`      → `TextDefinition.frame.{size,anchor,align,vertical_align,...}`
  `TextSpec.placement`   → `TextDefinition.frame.placement`
  `TextSpec.position`    → `TextDefinition.frame.placement.offset` (z droppato)
  `TextSpec.animators`   → `TextDefinition.animation.animators`
  `TextSpec.direction`   → `TextDefinition.animation.direction`
  `TextSpec.language`    → `TextDefinition.animation.language`
  `TextSpec.script`      → `TextDefinition.animation.script`
  `TextSpec.cache_layout` → `TextDefinition.animation.cache_layout`

Per il round-trip test (`tests/architecture/test_text_definition_round_trip.cpp`),
il test stesso documenta che `TextSpec ↔ TextDefinition` round-trip è
identity per tutti i 22 field — la conversione è lossless.

## Vincoli
- Niente nuove public SDK API (no `to_text_spec_from_definition_2()`).
- Niente nuovi singleton/registry/resolver/cache.
- Nessuna rimozione di `TextSpec` (è ancora la struttura canonica per
  `TextRunSpec` e per la pipeline).  Solo i 2 entry point marcati
  `[[deprecated]]` vengono rimossi DOPO la migrazione.

## Workaround attuale
I callers continuano a compilare (i `[[deprecated]]` sono solo warning).
Il WBH env-block non ha una build funzionante; questo ticket viene
aperto a completamento del Blocco 5.1 per tracciare la migrazione
futura quando sarà disponibile un build host.

## Criteri di accettazione
- 0 reference a `text(name, TextSpec)` in production source.
- 0 reference a `from_text_spec()` in production source.
- 0 reference a `text(name, TextSpec)` in test source.
- 0 reference a `from_text_spec()` in test source.
- `LayerBuilder::text(name, TextSpec)` overload rimosso.
- `from_text_spec()` adapter rimosso.
- macchina-verifica: build + ctest 11/11 verde post-migrazione.

## See also
- [TICKET-CENTERED-TEXT-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION.md) (P2) — i 100+ call sites di `centered_text` / `glow_text` tipicamente wrappano `TextSpec`; questo ticket DEVE essere completato DOPO TICKET-TEXT-SPEC-MIGRATION (P1) per non rompere i callers.
