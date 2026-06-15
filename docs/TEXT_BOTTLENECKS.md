# Text Rendering — Colli di Bottiglia

> **Data analisi originale:** 8 Giugno 2026 (build `2796e99`)
> **Ultimo aggiornamento:** 15 Giugno 2026 (aggiornamento SIMD benchmark)
> **Pipeline analizzata:** Font → Layout → Rasterizzazione → Material Effects → Compositing
>
> ✅ = Risolto  🟡 = Parzialmente risolto  🔴 = Ancora aperto

---

## Architettura della Pipeline Testo

```
FontEngine (FT + HB)   →   TextLayoutEngine   →   text_rasterizer_render (Blend2D)
     ↓                                                                    ↓
  shape_text()                        ↓                          BLImage PRGB32
  measure_text()              word wrap, align,                   + cache LRU 128MB
  get_font_metrics()          auto-fit, ellipsis                       ↓
                                                                 TextMaterial
                                                               (gradient, bevel,
     TextShadow/TextGlow      ←   SoftwareTextProcessor           highlight, 
     (cache 64MB, blur)           (orchestrator)                  inner shadow)
                                         ↓
                                blend2d_bridge::composite_*
                                   → framebuffer
```

### File Chiave

| File | Ruolo | Righe |
|---|---|---|
| `src/backends/text/font_engine.cpp` | FreeType + HarfBuzz shaping | ~400 |
| `src/backends/text/text_rasterizer_render.cpp` | Blend2D text rasterization | ~600 |
| `include/chronon3d/backends/text/text_layout_engine.hpp` | Text layout + wrapping | ~500 header-only |
| `src/backends/text/text_material.cpp` | Premium text effects | ~350 |
| `src/backends/software/processors/software_text_processor.cpp` | Pipeline orchestrator | ~260 |
| `src/backends/software/processors/text_glow.cpp` | Glow effect | ~110 |
| `src/backends/software/processors/text_shadow.cpp` | Shadow effect | ~110 |
| `src/backends/text/text_rasterizer_cache.cpp` | LRU cache | ~70 |

---

## ✅ 1. Dual Font Engine (FreeType+HarfBuzz vs Blend2D) — RISOLTO

> **Risolto:** ~Giugno 2026

**Stato attuale**: Sia la misurazione che il rendering usano **HarfBuzz** come shaper unico. `HbToBlGlyphRun` converte i glifi HarfBuzz in formato Blend2D per `fillGlyphRun()`. Lo stroke usa `FtGlyphPathBuilder` che decompone gli outline FreeType dei glifi già shaped da HarfBuzz. Blend2D è usato **solo** per la rasterizzazione (fill/stroke), non per shaping/measurement.

Il commento nel codice ora dice: *"Use FontEngine (HarfBuzz) for text measurement — same shaper as the fillGlyphRun calls below. This eliminates the measurement vs renderer width discrepancy."*

Il pixel-ink centering è ora **opt-in** via `TextCenteringMode::PixelInk` e descritto come *"debug/transition aid"*.

**Dove**: `text_rasterizer_render.cpp` — `HbToBlGlyphRun`, `FtGlyphPathBuilder`, `render_run()`

---

## ✅ 2. Layout Engine: Chiamate Per-Carattere O(n²) — RISOLTO

> **Risolto:** ~Giugno 2026

**Stato attuale**: Le funzioni `measure_char()`, `measure_string_input()`, `per_char` non esistono più nel codice. Il layout engine ora usa `FontEngine::shape_text()` per l'intera stringa in una singola chiamata HarfBuzz. `measure_text()` delega a `shape_text()` e restituisce `run->width`.

`PlacedGlyphRun` e `resolve_placed_glyph_run()` forniscono posizionamento canonico con tracking-aware advance, eliminando la logica duplicata di tracking tra fill, stroke, typewriter e TextAnimator.

**Dove**: `font_engine.cpp` — `shape_text()`, `measure_text()`, `resolve_placed_glyph_run()`

---

## ✅ 3. Ink Trimming: Scansione Full-Image O(w×h) — RISOLTO

> **Risolto:** ~Giugno 2026

**Stato attuale**: La scansione ora usa **campionamento stride 4×2** (`kSampleStrideX=4`, `kSampleStrideY=2`) → **8× meno pixel** rispetto alla scansione completa. I bound campionati vengono espansi per compensare i gap dello stride. I conteggi per riga vengono propagati alle righe saltate. Il descender margin (`font_size * 0.25f`) protegge le code di g, p, q, y, j dal trimming prematuro.

**Dove**: `text_rasterizer_render.cpp` — sezione "Trim trailing rows AND compute ink-bounds" con `kSampleStrideX=4`, `kSampleStrideY=2`

---

## ✅ 4. Bevel: Edge Detection CPU-Intensiva O(w×h×bp²) — RISOLTO

> **Risolto:** ~Giugno 2026

**Stato attuale**: Il bevel ora usa **sliding-window maximum separabile** (deque per riga/colonna) con complessità **O(w×h)** invece di O(w×h×bp). Il codice precomputa `h_max` (orizzontale) e `v_max` (verticale) in due passate, poi usa i massimi precomputati per il rilevamento dei bordi in un singolo passo.

**Dove**: `text_material.cpp` — sezione "Bevel (fake 3D edge)" con deque-based sliding window

---

## 🟡 5. Shadow/Glow: Copie Pixel per Layer — PARZIALMENTE RISOLTO

> **Migliorato:** ~Giugno 2026

**Stato attuale**: Il blur viene ora applicato **direttamente su BLImage**, evitando l'allocazione e copia intermedia del Framebuffer. La cache BLImage (`text_shadow_cache`, `text_glow_cache`) evita la re-generazione quando i parametri non cambiano. Il glow usa una pipeline condivisa (`GlowPipeline::render`).

Le conversioni `bl_image_prgb32_to_color_row` e `color_to_prgb32_row` sono state implementate con path AVX2 2-pixel (ScalableTag 8 lanes, LowerHalf/UpperHalf, transparent-pair fast-path). **Benchmark standalone** (std::chrono, 200 warmup + 10 runs, mediana) mostra che il path SIMD è **1.5-2.7× più lento** dello scalare per righe tipiche del glow (256-3840px). Il bottleneck è lo scalar unpack/pack dei uint32 (bit shifts, `1/a` division), non il calcolo float SIMD. Solo a 1920px per PRGB32→Color c'è un leggero miglioramento (1.12×, transparent-pair fast-path). 28 unit test validano la correttezza.

**Rimanente**: Per ottenere un vero speedup, servirebbe integer SIMD unpack (`ScalableTag<uint32_t>` + `ShiftRight<24>` + `And` + `ConvertTo<float>`) per eliminare il bottleneck scalare.

**Dove**:
- `text_shadow.cpp` — blur diretto su BLImage + cache
- `text_glow.cpp` — GlowPipeline condivisa + cache
- `highway_color_kernels.cpp` — implementazioni AVX2 + benchmark negative
- `tests/bench/bench_prgb32_conversion.cpp` — benchmark standalone SIMD vs scalar
- `tests/simd/test_simd_kernels.cpp` — 28 unit test PRGB32↔Color

---

## ✅ 6. Cache Granularità: Solo Full-Image — RISOLTO

> **Risolto:** Giugno 2026

**Stato attuale**: Il `GlyphAtlas` è integrato nel percorso critico di `text_rasterizer_render.cpp`. Per ogni run di testo con fill solido (no gradienti, no stroke, no box background, no transform geometrico), il renderer:

1. **Lookup**: verifica che tutti i glifi siano in cache con lo stesso `fill_color_rgba`
2. **Hit**: blita i bitmap individuali da atlas (salta `fillGlyphRun`)
3. **Miss**: renderizza normalmente, poi dopo `ctx.end()` estrae e memorizza i bitmap individuali via `glyph_atlas_store_from_placed_run()`

La cache è keyed da `(font_path, glyph_id, font_size)` con matching del colore fill. 3 nuovi counter profiling: `glyph_atlas_hits`, `glyph_atlas_misses`, `glyph_atlas_stored`.

**Dove**:
- `glyph_atlas.cpp` — `glyph_atlas_store_from_placed_run()` (estrazione per-glyph da PlacedGlyphRun)
- `text_rasterizer_render.cpp` — `try_atlas_blit()`, `can_use_glyph_atlas()`, `pending_glyph_stores`
- `counters.hpp` — counter `glyph_atlas_hits/misses/stored`

---

## ✅ 7. Mutex Contention sui Lock Globali — RISOLTO

> **Risolto:** ~Giugno 2026

**Stato attuale**: `FontEngine::Impl` usa `std::shared_mutex` con `shared_lock` per letture (lookup, `can_load()`) e `unique_lock` per inserimento. `can_load()` usa un pattern a due fasi: shared lock prima, upgrade a exclusive solo su miss. Il `glyph_bbox_cache` usa `LruCache` con sharding (2 shard). Le cache testo, shadow e glow usano `shared_mutex` condiviso.

**Dove**:
- `font_engine.cpp` — `shared_mutex` + two-phase locking in `can_load()`
- `text_rasterizer_cache.cpp` — `shared_mutex` per cache testo
- `glyph_atlas.cpp` — `shared_mutex` per atlas per-glyph

---

## Riepilogo Priorità

| # | Area | Gap | Impatto | Sforzo | File | Stato |
|---|---|---|---|---|---|---|
| **1** | Dual font engine (FT+HB vs B2D) | Misura diverse dal render | 🔥🔥🔥 Layout errati, codice fragile | Alto | `text_layout_engine.hpp`, `text_rasterizer_render.cpp` | ✅ Risolto |
| **2** | Layout: per-char measure_text() | O(n²) per testo con wrap | 🔥🔥🔥 Lento per testi lunghi | Medio | `text_layout_engine.hpp`, `font_engine.cpp` | ✅ Risolto |
| **3** | Ink trimming full-image scan | w×h pixel letti a ogni miss | 🔥🔥 Overhead su testi grandi | Basso | `text_rasterizer_render.cpp` | ✅ Risolto |
| **4** | Bevel O(w×h×bp²) | Edge detection naive | 🔥🔥 2-5ms su testi medi | Medio | `text_material.cpp` | ✅ Risolto |
| **5** | Shadow/glow: copie pixel | BLImage→FB→blur→FB | 🔥 1-3ms per layer | Medio | `text_shadow.cpp`, `text_glow.cpp` | 🟡 SIMD benchmark negativo |
| **6** | Cache full-image, non per-glyph | Miss per scale diverse | 🔥 Animazioni degradate | Alto | `text_rasterizer_render.cpp`, `glyph_atlas.cpp` | ✅ Risolto |
| **7** | Mutex contention | Lock globali sotto multi-thread | 🔥 Solo con parall. frame-level | Basso | `font_engine.cpp`, cache vari | ✅ Risolto |

---

## Roadmap Suggerita (Aggiornata)

### Fase 1 — ✅ Completata
- [x] **#3** Ottimizzare ink trimming con scansione campionata (stride 4×2)
- [x] **#7** Shared mutex + two-phase locking per font/cache
- [x] **#2** Misura caratteri via singola chiamata `shape_text()`
- [x] **#4** Bevel con sliding-window maximum separabile
- [x] **#1** HarfBuzz unico shaper per misurazione + rendering

### Fase 2 — Da completare
- [x] **#5** SIMD AVX2 2-pixel path implementato + benchmark (SIMD più lento — integer unpack necessario per speedup reale)
- [x] **#6** Integrare GlyphAtlas nel percorso critico di `text_rasterizer_render.cpp`

### Fase 3 — Nuove feature
- [ ] MSDF font atlas per scalabilità testo (ROADMAP L7)
- [ ] Testo CJK con line-breaking ICU
