# TICKET-CENTERED-TEXT-MIGRATION — Migrate centered_text/glow_text to canonical fluent API (Blocco 5.1)

## Stato: OPEN (P2, depends on TICKET-TEXT-SPEC-MIGRATION P1)

## Problema
L'audit statico (Blocco 5.1) richiede di rimuovere i helper
`centered_text()` e `glow_text()` residui.  Questi helper sono già
marchiati `[[deprecated]]` (emettono un one-time spdlog::warn) ma NON
sono ancora stati rimossi perché hanno 100+ call sites che dipendono
da loro.

Definizioni:
- `centered_text(CenterTextOptions)` →
  `content/text/text_helpers_centered.hpp:66`
- `glow_text(CenterTextOptions, glow_color, radius, intensity)` →
  `content/text/text_helpers_centered.hpp:121`

Callers principali (100+):
- `tests/deterministic/test_visual_regression_scenarios.cpp` — 5+ call
  sites che usano `auto ts = centered_text(make_opts(...))`.
- `tests/text/test_text_definition.cpp` — 10+ call sites
  (centered_text/glow_text convergence tests).
- `tests/text/test_text_simplicity_adapters.cpp` — 2+ call sites
  (deprecation warning capture).
- `content/text_placement/text_placement_compositions.cpp` — 5+ call
  sites (`make_centered_text_comp` wrappers).
- `content/certification/cert_multilingual.cpp`,
  `content/certification/cert_lower_third.cpp`,
  `content/certification/cert_long_text.cpp`,
  `content/certification/cert_title.cpp` — 16+ call sites.
- `content/examples/light/light_text_animations.cpp` — 1 call site.
- `content/common/animation_helpers.hpp` — TODO migrate.

## Soluzione accettabile
Migrare ogni caller a `text(name, const TextDefinition&)` con costruzione
diretta del `TextDefinition` (stesso field-mapping di
TICKET-TEXT-SPEC-MIGRATION).  Per `centered_text(opts)` la migrazione
più diretta è:

```cpp
// PRIMA:
auto def = centered_text(opts);

// DOPO:
auto def = TextDefinition{
    .content = {.value = opts.text},
    .frame = {.size = {900.0f, opts.font_size * 1.5f},
              .placement = TextPlacement{TextPlacementKind::CanvasCenter},
              .align = TextAlign::Center,
              .vertical_align = VerticalAlign::Middle},
    .style = {.font = {.font_path = opts.font_path,
                       .font_size = opts.font_size,
                       .font_weight = 700},
              .color = opts.color},
};
```

Per `glow_text(opts, glow_color, radius, intensity)` aggiungere
`.style.material.use_material_glow = true` + i field `glow_*`
corrispondenti (vedi `content/text/text_helpers_centered.hpp:121-135`
per il mapping esatto).

## Vincoli
- Niente nuove public SDK API.
- Niente nuovi singleton/registry/resolver/cache.
- La rimozione di `centered_text` / `glow_text` avviene DOPO la
  migrazione di tutti i call sites (no breaking change prematura).

## Workaround attuale
I callers continuano a funzionare (i `[[deprecated]]` sono solo
warning).  La rimozione finale è deferred alla wave successiva.

## Criteri di accettazione
- 0 reference a `centered_text` in production source.
- 0 reference a `glow_text` in production source.
- 0 reference a `centered_text` in test source.
- 0 reference a `glow_text` in test source.
- `centered_text()` helper rimosso.
- `glow_text()` helper rimosso.
- macchina-verifica: build + ctest 11/11 verde post-migrazione.
