# Text Rendering — Colli di Bottiglia

> **Data analisi:** 8 Giugno 2026 (build `2796e99`)
> **Pipeline analizzata:** Font → Layout → Rasterizzazione → Material Effects → Compositing

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

## 🔴 1. Dual Font Engine (FreeType+HarfBuzz vs Blend2D)

**Sintomo**: Layout leggermente sballato che richiede correzioni post-hoc a livello di pixel (ink centering). Il codice ha un commento esplicito: *"wrong if FreeType+HarfBuzz and Blend2D disagree on glyph widths"*.

**Causa**: Due motori font completamente diversi:
- **FreeType + HarfBuzz** → usato per `measure_text()`, `shape_text()`, `get_font_metrics()` nel layout engine
- **Blend2D** → usato per la rasterizzazione effettiva dei glifi (`fillUtf8Text`, `strokeUtf8Text`)

Le larghezze dei glifi possono differire tra i due motori (hinting, rendering engine). Il codice compensa con uno scan pixel post-raster per rilevare l'ink bound e regolare la centratura.

**Dove**:
- `text_rasterizer_render.cpp` — righe ~200-280: calcolo `dx_align` da FontEngine, poi corretto da ink scanning
- `text_rasterizer_render.cpp` — righe ~370-450: ink_center_frac correction

**Fix**: Usare Blend2D (`BLFont.getTextMetrics()`) anche per la misurazione nel layout engine, eliminando la dipendenza da FT+HB nel percorso di layout critico. Tenere FT+HB solo per `TextAnimator` (che ha bisogno di glyph bbox per animazione per-glyph).

**Sforzo**: Alto (richiede cambiamenti architetturali in `TextLayoutEngine`)

---

## 🔴 2. Layout Engine: Chiamate Per-Carattere O(n²)

**Sintomo**: Testo lungo con word-wrap è lentissimo. L'auto-fit (ricerca binaria) esegue fino a 8 layout completi.

**Causa**: `TextLayoutEngine` in word-wrap e character-wrap fa una chiamata `measure_text()` per **ogni carattere o token**. Ogni chiamata attiva HarfBuzz shaping per una stringa di 1 carattere — overhead di shaping + lock mutex sproporzionato rispetto al lavoro fatto.

In `auto_fit` mode, la ricerca binaria esegue fino a **8 layout completi**, ciascuno con le stesse chiamate per-carattere.

**Dove**:
- `text_layout_engine.hpp` — `measure_char()`, `measure_string()` chiamate in loop per ogni carattere
- `text_layout_engine.hpp` — `layout_single_run()`: token loop con `measure_string_input()`
- `text_layout_engine.hpp` — `auto_fit`: fino a 8 iterazioni bisezione

**Esempio**: Una frase di 100 caratteri con word-wrap = 100 chiamate `measure_text()`. Con auto-fit: fino a 800 chiamate. Ogni chiamata: lock mutex + HarfBuzz shape di 1 carattere + unlock.

**Fix**: Misurare una volta la larghezza di ogni carattere all'inizio (array di float), poi usare lookup O(1) durante il wrapping. Oppure usare `shape_text()` una volta sola per l'intero paragrafo e scorrere i `glyph_positions` risultanti.

**Sforzo**: Medio

---

## 🟡 3. Ink Trimming: Scansione Full-Image O(w×h)

**Sintomo**: Ogni rasterizzazione testo (cache miss) ha un overhead di scansione dell'intera immagine.

**Causa**: Dopo aver rasterizzato il testo su BLImage, il codice esegue una scansione O(w×h) per:
1. Trovare l'ultima riga con contenuto significativo (trimming)
2. Calcolare l'ink bounding box per la correzione di centratura
3. Pulire righe isolate di "haze" (pixel semi-trasparenti)

Per un testo grande (es. 1920×1080 con font 96px), sono ~2M pixel letti in loop.

**Dove**:
- `text_rasterizer_render.cpp` — sezione "Trim trailing rows AND compute ink-bounds" (~righe 370-460)

**Fix**: 
- Campionare ogni 4-8 pixel invece di ogni pixel per trovare i bound
- Usare le informazioni già note dal layout engine (bounding box teorici) per limitare l'area di scansione
- Early exit: fermarsi quando l'ultima riga di contenuto è stata trovata

**Sforzo**: Basso

---

## 🟡 4. Bevel: Edge Detection CPU-Intensiva O(w×h×bp²)

**Sintomo**: L'effetto bevel aggiunge 2-5ms per testo, scalando con la dimensione dell'immagine e il raggio bevel.

**Causa**: Il bevel scan per ogni pixel cerca in un intorno di `bevel_px` pixel in tutte e 4 le direzioni (top, left, bottom, right). Per ogni direzione, fa un loop su [x-bp, x+bp] o [y-bp, y+bp], con `std::max` su ogni campione.

Per un testo 500×200 con bevel 2px: ~100K pixel × 5 accessi × 4 direzioni = 2M letture.

**Dove**:
- `text_material.cpp` — sezione "Bevel (fake 3D edge)" (~righe 160-270)

**Fix**: Implementare con morphological dilate/erode usando box filter separabile (orizzontale/verticale), O(w×h×2) invece di O(w×h×bp). Lo stesso principio del box blur già usato per inner shadow.

**Sforzo**: Medio

---

## 🟡 5. Shadow/Glow: 3-4 Copie Pixel per Layer

**Sintomo**: Ogni shadow/glow con blur copia i pixel 3-4 volte. Per N ombre, costo lineare.

**Causa**: Pipeline attuale per shadow/glow con blur:
1. Crea BLImage copia del testo tintata (write)
2. Acquisisce Framebuffer dal pool (allocazione)
3. Compone BLImage sul FB via `blend2d_bridge::composite_bl_image` (copia pixel)
4. Applica blur sul FB (lettura + scrittura)
5. Compone FB sul framebuffer finale con blend mode (copia pixel)

Ogni copia costa w×h×4 byte di throughput memoria.

**Dove**:
- `text_shadow.cpp` — ~righe 50-80
- `text_glow.cpp` — ~righe 50-80

**Fix**:
- Unire tint + blur in un unico kernel (tinta al volo mentre si sfoca)
- Specializzare il blur per lavorare direttamente su BLImage senza passare dal Framebuffer
- Per ombre multiple, fondere in un unico passo

**Sforzo**: Medio

---

## 🟢 6. Cache Granularità: Solo Full-Image

**Sintomo**: Animazioni con scale/rotation continuous = 100% cache miss. Se lo stesso testo cambia padding, size, o trasformazione, nessun hit.

**Causa**: La cache testo (`TextRasterization`) è a livello di BLImage intero. La chiave include `effective_size`, `padding`, e `transform`. Qualsiasi variazione = miss.

Configurazione attuale: LRU 128MB, 8 shard, mutex condiviso. La cache include anche un'opportunità di deduplicazione quando lo stesso testo è renderizzato più volte alla stessa dimensione.

**Dove**:
- `text_rasterizer_cache.cpp` — `store_text_cache()`, `lookup_text_cache()`
- `text_rasterizer_render.cpp` — `hash_text_style()` chiamata all'inizio

**Fix**:
- Per animazioni con transform, rasterizzare a una risoluzione fissa e scalare via Blend2D (che ha JIT per transform). La cache tiene solo il testo base.
- Valutare un glyph atlas per riutilizzo跨-frame dei singoli glifi

**Sforzo**: Alto

---

## 🟢 7. Mutex Contention sui Lock Globali

**Sintomo**: Sotto carico multi-thread (es. frame-level parallelism), i lock globali diventano contention point.

**Causa**:
- `shared_font_engine()`: mutex [`std::mutex`] per ogni shaping (in `FontEngine::Impl::cache_mutex`)
- Cache raster: `std::shared_mutex` (shared_lock per read, unique_lock per write)
- Cache shadow/glow: `std::mutex` separati per ciascuna

Con 2+ render thread, ogni shaping text compete per lo stesso mutex.

**Dove**:
- `font_engine.cpp` — `cache_mutex` locked in `shape_text()`, `measure_text()`, `get_font_metrics()`
- `text_rasterizer_cache.cpp` — `get_text_cache_mutex()` (shared_mutex)
- `text_processor_helpers.hpp` — `text_glow_cache_mutex()`, `text_shadow_cache_mutex()`

**Fix**:
- Shard: usare mutex per-specifica-font invece di uno globale
- Read-write lock per font face cache (la maggior parte delle operazioni sono read)
- Thread-local font engine per shaping parallelo

**Sforzo**: Basso

---

## Riepilogo Priorità

| # | Area | Gap | Impatto | Sforzo | File |
|---|---|---|---|---|---|
| **1** | Dual font engine (FT+HB vs B2D) | Misura diverse dal render | 🔥🔥🔥 Layout errati, codice fragile | Alto | `text_layout_engine.hpp`, `text_rasterizer_render.cpp` |
| **2** | Layout: per-char measure_text() | O(n²) per testo con wrap | 🔥🔥🔥 Lento per testi lunghi | Medio | `text_layout_engine.hpp`, `font_engine.cpp` |
| **3** | Ink trimming full-image scan | w×h pixel letti a ogni miss | 🔥🔥 Overhead su testi grandi | Basso | `text_rasterizer_render.cpp` |
| **4** | Bevel O(w×h×bp²) | Edge detection naive | 🔥🔥 2-5ms su testi medi | Medio | `text_material.cpp` |
| **5** | Shadow/glow: 3-4 copie pixel | BLImage→FB→blur→FB | 🔥 1-3ms per layer | Medio | `text_shadow.cpp`, `text_glow.cpp` |
| **6** | Cache full-image, non per-glyph | Miss per scale diverse | 🔥 Animazioni degradate | Alto | `text_rasterizer_cache.cpp` |
| **7** | Mutex contention | Lock globali sotto multi-thread | 🔥 Solo con parall. frame-level | Basso | `font_engine.cpp`, cache vari |

---

## Roadmap Suggerita

### Fase 1 — Visibilità (basso sforzo, alto impatto diagnostico)
- [ ] **#3** Ottimizzare ink trimming con scansione campionata
- [ ] **#7** Shard dei mutex per font/cache

### Fase 2 — Ottimizzazioni mirate (medio sforzo)
- [ ] **#2** Misura caratteri una tantum in array per layout
- [ ] **#4** Bevel con box filter separabile
- [ ] **#5** Unire tint + blur in kernel unico per shadow/glow

### Fase 3 — Architetturale (alto sforzo)
- [ ] **#1** Unificare i due font engine: Blend2D per misurazione, FT+HB solo per TextAnimator
- [ ] **#6** Glyph atlas per riutilizzo跨-frame
