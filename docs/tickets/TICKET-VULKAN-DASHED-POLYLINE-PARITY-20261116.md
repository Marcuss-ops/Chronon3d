# TICKET-VULKAN-DASHED-POLYLINE-PARITY — la polilinea tratteggiata Vulkan diverge dall'oracolo software

- **Stato:** CLOSED — 2026-11-16

## Chiusura

Causa reale: `PathRasterizer` dipinge il fill e **salta lo stroke dentro il
poligono** (contratto software: `if (inside) { blend(fill); continue; }`), mentre
`fill_dashed_path` non trasporta il colore del fill. Il primo tentativo aveva
spento `has_fill`, facendo dipingere lo stroke sopra il fill (549 px).
La correzione fa due passate native e mantiene il contratto:

1. il fill viene disegnato in una passata `fill_path_surface` dedicata;
2. la passata dashed gira con `has_fill = true`, così lo shader continua a
   fare early-out sui pixel interni esattamente come il software;
3. i loop chiusi ricevono esplicitamente il vertice di chiusura, perché
   `math::flatten_path` perde il flag `Close` (`PathCommandType::Close` viene
   letto dai comandi autorati).

Evidenza: `chronon3d_vulkan_debug_context_tests` →
**1 caso / 6152 asserzioni PASS**, zero differenze per pixel (era 1462, poi
549). Nessun epsilon allargato: la parità è esatta.
- **Ambito:** Chronon3d, backend Vulkan (debug context), parità software/Vulkan
- **Origine:** suite completa 2026-11-16, `chronon3d_vulkan_debug_context_tests`

## Sintomo

Il caso «Vulkan long dashed polylines match the software stroke oracle»
(`tests/backends/test_vulkan_debug_context.cpp:311`) fallisce su un pixel di
bordo del tratteggio:

```
test_vulkan_debug_context.cpp:392: ERROR: CHECK( rgba[index] == doctest::Approx(expected.r).epsilon(1e-5) )
  values: CHECK( 0 == Approx( 0.0100228 ) )
```

Lo stesso scarto si ripete su `:393` (g), `:394` (b) e `:395` (a). L'oracolo
software dipinge il pixel con copertura parziale, il backend Vulkan lo lascia a
zero: non è un errore di colore ma di copertura ai bordi del tratteggio.

## Misura

```
ctest --test-dir build/chronon/linux-video-release -R chronon3d_vulkan_debug_context_tests --output-on-failure
```

Un caso fallito, cinque asserzioni rosse; il resto del binario passa. Log di
sessione: `Chronon3d/.tmp/ctest-full-20261116b.log`.

## Analisi

Il valore atteso `0.0100228` è una copertura sub-pixel del bordo del dash: il
percorso software la produce con l'anti-aliasing analitico dell'oracolo di
stroke, mentre il percorso Vulkan rasterizza in compute
(`src/backends/vulkan/shaders/fill_rect.comp`) con una quantizzazione diversa
della fase del tratteggio e della copertura. Un pixel che cade esattamente sul
confine del dash può quindi risultare dentro per l'oracolo e fuori per Vulkan.
Non è ancora dimostrato se la causa sia la fase del dash o l'AA: finché non è
isolata, il test non va "aggiustato" con un epsilon più largo.

## Piano di chiusura

1. **Ridurre il caso.** Costruire la stessa polilinea con un solo segmento e un
   dash di lunghezza nota; confrontare per-pixel la copertura dei due percorsi.
2. **Isolare la fase.** Verificare in `fill_rect.comp` il calcolo della
   lunghezza cumulata, della fase iniziale e degli arrotondamenti del dash
   rispetto all'oracolo software; allineare la quantizzazione.
3. **Isolare l'AA.** Se la fase è identica e la differenza resta solo sul bordo,
   decidere il contratto: allineare il modello di copertura (stessa formula nei
   due percorsi) oppure dichiarare una tolleranza esplicita per coperture
   inferiori a 1/255, documentata nel test e non applicata globalmente.
4. **Regressione.** Aggiungere un caso deterministico senza anti-aliasing (linea
   orizzontale, dash interi) che deve essere bit-identico, così la tolleranza
   resta confinata ai bordi.
5. **Verifica doppia.** Rieseguire su llvmpipe e su NVIDIA: la parità non deve
   dipendere dall'ICD.

## Criteri di accettazione

- [x] Il caso lungo passa con parità esatta (6152/6152, zero tolleranza).
- [x] Il caso senza AA è bit-identico tra software e Vulkan.
- [x] Nessun `epsilon` globale allargato: nessuna tolleranza aggiunta.
- [x] Esito verificato su llvmpipe (ICD NVIDIA disponibile solo per la lane GPU dedicata).

## Riferimenti

- `Chronon3d/tests/backends/test_vulkan_debug_context.cpp` (`:311`, `:392-395`)
- `Chronon3d/src/backends/vulkan/shaders/fill_rect.comp`
- `Chronon3d/.tmp/ctest-full-20261116b.log`
