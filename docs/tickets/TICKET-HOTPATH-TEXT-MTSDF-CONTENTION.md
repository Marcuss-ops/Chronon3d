# TICKET-HOTPATH-TEXT-MTSDF-CONTENTION — MTSDF FreeType critical section

## Contesto

`src/backends/text/mtsdf_generator.cpp::generate_mtsdf_glyph()` prendeva un
`static std::mutex` function-local (quindi **un solo lock per processo**, non
per face) e lo deteneva per l'**intera** funzione:

```
FT_Load_Glyph → FT_Outline_Get_CBox → FT_Outline_Decompose
→ costruzione edges → distanza O(width × height × edges) per pixel
```

La sezione protetta necessaria è solo lo stato mutabile di FreeType
(`FT_Load_Glyph` aggiorna `face->glyph`). Tutto il resto lavora su dati locali
(`OutlineBuild`, `std::vector<Edge>`, `MtsdfGlyphBitmap`).

Effetto: ogni glyph MTSDF di ogni font, generato da qualunque worker TBB,
serializzava sulla stessa mutex — un tetto rigido su sottotitoli, composizioni
multi-font e generazione cold dell'atlas.

## Modifiche implementate

**File**: `src/backends/text/mtsdf_generator.cpp`

1. La guardia copre ora **solo** il lavoro posseduto da FreeType:
   `FT_Load_Glyph`, `FT_Outline_Get_CBox`, `FT_Outline_Decompose` (legge la
   `FT_Outline` della face tramite i callback) e la lettura di
   `face->glyph->advance.x`. `min_x/max_x/min_y/max_y` e `advance_x` sono
   copiati **dentro** la sezione critica: dopo l'unlock la face può essere
   ricaricata da un altro worker.
2. Costruzione edges, padding/bbox arithmetic, `result.rgba8.resize` e il loop
   di distanza girano **fuori** dal lock e sono paralleli.
3. Instrumentazione tramite lo strumento canonico (X-macro
   `CHRONON_COUNTERS_TEXT` in
   `include/chronon3d/core/profiling/render_counter_macros.hpp`):
   - `mtsdf_glyphs_generated` — glyph completati;
   - `mtsdf_lock_wait_us` — tempo di attesa della mutex;
   - `mtsdf_lock_hold_wall_us` — tempo di detenzione (RAII meter, stesso
     pattern di `FramebufferPool`).

   Nomi conformi al gate `TICKET-TIMING-SUFFIX-GATE`
   (`tests/core/test_render_counters.cpp`: qualificatore `_wall`/`_cpu`/`_gpu`/
   `_wait` prima dell'unità).

## Demolition Debt

| Campo | Valore |
|---|---|
| **Owner** | Text backend maintainer |
| **Reason** | La boundary riceve una `FT_Face` grezza, non l'entry del `FreeTypeFaceCache` che la possiede: la mutua esclusione resta quindi process-wide invece che per-face. |
| **Exit condition** | La guardia è per-face (il mutex per-entry vive accanto alla face e questo boundary lo riceve), e `mtsdf_lock_wait_us` su un corpus testo multi-font a 8 worker non cresce più col numero di worker. |
| **Equivalence test/gate** | Byte-parity del bitmap MTSDF generato prima/dopo (hash del `MtsdfGlyphBitmap` per una matrice (font, glyph, em_px, range, padding)) + `mtsdf_glyphs_generated` invariato a parità di corpus. |
| **Removal scope** | `static std::mutex face_mutex` in `generate_mtsdf_glyph`; il parametro `FT_Face` sostituito dall'handle tipizzato del cache entry; i tre contatori `mtsdf_*` restano (diagnostica permanente). |
| **Status** | `ACTIVE` — parte computazionale già fuori dal lock; resta la granularità per-face. |

## Cosa NON è stato fatto (e perché)

- **Nessuna misurazione runtime**: la riduzione è strutturale (la sezione
  critica non contiene più lavoro O(pixel × edges)). La conferma numerica
  richiede un host con corpus testo; i contatori sono ora la superficie di
  misura (`mtsdf_lock_wait_us` vs `mtsdf_lock_hold_wall_us`: se il wait domina
  l'hold, la granularità per-face è il prossimo passo; se l'hold domina,
  il collo di bottiglia è il codice FreeType, non la contesa).
