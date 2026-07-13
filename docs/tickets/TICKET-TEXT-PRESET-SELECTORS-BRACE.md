# TICKET-TEXT-PRESET-SELECTORS-BRACE — 3 missing AnimatedValue&lt;f32&gt; initializer closing braces

## Stato: CLOSED (2026-07-13, rot-cascade resolved upstream at commit `4791e98b` — braced initializers correctly defined in `src/registry/text_preset_factories_reveal_selectors.cpp`; local fix superseded)

## Problema

In `src/registry/text_preset_factories_reveal_selectors.cpp`, le 3 funzioni
di compositor body del SELECTOR sub-bucket (Reveal · SELECTORS) hanno
una rot-syntax nel initializer di `AnimatedValue<f32>{...}`: manca 1
closing brace tra la fine della inner-list e la chiusura della
dichiarazione.

3 siti affetti (uno per funzione):

1. `compose_masked_line_reveal` — `line_sel.end` initializer
   `AnimatedValue<f32>{{Frame{0}, 0.0f, eo_line}, {Frame{30}, 100.0f, eo_line}};`
   — il `};` finale chiude solo la inner initializer-list, ma manca
   la chiusura di `AnimatedValue<f32>{...}`. Il compilatore riporta
   "expected `}` before `;` token".
2. `compose_word_cascade` — `word_sel.end` initializer con stesso pattern
   (Frame{48}).
3. `compose_character_cascade` — `grapheme_sel.end` initializer con stesso
   pattern (Frame{24}).

Il basic sub-bucket (`text_preset_factories_reveal_basic.cpp`) NON ha
questo pattern perché nessuno dei 6 `compose_*` basic usa una
`AnimatedValue<f32>` initializer-list con più keyframe — usa solo il
pattern single-value `AnimatedValue<f32>{0.0f}` che è bracket-balanced.

## Decisione di riparazione (chore cycle 2026-07-13)

Aggiungere 1 closing brace per ciascuno dei 3 siti:
`line_sel.end = AnimatedValue<f32>{ ... }};` (chiusura inner-list +
chiusura initializer-list + `;`).
Idem per `word_sel.end` e `grapheme_sel.end`.

Pattern-replacement univoco per ciascuno dei 3 siti via unique anchor
(Frame{value}/easing name diversi per funzione):
- Line: `{Frame{30}, 100.0f, eo_line}};` → `{Frame{30}, 100.0f, eo_line}}};`
- Word: `{Frame{48}, 100.0f, eo_words}};` → `{Frame{48}, 100.0f, eo_words}}};`
- Char: `{Frame{24}, 100.0f, eo_char}};` → `{Frame{24}, 100.0f, eo_char}}};`

Il fix è stato applicato in working tree (`str_replace` 3-sito) ma NON
committed a questa session perché la catena rot-cascade include almeno
un altro layer (vedi TICKET-RENDER-GRAPH-CACHEKEY-INCOMPLETE-TYPE).

## Criteri di accettazione per chiusura ticket

1. Stage 2 (`cmake --build build/manual-test --target
   chronon3d_core_tests`) deve passare senza l'errore
   "expected `}` before `;` token" presso
   `src/registry/text_preset_factories_reveal_selectors.cpp`.
2. Le 3 funzioni selectors (`compose_masked_line_reveal`,
   `compose_word_cascade`, `compose_character_cascade`) devono produrre
   `std::optional<TextAnimatorSpec>` con `selectors[0].end` valorizzato
   con `AnimatedValue<f32>` correttamente popolato.
3. Regression lock: aggiungere un singolo test in
   `tests/text/test_text_preset_registry.cpp` (o equivalente) che
   istanzia le 4 preset reveal-selectors e verifica che
   `descriptor.builder` sia invocabile senza eccezioni.

## Forward-points & cross-link

- **Parallel onion-peel rot**: TICKET-RENDER-GRAPH-CACHEKEY-INCOMPLETE-TYPE
  (build rot successivo a questo, downstream del pattern rot-cascade).
- A monte: TICKET-TEXT-HEADER-CMAKE-REWIRE-ROT (4-commit cascade precedent
  per onion-peel rot-fix atomic commits).
- I 3 siti rot pointer a una singola root-cause: la conversione del
  pre-refactor `GlyphSelectorSpec.line_sel` (con `end: AnimatedValue`
  nested-init) del vecchio `text_preset_factories_reveal.cpp` (pre-split
  refactoring) al post-split `text_preset_factories_reveal_selectors.cpp`
  ha perso 1 closing brace durante l'estrazione TU (la catena
  M1.5#3/multi-factory split).

## Origine

Ticket aperto durante Azione 18 chore cycle (regression lock per il
silent failure di `content/animation_compositions.cpp::anim_typewriter`)
quando l'agent 2 ha scoperto il rot con il rebuild dopo `rm -rf
build/manual-test` + `cmake --preset linux-fast-dev`. Cadenza
onion-peel: prima rot-fix risolve 3 errori di braced-init, ma ne
affiorano altri in `node_runner.cpp` (forward-point TICKET-RENDER-GRAPH-
CACHEKEY-INCOMPLETE-TYPE) che bloccano Stage 2 BUILD per un'altra rot
ben distinta.
