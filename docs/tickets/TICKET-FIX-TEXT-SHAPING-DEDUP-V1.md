# TICKET-FIX-TEXT-SHAPING-DEDUP-V1 — Shape-call counter for dedup regression lock

> Stato: **DONE (this commit)**
> Componente: Text shaping instrumentation
> Pattern: Cat-5 ticket cap (filling missing canonical-artifact slot after upstream `feat(text): ShapedGlyphLine unifies text shaping`)

## Problema

L'upstream MARcuss-ops ha già spedito `[content/common/text/glyph_layout.hpp](content/common/text/glyph_layout.hpp)` `ShapedGlyphLine` class con la Point-8 single-shape
efficiency contract (ctor chiama `engine.shape_text` una sola volta; accessori
`width()/layout()/cursor_position()/cursor_at_end()/bbox()/reveal_count()` leggono
dal `m_run` cached). Il `tests/content/test_shaped_glyph_line.cpp` esistente
verifica già la coerenza degli accessori ma NON misura il numero di
`engine.shape_text` invocations per riga.

Senza contatore esplicito:
1. Il regression lock è invisibile nei test — un futuro refactor potrebbe
   re-introdurre la `engine.shape_text` chiamata multipla (la "scansione
   interna ripetuta nella ricerca del prossimo cluster" che la Fase 6.2
   elimina) senza trigger di test failure.
2. Il B02-Typewriter200Glyphs bench (200 glyphs con animazione per-character)
   non ha telemetry per verificare la Point-8 promise in fase di esecuzione.

## Soluzione

Aggiunto un counter diagnostico `s_shape_calls_per_line` (file-static
`std::atomic<int>` in `[content/common/text/glyph_layout.cpp](content/common/text/glyph_layout.cpp)`) incrementato esattamente una volta per ogni
invocazione di `engine.shape_text` dal primary ctor di `ShapedGlyphLine` E
dalla factory `try_shape`. Surface pubblica minimal (`glyph_layout.hpp`):

```cpp
void reset_shape_call_counter() noexcept;
int  get_shape_call_count() noexcept;
```

Test (`tests/content/test_shaped_glyph_line.cpp::"ShapedGlyphLine:
shape_calls_per_line counter == 1 on B02-equivalent 200-glyph line"`):

```cpp
content::text_reveal::reset_shape_call_counter();
content::text_reveal::ShapedGlyphLine line(text_200, 72.0f, spec, 4.0f, 0.0f, engine);
CHECK(content::text_reveal::get_shape_call_count() == 1);

const f32 w        = line.width();
auto         glyphs = line.layout();   // accessor invocations must NOT increment
auto         box    = line.bbox();
CHECK(content::text_reveal::get_shape_call_count() == 1);   // Point 8 contract holds
```

B02-equivalent: 200 glyphs di `THEQUICKBROWNFOXJUMPSOVERTHELAZYDOG` ripetuto
fino a 200 caratteri (stessa shape complexity di `bench_b02_typewriter_200_glyphs()`
in `[examples/bench_corpus/bench_corpus_scenes.cpp](examples/bench_corpus/bench_corpus_scenes.cpp)`).

Tutti gli accessori (`width`, `layout`, `bbox`, `cursor_position`,
`cursor_at_end`, `reveal_count`) leggono da `m_run` cached — nessuna
re-shape call. Il test copre `width/layout/bbox/cursor_at_end/reveal_count`
per verificare che la Point-8 promise regga sull'intera API surface.

## Criteri di accettazione

- [x] Counter `s_shape_calls_per_line` aggiunto in `glyph_layout.cpp` come
      file-static `std::atomic<int>` (Cat-3 minimal-surface: nessun nuovo
      SDK symbol; surface esposto via 2 free functions).
- [x] Increment sites: esattamente 2 (primary ctor + `try_shape` factory).
- [x] `reset_shape_call_counter()` + `get_shape_call_count()` dichiarati
      in `glyph_layout.hpp` pubblicamente.
- [x] Test `shape_calls_per_line counter == 1 on B02-equivalent 200-glyph line`
      aggiunto a `tests/content/test_shaped_glyph_line.cpp`.
- [x] Test assertions su 5 accessori (`width`, `layout`, `bbox`,
      `cursor_at_end`, `reveal_count`) per garantire Point-8 contract
      sull'intera API surface.
- [x] Cat-2: zero new symbols nel SDK pubblico in `include/chronon3d/` (`GlyphRun`,
      `FontEngine`, etc. restano immutati).
- [x] Cat-3: counter è single-purpose, no duplicazione di conta-shapes esistente.

## Forward-points

- **TICKET-FIX-TEXT-SHAPING-DEDUP-V1-3DOC-CAT5-ALIGN**: dopo la chiusura,
  verificare se i canonici docs (`CURRENT_STATUS.md`, `FOLLOWUP_TICKETS.md`,
  `ROADMAP.md`) richiedono aggiornamenti di stato per la Point-8 dedup
  ceremony. Probabilmente no — dedup è implementazione refactor non stato
  release-blocker.
- **TICKET-SHAPED-LINE-PER-LINE-VS-PER-INSTANCE-SEMANTICS**: il counter
  GLOBAL misura "engine.shape_text invocations across ShapedGlyphLine
  instance lifetimes". Per misurare "engine.shape_text invocations within
  a single ShapedGlyphLine instance lifetime" sarebbe necessario un
  counter membro non-static (più invasivo). Risolto: la Point-8 promise
  implica naturalmente che un singolo ctor = un singolo shape_text call,
  quindi il counter GLOBAL è sufficiente a garantire il contract.
- **TICKET-B02-200-GLYPHS-BENCH-INSTRUMENTATION**: il bench `bench_b02_typewriter_200_glyphs()`
  potrebbe in futuro esporre il counter come telemetry runtime
  (build-time check + per-frame check). Out of scope F6.2.
- **TICKET-SHAPED-GLYPH-LINE-PERF-BASELINE**: stabilire una baseline
  del counter su una composizione canonica (es. cinematic showcase) per
  il perf-gate future. Connettibile a `tools/check_perf_regression.sh`
  (introdotto in F1.6).
- **TICKET-EXPAND-TEST-COVERAGE-V2**: aggiungere test per `try_shape`
  factory path (failure → `std::nullopt`, success → populated
  instance, counter increment == 1). Out of scope F6.2.

## Origine

FASE 6.2 (this commit) richiede il contatore `shape_calls_per_line` come
acceptance criterion — la dedup text shaping era shipped senza telemetry,
lasciando il Point-8 single-shape efficiency contract non-testato. Questo
commit è il Cat-5 ticket cap che riempie quello slot.
