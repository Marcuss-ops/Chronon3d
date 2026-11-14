# TICKET-TEXT-CLOSURE-20261109 — Chiusura del testo: pixel truth, caret RTL e qualità avanzata

**Status:** CLOSED — product exception (ADR-040, 2026-11-10). T1-01/T1-02/T2-01 were implemented on 2026-11-13, but the frozen pixel gate is still not green: 1/8 cases pass. The other 7 cases and the three implemented paths' pixel-parity gaps are accepted by explicit product decision, not passed. See §17 and [ADR-040](../adr/ADR-040-text-parity-product-exception.md).
**Parent:** [TICKET-TEXT-ANIMATOR-PARITY-20261107](TICKET-TEXT-ANIMATOR-PARITY-20261107.md)
**Quality authority:** [TICKET-TEXT-QUALITY-V2](TICKET-TEXT-QUALITY-V2.md)
**Related:** [TICKET-SVG-TEXT-PARITY-REMAINING](TICKET-SVG-TEXT-PARITY-REMAINING.md), [TICKET-CHRONON-2.0-MASTER](TICKET-CHRONON-2.0-MASTER.md), [ADR-037](../adr/ADR-037-pre-shaping-selector-unit-map.md)
**Closeout evidence:** [text-parity-20261110-product-exception/REPORT.md](../evidence/text-parity-20261110-product-exception/REPORT.md) — CLI SHA `ba6e14ad788fcab97000affb177f0f7bbb367595b3ffe52dd579c6990a1da75e`; 1/8 PASS. Implementation update: [text-parity-20261113/REPORT.md](../evidence/text-parity-20261113/REPORT.md) — CLI SHA `410d7395a2c8782695f4e43a69e91b4312088742f7f2679b81bccea5647d20d3`; T1-01/T1-02/T2-01 implemented, still 1/8 PASS.
**Owner:** Chronon3D text/runtime.
**Scope:** chiusura del testo non-SVG: certificazione pixel CPU/GPU su glifi reali, caret RTL/bidi, shadow canonica sul run, `VectorStroke`, policy di generazione MTSDF/`msdfgen`, e residui di parità oggi documentati.
**Non-goals:** SVG text (P2-02, ticket separato), DirectYUV/NV12 (lane separata), un secondo text renderer, un nuovo resolver o una nuova cache authority.

**Reopened exit contract (superseded by ADR-040):** T0-01 requires its frozen
thresholds on every corpus case with strict native Vulkan and same-backend
repeat determinism. T1-01 and T1-02 require canonical-shadow and vector-stroke
semantics on Vulkan instead of rejection. T2-01's transition/span/paint-range
combination requires a typed/native execution path or a proven AE-compatible
contract. ADR-040 is the explicit product decision that closes this ticket with
those gates accepted/deferred; the thresholds themselves are unchanged and the
seven failing cases are not PASS.

**Prior same-SHA evidence:** [`text-parity-20261109-final-sha/REPORT.md`](../evidence/text-parity-20261109-final-sha/REPORT.md) — CLI SHA `fe39acb857280be500fc8c8149f014687e683ccf7c5b7dc8f9c33a4247b9582d`; software repeats match, all eight Vulkan cases use MTSDF, and all eight remain FAIL against unchanged thresholds. The pre-revert post-cache-migration evidence is linked below; the final
post-revert checkpoint is in §17.
The pre-revert current-checkout rerun on 2026-11-10 after the typed cache
migration used CLI SHA
`74d9b861e7509e81ef271dda96294f896c93989c1c7824f77e83611687b2cf22` and
reproduced 0/8; see
[`text-parity-current-cache-20261110/REPORT.md`](../evidence/text-parity-current-cache-20261110/REPORT.md)
and its per-frame `report.json`. That historical result is superseded by §17.
The follow-up MTSDF B-spline reconstruction probe on `latin_basic` was rejected:
alpha IoU moved 0.947735 → 0.947630 while MAE/PSNR improved only marginally;
all gates still fail. Its candidate run and rollback are recorded in
[`text-parity-20261110-bspline-latin/REPORT.md`](../evidence/text-parity-20261110-bspline-latin/REPORT.md).
The 2026-11-10 High-profile range-24 probe was also rejected on the focused
`latin_basic` case: IoU 0.947987, coverage 1.00036, MAE 0.02023,
p99 0.50980, SSIM 0.98288 and PSNR 21.13 dB. It is slightly below the
range-40 current baseline IoU 0.947735 and below the earlier range-32 probe
(IoU 0.95306); all frozen gates still fail. The source profile was restored
to range 40 / padding 60 / generation 4. Full report:
[`text-parity-20261110-range24-latin/REPORT.md`](../evidence/text-parity-20261110-range24-latin/REPORT.md).
**Final 2026-11-10 post-revert checkpoint:** a second 2×2 coverage probe was
rejected and reverted; the rebuilt CLI is back to SHA
`ba6e14ad788fcab97000affb177f0f7bbb367595b3ffe52dd579c6990a1da75e`. The full
corpus is 1/8 PASS (`multipage`); the other seven remain below the frozen gate.
ADR-040 closes this ticket by product exception, not by a parity PASS. The
per-frame report is in
[`text-parity-20261110-product-exception/REPORT.md`](../evidence/text-parity-20261110-product-exception/REPORT.md).

## Glyph-edge source audit — 2026-11-10

The current `latin_basic` frame has equal non-zero mask counts (19,565) and
only a 1 px left-bbox difference, while the amplified diff outlines the glyph
contours. The source paths confirm materially different coverage production:
the software renderer rasterizes shaped outlines with `BLFont` at the run's
font size/raster scale, while the GPU path samples an unhinted FreeType outline
distance field generated at a shared 128-em profile and reconstructs a linear
screen-space ramp in `text_tile_raster.comp`. This points to size-specific edge
coverage as the next implementation target; it is an evidence-backed
hypothesis, not yet a proven sole cause. Distance-range and generic smoothing
changes have not closed the gap. Any coverage-mode candidate must retain
native Vulkan execution and compare against the same frozen corpus before it
can replace the current path.

The first targeted coverage experiment was rejected: 2×2 MTSDF subpixel sampling worsened `latin_basic` IoU (0.947735 → 0.946881) and raised coverage to 1.04002. Its run and reason for rejection are recorded in [`coverage2x/REPORT.md`](../evidence/text-parity-20261109-coverage2x/REPORT.md); the experiment is not retained in source. The projected-scale shader candidate also produced a byte-identical `exact_3d` raw, so it was removed; see [`coverage-diagnosis/REPORT.md`](../evidence/text-parity-20261109-coverage-diagnosis/REPORT.md).
Matching the MTSDF flattener's curve minimum subdivision to the software flattener was also rejected after a same-SHA full-corpus run showed no material metric improvement; its payload generation-ID change was reverted. See [`contour-step/REPORT.md`](../evidence/text-parity-20261110-contour-step/REPORT.md).

The pre-cache-migration current-checkout rerun on 2026-11-10 uses CLI SHA
`fe39acb857280be500fc8c8149f014687e683ccf7c5b7dc8f9c33a4247b9582d` and also
has a 0/8 result; see
[`text-parity-current-20261110/REPORT.md`](../evidence/text-parity-current-20261110/REPORT.md).
The targeted generator, animator, driver, selector metadata and tile-raster
canary binaries pass (7/7, 55/55, 22/22, 3/3 and 1/1 test cases respectively),
but those functional tests do not satisfy T0-01's pixel gate. The final
post-revert corpus on CLI SHA
`ba6e14ad788fcab97000affb177f0f7bbb367595b3ffe52dd579c6990a1da75e` is 1/8 (see
§17); ADR-040 closes this ticket by product exception, not by a pixel-gate
PASS.

---

## 0. Perché questo ticket

P2-01 e il lowering canonico di `text_transitions` sono chiusi. Restano però tre classi di lavoro che non sono “feature nuove” ma **condizioni per poter dichiarare chiuso il testo**:

1. **Prova pixel reale** — il canary Vulkan usa probe sintetici. Non esiste ancora un confronto deterministico, sullo stesso piano e sullo stesso frame, tra rasterizzatore software e MTSDF Vulkan con font e atlas reali.
2. **Caret RTL/bidi** — il caret nativo è canary-verified, ma la geometria è ancora derivata da una scansione in ordine visivo; manca la certificazione dell’edge logico in RTL e nei run misti.
3. **Qualità avanzata** — shadow canonica sul run, stroke vettoriale con join/miter e generazione MTSDF di qualità superiore. Sono nel perimetro di `TICKET-TEXT-QUALITY-V2`, ma senza un piano esecutivo il master resta aperto a tempo indefinito.

Questo ticket è il piano di chiusura. Non duplica l’authority della qualità: la usa come contratto e aggiunge i gate di integrazione, le prove e l’ordine di esecuzione.

---

## 1. Stato verificato al 2026-11-09

| Area | Fatto verificato | Evidenza |
|---|---|---|
| Rotazione 3D | X/Y esatti oltre 89°, edge-on a 90°, specchiatura oltre 90°; `TextRunDynamic` resta 160 byte; flag `kTextRunEffectExact3DRotation` | `include/chronon3d/runtime/render_surface.hpp`, `src/render_graph/nodes/text_run/gpu_text_run.hpp`, `text_tile_raster.comp` |
| Lowering transizioni | `StaticLayerProgram` porta `text_transitions`; helper condiviso; `AnimatedTextDocument` allegato; ramo legacy rimosso per il testo | `include/chronon3d/compiler/compiled_layer.hpp`, `src/render_plan/render_plan_compiler_text.cpp`, `src/compiler/scene_materializer.cpp` |
| Effetti per-glico | Shadow, glow, gradient, stroke, background e skew nativi; predicato fail-closed | `src/backends/vulkan/shaders/text_tile_raster.comp`, `src/backends/vulkan/shaders/text_tile_bin.comp`, `src/render_graph/nodes/text_run/gpu_text_run_animation.cpp` |
| Caret nativo | `resolve_text_caret_rect()` calcola il rect; `TextRunDynamic` lo trasporta; shader lo disegna dopo il fill | `include/chronon3d/text/text_run_shape.hpp`, `src/render_graph/nodes/text_run/text_run_draw.cpp`, `text_tile_raster.comp` |
| Generazione MTSDF | Generatore custom: outline FreeType, flattening, edge coloring `i % 3`, distanza per canale, `MtsdfProfile` con `generation_id=1` | `src/backends/text/mtsdf_generator.cpp`, `include/chronon3d/text/mtsdf_profile.hpp` |
| Atlas | `GlyphRepresentation` con `Coverage`, `Sdf`, `Msdf`, `Mtsdf`, `CoverageRgba`; chiave con profilo e variation axes | `include/chronon3d/runtime/gpu_glyph_atlas.hpp` |
| Test esistenti | Canary text tile 154/154; render graph GPU text 12/12; compiler boundary 14/14; C ABI transizioni 2/2 | `docs/tickets/TICKET-TEXT-ANIMATOR-PARITY-20261107.md` §P2-01 |

**Conclusione dello stato attuale:** la copertura funzionale c’è; manca la **certificazione di produzione** e la qualità avanzata. Il canary sintetico non sostituisce un pixel diff su font reali.

---

## 2. Gap matrix

> The statuses below are the technical states observed at the closeout probe.
> ADR-040 closes the ticket by product exception; it does not turn OPEN/PARTIAL
> rows into PASS. See §17.

| ID | Gap | Priorità | Stato | Gate di uscita |
|---|---|---|---|---|
| T0-01 | Pixel diff CPU/GPU su glifi reali, tutti gli effetti nativi | P0 | OPEN — 1/8 corpus cases pass; 7/8 accepted by ADR-040, not PASS | Report per-frame + soglie + evidenza same-SHA |
| T0-02 | Caret RTL/bidi: edge logico, cluster, ligature, run misti | P0 | PARTIAL — functional tests pass; pixel case remains below T0-01 thresholds | Unit + integration + pixel diff RTL |
| T0-03 | Multi-shadow GPU compositing and CPU/GPU pixel parity | P0 | PARTIAL — ordered multi-pass dispatch is implemented in `text_run_draw.cpp`; `multi_shadow` still misses the frozen pixel gate | Two ordered layers visible and within the frozen pixel gate |
| T1-01 | Shadow canonica sul run: maschera unione, blur gaussiano, source-over lineare | P1 | IMPLEMENTED — targeted IoU 0.994251 (threshold 0.995); not PASS | GPU union-mask + canonical blur parity |
| T1-02 | `VectorStroke`: joins miter/bevel/round + miter limit | P1 | IMPLEMENTED — targeted IoU 0.966888, coverage 1.032781; not PASS | Native Vulkan joins and miter-limit parity |
| T1-03 | Policy `Fast`/`High`/`Ultra` e generatore HQ (`msdfgen` solo se serve) | P1 | PARTIAL — `Fast`/`High`; High range/padding candidate under full-corpus evaluation (`generation_id=4`); cache identity and parity gate still open | ADR-038 + corpus + cache identity |
| T2-01 | `text_transitions` + `text_spans`/glyph paint ranges | P2 | IMPLEMENTED — targeted IoU 0.983597, p99 0.070588; not PASS | AE-compatible typed/native combination semantics |
| T2-02 | Typewriter al frame 0 e Scramble con larghezza outgoing | P2 | PASS — frame 0 vuoto legale; layout scramble per-frame e bounds unione | ADR-039 + test driver |
| T2-03 | Line Anchor selector-scoped | P3 | CLOSED — run-level e' il contratto di prodotto | ADR-039 |
| T2-04 | GPU fast path run-level più stretta del per-glico | P3 | CLOSED — ottimizzazione P3, non blocco di correttezza | ADR-039 |

---

## 3. T0-01 — Pixel truth gate CPU/GPU

### 3.1 Obiettivo

Produrre un artefatto **pre-encode** per ogni backend, sullo stesso piano, stesso frame, stesso font e stesso atlas, e confrontarlo senza encoder, container, chroma conversion o readback ambiguo.

Il gate deve rispondere a una domanda sola:

> A parità di input, il percorso software e il percorso Vulkan/MTSDF producono la stessa geometria, la stessa copertura e gli stessi effetti entro una soglia dichiarata?

### 3.2 Corpus minimo

Creare `Chronon3d/tests/text/fixtures/pixel_parity/` con piani V3 e un manifest. Ogni piano deve essere deterministico e non usare asset esterni.

| ID | Contenuto | Font | Frame campionati | Cosa dimostra |
|---|---|---|---|---|
| `latin_basic` | `CHRONON 3D` a 96 px, fill bianco, nero | SF-Pro-Display-Semibold.otf | 0, 15, 30 | Fill, hinting/antialias, allineamento |
| `exact_3d` | `BEYOND 89`, rotazione Y 0→120→0; X 100 su una variante | SF-Pro-Display-Semibold.otf | 0, 10, 15, 20, 45, 70, 90, 110 | Edge-on 90°, specchiatura, pivot, envelope |
| `effects` | shadow, glow, gradient, stroke, background, skew | Inter-Bold.ttf | 0, 20, 40 | Effetti per-glico nativi |
| `multi_shadow` | due shadow layer, offset opposti, overlap tra glifi | Inter-Bold.ttf | 0, 30 | Ordine, source-over, nessun `alpha-max` |
| `rtl_caret` | arabo + ebraico + misto LTR/RTL, caret statico | NotoNaskhArabic-Regular.ttf, NotoSansHebrew-Regular.ttf, Inter-Bold.ttf | 0, 8, 16, 24 | Edge logico, cluster, caret dopo il fill |
| `transitions` | `TEXT READY` → `CANONICAL`, dissolve + cut | SF-Pro-Display-Semibold.otf | 0, 20, 40, 60 | Lowering canonico, crossfade, layout |
| `fractional` | testo a scale 1.37, offset 0.5 px, rotazione 12° | Inter-Bold.ttf | 0, 1, 2 | Bilinear, subpixel, bordi |
| `multipage` | run lungo > una pagina atlas 2048² | SF-Pro-Display-Semibold.otf | 0, 30 | Paginazione, caret solo sulla prima pagina |

Regole del corpus:

- 1280×720, 30 fps, 120 frame massimo.
- Nessun `--profile preview`; entrambi i backend usano lo stesso profilo di render.
- Font caricati da `--assets-root "$PWD/Chronon3d"`.
- Il corpus frozen non combina `text_transitions` con `text_spans` o `glyph_paint_ranges`; la combinazione T2-01 ha un piano mirato separato.
- Un piano per caso; niente varianti nascoste nel codice del test.

### 3.3 Acquisizione

Per ogni piano e per ogni backend:

```bash
# software
chronon3d_cli render \
  --plan tests/text/fixtures/pixel_parity/<id>.plan.json \
  --assets-root "$PWD/Chronon3d" \
  --backend software \
  --video-sink raw \
  --frames <lista> \
  -o /tmp/text-parity/<id>/software.raw

# vulkan strict-native
chronon3d_cli render \
  --plan tests/text/fixtures/pixel_parity/<id>.plan.json \
  --assets-root "$PWD/Chronon3d" \
  --backend vulkan \
  --gpu-hot-path-mode require_gpu_native \
  --video-sink raw \
  --frames <lista> \
  -o /tmp/text-parity/<id>/vulkan.raw
```

Contratti:

- Il raw sink scrive RGBA8 premultiplied, frame concatenati; `frame_bytes = width*height*4`.
- `tools/raw_frame_to_png.py` si usa solo per ispezione umana, non per la metrica.
- Il log Vulkan deve contenere `route=mtsdf` e non deve contenere `fallback` o `UnsupportedCapability`.
- Il log del piano `transitions` non deve contenere il percorso legacy `CompiledComposition`.
- Il piano e il binario vengono hashati nel manifest; un cambio di font o di piano invalida il golden.

### 3.4 Confronto

Nuovo harness `Chronon3d/tools/verify_text_cpu_gpu_parity.py`. Deve usare solo le dipendenze già presenti in `quality_probe.py` (`numpy`, `Pillow`), oppure numpy puro per il raw.

Per ogni frame:

1. Calcolare maschera alpha (`alpha > 0`) e bounding box.
2. Calcolare:
   - `alpha_iou` tra le due maschere;
   - `coverage_ratio = area_vulkan / area_software`;
   - `rgb_mae`, `rgb_p95`, `rgb_p99`, `rgb_max` nella ROI di inchiostro;
   - `psnr` e `ssim` luma nella ROI;
   - edge band: dilatazione della maschera di 2 px, esclusa dal confronto “piatto”.
3. Salvare un diff PNG amplificato 8× e un `report.json` per frame.
4. Fallire se una maschera è vuota o se il bbox differisce di più di 1 px per lato.

Metriche di riferimento:

- Il confronto è **premultiplied RGBA8**, non PNG unpremultiplied; il PNG serve solo alla review.
- La ROI esclude il fondo nero e include shadow/glow dilatati.
- Il report deve contenere anche il tempo di render per frame e il device/driver Vulkan.

### 3.5 Soglie di accettazione

Queste sono le soglie iniziali. Se il primo baseline è peggiore, **non si abbassano silenziosamente**: si apre un fix o un’eccezione documentata con owner.

| Metrica | Soglia |
|---|---|
| `alpha_iou` | ≥ 0.995 |
| `coverage_ratio` | 0.99–1.01 |
| `rgb_mae` ROI | ≤ 2/255 |
| `rgb_p99` ROI | ≤ 8/255 |
| `ssim_luma` ROI | ≥ 0.98 |
| `psnr_luma` ROI | ≥ 35 dB |
| Fuori dalla edge band | nessun pixel con alpha o RGB > 2/255 |
| Repeat same-backend | SHA-256 del raw identico su due run |
| Vulkan path | `require_gpu_native`, nessun fallback |

Se `ssim` e `alpha_iou` passano ma un effetto è visibilmente sbagliato, il gate fallisce comunque: il report deve avere una review visuale dei frame `effects` e `multi_shadow`.

### 3.6 Implementazione, passo per passo

1. Aggiungere i piani e il manifest; congelare font, testo, frame e seed.
2. Aggiungere lo script di acquisizione che invoca il CLI due volte e archivia raw, log, piano e SHA.
3. Aggiungere il comparatore e il report JSON/PNG.
4. Eseguire il baseline su un host GPU; salvare i valori misurati in `docs/evidence/text-parity-20261109/REPORT.md`.
5. Aprire un fix per ogni caso rosso. Non modificare le soglie durante il fixing.
6. Ripetere su un secondo run per il determinismo same-backend.
7. Collegare il gate al profilo GPU/WBH, **non** alla lane headless senza device.

### 3.7 Cosa il gate deve scoprire

- Differenze di coverage sulle curve e sui bordi.
- Errori di segno/trasformazione nella rotazione esatta.
- Shadow/glow clippate al box del glifo.
- Gradienti valutati su piani diversi.
- Caret disegnato sotto il fill o con coordinate visuali errate.
- Perdita di pagina atlas o caret duplicato su run multi-pagina.
- Crossfade `text_transitions` con layout o alpha diversi.

---

## 4. T0-02 — Caret RTL/bidi

### 4.1 Comportamento attuale

`resolve_text_caret_rect()` in `include/chronon3d/text/text_run_shape.hpp`:

- scandisce `placed.glyphs` in ordine visivo;
- sceglie il primo glifo il cui intervallo `byte_offset..byte_offset+byte_len` contiene `caret.byte_index`;
- calcola `x = anchor->x + frazione * advance_x`;
- usa `ascent`/`descent` per l’altezza.

Questo funziona per il caso LTR coperto dal test, ma non esprime l’edge logico di un caret RTL:

- in un run RTL i glifi sono in ordine visivo inverso rispetto ai byte;
- l’inizio logico di un cluster è il bordo **destro** del cluster, non il sinistro;
- un caret all’interno di una legatura o di un cluster con combining marks non deve tagliare il cluster;
- in un paragrafo misto il lato dipende dal run/cluster che contiene il boundary, non dalla direzione globale del paragrafo.

### 4.2 Semantica richiesta

Per un insertion point `b` (byte offset UTF-8 valido):

1. Trovare il cluster il cui intervallo `[byte_offset, byte_offset + byte_len)` contiene `b`, oppure il cluster adiacente se `b` è un boundary.
2. Determinare la direzione risolta del cluster (LTR/RTL).
3. Se `b` è al bordo logico iniziale:
   - LTR: `x = cluster_left`;
   - RTL: `x = cluster_right`.
4. Se `b` è al bordo logico finale:
   - LTR: `x = cluster_right`;
   - RTL: `x = cluster_left`.
5. Se `b` è dentro un cluster:
   - consentire solo boundary di grapheme/UTF-8 validi;
   - avanzare nella direzione logica del cluster;
   - se il boundary cade dentro una legatura o un combining mark, **snap** all’edge del cluster e non split.
6. `y = baseline - ascent`; altezza da `caret.height` o `ascent + descent`.
7. `visible` e blink restano invariati.

### 4.3 Implementazione

- Preferenza: aggiungere la direzione risolta a `PlacedGlyphRun::Cluster` (o a `PlacedGlyphRun` se il run è omogeneo), popolata dal resolver bidi già esistente. Non dedurre la direzione dall’ordine visivo o dal segno di `advance_x`: dopo la normalizzazione RTL gli advance sono positivi.
- Se il layout ABI non può cambiare, materializzare una mappa `cluster_byte_offset -> direction` in `TextRunShape` al momento del layout e usarla nel caret; la mappa è derivata, non una seconda authority.
- Riscrivere `resolve_text_caret_rect()` per usare i **cluster**, non la scansione dei glifi.
- Validare `caret.byte_index` come boundary UTF-8; un indice dentro un code point multibyte deve fallire closed nel decoder o essere normalizzato.
- Non cambiare `TextRunDynamic` (160 byte): il caret continua a viaggiare come `caret_x/y/width/height` + `caret_flags`.
- Software e shader consumano lo stesso rect; nessuna copia della logica nel GLSL.

### 4.4 Test

**Unit — sintetico**

- LTR `AB`, glyph visivi `[A,B]`, caret a 0,1,2 → x = left, boundary, right.
- RTL `AB`, glyph visivi `[B,A]`, caret a 0,1,2 → x = right, boundary, left.
- Cluster con combining mark: caret dentro il cluster → snap, nessuna divisione.
- UTF-8 `è` (2 byte): indice 1 non è un boundary valido.

**Integration — font reale**

- NotoNaskhArabic-Regular `مرحبا` (64 px), caret a ogni boundary.
- NotoSansHebrew-Regular `שלום`.
- Misto `Hello مرحبا World`, caret nei tre run e ai confini.
- Multiline: caret sulla riga corretta.

**Pixel**

- Piano `rtl_caret`, caret verde statico (blink 0), su nero.
- Software vs Vulkan con il gate T0-01.
- Verificare: caret dentro il rect atteso, nessun glifo sopra, nessun caret duplicato su multi-pagina.
- Ispettare il PNG del caret e il diff.

### 4.5 Exit

- `resolve_text_caret_rect` non usa più l’ordine visivo per decidere l’edge.
- Unit RTL e mixed bidi verdi.
- Pixel diff RTL verde con soglie T0-01. **2026-11-09:** unit verdi e caret corretto visivamente; pixel non verde per l'eccezione AA T0-01.
- Evidenza in `docs/evidence/text-caret-rtl-2026.../REPORT.md`.

---

## 5. T0-03 / T1-01 — Shadow canonica e multi-shadow

### 5.1 Stato

> **2026-11-13:** `T1-01` è implementato (maschera unione, blur separabile 3σ,
> source-over lineare). Gate mirato: IoU `0.994251`, coverage `0.994642`; resta
> sotto la soglia `0.995` e non è PASS. Vedi
> [text-parity-20261113](../evidence/text-parity-20261113/REPORT.md).

- Il software raster ha già un pass di shadow con maschera unione (`src/backends/software/processors/text_run/text_run_processor/raster.cpp`, Stage 4: render di tutti i glifi su una superficie, poi blur, poi composite). Questo è il punto di partenza giusto; non va reimplementato da zero.
- Il blur software usa `apply_separable_box_blur()` con tier (`kBlurTierRadii`), quindi è una **box blur a tier**, non una gaussiana canonica.
- Il Vulkan shader applica shadow per-glico con `if (sh.a > shadow_layer.a) shadow_layer = sh;` (alpha-max), non source-over sull’unione.
- RISOLTO 2026-11-09: `text_run_draw.cpp` itera tutte le `TextShadow` come passate shadow-only ordinate, poi un fill; limite 4 fail-closed. Resta il blur non canonico (T1-01).
- La shadow per-glico di `GlyphInstanceState::shadow` è un effetto separato (P1-06) e resta `Fast`; non va confusa con la shadow layer del run.

### 5.2 Contratto canonico

Per `TextRunShape::shadows` (layer shadow):

1. Silhouette unione di tutti i glifi del run, alla stessa trasformazione.
2. Spread opzionale (se il modello lo espone; oggi `TextShadow` ha solo `offset`, `blur`, `opacity`, `color`).
3. Blur gaussiano separabile, raggio in pixel di output.
4. Colore/opacità applicati dopo il blur, in spazio lineare.
5. Source-over lineare una volta sola, sotto il testo.
6. Le shadow multiple si compongono nell’ordine autore (la prima sotto la seconda), senza `alpha-max`.

### 5.3 CPU

- Estrarre il pass unione in un helper nominato, con input `std::span<const GlyphInstanceState>` e output mask.
- Aggiungere `ShadowQuality { Fast, Canonical }` alla pipeline testo.
  - `Fast`: mantiene box blur/tier e il throughput attuale.
  - `Canonical`: gaussiana separabile (kernel esatto o 3-pass box con errore documentato) e source-over lineare.
- Riusare `TextScratchState::blur_buffer` o aggiungere un buffer scratch dedicato; nessuna allocazione per frame nel caso caldo.
- Non cambiare il significato di `TextShadow::blur`; il raggio è in pixel di output.

### 5.4 GPU

Due opzioni, da scegliere in ADR:

**A. Pass run-level (preferita per Canonical).**
- Costruire una maschera unione in un transient surface alla bbox del run.
- Blur separabile in compute (orizzontale + verticale) su R32F/RGBA16F.
- Compositare la shadow con source-over prima del testo.
- Richiede: nuova surface scratch, bind group, dispatch, budget VRAM, nessun readback CPU.

**B. Multi-pass per layer shadow.**
- Disegnare il run una volta per shadow con un flag `shadow_only` e senza fill; comporre in ordine.
- Più semplice da integrare, ma N draw e nessuna union mask: due glifi sovrapposti possono ancora saturare. Non è Canonical; è la modalità `Fast` implementata il 2026-11-09.

Per `Fast`, il comportamento attuale può restare, ma deve essere esplicito nel piano e nel report. Per `Canonical`, la strada A è l’unica che soddisfa il contratto.

### 5.5 Multi-shadow

- Il codice GPU deve iterare tutte le `shape.shadows`, non fermarsi alla prima.
- Se si sceglie il pass run-level, ogni shadow è un pass separato sulla stessa mask; il costo è N blur.
- Se si sceglie multi-pass, il limite N va dichiarato (es. 4) e validato fail-closed.
- Test con due shadow: una offset `(-6,-4)`, una `(+8,+6)`, colori diversi, overlap tra glifi. Verificare ordine e assenza di banda rettangolare.

### 5.6 Test e gate

- Golden software: glifi adiacenti con overlap, blur 0/8/24, offset negativo, due shadow.
- Confronto CPU/GPU con T0-01 (`multi_shadow`).
- Frame time: registrare `render_ms` per `Fast` e `Canonical`; una regressione > 25% su `Canonical` richiede profile e decisione, non silenzio.
- Nessun clip al box del singolo glifo.
- Nessun `alpha-max` visibile su overlap.

---

## 6. T1-02 — VectorStroke

### 6.1 Stato e due semantiche dichiarate

> **2026-11-13:** `T1-02` è implementato su Vulkan con atlas `CoverageRgba`
> dedicato, espansione Clipper2 e canali fill/stroke per-glico. Gate mirato:
> IoU `0.966888`, coverage `1.032781`; il residuo di outline/AA non è PASS.
> Vedi [text-parity-20261113](../evidence/text-parity-20261113/REPORT.md).

Il software raster usa già uno stroke vettoriale Blend2D: `BL_STROKE_JOIN_ROUND/BEVEL/MITER_CLIP` e `setStrokeMiterLimit` in `raster.cpp`. Il Vulkan usa invece la dilatazione del campo distanza. La differenza è reale e oggi non è dichiarata nel piano: va resa esplicita, non “aggiustata” facendo sembrare un miter un SDF.

Il ticket ADR deve dichiarare:

- `DistanceStroke`: dilatazione del campo distanza, join tondi, veloce, comportamento attuale. Default per compatibilità.
- `VectorStroke`: espansione dell’outline con join `miter|bevel|round`, `miter_limit`, cap e fill rule; rappresentazione HQ o coverage dedicata.

### 6.2 Modello

- Estendere `TextPaint`, che espone già `stroke_join`, `stroke_cap` e `stroke_miter_limit`; non creare un secondo tipo.
- Aggiungere solo `stroke_mode` (`Distance`/`Vector`/`Auto`) al piano testo/layer style.
- `DistanceStroke` resta il default GPU-native e usa la dilatazione SDF; `VectorStroke` usa l’outline e i join vettoriali; `Auto` mantiene il comportamento storico del backend e **non è una modalità di parità**.
- I piani del corpus T0-01 devono scegliere `Distance` o `Vector` esplicitamente.
- V3: `style.stroke_mode` è nuovo; `style.stroke_join`, `style.stroke_cap` e `style.stroke_miter_limit` sono già mappati dal compiler (`render_plan_compiler_text.cpp`). Schema, decoder, compiler e fingerprint vanno estesi solo per `stroke_mode`.
- `GlyphPaint`/`TextRunDynamic` non vanno allargati oltre 160 byte senza un piano ABI; per VectorStroke si usa un atlas di coverage dedicato, non un nuovo campo per-glico.

### 6.3 Generazione

Il software ha già il path vettoriale; il lavoro GPU è portare `VectorStroke` nello stesso contratto senza usare la dilatazione SDF. Il piano deve inoltre decidere se `VectorStroke` non supportato su GPU debba fare fallback software esplicito o fail-closed, mai passare silenziosamente a `DistanceStroke`.

- Ottenere i contorni da `FontEngine::glyph_outline_contours()` (già esistente) o dalla pipeline outline canonica.
- Applicare l’offset con `FT_Stroker` o un offsetter interno; per miter/bevel servono intersezioni dei segmenti e limite.
- Rasterizzare il contorno stroked in coverage HQ (o in una rappresentazione vettoriale HQ) e caricarlo nell’atlas come chiave dedicata:
  - `GpuGlyphKey` + `stroke_mode`, `stroke_join`, `stroke_miter_limit`, `stroke_width` quantizzata;
  - `generation_profile`/`generation_id` per invalidare.
- Non usare la dilatazione SDF per `VectorStroke`.
- Il limite: `VectorStroke` è più costoso e non adatto a run enormi; il piano deve poter scegliere `Fast`.

### 6.4 Test

- Golden: `M`, `W`, `A`, `o` con join miter/bevel/round, miter limit 2/4/10.
- Angolo acuto: miter oltre limite deve fare fallback a bevel, non esplodere.
- Contatori chiusi: il counters interno non deve riempirsi.
- Self-intersection e overlapping contours.
- Cache key: cambiare join/miter/width deve missare.
- CPU/GPU pixel diff con T0-01.
- Frame time: `VectorStroke` vs `DistanceStroke` su 100 glifi.

### 6.5 ADR

Prima del codice: ADR “Test stroke semantics” con:
- due modalità e default;
- join/cap/miter limit;
- rappresentazione e cache key;
- policy di fallback;
- nessun secondo renderer/resolver.

---

## 7. T1-03 — msdfgen e policy Fast/High/Ultra

### 7.1 Prima il corpus, poi la dipendenza

`msdfgen` non va aggiunto “per principio”. Il generatore custom (`src/backends/text/mtsdf_generator.cpp`) usa edge coloring `i % 3`, flattening e distanza per canale. Va misurato contro un riferimento ad alta risoluzione:

- curve strette (`S`, `8`, `@`);
- diagonali sottili (`A`, `W`, `K`);
- counters chiusi (`o`, `e`, `B`);
- script complessi (arabo, ebraico);
- CJK;
- variable font (`Inter-Var.ttf`);
- glifi con contorni sovrapposti (Inter-Bold).

Metriche:
- errore di distanza ai bordi rispetto a un raster 8×;
- artifact mediani (pixel “a puntini”);
- contrasto dei thin stems;
- tempo di generazione e memoria;
- dimensione atlas.

Solo se il corpus fallisce in modo riproducibile si apre lo spike `msdfgen`.

### 7.2 Vincolo di architettura

`tools/architecture_rules_core.toml` vieta gli include `<msdfgen>` nel codice headless. Un’integrazione deve:

- essere un **leaf** in `src/backends/text/` o in un target dedicato;
- non esporre header `msdfgen` nel public API;
- aggiungere un’eccezione architetturale motivata e un ADR;
- mantenere `MtsdfProfile`, cache e preparation authority esistenti;
- avere fallback esplicito al generatore custom, non un path silenzioso.

### 7.3 `generation_id` e cache

The original plan below was authored for generation 2. The active High
candidate is generation 4 (128 em, distance range 40, padding 60); the 256-em
test profile was discarded. Same-SHA evidence and cache identity are recorded
in ADR-038 and [`text-parity-20261109-range40/REPORT.md`](../evidence/text-parity-20261109-range40/REPORT.md).

- Cambiare `MtsdfProfile::generation_id` solo quando il payload cambia davvero.
- Aggiornare `MtsdfGlyphCacheKey`, `GpuGlyphKey`, `generation_profile` e i test di hash.
- Byte-parity della preparation: due run devono produrre lo stesso payload; un cambio di generatore deve invalidare tutti i cache hit.
- Nessuna migrazione di cache persistente: l’atlas è volatile.

### 7.4 Policy Fast/High/Ultra

- Non aggiungere valori a `GlyphRepresentation` senza ADR: la tassonomia è già usata nelle chiavi e nei test.
- Preferenza: aggiungere una dimensione `quality` al profilo/chiave, mantenendo `Mtsdf` come rappresentazione.
- `Fast`: profilo attuale, generazione custom, box shadow veloce.
- `High`: MTSDF HQ (msdfgen o generatore migliorato), gaussiana canonica, `VectorStroke` opzionale.
- `Ultra`: solo se c’è un caso d’uso e un budget; non un terzo renderer.
- La scelta deve essere visibile nel piano e nel report, non dedotta dal backend.

### 7.5 Test

- Corpus di glifi con riferimento high-res.
- Byte-parity del payload.
- Cache invalidation per `generation_id` e quality.
- CPU/GPU pixel diff con T0-01.
- Benchmark: cold generation, warm atlas, memoria, frame time.

---

## 8. T2 — Residui di parità

### 8.1 `text_transitions` + styled spans

> **2026-11-13:** `T2-01` è implementato. Ogni keyframe porta `spans` e
> `glyph_paint_ranges` opzionali, validati contro il testo di quel keyframe; il
> fallback layer-level è accettato solo se le range restano valide. La route
> nativa propaga `GlyphInstanceState::fill` nel record dinamico. Gate mirato:
> IoU `0.983597`, p99 `0.070588`; il residuo di maschera MTSDF non è PASS.
> Vedi [text-parity-20261113](../evidence/text-parity-20261113/REPORT.md).

Il vecchio contratto falliva closed con:

```text
text layer '<id>' cannot combine text_transitions with styled text spans
```

`src/render_plan/render_plan_compiler_text.cpp` rifiutava `text_spans` e `svg_glyph_paint_ranges` con transizioni. Era corretto finché le range erano autoreferenziate sul testo base; ora la validazione è per-keyframe.

Se il prodotto richiede testo animato con stili per keyframe:

- aggiungere a ogni keyframe `spans` e `glyph_paint_ranges` opzionali;
- validare ogni range contro il testo di **quel** keyframe, non contro il base;
- `AnimatedTextDocument` deve trasportare una `TextDefinition` completa per keyframe, non solo la stringa;
- il decoder e lo schema devono rifiutare range non validi;
- test: typewriter con colore per parola, scramble con paint range, morph con span.

Se non serve, dichiararlo **non-goal** nel master e rimuovere l’ambiguità.

### 8.2 Typewriter frame 0 e Scramble larghezza

- Typewriter: oggi la demo parte al frame 6 perché il route MTSDF rifiuta un frame con zero glifi. Fix: gestire esplicitamente un run vuoto (draw no-op con successo) invece di inserire uno spazio; oppure mantenere un layout invisibile. Test: frame 0 con zero glifi, frame 1 con il primo grapheme, nessun crash.
- Scramble: la superficie tight è dimensionata sul primo layout; il testo outgoing può essere più largo. Fix: calcolare la larghezza massima tra outgoing, incoming e frame intermedi durante il build della transizione, e usarla per la surface. Test: `READY` → `CANONICAL` senza clipping.

### 8.3 Line Anchor e GPU fast path

- `LineAnchorProperty` è run-level; se il prodotto richiede anchor per linea/selector, va progettato un evaluator per-linea. Se non serve, documentare il limite e chiudere.
- Il fast path GPU run-level è più stretto del per-glico MTSDF. Non è un bug di correttezza: il per-glico è il fallback completo. Se si vuole chiudere, serve un benchmark che dimostri il guadagno e una matrice di casi; altrimenti resta un’ottimizzazione P3.

---

## 9. Ordine di esecuzione

| Stage | Lavoro | Dipende da | Dimensione |
|---|---|---|---|
| 0 | Corpus + harness T0-01 | — | M |
| 1 | Baseline e fix dei casi rossi | Stage 0 | M/L |
| 2 | Caret RTL T0-02 | Stage 0 per il pixel test | M |
| 3 | Multi-shadow T0-03 | Stage 0 | S/M |
| 4 | Shadow canonica T1-01 | Stage 3, profiler | M/L |
| 5 | ADR + VectorStroke T1-02 | Stage 1 | L |
| 6 | Corpus msdfgen + decisione T1-03 | Stage 1, Stage 4 | M/L |
| 7 | Residui T2 solo se richiesti | Stage 6 | S/M |

Regola: nessuno stage “qualità” può chiudersi senza il report T0-01 aggiornato. Un golden senza pixel diff è solo un’immagine.

---

## 10. Comandi di verifica

Build mirata, `-j12` massimo:

```bash
cmake --build Chronon3d/build/chronon/linux-video-release \
  --target chronon3d_cli chronon3d_text_animator_property_tests \
           chronon3d_render_graph_tests \
           chronon3d_text_tile_raster_canary_tests -j12
```

Test esistenti da non perdere:

```bash
Chronon3d/build/chronon/linux-video-release/tests/chronon3d_text_animator_property_tests
Chronon3d/build/chronon/linux-video-release/tests/chronon3d_render_graph_tests \
  --test-case='GPU *text*'
Chronon3d/build/chronon/linux-video-release/tests/chronon3d_c_abi_tests \
  --test-case='*text_transitions*'
Chronon3d/build/chronon/linux-video-release/tests/chronon3d_compiler_boundary_tests
Chronon3d/build/chronon/linux-video-release/tests/chronon3d_text_tile_raster_canary_tests
```

Nuovi gate:

```bash
python3 Chronon3d/tools/verify_text_cpu_gpu_parity.py \
  --corpus Chronon3d/tests/text/fixtures/pixel_parity/manifest.json \
  --out docs/evidence/text-parity-2026... \
  --require-same-sha
```

```bash
python3 Chronon3d/tools/raw_frame_to_png.py --self-test
python3 Chronon3d/tools/quality_probe.py --help
```

Per il gate GPU: eseguire su host con device reale e profilo WBH. La lane `ubuntu-latest` senza device non è una certificazione: il test deve fallire o essere escluso esplicitamente, non skippare in silenzio.

---

## 11. Rischi e mitigazioni

| Rischio | Impatto | Mitigazione |
|---|---|---|
| Software e MTSDF sono rasterizzatori diversi | Differenze di antialias fisiologiche | Soglie su alpha IoU + edge band, non uguaglianza byte |
| GPU readback/format diverso dal raw sink | Diff falso | Acquisire entrambi via raw sink, stesso formato RGBA8 |
| Driver Vulkan non deterministico | Repeat test rosso | Registrare device/driver; se il repeat è instabile, aprire un bug, non allentare il gate |
| Shadow canonica troppo lenta | Regressione frame time | `Fast` resta disponibile; `Canonical` opt-in e misurata |
| `msdfgen` viola il deny architetturale | Build/CI rossa | ADR + eccezione leaf, oppure nessuna dipendenza se il corpus passa |
| Cambio `PlacedGlyphRun` per il caret | Rebuild ampio | Preferire una mappa derivata in `TextRunShape` se l’ABI è sensibile |
| `text_transitions` con stili per keyframe | Esplosione del modello | Non-goal esplicito finché non c’è un caso d’uso |

---

## 12. Mappa dei file

**Da modificare**

- `include/chronon3d/text/text_run_shape.hpp` — caret RTL, cluster resolution.
- `src/render_graph/nodes/text_run/text_run_draw.cpp` — multi-shadow, caret, pass GPU.
- `src/backends/software/processors/text_run/text_run_processor/raster.cpp` — shadow canonica.
- `src/backends/software/processors/text_run/text_run_processor/scratch.cpp` — blur gaussiano/scratch.
- `src/backends/vulkan/shaders/text_tile_raster.comp` — shadow run-level, caret, VectorStroke HQ se necessario.
- `src/backends/vulkan/shaders/text_tile_bin.comp` — envelope shadow/glow.
- `src/backends/text/mtsdf_generator.cpp` — generatore HQ, `generation_id`.
- `include/chronon3d/runtime/gpu_glyph_atlas.hpp` — chiavi quality/stroke.
- `include/chronon3d/text/mtsdf_profile.hpp` — generation/quality.
- `src/render_plan/render_plan_decoder_text.cpp` / `src/render_plan/render_plan_compiler_text.cpp` — stroke mode, transizioni con span se supportate.
- `schemas/json/chronon.render-plan.v3.extensions.schema.json` — nuovi campi.
- `docs/tickets/TICKET-TEXT-ANIMATOR-PARITY-20261107.md` — link a questo ticket.
- `docs/tickets/TICKET-TEXT-QUALITY-V2.md` — stato P1-3/P2 dopo l’esecuzione.

**Da creare**

- `Chronon3d/tests/text/fixtures/pixel_parity/*.plan.json` + `manifest.json`.
- `Chronon3d/tools/verify_text_cpu_gpu_parity.py`.
- `Chronon3d/tests/text/test_text_caret_rtl.cpp`.
- `Chronon3d/docs/evidence/text-parity-2026.../REPORT.md`.
- `Chronon3d/docs/evidence/text-caret-rtl-2026.../REPORT.md`.
- ADR per stroke semantics e per quality policy/MTSDF.

---

## 13. Previous-scope closeout checklist (superseded 2026-11-09)

The checked items below document the earlier, narrower closeout that accepted
an AA exception and GPU non-goals. They are historical evidence only; the
reopened full-parity exit criteria are authoritative.

- [x] Corpus T0-01 completo e congelato.
- [x] Baseline CPU/GPU registrato con device, driver, SHA del binario e del piano.
- [x] Nessun caso T0-01 rosso, oppure eccezione scritta con owner e motivo. (eccezione AA in `docs/evidence/text-parity-20261109/REPORT.md`)
- [x] Caret RTL e misto bidi verde su unit; pixel sotto l'eccezione AA T0-01 documentata.
- [x] Multi-shadow GPU verificato con due layer.
- [x] Shadow canonica CPU con source-over lineare; GPU run-level dichiarato non-goal e fail-closed in ADR-038. Il golden di riferimento GPU non è un exit gate di questa chiusura.
- [x] ADR-038 approvato per `DistanceStroke`/`VectorStroke`; GPU `Vector` fail-closed, nessuna parità GPU dichiarata.
- [x] Decisione `msdfgen` presa sul corpus: non aggiunto, il residuo è antialias/placement, non il generatore.
- [x] `generation_id` Fast=1/High=2 e cache key/hash/fingerprint aggiornati per quality, shadow quality e stroke mode.
- [x] Residui T2 chiusi come non-goal espliciti o fix (ADR-039).
- [x] `TICKET-TEXT-ANIMATOR-PARITY` chiuso con non-goal GPU espliciti e l'eccezione AA T0-01.
- [x] Nessun commit creato: resta la regola del repository di non committare senza richiesta esplicita.

### 13.1 Reopened full-parity exit checklist — WAIVED by ADR-040 (not PASS)

> These items remain technically unmet. ADR-040 closes the ticket by product
> exception; the unchecked boxes below are not a parity pass. Current evidence:
> [`text-parity-20261110-product-exception/REPORT.md`](../evidence/text-parity-20261110-product-exception/REPORT.md)
> (1/8 PASS, CLI `ba6e14ad…`).

- [ ] Every frozen T0-01 corpus case passes unchanged thresholds on a strict
  native Vulkan route; same-backend software output repeats byte-identically.
- [ ] Caret RTL/bidi meets the T0-01 pixel thresholds, not only unit checks.
- [ ] GPU canonical union-mask shadow and GPU vector stroke execute with the
  authored semantics and match software within the frozen gate.
- [ ] `text_transitions` combined with `text_spans`/glyph paint ranges has a
  typed lowering and a corpus case that proves behavior.
- [ ] Repeat the full corpus on the final CLI SHA and attach `REPORT.md`, JSON,
  per-frame raw artifacts/logs, and the architecture/build evidence.

---

## 14. Nota di scope

P2-02 (SVG text) resta in `TICKET-SVG-TEXT-PARITY-REMAINING`. DirectYUV/NV12 e la resa encoder restano fuori da questo ticket: non vanno mescolati al pixel diff CPU/GPU del testo, altrimenti il report misura il convertitore invece del renderer.

---

## 15. Historical implementation closeout — 2026-11-09 (superseded)

**Esito:** P0 implementati e verificati. T0-02 e T0-03 chiusi funzionalmente;
T0-01 completo come harness/corpus/fix di placement, con eccezione AA
documentata. T1/T2 sono chiusi nella sezione 16; la matrice riflette lo stato
finale, inclusi i non-goal GPU espliciti.

**Evidenza:** `docs/evidence/text-parity-20261109/REPORT.md` e `report.json`
(per-frame, SHA del CLI e dei piani, identità Vulkan, repeat).

### 15.1 Build e test

```text
cmake --build build/chronon/linux-video-release \
  --target chronon3d_cli chronon3d_mtsdf_generator_tests \
           chronon3d_mtsdf_font_identity_tests \
           chronon3d_text_animator_property_tests \
           chronon3d_text_tile_raster_canary_tests \
           chronon3d_render_graph_tests -j12
BUILD_EXIT=0
```

- MTSDF generator: 7/7 casi, 178/178 assert.
- MTSDF font identity: 5/5 casi, 36/36 assert.
- Text animator: 55/55 casi, 267/267 assert.
- Text tile canary: 1/1 caso, 154/154 assert.
- Render graph GPU text: 12/12 casi, 68/68 assert.

### 15.2 T0-01 — pixel gate

Il corpus congelato contiene 8 casi; tutti renderizzano su software e Vulkan,
con repeat software byte-identico e `route=mtsdf` su ogni frame campionato.

Fix principale: l'origine del piano MTSDF era quantizzata a `floor(origin_x/y)`
in em px. A 96 px (scala 1.5×) questo spostava il testo GPU di circa 2 px in X
e 1 px in Y. Ora l'origine frazionaria è preservata in `MtsdfGlyphBitmap`,
`GpuGlyphMetrics` e `plane_left/top/right/bottom`. Su `latin_basic` lo shift
migliore passa da (2,1) a (0,0), l'alpha IoU da 0.8137 a 0.9528 e il bbox da
2 px a 1 px.

Soglie invariate. Nessun caso raggiunge alpha IoU ≥ 0.995: il massimo è
0.98464 (`fractional`), che però ha SSIM 0.99981 e PSNR 42.64 dB. La causa è
la copertura sub-pixel Blend2D vs MTSDF canonico a 64 em; l'eccezione e il
percorso di fix (T1-03 HQ generation, T1-01 shadow canonica) sono nel REPORT.
`effects` e `multi_shadow` hanno anche differenze di blur shadow/glow (T1-01).

### 15.3 T0-02 — caret RTL/bidi

`resolve_text_caret_rect()` usa i cluster in ordine logico, l'edge iniziale /
finale per LTR/RTL, snap su legature/combining e normalizzazione UTF-8.
`ShapedFontSpan::direction` è popolato dal resolver, non dedotto dall'ordine
visivo. Unit verdi (gruppo caret 5/5, 18 assert; suite completa 55/55) e frame
RTL con caret a destra per arabo/ebraico e al boundary nel misto. Il pixel gate
RTL resta sotto l'eccezione AA T0-01.

### 15.4 T0-03 — multi-shadow

Rimosso il rifiuto stale `shadows.size() > 1` in `prepare_mtsdf_text_run`.
Il draw emette N passate shadow-only ordinate + una passata fill; limite 4
dichiarato e fail-closed. `multi_shadow` renderizza 31/31 frame con
`route=mtsdf`; rosso e blu sono visibili e nell'ordine autore. La differenza
di bbox/coverage residua è il blur software vs shader, non un layer mancante.

### 15.5 File toccati in questa sessione

- `include/chronon3d/text/mtsdf_glyph.hpp`
- `include/chronon3d/runtime/gpu_glyph_atlas.hpp`
- `src/backends/text/mtsdf_generator.cpp`
- `src/runtime/gpu_glyph_atlas_core_detail.hpp`
- `src/render_graph/nodes/text_run/text_run_packing.cpp`
- `src/render_graph/nodes/text_run/text_run_draw.cpp`
- `tests/text/test_mtsdf_generator.cpp`
- `tools/verify_text_cpu_gpu_parity.py`
- `tests/text/fixtures/pixel_parity/*` + `manifest.json`
- `docs/evidence/text-parity-20261109/REPORT.md`

Nessun commit creato: resta la regola del repository di non committare senza
richiesta esplicita.

---

## 16. Historical closeout — quality modes, T2 and GPU non-goals (superseded)

### 16.1 Quality e stroke espliciti

- `TextQuality` resta `Fast` (64 em, generation 1) / `High` (128 em,
  generation 2). La quality e' nella chiave della prepared-text cache, nel hash
  del run e nel fingerprint dello stile.
- `TextShadowQuality` aggiunge `Canonical`: maschera unione del run, gaussiana
  separabile con `sigma = raggio / 3`, colore/opacita' dopo il blur,
  source-over lineare. `Fast` resta il default e conserva il box blur a tier.
- `TextStrokeMode` rende esplicite le semantiche: `Auto` (storico), `Distance`
  (SDF GPU), `Vector` (outline software). `Distance` su software e `Vector` su
  GPU falliscono con `UnsupportedCapability`; nessun alias silenzioso.
- Schema V3, decoder, proiezione V2, fingerprint e hash accettano i nuovi campi
  solo sui layer testo. I piani che non li usano restano invariati.
- `msdfgen` non e' stato aggiunto: il corpus 2026-11-09 non mostra un difetto del
  generatore, ma una differenza di antialias/copertura tra rasterizzatori.

### 16.2 T2-02 e residui

- Typewriter frame 0: il testo transitorio e' vuoto, non `" "`, e
  `transition_text_authoritative` impedisce il fallback al testo attivo. Il
  driver ricostruisce un layout a zero glifi; il software ritorna `Outcome{0}`
  prima di acquisire scratch; il path MTSDF ritorna `Outcome{0}` prima di
  cache/atlas. I test `AnimatedTextDocument: Typewriter reveals whole grapheme
  clusters` e `TextRunDriver: Typewriter frame 0 rebuilds an empty layout`
  fissano il contratto.
- Scramble: il layout attivo viene ricostruito dal `transition_text` per frame;
  `compute_text_run_visual_bounds()` unisce active/transition e
  `dissolve_layout` (outgoing), quindi la superficie contiene entrambi. Il test
  driver `Scramble transition rebuilds layout with transition_text` copre il
  rebuild; nessuna seconda cache di larghezza.
- T2-01/T2-03/T2-04: non-goal espliciti in ADR-039, con il fail-closed
  conservato per spans+transitions.

### 16.3 Historical limitations and reopened work

These limitations were accepted by the previous ticket scope. Full parity now
requires replacing each rejection or exception with implementation and
equivalence evidence; the entries below are not completion claims.

The attempted 256-em MTSDF variant did not improve `latin_basic` parity
(alpha IoU 0.947848 vs 0.948086 at 128 em) and has been discarded. Its one-frame
measurement and raw artifact hashes are recorded in
[`profile-256-single-frame.md`](../evidence/text-parity-20261109-hq/profile-256-single-frame.md).

- Il pixel gate T0-01/T0-02 resta rosso. Le soglie non sono state abbassate.
  Il corpus standard e HQ sono in `docs/evidence/text-parity-20261109/REPORT.md`
  e `docs/evidence/text-parity-20261109-hq/report.json`.
- GPU `Canonical` shadow e GPU `Vector` stroke non sono implementati. Sono
  rifiutati esplicitamente; non esiste una dichiarazione di parita' GPU per
  queste modalita'. La rimozione del fail-closed e' tracciata come Demolition
  Debt in ADR-038.
- Il golden di riferimento della shadow GPU run-level e il benchmark GPU non
  fanno parte di questa chiusura. Il benchmark CPU `Fast` vs `Canonical` e' da
  registrare quando la build e' disponibile; il costo e' O(raggio) per pixel ed
  e' opt-in.

### 16.4 File toccati in questa chiusura

- `include/chronon3d/scene/model/shape/shape.hpp`
- `include/chronon3d/render_plan/render_plan_text.hpp`
- `src/render_plan/render_plan_decoder_layer_style.cpp`
- `src/render_plan/render_plan_compiler_text.cpp`
- `src/render_plan/render_plan_validator_v2_projection.cpp`
- `src/render_plan/fingerprint_values.cpp`
- `src/text/text_run_hash.cpp`
- `src/backends/software/processors/text_run/text_run_processor/prepare.cpp`
- `src/backends/software/processors/text_run/text_run_processor/raster.cpp`
- `src/backends/software/processors/text_run/text_run_processor/scratch.cpp`
- `src/backends/software/processors/text_run/text_run_processor/text_run_stages.hpp`
- `src/render_graph/nodes/text_run/text_run_packing.cpp`
- `src/render_graph/nodes/text_run/gpu_text_run_animation.cpp`
- `src/render_graph/nodes/text_run/text_run_draw.cpp`
- `src/text/animated_text_document.cpp`
- `tests/text/test_animated_text_document.cpp`
- `schemas/json/chronon.render-plan.v3.extensions.schema.json`
- `docs/adr/ADR-038-text-quality-shadow-and-stroke-modes.md`
- `docs/adr/ADR-039-text-transition-residuals.md`
- `docs/tickets/TICKET-TEXT-CLOSURE-20261109.md`
- `docs/tickets/TICKET-TEXT-QUALITY-V2.md`
- `docs/tickets/TICKET-TEXT-ANIMATOR-PARITY-20261107.md`

### 16.5 Verifica

Gli esiti di build, test mirati e gate pixel sono registrati sotto; nessun
risultato non eseguito viene dichiarato PASS.

---

## 17. Product-exception closeout — 2026-11-10

**Decision:** [ADR-040](../adr/ADR-040-text-parity-product-exception.md) records
the product owner's explicit authorization to close this ticket with the AA and
GPU-gate residual accepted. This is **not** a parity PASS; the frozen
thresholds are unchanged.

**Build:** `cmake --build build/chronon/linux-video-release --target
chronon3d_cli -j12` → exit 0. CLI SHA-256
`ba6e14ad788fcab97000affb177f0f7bbb367595b3ffe52dd579c6990a1da75e`.

**Result:** `1/8` frozen cases pass (`multipage`). The other seven miss at least
one unchanged threshold.

| Case | alpha IoU | coverage | RGB p99 | PSNR luma | Result |
|---|---:|---:|---:|---:|---|
| `multipage` | 0.995177 | 1.000744 | 0.019608 | 35.162 | PASS |
| `latin_basic` | 0.985900 | 0.998673 | 0.082353 | 35.058 | FAIL IoU/p99 |
| `transitions` | 0.983597 | 1.003218 | 0.094118 | 34.408 | FAIL IoU/p99/PSNR |
| `fractional` | 0.963981 | 0.985111 | 0.176471 | 29.418 | FAIL IoU/coverage/p99/PSNR |
| `exact_3d` | 0.943576 | 0.961831 | 0.227451 | 30.023 | FAIL IoU/coverage/bbox/p99/PSNR |
| `rtl_caret` | 0.928066 | 0.956654 | 0.039216 | 25.232 | FAIL IoU/coverage/p99/PSNR/SSIM |
| `multi_shadow` | 0.927669 | 0.933783 | 0.431373 | 21.176 | FAIL IoU/coverage/bbox/RGB/SSIM |
| `effects` | 0.923020 | 0.923020 | 0.180392 | 28.594 | FAIL IoU/coverage/bbox/RGB/flat |

The per-frame raw metrics and the verifier's `pass: false` are in
[`report.json`](../evidence/text-parity-20261110-product-exception/report.json);
the human-readable summary is in
[`REPORT.md`](../evidence/text-parity-20261110-product-exception/REPORT.md).

**Rejected probe:** a second 2×2 coverage probe on this baseline worsened
`latin_basic` (IoU 0.985900 → 0.962797, coverage 0.998673 → 1.038640) and
`transitions` (0.983597 → 0.956730, coverage 1.003218 → 1.045227). It was
removed and the shader was rebuilt from the original source; the final CLI SHA
above is the baseline binary, not the probe.

**Accepted/deferred by ADR-040:**

- T0-01 residual on seven cases: AA/coverage (curves and diagonals), effect
  composition/glow/background, multi-shadow union mask, and 3D projection.
  These are accepted as documented residual, not PASS.
- T1-01 GPU canonical union-mask shadow: deferred; CPU `Canonical` exists,
  Vulkan remains fail-closed.
- T1-02 GPU `VectorStroke`: deferred; the software vector path exists, Vulkan
  remains fail-closed.
- T2-01 `text_transitions` + spans/paint ranges: remains the ADR-039 non-goal;
  the fail-closed rejection stays.
- T0-02/T0-03 pixel cases: functional paths pass, frozen pixel gate not met;
  accepted under T0-01.
- T1-03 High-profile/cache parity and the editorial-corpus extension: deferred
  technical debt; no new profile claim.

**Exit conditions:** removing this exception requires GPU canonical shadow,
GPU vector stroke and a typed T2-01 path, followed by a full frozen-corpus
rerun on the final SHA. The fail-closed paths must stay until then; no silent
fallback or threshold change is authorized. Owner: Chronon3D text/runtime.

**Working tree:** this closeout changes documentation and evidence only. The
2×2 shader probe was reverted; no rendering-code change from this closeout is
retained.
