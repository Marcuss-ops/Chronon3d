# TICKET-CENTERED-TEXT-MIGRATION — Migrate centered_text/glow_text to canonical fluent API (Blocco 5.1)

## Stato: OPEN (P2, Phase 2 IN-PROGRESS via blocco 5.2 bulk-migration catena)

> Phase 2 bulk-migration catena aperta this session in [TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md) (NEW, per-AREA Chore B precedent: 7+1 sub-chori). Parent ticket rimane OPEN finché helper rimossi in `Blocco 5.2.I-FINAL` sub-chore. Forward-point dependency on TICKET-TEXT-SPEC-MIGRATION (P1) PRESERVED (rotation indipendente).

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

## Phase tracking

- **Phase 1 — Deprecation bridge (Blocco 5.1)**: DONE 2026-07-14 (commits FF-sync `b6397b90` + `74c924b9` + `bacbfc5a` + `cc3ad1a3`). `centered_text(CenterTextOptions)` + `glow_text(CenterTextOptions, glow_color, radius, intensity)` enhanced/updated `[[deprecated]]` markers; one-shot `spdlog::warn` runtime diagnostic fired via static-local bool gate (TICKET-104 precedent pattern). Cronaca estesa lives in [TICKET-BLOCO-5-1-3DOC-CAT5-ALIGN](TICKET-BLOCO-5-1-3DOC-CAT5-ALIGN.md) §Phase summary cronaca per AGENTS.md Cat-3 anti-dup.
- **Phase 2 — Complete removal (Blocco 5.2, IN-PROGRESS 2026-07-14, this session)**: bulk-migration catena aperta in [TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md) (NEW canonical cronaca home per AGENTS.md Cat-3 anti-dup; full sub-chori inventory + 7+1 forward-points nel child ticket-home). Acceptance finale (sub-chore (i) HELPER-REMOVAL-FINAL P1): `0 reference a centered_text + glow_text + compute_single_line_glyph_layout in src/+include/+content/+tests/+apps/` + `bash tools/wrap_push.sh origin main` push-range 11/11 verde. Cat-5 3-doc same-commit atomic per AGENTS.md §`### 2×-in-one-chore: deprecation reversal bundles forward-point tickets` rule.

## See also
- [TICKET-TEXT-SPEC-MIGRATION](TICKET-TEXT-SPEC-MIGRATION.md) (P1) — companion ticket; TICKET-CENTERED-TEXT-MIGRATION DEVE essere completato DOPO TICKET-TEXT-SPEC-MIGRATION per non rompere i callers (depends-on chain stabilito in §Stato header).
- [TICKET-BLOCO-5-1-3DOC-CAT5-ALIGN](TICKET-BLOCO-5-1-3DOC-CAT5-ALIGN.md) (DONE 2026-07-14) — chaser-chore canonical ticket-home per la cronaca Blocco 5.2 forward-point tracking.
