# TICKET-TEXT-SHAPING-TIMING-V1 — Per-frame shaping/bidi telemetry

## Stato: OPEN (implementazione su `main`, macchina-verifica DEFERRED-WBH)

## Problema

Chronon non espone alcun timing/counter per lo shaping HarfBuzz né per la
segmentazione bidi. I counter `text_layout_ms` e `text_rasterization_ms` esistono
già nella X-macro `CHRONON_COUNTERS_TEXT` ma non vengono mai incrementati, e non
esiste alcun conteggio delle invocazioni di `FontEngine::shape_text()`. Di
conseguenza non è possibile rispondere alla domanda "lo shaping avviene durante
`prepare` o a ogni frame?" né catturare come regressione il re-shaping per-frame
(shaping ≈ 1.2 ms su ogni frame invece che ≈ 0 dopo il frame 0).

## Soluzione

Aggiungere tre counter al dominio testo e cablarli nei punti canonici, seguendo
il pattern X-macro + `profiling::g_current_counters` già usato da
`glyph_texture_updater.cpp`:

- `text_shaping_calls` — conteggio delle invocazioni riuscite di
  `FontEngine::shape_text()`. È il detector di regressione: steady state ≈ 0
  (shaping solo a prepare-time), re-shaping per-frame ≈ `frames_total`.
- `text_shaping_ms` — tempo wall cumulato di shaping (rounding a ms via
  `std::llround`, coerente con i counter `*_ms` esistenti).
- `text_bidi_ms` — tempo wall cumulato di segmentazione FriBidi
  (`segment_bidi_runs`), anch'esso atteso a prepare-time.

## Evidenza (file toccati)

- `include/chronon3d/core/profiling/render_counter_macros.hpp` — 3 counter nel
  dominio `CHRONON_COUNTERS_TEXT` (auto-registrati in `kCounterNames` e nella
  tabella `render_counters` sqlite via X-macro).
- `src/backends/text/font_engine.cpp` — cablaggio shaping in `shape_text()`
  (conteggio + ms accumulati solo sullo shaping riuscito, guardati da
  `g_current_counters`).
- `src/backends/text/bidi_segmenter.cpp` — cablaggio `text_bidi_ms` nel path
  FriBidi riuscito.
- `apps/chronon3d_cli/utils/telemetry/telemetry_capture.hpp` — persistenza dei
  3 counter in `capture_counters()` (dashboard / `render_counters` sqlite).
- `include/chronon3d/core/profiling/benchmark_report.hpp` +
  `src/core/benchmark_report.cpp` + `tests/cli/bench_json_tests.cpp` — superficie
  JSON `chronon3d.bench.v3` con roundtrip test per i 3 campi.

## Criteri di accettazione

- [ ] I 3 counter compaiono in `kCounterNames` e vengono persistiti nella tabella
      `render_counters` (key-value) via `capture_counters()`.
- [ ] `shape_text()` incrementa `text_shaping_calls` + `text_shaping_ms` solo
      sullo shaping riuscito, senza toccare i path `std::nullopt`.
- [ ] `segment_bidi_runs()` accumula `text_bidi_ms` solo sul path FriBidi riuscito.
- [ ] Roundtrip bench JSON dei 3 campi verificato da `bench_json_tests`.
- [ ] Nessun nuovo simbolo SDK pubblico in `include/chronon3d/`; nessun nuovo
      singleton/registry/resolver/cache; nessun `<msdfgen>/<libtess2>/<unicode[...]>`.

## Forward-points

- **TICKET-TEXT-SHAPING-TIMING-V1-WBH-VERIFY** — build + `ctest` su working build
  host (questo ambiente non dispone di vcpkg FreeType/HarfBuzz/FriBidi);
  verifica che un render con text layer in steady state riporti
  `text_shaping_calls` ≈ 0 mentre un re-shaping per-frame riporti
  `text_shaping_calls` ≈ `frames_total`.
- **Cablare `text_layout_ms` / `text_rasterization_ms`** — i due counter sono già
  dichiarati ma mai incrementati; il cablaggio richiede il chiarimento del punto
  di misura canonico nel layout engine (fuori scope per questo ticket, PR piccole
  e mirate).
