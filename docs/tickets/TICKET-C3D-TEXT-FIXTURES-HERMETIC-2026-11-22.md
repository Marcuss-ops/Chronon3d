# TICKET-C3D-TEXT-FIXTURES-HERMETIC-2026-11-22 — rendere ermetici i test text/font

**Stato:** `DONE (suite green)` (2026-11-22)
**Priorità:** P1
**Data:** 2026-11-22
**Owner:** text / render graph test fixtures
**Origine:** verifica wave 2026-11-22, suite `chronon3d_render_graph_tests`
**Padre:** [TICKET-CHRONON-AUDIT-2026-11-22](TICKET-CHRONON-AUDIT-2026-11-22.md)

## Problema

Al momento dell'apertura, la suite completa `chronon3d_render_graph_tests`
aveva **8 FAIL text/font** su 467 casi: bbox vuote, zero pixel visibili,
`compile_text_layout: combined paint chunk failed shaping` e
`[font-fallback] Missing glyph`.

Non sono causati dal blur backdrop, dalla cache LUT o dai flag `getenv`
toccati in questa wave; sono un problema di fixture/font stack dei test.

## Evidenza

Casi falliti principali (file e sintomo):

- `tests/render_graph/features/test_overlay_cert_fixture.cpp`:
  - `OverlayCert1: steady state renders headline subtitle watermark and shadow`
    → `count_lit == 0`, `count_dark == 0`.
  - `OverlayCert1: headline fade-in is monotonic across 0,2,5,9,10 frames`
    → tutte le energie `0`.
- `tests/render_graph/nodes/test_multi_source_text_run.cpp:242`:
  - `MultiSourceNode: execute with mixed rect + text_run returns valid fb`
    → `REQUIRE(result.ok())` falso.
- `tests/render_graph/nodes/test_text_run_predicted_bbox.cpp`:
  - `AnimCertTitle bbox centre X ...`
  - `Full-canvas TextRun keeps parent center and produces visible ink`
  - `Pinned absolute TextRun stays centered across animation boundary`
    → bbox `x0=1920 y0=1080 x1=-1 y1=-1`, `visible_pixels=0`.

Log caratteristico:

```text
[font-fallback] Missing glyph for cluster [U+0043] ...
materialize_prepared_text: compile_text_layout failed — kind=5
  msg=compile_text_layout: combined paint chunk failed shaping
[source-pass] layer='text_layer' node='text_run'
  TextRun materialization produced no shape
```

Il fixture tracciato esiste:
`tests/fixtures/Inter-Bold.ttf`, esposto da
`tests/text/test_text_font_fixture.hpp` (`test_text_fixture::inter_bold()`).
La fixture del test non lo sta usando in modo affidabile, oppure il font stack
non viene montato nel renderer usato da questi casi.

## Piano di chiusura

1. **Riprodurre isolato** con i tre filtri, senza tutta la suite, e confermare
   che il fallimento non dipende dall'ordine dei test.
2. **Un solo caricamento font.** Nei fixture `make_overlay_cert_1`,
   `make_renderer` e nei test TextRun, passare esplicitamente
   `test_text_fixture::inter_bold()` (path assoluto `CHRONON3D_SOURCE_DIR`).
3. **Mount esplicito.** Se il resolver richiede una root, montare
   `tests/fixtures/` e verificare che `Inter-Bold.ttf` sia visibile prima del
   render; non dipendere dai font di sistema.
4. **Fallback onesto.** Se il fixture manca, usare `MESSAGE` + skip esplicito
   (`test_text_fixture::skip_if_missing`) invece di produrre una bbox vuota e
   far fallire un `CHECK`.
5. **Nessun test “vuoto ma verde”.** Aggiungere un'asserzione che il font
   engine abbia almeno un face caricato prima di verificare l'inchiostro.
6. **Rieseguire** i tre gruppi e poi la suite completa.

## Gate di accettazione

- [x] Gli 8 casi text/font passano.
- [x] Nessun `Missing glyph` / `combined paint chunk failed shaping` nel log
      della suite.
- [x] `chronon3d_render_graph_tests`: **469/469 PASS** (467 casi originali +
      i 2 nuovi test LUT).
- [x] Se il font manca, restano le asserzioni sul framebuffer e sull'inchiostro
      che falliscono con valori espliciti; nessuno skip silenzioso.
- [x] Nessun font di sistema richiesto: `assets/fonts/` e
      `tests/fixtures/Inter-Bold.ttf` sono tracciati nel repository.

## Chiusura (2026-11-22)

- I tre gruppi text/font e la suite completa passano: **469/469 casi,
  19901/19901 asserzioni, 0 skipped**.
- Nessuna occorrenza di `Missing glyph` o `combined paint chunk failed shaping`
  nel log della suite.
- Il residuo che impediva il 469/469 era il watermark: non un font, ma un bug
  in `image_cache.cpp` (`downscale_for_runtime` cambiava width/height prima di
  spostare i buffer ridimensionati). Fix applicato; il caso
  `OverlayCert1: steady state...` ora passa (10/10 asserzioni).
- Non è stato necessario cambiare il font stack: le fixture tracciate erano
  già risolte correttamente nella suite corrente.

## Comandi di verifica

```bash
ninja -C build/chronon/linux-video-test -j2 chronon3d_render_graph_tests

./build/chronon/linux-video-test/tests/chronon3d_render_graph_tests \
  -tc='OverlayCert1*' -tc='MultiSourceNode*' -tc='*TextRun*'

./build/chronon/linux-video-test/tests/chronon3d_render_graph_tests
```

## Rischi e rollback

- Non “aggiustare” i test abbassando le soglie: il problema è che non c'è
  inchiostro, non che la soglia sia troppo alta.
- Se il shaping fallisce per un bug del font engine, il ticket resta aperto e
  il fix passa al owner text engine; non si chiude con skip permanenti.

## Definition of Done

La suite render-graph è 469/469 verde su checkout pulito, con i font fixture
tracciati e nessun messaggio di glyph mancante.
