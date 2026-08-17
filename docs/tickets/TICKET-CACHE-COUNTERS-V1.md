# TICKET-CACHE-COUNTERS-V1 — Domain-scoped cache hit/miss counters

## Stato: OPEN (implementazione su `main`, macchina-verifica DEFERRED-WBH)

## Problema

Chronon espone solo un generico `cache_hits`/`cache_misses` (che di fatto misura
soltanto il node cache del render graph) e `glyph_atlas_hits`/`glyph_atlas_misses`.
Non è possibile attribuire il riuso di asset a una cache specifica, né rispondere
alle domande del benchmark delle 100 varianti:

- un'immagine statica viene decodificata una volta (miss ≈ 1) o a ogni frame?
- il font face è risolto a prepare-time (miss ≈ 1) o per-frame?
- il glyph atlas è caldo (hit ratio ≈ 1)?
- gli asset promossi a superficie GPU vengono riusati o ricaricati?

## Soluzione

Aggiungere 10 counter al dominio `CHRONON_COUNTERS_CACHE` (auto-registrati in
`kCounterNames` + tabella `render_counters` sqlite via X-macro), cablati nei
punti canonici di ciascuna cache:

| Counter | Punto di cablaggio |
| ------- | ------------------ |
| `node_cache_hits` / `node_cache_misses` | `src/render_graph/executor/cache_evaluator.cpp` (a fianco dei generici `cache_hits`/`cache_misses`) |
| `image_cache_hits` / `image_cache_misses` | `src/backends/assets/image_cache.cpp` `get_or_load()` |
| `font_cache_hits` / `font_cache_misses` | `src/backends/text/font_engine.cpp` `shape_text()` (lookup `face_cache`) |
| `glyph_cache_hits` / `glyph_cache_misses` | `src/backends/text/glyph_texture_updater.cpp` (a fianco di `glyph_atlas_*`) |
| `gpu_asset_cache_hits` / `gpu_asset_cache_misses` | `src/runtime/gpu_asset_cache.cpp` `acquire()` |

`node_cache_*` è un alias esplicito dei generici `cache_hits`/`cache_misses`
(mantenuti per backward-compat): entrambi sono incrementati nello stesso punto,
così il summary può usare un naming unificato `*_cache_hits`/`*_cache_misses`
senza rompere i consumer esistenti.

## Evidenza (file toccati)

- `include/chronon3d/core/profiling/render_counter_macros.hpp` — 10 counter nel
  dominio `CHRONON_COUNTERS_CACHE`.
- `src/render_graph/executor/cache_evaluator.cpp` — node cache.
- `src/backends/assets/image_cache.cpp` — image cache (pre-check `contains` prima
  di `compute_if_absent`, guardato da `g_current_counters`).
- `src/backends/text/font_engine.cpp` — font face cache.
- `src/backends/text/glyph_texture_updater.cpp` — glyph cache.
- `src/runtime/gpu_asset_cache.cpp` — GPU asset cache.
- `apps/chronon3d_cli/utils/telemetry/telemetry_capture.hpp` — persistenza in
  `render_counters` / dashboard.
- `include/chronon3d/core/profiling/benchmark_report.hpp` +
  `src/core/benchmark_report.cpp` + `tests/cli/bench_json_tests.cpp` — superficie
  JSON `chronon3d.bench.v3` con roundtrip.

## Criteri di accettazione

- [ ] I 10 counter compaiono in `kCounterNames` e sono persistiti in
      `render_counters` via `capture_counters()`.
- [ ] Node/glyph/gpu-asset incrementano hit/miss sullo stesso evento dei counter
      pre-esistenti (nessun doppio conteggio tra le due coppie).
- [ ] Image cache: `image_cache_misses` ≈ 1 per asset statico (decode-once),
      `image_cache_hits` cresce con i lookup successivi.
- [ ] Font cache: `font_cache_misses` ≈ 0 in steady state (face risolto una volta).
- [ ] Roundtrip bench JSON dei 10 campi verificato da `bench_json_tests`.
- [ ] Nessun nuovo simbolo SDK pubblico; nessun nuovo singleton/registry/cache.

## Forward-points

- **TICKET-CACHE-COUNTERS-V1-WBH-VERIFY** — build + `ctest` su working build host
  (questo ambiente non dispone di vcpkg FreeType/HarfBuzz/FriBidi/Blend2D);
  verifica su un render con asset statici che `image_cache_misses`/`font_cache_misses`
  siano ≈ 1 e che `glyph_cache_hits`/`gpu_asset_cache_hits` crescano senza miss
  in steady state.
- **`node_lookup_ms`** — il timing del lookup del node cache (plan item #7) è fuori
  scope qui; richiede un punto di misura dedicato in `cache_evaluator.cpp`.
