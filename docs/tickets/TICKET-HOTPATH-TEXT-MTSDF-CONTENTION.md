# TICKET-HOTPATH-TEXT-MTSDF-CONTENTION — MTSDF FreeType critical section

> **Stato:** LANDED (codice) 2026-09-13 — la guardia è ora **per-face**: il
> boundary del generatore riceve un `MtsdfFaceGuard` (face + mutex) invece di
> una `FT_Face` grezza, e la `static std::mutex` function-local è stata
> rimossa. Il mutex per-entry vive accanto alla face nel `FreeTypeFaceCache`
> (`FreeTypeFaceLease`) e viene consegnato a chi muta la face. Resta da
> eseguire solo la conferma **numerica** (`mtsdf_lock_wait_us` che non cresce
> col numero di worker su corpus multi-font), che richiede un host con corpus
> testo: i contatori `mtsdf_*` sono la superficie di misura.

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

### Fase 1 (precedente) — lavoro fuori dal lock

**File**: `src/backends/text/mtsdf_generator.cpp`

1. La guardia copre **solo** il lavoro posseduto da FreeType:
   `FT_Load_Glyph`, `FT_Outline_Get_CBox`, `FT_Outline_Decompose` (legge la
   `FT_Outline` della face tramite i callback) e la lettura di
   `face->glyph->advance.x`. `min_x/max_x/min_y/max_y` e `advance_x` sono
   copiati **dentro** la sezione critica: dopo l'unlock la face può essere
   ricaricata da un altro worker.
2. Costruzione edges, padding/bbox arithmetic, `result.rgba8.resize` e il loop
   di distanza girano **fuori** dal lock e sono paralleli.

### Fase 2 (2026-09-13) — granularità per-face

**File**: `include/chronon3d/backends/text/mtsdf_generator.hpp`,
`src/backends/text/mtsdf_generator.cpp`,
`include/chronon3d/backends/text/text_render_resources.hpp`,
`src/backends/text/text_render_resources.cpp`,
`src/media/video/direct_yuv/text_texture_cache.cpp`,
`src/render_graph/nodes/text_run/gpu_text_run.cpp`

3. La `static std::mutex face_mutex` è stata **rimossa**. Il parametro `FT_Face`
   è sostituito da `MtsdfFaceGuard { FT_Face face; std::mutex* mutex; }`: il
   generatore prende in lock **il mutex di chi possiede la face**, quindi font
   diversi rasterizzano concorrentemente. Un guard senza mutex **fallisce
   chiuso** (`valid() == false` → `nullopt`), perché non può dimostrare quale
   lock protegge la face.
4. `FreeTypeFaceCache` memorizza ora `FreeTypeFaceLease { shared_ptr<FT_Face>
   face; shared_ptr<std::mutex> mutex; }` per chiave `(path, face_index,
   size_bucket)`. Chiavi identiche condividono **entrambi** (serializzazione
   corretta sulla stessa face); chiavi diverse ottengono face e mutex distinti.
   `get_face()` resta come proiezione (`get_face_lease(...).face`), quindi
   nessun call-site di sola lettura cambia comportamento.
5. `FontFaceHandle` trasporta `ft_mutex` accanto a `ft_lifeline`/`ft_face`, e
   `gpu_text_run.cpp` inoltra il guard `{ft_face, ft_mutex.get()}`.
6. Le face locali di `text_texture_cache.cpp` (`FaceOwner`) hanno una mutex
   propria accanto alla face, così anche il boundary DirectYUV riceve sempre il
   lock che protegge la sua face.

## Evidenza

- **Gate unità**: `chronon3d_mtsdf_generator_tests` — 6 test case / 114
  assertions `PASS`. Coprono: glyph multi-canale reale; fail-closed del guard
  senza mutex; **byte-parity** dell'intera matrice glyph ripetuta; granularità
  per-face (`get_face_lease` stessa chiave → stesso mutex, chiave diversa →
  mutex diverso); parità byte-esatta della generazione concorrente su 8 worker
  rispetto alla seriale; parità di `mtsdf_glyphs_generated`.
- **Verifica strutturale**: `grep 'static std::mutex\|face_mutex'
  src/backends/text/mtsdf_generator.cpp` → nessun match.
- I tre contatori `mtsdf_*` (X-macro `CHRONON_COUNTERS_TEXT` in
  `include/chronon3d/core/profiling/render_counter_macros.hpp`) restano come
  diagnostica permanente; nomi conformi al gate `TICKET-TIMING-SUFFIX-GATE`
  (`_wait` / `_wall` prima dell'unità).

## Demolition Debt

| Campo | Valore |
|---|---|
| **Owner** | Text backend maintainer |
| **Reason** | Il boundary riceveva una `FT_Face` grezza, non l'entry del `FreeTypeFaceCache` che la possiede: la mutua esclusione restava process-wide invece che per-face. |
| **Exit condition** | Guardia per-face (fatto: `MtsdfFaceGuard` trasporta il mutex per-entry) **e** `mtsdf_lock_wait_us` su corpus testo multi-font a 8 worker che non cresce col numero di worker. |
| **Equivalence test/gate** | Byte-parity del bitmap MTSDF per la matrice (font, glyph, em_px, range, padding) — `MTSDF bitmap byte-parity across repeated generation` + `MTSDF concurrent generation on a shared face is byte-identical to serial` — più `mtsdf_glyphs_generated` invariato (`MTSDF counter parity`). |
| **Removal scope** | `static std::mutex face_mutex` in `generate_mtsdf_glyph` → **RIMOSSO**; parametro `FT_Face` → sostituito da `MtsdfFaceGuard`; i tre contatori `mtsdf_*` restano (diagnostica permanente). |
| **Status** | `READY_TO_REMOVE` — la parte strutturale è chiusa; resta solo la conferma numerica multi-font su host con corpus, che non implica codice ulteriore. |

## Cosa NON è stato fatto (e perché)

- **Nessuna misurazione runtime della curva `mtsdf_lock_wait_us` vs worker**:
  richiede un host con corpus testo multi-font. La riduzione è strutturale
  (nessuna `static std::mutex` → nessun lock process-wide) e la correttezza del
  parallelismo è coperta dalla parità byte-esatta concorrente.
- **Correzione storica:** una precedente revisione di questo ticket dichiarava
  `REMOVED` citando simboli inesistenti; una revisione successiva lo riportò ad
  `ACTIVE` perché la `static std::mutex` era ancora nel sorgente. Entrambe le
  versioni sono superate: i simboli `MtsdfFaceGuard` / `FreeTypeFaceLease` /
  `get_face_lease` ora **esistono** e la `static std::mutex` **non esiste più**
  (verificato con `grep` sul sorgente e con il gate di unità).
