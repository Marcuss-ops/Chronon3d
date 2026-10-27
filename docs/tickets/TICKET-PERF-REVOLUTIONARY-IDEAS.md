# TICKET-PERF-REVOLUTIONARY-IDEAS — Idee di performance da validare

**Stato:** PROPOSTA + fix implementati e misurati (2026-10-27): 1° lotto §7
(C-5/C-6/C-11/C-12/C-13 + strumentazione P1) e 2° lotto P4
(parallelismo intra-nodo + EVP sha256, wall −24%, output bit-identico).
Esito e numeri in §5b/P4.
**Area:** rendering software/GPU, memoria, layout dati
**Fonte dati misurati:** `docs/benchmarks/profile-2026-10-08-stage-timings.md`
**Evidenza da codice:** analisi 2026-10-27 dei path caldi in 3 round (idee
I-9…I-24 ↔ problemi C-1…C-13, riferimenti file:riga). Sono **evidenze statiche,
non misurazioni**: valgono le stesse regole — nessun fattore è reale finché non
passa da un benchmark ripetibile.
**Problemi misurati:** §5b (P1…P4, log del 2026-10-25) — gli unici numeri certi
oggi; P1 (strumentazione) è il blocco per leggere tutti gli altri.

Questo ticket raccoglie idee per salti di prestazione (2x, 10x, 100x). Ogni idea
è un'**ipotesi**: va misurata prima di implementarla. Nessuna stima percentuale
è valida finché non esiste un benchmark ripetibile (regola di `AGENTS.md`).

---

## TODO — cosa manca (agg. 2026-10-27)

- [x] Aggiornare l'esito reale delle suite mirate del primo lotto: nessun verde senza test verdi. Esito 2026-10-27 pomeriggio: 12 suite mirate verdi (Debug), 3 fallimenti residui ambientali (corpus glifi, MP4 warm-cold CLI, capability GPU) su path non toccati. Dettaglio in §7 e §5b/P4.
- [ ] C-10: compilazione delle espressioni in AST/bytecode (prossimo intervento architetturale).
- [ ] C-1…C-4, C-7…C-9: decidere/misurare su golden (C-1 cambia i numeri del blur, C-2 va misurata sul corpus, C-3 è una riscrittura del rasterizer) o toccano il lane Vulkan/tooling.

## 1. Dati misurati (baseline 2026-10-08, RTX A4000, Vulkan, 1 run per shape)

| Stage | Totale 3 run (ms) | Note |
|---|---:|---|
| `execute_ms` | 19 950 | corpo del job, contiene gli stage sotto |
| `render_ms` | 7 547 | GPU + CPU insieme, non separabili |
| `writer_encode_ms` | 6 910 | include encode |
| `encode_ms` | 3 970 | encoder video |
| `session_teardown_ms` | 549 | candidato a riuso sessione |
| `full_graph_reset_ms` | 535 | reset grafo per job, candidato a cache |
| `dispatch_ms` (14 call) | 616 | overhead di dispatch |

Limiti dichiarati dal profilo: nessun timestamp GPU per pass, nessun profiler a
campionamento, una sola run per shape (varianza ignota). **Non è possibile dire
oggi quale fetta di `render_ms` sia GPU e quale CPU.**

Osservazione: `writer_encode_ms` + `render_ms` ≈ 14 500 ms su 19 950 ms: il
collo di bottiglia dichiarato è rendering e codifica, non l'orchestrazione.

---

## 2. Idee

### I-1. Rendering incrementale per tile (candidato a 10x+ su timeline statiche)
- **Ipotesi:** su composizioni con layer statici o cue rari, la maggior parte dei
  tile non cambia tra frame consecutivi. Rendere solo i tile sporchi e riusare il
  resto eliminerebbe gran parte del lavoro.
- **Base esistente:** `DirtyTileMask`, `TileGrid`, `TemporalRateClass`
  (Static / Cue / KeyframeInterval / PerFrame / Subframe).
- **Da verificare:** quota reale di tile invariati su corpus rappresentativo
  (`docs/benchmarks/gpu-corpus-v1.json`).
- **Rischio:** il progetto è deterministico. Il riuso deve dare hash identici a
  frame ricalcolati (vincolo serial == parallel e cold == warm già nei gate).

### I-2. Riduzione della banda dei pixel
- **Fatto:** `Color` è `f32` a 4 canali = 16 byte/pixel. Un 1920×1080 occupa circa
  33 MB per buffer; ogni passaggio di compositing e blur ne legge e scrive più di uno.
- **Roofline (aritmetica, non misura):** a ~50 GB/s di banda DRAM il pavimento per
  un passaggio full-frame read+write è ~1,3 ms a 1080p e ~5,3 ms a 4K. Una catena
  di N passaggi non fusi paga N volte questo pavimento, indipendentemente dal
  compilatore. È il tetto fisico contro cui va valutata ogni altra idea.
- **Ipotesi:** per gli stadi non HDR, `f16` o 8 bit ridurrebbe la banda di 2–4x.
- **Rischio:** cambia i risultati numerici; richiede golden nuovi e decisione
  esplicita sulla precisione (non è un cambio locale).

### I-3. Blur verticale a colonne (correzione locale, misurabile)
- **Fatto (da codice):** in `apply_fullres_blur` il secondo passaggio itera
  `for x { for y }` e legge `tmp.pixels_row(y)[x]`: ogni accesso cade su una riga
  diversa e usa 16 byte di una linea da 64 byte.
- **Ipotesi:** elaborare blocchi di 4–8 colonne, o trasporre a tile, usa ogni
  linea di cache per intero. Il guadagno è da misurare, non assunto.
- **Verifica richiesta:** output bit-identico al percorso attuale.

### I-4. Zero-init evitabile
- **Fatto:** `Framebuffer(w, h)` riempie sempre di `Color::transparent()`. Esiste
  già `Framebuffer(w, h, initial_clear=false)`.
- **Idea:** usarlo in tutti i buffer completamente sovrascritti nel percorso caldo.
- **Verifica:** elenco dei call site e test che i pixel siano tutti scritti.

### I-5. Riuso della sessione di render e del grafo
- **Dato:** `session_teardown_ms` (549) + `full_graph_reset_ms` (535) +
  `dispatch_ms` (616) ≈ 1,7 s su 3 run, cioè circa 8–9% di `execute_ms`.
- **Idea:** mantenere la sessione Vulkan e il grafo compilato tra job con la stessa
  forma. Il riuso è già previsto da `CompiledGraphCache` e `TemplateProgramCache`.
- **Vincolo:** non aggiungere una cache nuova senza ADR (`AGENTS.md`). Si tratta di
  estendere una cache esistente.

### I-6. Pipeline di encode in parallelo al rendering
- **Dato:** `encode_ms` (3 970) è sequenziale rispetto a `render_ms` (7 547)?
  Da verificare nel codice di `AsyncEncoderSink` (`runtime/async_encoder_sink.hpp`),
  che sembra già esistere.
- **Idea:** se il codifica non si sovrappone già al rendering, sovrapporla
  potrebbe nascondere fino a `encode_ms` del tempo totale.
- **Verifica:** leggere la pipeline reale prima di proporre modifiche.

### I-7. Layout SoA per elementi nel loop interno (solo se il conteggio lo giustifica)
- **Idea:** dati per particelle, glifi, vertici mesh in array contigui.
- **Condizione:** ha senso solo se il numero di elementi per frame è grande
  (migliaia o più). Con poche decine di layer non porta nulla.
- **Verifica:** contare gli elementi nel corpus prima di riorganizzare.

### I-8. Video in zero-copy end-to-end
- **Stato:** già oggetto di `docs/ZERO_COPY_GATES.md` (15 gate). Non è un'idea
  nuova; è un piano in corso. Qui solo come riferimento.

### I-9. Backdrop blur: riscrittura del nodo (evidenza forte da codice)
- **Fatto (da codice):** `render_graph/nodes/composite_backdrop_software.cpp:28-70`
  alloca 3 `std::vector<Color>` full-frame per chiamata (~33 MB caduno a 1080p,
  ~132 MB a 4K, tutti zero-init), poi esegue una convoluzione separabile naive
  O(radius) per pixel con clamp a ogni campione, senza downsample.
- **Base esistente NON usata:** `backends/software/effects/effect_blur.cpp` ha già
  `DownsampleHalf`/`DownsampleQuarter`; `highway_separable_kernels.cpp` ha kernel
  separabili SIMD; `FramebufferPool` esiste per gli scratch.
- **Ipotesi (ordine di grandezza, da misurare):** riusare downsample + kernel a
  somma ricorsiva riduce il nodo di 1–2 ordini di grandezza; il nodo attuale
  muove ≥6 buffer full-frame per frame solo in allocazioni/copie.
- **Verifica:** il downsample NON è bit-identico: serve golden dedicato o una
  modalità full-res di riferimento per la parità.
- **Rischio:** medio-basso (il comportamento è già collaudato in `effect_blur`).

### I-10. Compositing e maschere clippati alla bbox utile
- **Fatto (da codice):** `render_graph/nodes/multi_source_node.cpp:349-367` e
  `632-657` iterano `frame_input.width×height` per ogni gruppo SVG/maschera anche
  quando il layer copre una piccola porzione di tela; schema analogo in
  `transition_catalog_programs_basic.cpp:268`.
- **Base esistente:** `pipeline/dirty/layer_bbox_collector.cpp` calcola già le bbox
  dei layer e `tile_execution_policy.cpp` ha `dirty_regions` — non vengono usati
  per clippare questi loop.
- **Ipotesi:** il guadagno è in proporzione all'area coperta: alto su composizioni
  con layer piccoli (bullet, mappe, didascalie), nullo su full-bleed.
- **Da verificare:** distribuzione delle aree dei layer sul corpus reale
  (`docs/benchmarks/gpu-corpus-v1.json`).
- **Rischio:** medio; il clipping deve restare bit-identico dentro la bbox.

### I-11. Rasterizer mesh 3D: edge stepping e shading a densità minore
- **Fatto (da codice):** `backends/software/specialized/mesh_renderer.cpp:238-280`
  ricalcola per ogni pixel 3 edge function da zero (sono affini), 3 divisioni
  baricentriche + `1/inv_depth`, ricalcola `use_depth` (invariante di loop) dentro
  il ciclo, valuta lighting Phong completo scalare, con 5+ early-out per pixel.
- **Ipotesi:** edge stepping incrementale, 1 reciprocante per triangolo, flat/
  gouraud dove basta, early-Z e binning per tile riducono il costo per pixel di
  un ordine di grandezza.
- **Verifica:** parità golden sui render 3D; i numeri cambiano (divisioni
  incrementali non sono bit-identiche).
- **Rischio:** medio.

### I-12. Effetti distort dal fallback CPU ai compute shader
- **Fatto (da codice):** warp/twirl/bulge/mesh-warp/roughen
  (`backends/software/processors/effects/distort/warp.cpp`) sono scalari, con
  `pow`/`sin`/`cos` per pixel (twirl, `warp.cpp:129-152`); il catalogo compute
  Vulkan esiste già (`backends/vulkan/shaders/chromatic_aberration.comp`,
  `affine_transform.comp`, `layer_batch.comp`) e i contatori `software_fallback`
  tracciano già le ricadute al CPU.
- **Ipotesi:** estendere il catalogo elimina il fallback per questi effetti nel
  lane Vulkan. Non tocca il backend software (che resta la reference).
- **Verifica:** parità software/Vulkan già governata dai gate esistenti.
- **Rischio:** medio.

### I-13. Color pipeline: dispatch fuori dal loop pixel (sblocca il SIMD)
- **Fatto (da codice):** `effects/color_pipeline.cpp:126-160` — per ogni pixel e
  per ogni stage la catena di 6 `std::get_if` è invariante di loop: il branch
  predictor la regge, ma blocca ogni vettorizzazione del compilatore sulla
  pipeline fusa.
- **Ipotesi:** specializzare la catena in kernel per combinazione di stage (o
  dispatch fuori dal loop) rende il loop vettorizzabile, senza cambiare l'ordine
  aritmetico delle stage.
- **Verifica:** atteso output bit-identico (stessa aritmetica, stesso ordine).
- **Rischio:** basso.

### I-14. Maschere e depth a canale singolo (densità 8x)
- **Fatto (da codice):** le maschere vengono lette solo su `.a`
  (`multi_source_node.cpp:634`, `blend2d_bridge_core.cpp:40`) ma vivono in
  framebuffer RGBA32F: 16 byte/pixel per 1 canale utilizzata.
- **Ipotesi:** un formato a canale singolo (o almeno f16) moltiplica la densità
  in cache per ~8 sulle passate di masking.
- **Rischio:** tocca un formato di buffer condiviso; va verificato chi legge altri
  canali delle stesse superfici.

### I-15. Scratch degli effetti fuori pool e copie full-frame ai confini
- **Fatto (da codice):** `warp.cpp:246-266` (`apply_roughen_edges`) alloca un
  `std::vector<float>` full-frame + deinterleave scalare RGBA→planar per ogni
  chiamata; `render_graph/nodes/native_surface.hpp:78-87` (`pack_framebuffer_rgba`)
  copia l'intero frame quando lo stride è paddato (la vista zero-copy
  `framebuffer_rgba_view` copre solo `stride == width`).
- **Ipotesi:** instradare gli scratch nel `FramebufferPool` ed estendere il
  contratto zero-copy elimina allocazioni/copie nei path caldi.
- **Vincolo:** la baseline `docs/baselines/steady-state-allocations-certified.txt`
  deve restare verde.
- **Rischio:** basso.

### I-16. Tooling batch: daemon persistente (area adiacente)
- **Fatto (da codice):** i tool di ChrononTemplate/ChrononMotion3D invocano
  `chronon3d_cli` via subprocess per ogni piano (spesso `validate` + `render` =
  due avvii, due parsing del piano, due scansioni asset) più `ffprobe`/`ffmpeg`
  per ogni artefatto.
- **Base esistente NON usata:** `src/daemon/daemon_runtime.cpp` e
  `src/ipc/composition_session.cpp` — il daemon esiste già, i tool non lo usano.
- **Ipotesi:** una sessione persistente multi-piano ammortizza startup, parsing e
  asset-load; si aggancia naturalmente a I-5.
- **Rischio:** basso, ma fuori dal cuore rendering: solo se i batch di piani
  diventano un caso d'uso reale.

---

## 2b. Nuove idee (round 2, verificate nel codice — I-17…I-20, proseguono la numerazione)

### I-17. Sovrapposizione encode già presente: non è un guadagno nuovo
- **Fatto:** `AsyncEncoderSink` (`include/chronon3d/runtime/async_encoder_sink.hpp`) usa una coda limitata di 4 frame con worker thread. L'encode è già in parallelo al rendering.
- **Conseguenza:** I-6 è chiuso. Il guadagno residuo sta nel dimensionare la coda, non nel parallelizzare.
- **Da misurare:** se la coda si riempie (producer bloccato) o si svuota (encoder fermo).

### I-18. Readback GPU→CPU per frame (candidato reale)
- **Fatto:** `phase_gpu_readback_ms` è definito come "framebuffer → encoder conversion/copy" in `render_telemetry_record.hpp`. Esistono `gpu_readback_bytes` e `gpu_readback_ms`.
- **Idea:** se ogni frame viene scaricato su CPU in float32, ogni frame 1080p costa 16 byte/pixel di banda PCIe. Scaricare in RGBA8 o NV12 direttamente sulla GPU ridurrebbe la banda di 4x o più.
- **Già esistente:** `ZERO_COPY_GATES.md` punta a `RGBA→NV12` su GPU, quindi la direzione è nota. Va verificato quale percorso è attivo di default.
- **Verifica:** leggere `gpu_readback_bytes` e `phase_gpu_readback_ms` da un job reale.

### I-19. Coda di encode dimensionata sul profilo
- **Idea:** la capacità di 4 è un default. Se il profilo mostra producer bloccato, aumentarla (o usare più slot) nasconde jitter dell'encoder.
- **Rischio:** più slot = più memoria per frame. Con 16 byte/pixel il costo è noto (I-2).

### I-20. Scelta encoder in base all'hardware
- **Fatto:** il codice supporta `libx264` (CPU) e `h264_nvenc` (GPU). Il profilo 2026-10-08 è su RTX A4000 con Vulkan.
- **Idea:** su job con encode dominante, NVENC (se disponibile) sposta il lavoro dalla CPU alla GPU. È una scelta di configurazione, non una riscrittura.
- **Verifica:** confrontare qualità (PSNR/SSIM) e tempo con `preset` e `crf`/`cq` fissati.

---

## 2c. Nuove idee (round 3 — I-21…I-24, dettaglio in C-10…C-13)

- **I-21. Compilare le espressioni una volta sola.** Le espressioni delle proprietà
  animate vengono riparse da stringa a ogni frame (C-10): è letteralmente
  l'esempio "parsing di stringhe che potevano rimanere ID" della regola 1.
  Il meccanismo `frame_evaluator()` esiste già e va usato.
- **I-22. Ricerca keyframe non lineare.** `evaluate_base_double` fa scan lineare
  del segmento per ogni valutazione (C-11): `upper_bound` + cursore monotono.
- **I-23. Morfologia stroke O(1).** dilate/erode usano sliding window naive
  O(radius) + gather per colonna (C-12): van Herk–Gil–Werman, bit-identico.
- **I-24. Downscale immagini specializzato.** Il resize di `ImageCache` è scalare
  in `double` con branch invariante per campione (C-13): priorità bassa
  (load-time), misurare prima.

Nota di round: `transform_kernels.cpp` (split edge/interior + prefetch + interior
senza bounds-check) e `per_pixel_dof_node.cpp` (ROI O(w·h) al posto del gather
O(w·h·r), path nativo GPU) sono stati ispezionati e sono **già a buon livello**:
non rientrano in questo round.

---

## 3. Problemi strutturali dal codice: dettaglio e soluzioni (2026-10-27)

Questa sezione non è speculazione: ogni voce cita il **codice attuale** (file:riga),
spiega **perché è un problema** in termini meccanici (byte mossi, allocazioni,
branch, istruzioni per pixel) e propone la **soluzione concreta** con le API che
esistono già nel repo. Il codice "DOPO" è una **proposta di patch, non
implementata**: ogni patch va protetta dai golden del caso e misurata prima di
entrare (regola di `AGENTS.md`).

---

### C-1 — Backdrop blur: 3 allocazioni full-frame + convoluzione naive O(radius)
**Dove:** `render_graph/nodes/composite_backdrop_software.cpp:28-70` → idea I-9

**Problema (codice attuale):**
```cpp
// allocazione di 3 buffer full-frame per OGNI chiamata (zero-init inclusa)
std::vector<Color> source(count), horizontal(count), blurred(count);   // 3 × 33 MB @1080p, 3 × 132 MB @4K
...
for (i32 y = 0; y < height; ++y) for (i32 x = 0; x < width; ++x) {
    Color out{0, 0, 0, 0};
    for (int k = -extent; k <= extent; ++k) {
        const Color c = sample(source, x + k, y);   // lambda con std::clamp per campione
        out.r += c.r * w; out.g += c.g * w; out.b += c.b * w; out.a += c.a * w;
    }
    horizontal[...] = out;
}
// poi la seconda passata verticale identica su `horizontal` → `blurred`
```
**Perché è un problema:** (a) 3 allocazioni heap full-frame + zero-init per frame
(~400 MB/s di sola inizializzazione a 30 fps su 4K); (b) la convoluzione costa
O(radius) MUL+ADD per pixel con clamp a ogni campione, scalare, su 2 passate;
(c) nessun downsample, nessun clipping alla regione visibile; (d) 6 buffer
full-frame letti/scritti per frame solo per il blur. Il repo possiede già la
soluzione: `renderer::apply_blur()` (`effects/render_effects_processor.hpp:35`,
impl. `backends/software/effects/effect_blur.cpp:303`) sceglie da solo la
strategia (`SeparableFullRes` / `DownsampleHalf` / `DownsampleQuarter`, con
upscaling bilineare e margini di halo già calcolati via
`effects::spatial_halo_extent`).

**Soluzione:** rimpiazzare tutto il blocco con la API esistente + uno scratch
persistente:
```cpp
// DOPO — nessuna allocazione, stessa interfaccia del nodo
const i32 margin = static_cast<i32>(std::ceil(settings.blur)) * 3;   // halo conservativo
const raster::BBox work_clip = expand(visible_bbox, margin);         // vedi C-2 per visible_bbox
auto& work = scratch.backdrop_scratch(bottom.width(), bottom.height()); // EffectScratchResources, persistente
work.clear(Color::transparent());
work.blit(bottom, 0, 0);
renderer::apply_blur(work, settings.blur, work_clip, /*passes=*/2);  // downsample + kernel esistenti
// ... poi il composito di `top` su `work` dentro work_clip (C-2)
```

**Verifica:** il downsample **non è bit-identico** alla gaussiana naive: serve un
golden dedicato per il backdrop blur e una decisione esplicita (accettare
l'approssimazione come già fatto per `effect_blur`, oppure mantenere il path
esatto dietro flag per i golden). Misurare il nodo da solo prima/dopo.

---

### C-2 — Compositing e maschere su tutta la tela invece che sulla bbox utile
**Dove:** `render_graph/nodes/multi_source_node.cpp:349-367` (blend gruppi) e
`632-657` (maschere + composito per elemento) → idea I-10

**Problema (codice attuale, composito per elemento):**
```cpp
for (i32 y = 0; y < ctx.frame_input.height; ++y) {          // INTERA tela, per ogni elemento
    for (i32 x = 0; x < ctx.frame_input.width; ++x) {
        const Color src = source_surface->get_pixel(x, y);  // bounds-check per pixel
        if (src.a <= 0.0f) continue;                        // branch imprevedibile
        const Color dst = draw_target->get_pixel(x, y);     // bounds-check per pixel
        const f32 keep = 1.0f - src.a;
        draw_target->set_pixel(x, y, Color{ src.r + dst.r * keep, ... });  // bounds-check
    }
}
```
**Perché è un problema:** per ogni elemento/gruppo/maschera si attraversano
`width*height` pixel indipendentemente dall'area coperta. Su una composizione 4K
con 15 elementi sono ~15 passaggi full-frame = ~2 GB di traffico DRAM per frame,
+ 3 bounds-check per pixel. Le bbox dei layer **esistono già**
(`pipeline/dirty/layer_bbox_collector.cpp`) e c'è già uno scanner di copertura
allocation-free: `alpha_bbox_scan(const Framebuffer&)`
(`chronon3d/text/alpha_bbox_scanner.hpp`).

**Soluzione:** rettangolo utile + puntatori riga nudi + "over" branchless (su
premultiplificata, `src.a == 0` degrada naturalmente a `dst`, quindi l'early-out
è eliminabile senza cambiare i risultati):
```cpp
// DOPO — stessa semantica, stesso ordine aritmetico
const Rect ink = alpha_bbox_scan(*source_surface);          // una scansione per superficie, da cachare
const raster::BBox box = intersect(to_bbox(ink), current_clip);
if (box.is_empty()) return true;
for (i32 y = box.y0; y < box.y1; ++y) {
    const Color* s = source_surface->pixels_row(y) + box.x0;   // nessun bounds-check nel loop
    Color*       d = draw_target->pixels_row(y) + box.x0;
    for (i32 x = 0, n = box.x1 - box.x0; x < n; ++x) {
        const f32 keep = 1.0f - s[x].a;
        d[x] = Color{ s[x].r + d[x].r * keep, s[x].g + d[x].g * keep,
                      s[x].b + d[x].b * keep, s[x].a + d[x].a * keep };
    }
}
```
La stessa identica trasformazione vale per `blend_group_surface` (349-363, con
`group_alpha` moltiplicato in testa) e per il loop maschere (632-643), dove la
scan della maschera sostituisce il get_pixel per pixel. A valle: versione SIMD
del loop su 4-8 pixel per iterazione (`highway_color_kernels.cpp` esiste già).

**Verifica:** atteso bit-identico dentro la bbox (a parte ±0/NaN); test di
parità fuori bbox (deve restare invariato). Contare i passaggi full-frame/frame
prima e dopo (misura §3.6).

---

### C-3 — Mesh rasterizer 3D: geometria ricalcolata per pixel
**Dove:** `backends/software/specialized/mesh_renderer.cpp:238-280` → idea I-11

**Problema (codice attuale):**
```cpp
for (int y = y0; y <= y1; ++y) {
    Color* row = fb.pixels_row(y);
    for (int x = x0; x <= x1; ++x) {
        const Vec2 p{float(x) + 0.5f, float(y) + 0.5f};
        const float w0 = edge(b, c, p) / area;   // edge() ricalcolata da zero: 2 MUL + 2 SUB
        const float w1 = edge(c, a, p) / area;   // + 1 DIV per baricentrica = 3 DIV/pixel
        const float w2 = edge(a, b, p) / area;
        if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f) continue;
        const float inv_depth = w0 * z0 + w1 * z1 + w2 * z2;
        if (!(inv_depth > 0.0f)) continue;
        const float depth = 1.0f / inv_depth;                  // 4ª DIV/pixel
        const bool use_depth = depth_buffer.size() == ...;      // invariante di loop, ricalcolato per pixel
        ... evaluate_lighting(...) ...                          // Phong completo scalare per pixel
    }
}
```
**Perché è un problema:** le edge function sono **affini** nel pixel: ricalcolarle
per pixel è 2 MUL + 2 SUB laddove bastano 2 ADD. Le 3 divisioni baricentriche
sono 1 reciprocante (`1/area`) + 3 MUL. `use_depth` è invariante di loop. Il
Phong completo (normalize + pow speculare) gira anche dove il flat shading
basterebbe. Con 5+ early-out per pixel il branch predictor è in difficoltà.

**Soluzione:** edge stepping incrementale + invariante fuori dal loop:
```cpp
// DOPO — stesse edge function, stesso ordine aritmetico (a meno di ULP sulle somme)
const float inv_area = 1.0f / area;                            // 1 DIV per triangolo
const float dw0_dx = (c.y - b.y) * inv_area, dw0_dy = (b.x - c.x) * inv_area;
const float dw1_dx = (a.y - c.y) * inv_area, dw1_dy = (c.x - a.x) * inv_area;
const float dw2_dx = (b.y - a.y) * inv_area, dw2_dy = (a.x - b.x) * inv_area;
float w0_row = edge(b, c, p00) * inv_area, w1_row = ..., w2_row = ...;
const bool use_depth = depth_buffer.size() == size_t(fb.width()) * fb.height();   // FUORI dal loop
for (int y = y0; y <= y1; ++y) {
    float w0 = w0_row, w1 = w1_row, w2 = w2_row;
    for (int x = x0; x <= x1; ++x, w0 += dw0_dx, w1 += dw1_dx, w2 += dw2_dx) {
        if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f) continue;   // early-Z prima delle interpolazioni
        ...
    }
    w0_row += dw0_dy; w1_row += dw1_dy; w2_row += dw2_dy;
}
```
Poi, come secondo step: shading flat/gouraud quando `source_material` non chiede
specular per-pixel (il caso comune dei bullet 3D), e tassellazione per tile per
il depth buffer.

**Verifica:** golden 3D (i numeri cambiano: somme incrementali ≠ somme dirette,
ULP-level); benchmark dedicato su una scena con N triangoli nota.

---

### C-4 — Twirl/warp: `pow` + `sin`/`cos` per pixel
**Dove:** `backends/software/processors/effects/distort/warp.cpp:129-152` → idea I-12

**Problema (codice attuale):**
```cpp
for (int y = ...) for (int x = ...) {
    const float distance = std::sqrt(dx * dx + dy * dy);
    float theta = 0.0f;
    if (distance < radius_px && distance > kGeomEpsilon) {
        const float t = 1.0f - distance / radius_px;
        theta = angle_rad * std::pow(t, params.falloff);   // pow per pixel
    }
    const float cos_t = std::cos(-theta), sin_t = std::sin(-theta);  // sin+cos per pixel
    ... ctx.sampler.bilinear(sx, sy); ...
}
```
**Perché è un problema:** `pow`+`sin`+`cos` sono ~50-100 cicli l'una sullo scalar
path; su un frame 4K sono ~250 M di trascendenti per singolo effetto. Eppure
`theta` dipende **solo** da `distance`: è una funzione 1D, tabellizzabile.

**Soluzione:** LUT 1D su `(cos, sin)` indicizzata sul raggio normalizzato:
```cpp
// DOPO — una LUT per chiamata (1024 voci), inner loop = solo FMA + bilinear
struct Pol { float c, s; };
Pol lut[kLut + 1];
for (int i = 0; i <= kLut; ++i) {
    const float t = 1.0f - static_cast<float>(i) / kLut;     // t = 1 - r/radius
    const float theta = angle_rad * std::pow(t, params.falloff);   // 1024 pow, non 8M
    lut[i] = {std::cos(-theta), std::sin(-theta)};
}
...
const float rn = distance * (kLut / radius_px);
const int idx = std::min(static_cast<int>(rn), kLut - 1);
const float fr = rn - idx;
const float cos_t = lut[idx].c + (lut[idx + 1].c - lut[idx].c) * fr;
const float sin_t = lut[idx].s + (lut[idx + 1].s - lut[idx].s) * fr;
```
Stessa identica lezione per `apply_bulge` (1 `sqrt` + 1 `pow`-free ma con
`distance < radius_px` branchy) e per `apply_corner_pin` (`inverse_bilinear`
completa per pixel: la matrice omografica inversa si può calcolare **una volta**
per chiamata). Attenzione: l'interpolazione della LUT introduce un errore ≤
ULP della tabella — serve golden con tolleranza esplicita.

**Verifica:** golden con soglia numerica dichiarata; benchmark su 4K.

---

### C-5 — Color pipeline: dispatch `std::get_if` per pixel (blocca il SIMD)
**Dove:** `effects/color_pipeline.cpp:126-160` → idea I-13

**Problema (codice attuale):**
```cpp
for (int y = y0; y < y1; ++y) {
    Color* row = fb.pixels_row(y);
    for (int x = x0; x < x1; ++x) {
        ...
        for (const auto& stage : m_stages) {                      // per pixel, per stage:
            if (auto* exp = std::get_if<ExposureStage>(&stage))   // 6 tipi di variant
                apply_exposure_stage(srgb, *exp);
            else if (auto* lvl = std::get_if<LevelsStage>(&stage))
            ... // catena completa a ogni pixel
        }
```
**Perché è un problema:** la catena di `std::get_if` è invariante di loop: il
branch predictor la risolve, ma il compilatore **non può vettorizzare** un loop
il cui corpo è una catena di variant dispatch. La pipeline fusa — che è già un
passaggio unico, ottimo per la cache — resta quindi scalare: si lascia sul tavolo
il fattore 4-8 di AVX2/NEON.

**Soluzione:** la chiusura dei tipi fusibili è nota e finita
(`append_fusible_stage`, `backends/software/effects/effect_stack.cpp:26-90`:
Exposure | Levels | Curves | Lut3D | Compose | ColorMatrix): si può specializzare
la catena per combinazione, una volta per `apply()`:
```cpp
// DOPO — compilazione della catena una volta per apply(), loop vettorizzabile
class ColorPipeline {
    // istanza di default: catena specializzata per la sequenza reale degli stage
    template <class... Stages>
    struct Kernel {
        static void process_row(Color* row, int n, const Stages&... st) {
            for (int x = 0; x < n; ++x) { /* stessi calcoli, senza dispatch */ }
        }
    };
    void apply(Framebuffer& fb, const std::optional<raster::BBox>& clip) const {
        dispatch_compiled_chain(m_compiled, fb, clip);   // switch UNA volta, non per pixel
    }
};
```
con un fallback scalare per le combinazioni rare (il corpo del kernel è lo stesso
codice di oggi, solo istanziato staticamente). A valle: versione SIMD della
catena più comune con `simd/pixel_kernels.hpp` / Highway.

**Verifica:** atteso **bit-identico** (stessa aritmetica, stesso ordine degli
stage). È la patch più sicura dell'intera sezione.

---

### C-6 — `apply_roughen_edges`: allocazione full-frame + deinterleave scalare
**Dove:** `backends/software/processors/effects/distort/warp.cpp:246-266` → idea I-15

**Problema (codice attuale):**
```cpp
const std::size_t count = size_t(width) * size_t(height) * 4;
std::vector<float> pixels(count);                    // allocazione full-frame per chiamata
for (int y = 0; y < height; ++y) {
    const Color* row = fb.pixels_row(y);
    for (int x = 0; x < width; ++x) {                // copia scalare pixel-per-pixel...
        const std::size_t i = (size_t(y) * width + x) * 4;
        pixels[i] = row[x].r; pixels[i + 1] = row[x].g;  // ...di dati GIÀ contigui:
        pixels[i + 2] = row[x].b; pixels[i + 3] = row[x].a;  // Color == 4×f32
    }
}
effects::distort::roughen_edges_rgba(pixels, width, height, {...});
```
**Perché è un problema:** `Color` **è** `float[4]` contiguo (`math/color.hpp:48`,
`static_assert` già presente in `native_surface.hpp:83`): quel loop scalare è una
ricopia di 16 B/px che `memcpy` fa a larghezza di banda piena. E il `vector` è
un'allocazione heap full-frame a ogni chiamata, quando esiste uno spazio di
lavoro persistente dichiarato apposta — `EffectScratchResources`
(`include/chronon3d/backends/software/software_session_resources.hpp:71`), con
contratto documentato in `radial_blur.hpp:25-27` ("un'assenza di scratch
alloca un framebuffer heap per frame, rompendo il gate zero-allocation").

**Soluzione:**
```cpp
// DOPO — nessuna allocazione, copia a larghezza di banda piena
std::vector<float>& pixels = scratch.rgba_scratch();   // persistente, grow-once
pixels.resize(count);
for (int y = 0; y < height; ++y)
    std::memcpy(pixels.data() + size_t(y) * width * 4,
                fb.pixels_row(y), size_t(width) * sizeof(Color));   // byte identici al loop scalare
effects::distort::roughen_edges_rgba(pixels, width, height, {...});
for (int y = 0; y < height; ++y)
    std::memcpy(fb.pixels_row(y),
                pixels.data() + size_t(y) * width * 4, size_t(width) * sizeof(Color));
```
(Se `roughen_edges_rgba` può operare su righe `Color*` direttamente, il doppio
buffer sparisce del tutto — da verificare nella sua firma.)

**Verifica:** atteso **bit-identico** (stessi byte copiati più in fretta). Il
gate `docs/baselines/steady-state-allocations-certified.txt` deve restare verde.

---

### C-7 — `pack_framebuffer_rgba`: copia full-frame con allocazione fresca ai confini CPU→GPU
**Dove:** `render_graph/nodes/native_surface.hpp:78-87` → idea I-15

**Problema (codice attuale):**
```cpp
inline std::vector<float> pack_framebuffer_rgba(const Framebuffer& framebuffer) {
    std::vector<float> rgba(width * height * 4);        // allocazione fresca per upload
    for (y...) std::memcpy(rgba.data() + y * width * 4, row, width * sizeof(Color));
    return rgba;
}
```
`framebuffer_rgba_view()` usa la vista zero-copy solo quando `stride() == width()`;
con lo stride allineato alla cache line (`align_stride_to_cache_line`), i frame a
larghezza non multipla di 4 ricadono nel packing con `std::vector` temporaneo.

**Soluzione:** buffer di packing persistente per contesto (o dal pool), mai
`std::vector` temporaneo:
```cpp
// DOPO
inline std::span<const float> framebuffer_rgba_view(
    const Framebuffer& fb, PackedRgbaScratch& scratch) {
    if (fb.stride() == fb.width())
        return {reinterpret_cast<const float*>(fb.data()), size_t(fb.width()) * fb.height() * 4};
    scratch.ensure(size_t(fb.width()) * fb.height() * 4);   // grow-once, riutilizzato tra frame
    pack_rows(fb, scratch.bytes());                         // memcpy per riga
    return scratch.view();
}
```
In parallelio: contatore `pack_scratch_frames` nel timing sidecar per misurare
quanto spesso accade il packing (se la quota è 0 sul corpus reale, l'idea decade).

**Verifica:** bit-identico; baseline allocazioni stabile; contatore packing prima/dopo.

---

### C-8 — Maschere in RGBA32F per un solo canale usato
**Dove:** `render_graph/nodes/multi_source_node.cpp:634`,
`backends/software/blend2d/blend2d_bridge_core.cpp:40` → idea I-14

**Problema (codice attuale):**
```cpp
const f32 coverage = std::clamp(mask_surface->get_pixel(x, y).a, 0.0f, 1.0f);
// mask_surface è un Framebuffer RGBA32F: 16 byte/pixel per leggere UN canale
```
**Perché è un problema:** le maschere sono scritte come superfici complete
(`draw_node` su `mask_surface`) ma lette solo su `.a`: 8x di densità persa in
cache rispetto a un buffer a canale singolo (2 B con f16, 4 B con f32).

**Soluzione (la più invasiva della sezione: misurare prima):**
1. Contare le maschere vive per frame e i byte che occupano (contatore nel
   profiling, già predisposto a `NodeMemoryEvent`).
2. Se il conteggio giustifica, introdurre un buffer alpha-only riutilizzabile
   (interfaccia minima: `alpha_at(x,y)`, `clear()`, target di disegno per
   `draw_node` — è quest'ultimo il vincolo: il renderer deve poter disegnare su
   un target 1-canale).
3. Fase intermedia a basso rischio: se una maschera non viene mai letta su altri
   canali (da verificare con un audit statico dei call site), rappresentarla come
   `f16` a 4 canali: dimezza la banda senza cambiare le API.

**Verifica:** golden con maschere (svg-group-isolation ha già test dedicati);
contatore byte maschera/frame prima/dopo.

---

### C-9 — Tooling batch: due avvii CLI per piano, il daemon esiste e non è usato
**Dove:** tool di `ChrononTemplate/tools/*` e `ChrononMotion3D/tools/*`
(`subprocess.run([cli, 'validate', ...])` + `subprocess.run([cli, 'render', ...])`
per ogni piano) → idea I-16

**Problema (pattern ripetuto in ~135 call site):**
```python
subprocess.run([str(cli), "validate", "--plan", str(path), ...], check=True)   # avvio 1: parse + scan asset
subprocess.run([str(cli), "render",  "--plan", str(path), ...], check=True)    # avvio 2: parse + scan asset DI NUOVO
```
**Perché è un problema:** ogni avvio ripaga startup runtime, parsing del piano,
risoluzione asset e font-shaping. Su un batch da 50 piani sono 100 avvii.

**Soluzione:** il daemon esiste già (`src/daemon/daemon_runtime.cpp`,
`src/ipc/composition_session.hpp:65` con `CompositionSession::render_frame`, e
`src/ipc/ipc_command_dispatcher.hpp:56` con `render_frame_timed`): prima opzione
(unica locale, nessuna nuova dipendenza) è un sottocomando batch nel CLI:
```
chronon3d_cli render-batch --plans plans.txt --assets-root . --out out/
```
che apre **una** sessione e itera i piani riusando runtime/asset cache; i tool
passano al sottocomando. In seconda battuta, i tool parlano col daemon via IPC
(esistente) invece di avviare processi.

**Verifica:** parità degli artefatti (hash video) tra percorso batch e percorso
attuale; misura wall-clock su un batch reale di N piani.

---

### C-10 — Espressioni riparse da stringa a ogni frame per ogni proprietà
**Dove:** `include/chronon3d/math/expression.hpp:549-556` +
`include/chronon3d/animation/core/animated_value.hpp:321-356` → idea I-21

**Problema (codice attuale):**
```cpp
// expression.hpp:549 — lexer + recursive-descent parsing, OGNI chiamata
inline double evaluate_expression(std::string_view expr, ...) {
    ExpressionParser parser(expr, vars);   // costruito da capo ogni volta
    double value = parser.parse();         // riparsa della stringa
    return parser.error() ? fallback : value;
}
```
chiamato da `AnimatedValue<T>::evaluate(SampleTime, ctx)` (riga 345) e
`evaluate(double)` (riga 356) — cioè **a ogni frame, per ogni proprietà animata
con espressione**. Nello stesso percorso viene anche ricostruita la
`std::unordered_map<std::string, double> vars{...}` (allocazioni heap per
evalutazione), e il parser muta `std::string` member (`m_current_object`,
`m_current_layer_name`, `m_current_property_path`) durante il parse: altre
allocazioni dentro il percorso per-frame.

**Perché è un problema:** è l'esempio letterale della regola 1 ("parsing di
stringhe che potevano rimanere ID numerici") e della regola 4 (allocazioni nel
percorso critico). La stringa dell'espressione è immutabile dopo il build
(`AnimatedValue::expression(std::string)`), quindi il 100% del lavoro di parse è
lavoro che non dovrebbe esistere nel frame loop. Con 50 proprietà animate su
30 fps × 10 s = 15 000 parsing ricorsivi, più mappe allocate e distrutte.

**Soluzione:** compilare una volta, valutare molte volte.
1. Il meccanismo esiste già: `frame_evaluator()` (`animated_value.hpp:283-288`)
   — "Install an immutable, frame-pure evaluator compiled from an authored
   driver. Construction happens before rendering". Estenderlo alle espressioni:
   al setter `expression(std::string)` compilare una volta in AST/bytecode
   (`m_compiled = math::compile_expression(m_expression)`), e far sì che
   `evaluate*` legga solo il compilato.
2. Il commento in `animated_value.hpp:255-266` descrive già la strada completa
   (Path B: lexer/AST/bytecode/VM); il passo minimo è memoizzare il parse per
   stringa — una cache `string → shared_ptr<CompiledExpr>` — se non si vuole
   toccare il parser ora.
3. Le `vars` per-chiamata diventano una struct fissa (l'`ExpressionContext`
   esiste già) e lo scratch del parser passa in arena thread-local (regola 4).

**Verifica:** risultati di valutazione identici (stessa semantica del parser
attuale — il compilato deve usare lo stesso grammar); benchmark su una
composizione expression-heavy (N proprietà × M frame); contatore allocazioni in
stato stazionario che resta a 0 dentro il frame loop.

---

### C-11 — Ricerca lineare del segmento keyframe a ogni valutazione
**Dove:** `include/chronon3d/animation/core/detail/animated_value_evaluation.inl:97-108`
→ idea I-22

**Problema (codice attuale):**
```cpp
// Linear scan for keyframe segment (uses > to correctly handle
// duplicate frames — matches upper_bound behavior).
size_t idx = 0;
for (size_t i = 0; i + 1 < m_keyframes.size(); ++i) {
    if (static_cast<double>(m_keyframes[i + 1].frame) > eval_frame) {
        idx = i;
        break;
    }
    idx = i + 1;
}
```
**Perché è un problema:** O(K) per proprietà per frame. Con pochi keyframe
artistici non si nota; con dati importati (motions campionati per frame,
Lottie densi, path registrati) K può essere migliaia, e il frame avanza in modo
monotono: si riesamina da capo una lista che si potrebbe attraversare una volta.
Regole 1 (lavoro ripetuto) e 3 (accesso che potrebbe essere indicizzato).

**Soluzione:**
1. Sostituzione diretta e semantica-preservante: il commento stesso dice
   "matches upper_bound behavior" — quindi
   `std::upper_bound(m_keyframes.begin(), m_keyframes.end(), eval_frame, ...)`
   è un drop-in con stesso risultato (inclusi i frame duplicati).
2. Aggiungere un cursore monotono (cache del primo indice provato):
   ```cpp
   // i frame avanzano monotoni nel render loop: provare idx, poi idx+1,
   // solo allora la ricerca binaria completa → O(1) amortizzato
   if (m_cursor + 1 < m_keyframes.size() && frame_at(m_cursor) <= eval_frame &&
       eval_frame < frame_at(m_cursor + 1)) { idx = m_cursor; }
   ```
   (il cursore va gestito come cache thread-safe o risolto localmente per
   chiamata se `AnimatedValue` è condiviso).

**Verifica:** unit test esistente sui frame duplicati deve restare verde (la
semantica `upper_bound` è già dichiarata); benchmark con K=1000 keyframe ×
10 000 valutazioni sequenziali.

---

### C-12 — Stroke: sliding window O(radius) e gather per colonna
**Dove:** `backends/software/processors/effects/stroke/stroke.cpp:55-134` → idea I-23

**Problema (codice attuale):**
```cpp
// riga 60 — finestra naive: O(2*radius+1) per pixel
for (int sx = x0; sx <= x1; ++sx) mn = std::min(mn, src[sx]);

// riga 89-95 — passata verticale: gather per colonna
for (int x = 0; x < w; ++x) {
    for (int y = 0; y < h; ++y) col_src[y] = fb.pixels_row(y)[x].a;   // 1 cache line per 4 byte
    dilate_row_horizontal(col_src.data(), col_dst.data(), h, radius);
    for (int y = 0; y < h; ++y) fb.pixels_row(y)[x].a = col_dst[y];
}
```
**Perché è un problema:** due moltiplicatori devastanti. (a) Il min/max scorrevole
naive costa O(w·h·radius) per passata: con radius=32 sono ~65 operazioni per
pixel dove ne basta 1. (b) Il gather per colonna tocca una cache line diversa per
ogni 4 byte letti: a 1080p sono ~2 milioni di cache line per passata solo in
lettura. Lo stroke fa dilate + erode = 2 morfologie × 2 passate. Stesso difetto
strutturale di I-3, in un path diverso.

**Soluzione:**
1. **van Herk–Gil–Werman** per min/max scorrevole: 3 passate fisse sui dati,
   costo O(1) per pixel **indipendente da radius**. Per min/max è una selezione,
   non un'accumulazione: il risultato è **bit-identico** (stessi valori
   selezionati, ordine di confronto irrilevante).
2. **Passata verticale su tile trasposte** (o su gruppi di 4-8 colonne, come in
n   I-3): ogni cache line viene letta una volta e usata per 4-8 pixel.
3. Lo scratch (`scratch.row_src/col_src`, già persistente) resta identico.

**Verifica:** bit-identico garantito dalla proprietà di min/max — test di parità
diretta; benchmark al variare di radius (il nuovo path è piatto, il vecchio
cresce linearmente).

---

### C-13 — Downscale immagini: `double` scalare con branch per campione
**Dove:** `backends/assets/image_cache.cpp:58-100` → idea I-24

**Problema (codice attuale):**
```cpp
for (int y ...) for (int x ...) {          // area-average, scalare
    ...
    for (int c = 0; c < 4; ++c) {
        const double value = buffer.pixels                              // branch INVARVIANTE
            ? static_cast<double>(buffer.pixels[source_offset + c])     // valutato per campione
            : static_cast<double>(buffer.linear_pixels[source_offset + c]);
        acc[c] += value * weight;
    }
}
// + 2 allocazioni heap per resize: std::make_unique<u8[]>, std::make_unique<float[]>
```
**Perché è un problema:** (a) il branch sul tipo di buffer è invariante di loop
ma viene rieseguito per ogni campione (regola 5); (b) aritmetica `double` scalare
dove `float` basta per output 8-bit (regola 7 — 2× di densità SIMD);
(c) allocazioni heap per resize (regola 4). Contro-perfetto: il path è a
load-time, una volta per immagine, e viene cachato (`ImageCache`): non è nel
frame loop.

**Soluzione (subordinata alla misura):**
1. Spezzare il loop per tipo di buffer (due kernel specializzati: u8 e f32),
   branch eliminato e inner loop vettorizzabile;
2. accumulazione in `float` con ordinamento somma→prodotto già presente — se la
   parità non regge, tenere `double` ma solo fuori dal branch;
3. buffer di resize da arena/scratch invece di `make_unique`.

**Verifica:** golden sulle immagini scalate (con float l'errore di accumulazione
può spostare ±1/255: serve soglia esplicita oppure tenere `double`);
misurare prima quanto tempo pesa il downscale nel `setup_renderer` di P1 — se
è trascurabile, chiudere l'idea come NON-FATTO.

---

## 4. Misure necessarie prima di qualsiasi implementazione

1. Timestamp Vulkan per pass (separare GPU da CPU in `render_ms`).
2. Più run per shape con varianza (mediana e deviazione).
3. Conteggio tile sporchi per frame sul corpus reale (per I-1).
4. Profilo a campionamento su `execute_ms` (per trovare la fetta CPU non loggata).
5. Split del profilo per backend (`software` vs `vulkan`): le idee I-9…I-15
   agiscono sul path software, che il profilo 2026-10-08 non separa.
6. Conteggio dei passaggi full-frame per frame e dei byte/passaggio, da confrontare
   con il pavimento roofline di I-2 (serve per ordinare I-9/I-10/I-13/I-14).
7. Distribuzione delle aree dei layer sul corpus (per I-10).
8. Conteggio delle proprietà animate con espressione per piano e del numero di
   keyframe per proprietà (per C-10/C-11): senza questi numeri le due idee
   restano ipotesi dimensionali.

## 5. Vincoli del repo

- Ogni nuova cache, registry o resolver richiede ADR.
- Ogni dipendenza o compatibility path richiede scheda Demolition Debt.
- Fix piccoli non toccano i canonici (`docs/CURRENT_STATUS.md`, ecc.).
- Ordine suggerito aggiornato (2026-10-27):
  1. **P1** (strumentazione sotto-fasi, §5b) — senza attribuzione ogni decisione
     è una scommessa;
  2. **P2** (esperimento daemon doppio) — decide se la leva è il daemon o il
     codice, e quindi dove investire;
  3. **patch bit-identiche già pronte:** C-11 (`upper_bound` + cursore), C-6
     (memcpy), C-13 (misurare e probabilmente chiudere come NON-FATTO);
  4. **patch ad alto moltiplicatore:** C-12 (VHGW, bit-identico), C-10
     (compilazione espressioni);
  5. **architetturali con ADR:** C-1/C-2 (↔ I-9/I-10) e I-1/I-5.

## 5b. Problemi di oggi misurati (2026-10-25) — problema dettagliato e soluzione

Fonte: 6 log `chronon3d-20261025-0826*.log` (job `animated_image_residency_*`, 1 frame ciascuno, 1920×1080, commit `9c4251b74`, 24 core, GCC 11.4). Questi dati sono ripresi dai campi che il CLI già registra; nessun codice è stato modificato.

### Dati grezzi

| Composition | Wall ms | `setup_renderer` ms | `rendering_loop` ms | `graph_total_wall_ms` | `Render Time` ms |
|---|---:|---:|---:|---:|---:|
| static (082652) | 3611 | 1352 | 2259 | 113 | 404 |
| static (082655) | 2869 | 1369 | 1500 | 86 | 364 |
| animated (082658) | 3223 | 1262 | 1961 | 90 | 324 |
| animated (082701) | 2649 | 1258 | 1391 | 72 | 268 |
| camera-only (082704) | 2428 | 1079 | 1349 | 66 | 238 |
| camera-only (082707) | 2536 | 1034 | 1502 | 62 | 240 |

Verifica di coerenza: in ogni run `setup_renderer + rendering_loop ≈ Wall Time` (es. 082655: 1369 + 1500 = 2869). Quindi il wall è interamente spiegato dalle due fasi.

### P1 — Il tempo va nel setup e nel loop fuori dal grafo

**Prova.** Il grafo costa 62–113 ms; `node_dispatch_wall_ms` è 2 ms. Il `setup_renderer` (1,0–1,4 s) e il `rendering_loop` (1,3–2,3 s) sono 4–20 volte il grafo. Il log dice `Hot attribution NEEDS WORK (0% explained)`: il 90%+ del tempo non ha una voce propria.

**Dove si trova nel codice.**
- `setup_renderer` = `render_job_setup.cpp`: `create_renderer` (se non c'è un renderer warm), poi `runtime::prepare_render` con `preflight_mode=FullComposition`, `preallocate_framebuffers=true`, `touch_memory=true`, `render_dummy_frame` opzionale, poi `compile` del grafo.
- `rendering_loop` = `pipe_export_render_loop.cpp`: `prepare_frame` + `render_frame` + scrittura. `render_ms` nel log è solo 364 ms, quindi circa 1,1 s del loop non è dentro `render_frame` e non ha un timer.

**Soluzione dettagliata (misura prima, poi taglio).**

*Step 1 — Strumentazione delle sotto-fasi (nessun cambio di comportamento).*
I due timer grossolani nascono in un solo punto:
`apps/chronon3d_cli/utils/job/render_job_finalize.cpp:198-205` emette
`setup_renderer` e `rendering_loop` come voci singole. Il modello di breakdown
esiste già e va esteso, non inventato: `pipe_export_session.hpp:51-54`
(`prepare_breakdown`) e `pipe_timing_sidecar_diagnostics.cpp:285-295` pubblicano
già `prepare_render_ms` nel timing sidecar. Punti di inserimento esatti:
- `render_job_setup.cpp:40` — timer intorno a `create_renderer` → `create_renderer_ms`;
- `render_job_setup.cpp:75-92` — timer intorno a `runtime::prepare_render` →
  `prepare_render_ms`, con sotto-timer per i suoi passi dichiarati
  (`preflight_mode=FullComposition`, `preallocate_framebuffers`, `touch_memory`,
  `render_dummy_frame`) → `preflight_ms`, `prealloc_ms`, `touch_memory_ms`,
  `dummy_frame_ms`;
- `pipe_export_render_loop.cpp` — timer per `prepare_frame` → `prepare_frame_ms`
  (il `render_frame` è già `render_ms`), e per la scrittura/chiusura → `write_ms`;
- ogni voce entra in `phases.push_back({...})` accanto alle due esistenti, così
  appare nei log e nel report senza nuovi formati.

*Step 2 — Esperimento warmup.* Rilanciare gli stessi 6 job del 2026-10-25 con
`warmup_renderer` e `warmup_dummy_frame` on/off: la differenza attribuisce il
costo del warmup senza congetture.

*Step 3 — Decisione dati alla mano.* Se il costo è fisso per job (atteso: sta
dentro `prepare_render`), la leva è l'ammortizzazione nel daemon (P2) — il codice
del CLI non va ottimizzato. Se resta per frame, si apre un'indagine dedicata nel
loop con lo stesso metodo.

**Criterio di accettazione:** (a) i sotto-timer sommano ≥ 90% di
`setup_renderer + rendering_loop`; (b) l'`Hot attribution` del report — che oggi
somma solo clear/copy/blend/transform/convert/pipe su base `n_exec`
(`render_job_report.cpp:191-193,253-258`) e copre lo 0% — sale a ≥ 80% o viene
ribasato sui totali di fase misurati. Nessun golden a rischio: la strumentazione
non tocca i calcoli.

### P2 — Nessun riuso cache tra job nella run CLI

**Prova.** In tutte e 6 le run: `cache_hits: 0`, `cache_misses: 0`, `node_cache_hits: 0`, `image_cache_misses: 0`, `gpu_asset_cache_hits: 1`. Ogni processo CLI è freddo. `reset_frame_value_cache` e `reset_temporal_history` sono chiamati ad ogni job anche nel daemon.

**Soluzione dettagliata.** Non è un bug del renderer: è il costo del processo
one-shot. Il percorso reale esiste già e va solo attivato:

1. **Il daemon è già nel CLI**: `apps/chronon3d_cli/commands/daemon/command_daemon.cpp`
   (sottocomando) e `apps/chronon3d_cli/daemon/daemon_service.cpp:263` (il
   servizio chiama `create_renderer` una volta e mantiene il renderer warm).
   Piano di riferimento: `TICKET-DAEMON-JOB-LIFECYCLE` (scheda chiusa e rimossa il 2026-10-27; storico in git).
2. **Esperimento di conferma (una run, nessuna nuova feature):** avviare il
   daemon, sottoporre lo stesso job `animated_image_residency_*` due volte via
   IPC (`src/ipc/ipc_command_dispatcher.hpp:56`, `render_frame_timed`),
   confrontare `setup_renderer` della 1ª e 2ª esecuzione:
   - 2ª ≈ 0 → la leva è l'adozione del daemon (inclusi i tool: C-9/I-16);
     il CLI one-shot resta per debug. Non ottimizzare il CLI.
   - 2ª ancora ≈ 1,3 s → il costo NON è cold start ma lavoro per-job inutile:
     allora aprire `reset_frame_value_cache` e `reset_temporal_history` (chiamati
     ad ogni job anche nel daemon: sono semantici per composizioni diverse, ma
     potrebbero essere chiave per la stessa composizione) e ispezionare
     `runtime::prepare_render` per ciò che rifà da capo ad ogni job, preflight
     `FullComposition` compreso.
3. Finché P2 non è chiuso, ogni stima su job ripetuti resta vaga: è la misura che
   sblocca la lettura di tutte le altre.

**Criterio di accettazione:** `setup_renderer` della 2ª esecuzione via daemon
misurato e riportato qui; `cache_hits`/`node_cache_hits` > 0 sulla 2ª esecuzione
della stessa composizione.

**Esperimento eseguito (2026-10-27) — criterio soddisfatto.** Metodo: daemon
`chronon3d_cli daemon -s … --backend software`, stesso piano
(`tools/fixtures/vram-calibration-1080p-overlay.plan.json`) sottomesso **due
volte** via IPC `RENDER_JOB` (frame 1-2, `report: true`), client wire-level sul
protocollo `CHN3` documentato in `chronon_ipc.hpp`.

| Metrica (report per-job) | 1ª esecuzione | 2ª esecuzione |
|---|---:|---:|
| `setup_renderer` | 1,0 ms | **49,2 ms** |
| `rendering_loop` | 3346 ms | 2680 ms |
| `cache_hits` / `node_cache_hits` | 0 / 0 | **1 / 1** |
| `cache_misses` / `node_cache_misses` | 1 / 1 | **0 / 0** |

Lettura: (1) la leva è l'adozione del daemon — `setup_renderer` via daemon è
1,0–49 ms contro i 1034–1369 ms del CLI one-shot della famiglia del 2026-10-25
(~25×); il CLI one-shot resta per debug, **non va ottimizzato**. (2) Il blocco
del riuso cache era `reset_frame_value_cache()` (`NodeCache::clear()`) chiamato
ad ogni setup job anche nel daemon: era igiene, non semantica — le chiavi di
`NodeCache` sono content-addressed e portano l'`EvaluationVersion`, quindi i
valori esatti di un job precedente che rende gli stessi contenuti restano
validi. Fix atterrato: `setup_render_job` ritrae i valori valutati quando
l'identità del job (digest del programma compilato, fallback `comp_id`)
corrisponde a quella che ha popolato la cache, e li azzera come prima su
identità diversa (gli scope dei nodi sono univoci solo dentro una
composizione). `reset_temporal_history()` resta incondizionato: ogni job è una
nuova sessione temporale e il frame zero non deve vedere il feedback del job
precedente. Stessa guardia applicata alla lane video pipe
(`pipe_export_pipeline_setup.cpp`, identità = fingerprint della composizione).
Dopo il fix: 2ª esecuzione della stessa composizione con
`cache_hits = node_cache_hits = 1 > 0` e `node_cache_misses = 0`, come da
criterio.

### P3 — Readback GPU→CPU di 33 MB per frame

**Prova.** `bytes_readback: 33177600` = 1920 × 1080 × 16 byte (RGBA float32). `gpu_readback: 23,1 ms`, cioè 0,8% del wall. Sul singolo frame è piccolo, ma per un job di N frame è 23 ms × N.

**Soluzione dettagliata.** Il punto di readback è unico e già contabilizzato:
`backends/vulkan/vulkan_surface_store_private.cpp:481-510` (stats
`readback_bytes` + i contatori Gate 7 `cpu_pixel_readback_bytes` di
`docs/ZERO_COPY_GATES.md`) su un buffer VMA di staging
(`vulkan_backend_resources_private.cpp:116-117`). Due percorsi distinti:

1. **Output video — non leggere affatto.** L'encode deve partire dalla superficie
   nativa: è esattamente ciò che i Gate 1/2/9 di `ZERO_COPY_GATES.md` impongono
   (`gpu_upload_bytes = 0`, `gpu_readback_bytes = 0`). Verificare quale percorso
   è attivo di default leggendo `gpu_readback_bytes` dal timing sidecar di un job
   video: se è > 0, il job non sta girando nel lane zero-copy e la correzione è
   di percorso/configurazione, non di formato pixel.
2. **Output still/8-bit — convertire sul device prima del transfer.** Il readback
   attuale porta RGBA float32 (33 177 600 B = 1920×1080×16). Aggiungere un passo
   GPU `RGBA32F→RGBA8` immediatamente prima del transfer: 8 294 400 B, 4× meno
   banda PCIe e 4× meno staging. La conversione finale è la stessa quantizzazione
   che oggi avviene dopo: `Color::linear_to_srgb8` (`math/color.hpp`, LUT
   `linear_to_srgb8_lut`) va replicata esattamente sul device per non cambiare i
   valori finali.
3. **Vincolo di parità:** (a) golden di confronto sullo still a 8-bit,
   (b) scelta esplicita sul rounding (la LUT è la reference), (c) contatore
   `gpu_readback_bytes` prima/dopo nel sidecar.

Resta fermo l'ordine del ticket: 23 ms è 0,8% del wall su job a 1 frame — non è
la priorità finché P1 non è chiuso. Per output lunghi (N frame) il costo è
23 ms × N e diventa rilevante.

**Verifica eseguita (2026-10-27) — percorso attivo misurato.** Metodo: binario
da `build/chronon/linux-video-test` (preset con `CHRONON3D_ENABLE_VULKAN=ON` +
`CHRONON3D_ENABLE_VIDEO=ON`; il preset `linux-fast-dev` ha Vulkan OFF e non può
vedere questo path). Stessa famiglia del 2026-10-25
(`tests/fixtures/animated_image_residency/static.plan.json`, still PNG,
frame 75):

| Contatore (report) | Misura oggi | Baseline 2026-10-25 |
|---|---:|---:|
| lane risolta | `vulkan_native` | `vulkan_native` (stessa famiglia) |
| `gpu_upload_bytes` | 33 177 600 | 33 177 600 |
| `gpu_readback_bytes` | **33 177 600** | 33 177 600 |
| `gpu_readback` | 12,47 ms | 12,76–31,57 ms |

Conferma: per still/sequence sulla lane Vulkan il readback float32 completo
(16 B/px) è **ancora attivo di default** — il punto 2 della soluzione (convertire
`RGBA32F→RGBA8` sul device prima del transfer: 8 294 400 B, 4× meno banda) resta
il fix giusto. Sul job video di default (`tools/fixtures/vram-calibration-1080p-video.plan.json`)
il timing sidecar (`*.timing.json`) riporta `cpu_pixel_readback_bytes: 0` e
`gpu_readback_bytes: 0` ma solo perché il feed dell'encoder cade su host-frame
(`encoder feed capability: native_surface=false`, lane software): il lane
zero-copy nativo di cui ai Gate 1/2/9 di `ZERO_COPY_GATES.md` **non è attivo di
default**, quindi il punto 1 (non leggere affatto) è una correzione di
percorso/configurazione ancora aperta.

**Non implementato in questo lotto:** la conversione device `RGBA32F→RGBA8`
(punto 2) resta bloccata dai suoi vincoli di parità dichiarati — (a) golden di
confronto sullo still a 8-bit, (b) scelta esplicita sul rounding con la LUT
`linear_to_srgb8_lut` come reference, (c) contatore `gpu_readback_bytes`
prima/dopo — che §4 elenca come prerequisiti, non come dettagli. Nessun verde
dichiarato su lavoro non fatto.

### P4 — Un solo worker TBB su 24 core

**Prova.** `tbb_arena_max_concurrency: 24`, `tbb_active_workers_peak: 1`, `Node parallelism WEAK`, `Graph parallelism MODERATE (1 parallel / 1 sequential levels)`. Il log consiglia di alzare le soglie di parallelismo.

**Soluzione dettagliata.** Prima una precisazione decisiva: le "soglie" che il
log consiglia di alzare sono **solo diagnostica** —
`apps/chronon3d_cli/utils/job/report/render_job_report.cpp:228-233` calcola
`Node parallelism` confrontando `tbb_active_workers_peak` con 4; non è un
parametro di tuning e "alzarlo" non cambia nulla.

Il grafo di questa composition è piccolo (2 ms di dispatch), quindi finché P1
non dimostra dove sta il tempo, il parallelismo del grafo non sposta il wall di
2,9 s. Quando P1 chiude, i veri levieri sono tre, in ordine:

1. **Parallelismo intra-nodo dove il lavoro c'è davvero.** I loop pixel
   full-frame di C-2/C-3 girano su un solo thread: è lì che `tbb_active_workers_peak`
   resta 1. Il pattern da replicare esiste già nel repo:
   `backends/assets/image_renderer_support_detail.hpp:139` usa
   `tbb::parallel_for(tbb::blocked_range<int>(0, height, 16), ...)` per righe
   disgiunte. Portare lo stesso schema ai loop di composito/blend (C-2) fa
   salire il peak dove oggi c'è solo un worker.
2. **Tile incrementale (I-1).** Se i tile sporchi sono pochi, il lavoro è poco e
   il parallelismo conta meno: misurare prima la quota (§4.3).
3. **Livelli del DAG.** `Graph parallelism` dipende da `lev_par_c` vs `lev_seq_c`
   (`render_job_report.cpp:218-226`), generati in
   `src/render_graph/compiler/frame_graph_builder_levels_detail.hpp`. Con 1 solo
   livello parallelo, verificare se nodi indipendenti finiscono in livelli
   distinti per dipendenze dichiarate troppo larghe: è un bug di costruzione del
   grafo, non un limite hardware.

**Criterio di accettazione:** sulla stessa job family, `tbb_active_workers_peak ≥ 4`
e `CPU saturation` GOOD (≥ 65% dei 24 core, `render_job_report.cpp:197-203`),
con riduzione di wall misurata. Vincolo: output bit-identico (righe disgiunte
non alterano i calcoli).

**Esito (2026-10-27, implementato e misurato — RelWithDebInfo, 6 log × 2 run
identificati da `tbb_active_workers_peak`):**

| Metrica (mediana su 6) | Pre | Post | Δ vs pre-P4 | Δ vs baseline 2026-10-25 |
|---|---:|---:|---:|---:|
| Wall ms | 2021 (1995–2102) | **1536 (1474–1637)** | **−24%** | −33…−55% |
| `setup_prepare_render` ms | 481 (468–495) | **349 (312–362)** | −28% (EVP sha256) | — |
| `loop_graph_eval` ms | 282 (243–302) | 242 (238–302) | −14% (shuffle noise) | — |
| `loop_other` ms | 1237 (1227–1352) | **925 (903–1017)** | **−312 ms** | — |
| `tbb_active_workers_peak` | 0–1 | **17–18** | **≥4 SATISFIED** | — |
| CPU totale (user+sys) ms | ~2400 | ~1980 (−18%) | — | — |

- **Output bit-identico: SATISFIED.** sha256 dei 6 PNG pre vs post: 6/6 IDENTICI
  (`.tmp/perf-paradigms/rel-pre/` vs `rel-post/`, stesso RelWithDebInfo).
- **Test (Debug):** 12 suite mirate verdi (backends_software, cache, effects,
  animation+helpers, timeline, render_job_contract, tbb_workers, compositor,
  memory, io, cli, daemon_render_concurrency). 3 fallimenti residui, tutti
  ambientali e su path non toccati dal lotto: `test_text_shaping_corpus`
  (copertura glifi corpus), `test_text_warm_cold` (roundtrip MP4 via CLI+ffmpeg),
  `test_effect_gpu_capability` (censimento capability Vulkan). Nessun fallimento
  in codice toccato da P4.
- **Lezione di build:** gli include TBB non possono stare nei fragment header
  inclusi *dentro* un `namespace` (errori di compilazione in tutti i TU ospiti);
  le include `tbb/parallel_for.h` vanno nel TU ospite prima del namespace
  (`image_writer.cpp`, `image_renderer.cpp`).
- **Lezione di attribuzione:** `parallel_for_tracked` ha portato il peak a 17–18
  worker ma i contatori `used_parallel_blend/transform/composite` del report
  (SoftwareCompositor) restano 0, quindi l'etichetta `Node parallelism` resta
  WEAK pur con 18 worker: la soglia `render_job_report.cpp:228-233` legge il
  campo sbagliato. Da correggere lì, non nel renderer.

**CPU saturation: criterio ≥65% NO SATISFIED — strutturalmente irraggiungibile su
questa family.** Tre dati lo provano:
1. CPU totale per job ≈ 1,98 core-s (misurato: user+sys ~1980 ms su 24 core); per
   un saturation 65% servono ≥15,6 core effettivi per il wall → wall ≤ **127 ms**.
2. Il floor seriale del job è ≈ 750–900 ms (deflate PNG ~860 ms seriale in
   `stbi_write_png` + setup ≥ 312 ms): il wall reale è 1536 ms → saturation
   reale **5,0–5,4%**, sia pre che post.
3. Anche idealizzando (deflate parallelo: 860 → ~35 ms wall), il wall scende a
   ~710 ms e il tetto di saturation resta **~12%** a lavoro totale invariato.

Il criterio 65% è significativo solo per render sostenuti warm multi-frame (il
caso video reale), non per job one-shot da 1 frame. Per questa family il gate è
`tbb_active_workers_peak ≥ 4` + wall misurato — entrambi rispettati.

**Prossima leva (già delimitata da questi dati):** `loop_other` residuo 903–1017
ms = deflate PNG seriale (~860 ms giù misurato) + IO. Quando serve un altro
di wall: compressione parallela (chunked deflate, stile pigz) o handoff di
scrittura al thread di encode già esistente (`loop_encode_handoff`).

### Limiti di questa analisi

- Ogni job è 1 frame: il dato misura il costo fisso del job, non il throughput per frame. Non si può estrapolare a video lunghi senza una run multi-frame.
- Una run per composition (6 run × 2 condition il 2026-10-27): varianza stimata solo sulle 6.
- ~~La causa dei 1,1 s fuori da `render_frame` non è identificata.~~ **Chiuso 2026-10-27:** `loop_other` = conversione float→RGBA8 full-frame seriale (~440 ms) + deflate PNG seriale (~860 ms) + IO. La conversione è ora parallela (righe disgiunte, bit-identica); il deflate resta il floor seriale.

## 6b. Stato del piano "1000 overlay/min" (aggiornato 2026-10-27)

**Obiettivo:** 60 ms per overlay sulla RTX A4000 (da confermare: nel repo non esiste un obiettivo di overlay al minuto). Dettagli e blocchi in `TICKET-DAEMON-JOB-LIFECYCLE` (scheda rimossa; storico in git), sezione "obiettivo 1000 overlay/min".

| Passo | Stato | Note |
|---|---|---|
| Verificare binari e GPU | FATTO | CLI presente e funzionante; GPU libera (4 MiB su 16 GB) |
| Fase 0: misura 200 overlay via `RENDER_BATCH` | DA FARE | serve scegliere il template di riferimento |
| Leggere percorso rettangolo (tight) e PNG con alpha | DA FARE | `tight_surface` non trovato in `include/` né `src/` |
| ADR per cache di template | DA FARE | obbligatorio per `AGENTS.md` riga 80 |
| Implementazione (rettangolo, binding, PNG senza encoder) | BLOCCATA | attende la Fase 0 e l'autorizzazione al core |
| Test di parità pixel | BLOCCATA | dipende dall'implementazione |

**Collegamenti al resto del ticket:** il "rettangolo tight" è la stessa leva di
C-2/I-10 (clip alla bbox utile); il readback 33 MB → <2 MB è P3/I-18 più un
formato di uscita più piccolo; il binding di template che evita la
ricompilazione è I-5/C-9; il costo per-overlay si attribuisce con il breakdown
di P1.

**Non ancora misurato (ipotesi):** readback 33 MB → <2 MB con rettangolo 800×150; 3 daemon sufficienti per 1000/min; il binding di un template evita la ricompilazione del grafo.

## 6. Stato

Dettaglio problema→soluzione: §3 (C-1…C-9, evidenza da codice) e §5b (P1…P4,
misurati il 2026-10-25). Le idee I-17…I-20 sono il round 2 (§2b).
Fix già atterrati: vedi §7.

| Idea | Stato |
|---|---|
| I-1 Tile incrementale | IPOTESI, da misurare |
| I-2 Banda pixel | IPOTESI, rischio numerico (aggiunto pavimento roofline) |
| I-3 Blur a colonne | IPOTESI, verifica bit-identica |
| I-4 Zero-init | IPOTESI, da elencare call site |
| I-5 Riuso sessione | IPOTESI, richiede estensione cache esistente |
| I-6 Encode in parallelo | DA VERIFICARE nel codice |
| I-7 SoA | CONDIZIONALE |
| I-8 Zero-copy video | GIÀ IN CORSO (riferimento) |
| I-9 Backdrop blur riscrittura | EVIDENZA DA CODICE (analisi 2026-10-27), priorità alta |
| I-10 Compositing clippato a bbox | EVIDENZA DA CODICE, da misurare sul corpus |
| I-11 Rasterizer mesh 3D | EVIDENZA DA CODICE, parità golden da definire |
| I-12 Distort → compute shader | EVIDENZA DA CODICE, solo lane Vulkan |
| I-13 Dispatch fuori loop pixel | IMPLEMENTATO 2026-10-27 (C-5), vedi §7 |
| I-14 Maschere 1 canale | EVIDENZA DA CODICE, da verificare lettori multi-canale |
| I-15 Scratch fuori pool | PARZIALE: C-6 implementato (§7); C-7 (pack upload) ancora da fare |
| I-16 Daemon per tool batch | EVIDENZA DA CODICE, area adiacente (tooling) |
| P1 Setup + loop fuori dal grafo: 1,0–1,4 s setup e 1,3–2,3 s loop su ~2,4–3,6 s di wall | MISURATO 2026-10-27: `loop_other` decomposto (convert ~440 seriale→ora parallelo, deflate ~860 reso floor); le 6 job di pomeriggio misurate su §5b P4 |
| P2 Nessun riuso cache tra job CLI (`cache_hits: 0`) | MISURATO oggi; verificare via daemon warm |
| P3 Readback 33 MB/frame, 23 ms | MISURATO oggi; ridurre a RGBA8 solo dopo golden |
| P4 TBB 1 worker su 24 core | **IMPLEMENTATO E MISURATO 2026-10-27**: peak 0–1 → **17–18**, wall −24% (median 2021→1536 ms), output bit-identico 6/6 sha256; `CPU saturation ≥65%` strutturalmente irraggiungibile su job 1-frame (bound: wall ≤127 ms vs floor seriale ≥750 ms), vedi esito in §5b/P4 |
| I-21/C-10 Espressioni riparse ogni frame | DA FARE: richiede compilazione AST (Path B), vedi §7 |
| I-22/C-11 Ricerca keyframe lineare | IMPLEMENTATO 2026-10-27, vedi §7 |
| I-23/C-12 Stroke O(radius) + gather colonne | IMPLEMENTATO 2026-10-27 (VHGW), vedi §7 |
| I-24/C-13 Downscale immagini double scalare | IMPLEMENTATO 2026-10-27 (branch hoist), vedi §7 |

---

## 7. Fix implementati (2026-10-27 — primo lotto)

Ordine di priorità (dal più importante al più facile). Tutte le patch sono
**bit-identiche** per analisi (stesse operazioni, stesso ordine aritmetico) tranne
dove notato, e **cancellano** il codice che sostituiscono: nessun fallback, nessun
"nuovo sistema + vecchio sistema" (Demolition Debt: ingresso = nessuno, uscita =
il codice rimosso).

| Fix | Cosa cambia | File toccati |
|---|---|---|
| **P1** | Sotto-fasi esplicite nei phase telemetry: `setup_create_renderer`, `setup_prepare_render`, `setup_finalize` (dai boundary già misurati in `setup_render_job`) e `loop_prepare_frame`, `loop_graph_eval`, `loop_encode_handoff`, `loop_other` (aggregati dai bucket per-frame che `FrameTelemetry` già registra). `loop_other` è il residuo reale, nominato onestamente | `render_job_setup.hpp/cpp`, `render_job_finalize.cpp` |
| **C-12** | Morfologia stroke con van Herk–Gil–Werman (O(1)/pixel indipendente da radius, contro O(2r+1) della finestra naive) + passata verticale su tile da 4 colonne (1 cache line serve 4 valori invece di 1). Cancellati `dilate_row_horizontal`/`erode_row_horizontal` (incluso il `len` morto) e la coppia duplicata `dilate_alpha`/`erode_alpha`; semantica di bordo legacy preservata esattamente (dilate = `max(0, finestra)`, erode = `min(0, finestra)` al bordo). Campi scratch rinominati `morph_tile_in/morph_tile_out/morph_work`, `col_dst` eliminato | `stroke.cpp`, `software_session_resources.hpp` |
| **C-11** | `evaluate_base_double`: `std::upper_bound` + passo indietro al posto dello scan lineare del segmento (O(log n) vs O(n) per valutazione). Cancellato il loop; la semantica sui frame duplicati è dichiarata uguale dal commento originale ("matches upper_bound behavior"). Il cursore monotono proposto nel ticket è stato scartato: sarebbe stato stato mutabile condiviso (cache senza ADR) | `animated_value_evaluation.inl` |
| **C-6** | `apply_roughen_edges`: pack/unpack del frame con `memcpy` per riga (`Color` è già 4×f32 contigui) al posto del deinterleave scalare, su `rgba_scratch` persistente nello scratch (zero allocazioni steady-state); firma allineata ai sibling (`EffectScratchResources&`). Cancellati i due loop scalari e la `std::vector` per chiamata | `warp.hpp/cpp`, `software_effect_processors_motion.cpp`, `software_session_resources.hpp` |
| **C-5** | `ColorPipeline::apply`: dispatch della variant risolto **una volta** per apply (thunk per tipo di stage), cancellata la catena di 6 `std::get_if` dal loop pixel (invariante di loop che bloccava la vettorizzazione). `std::monostate` → kernel nullo → stage saltata, come prima | `color_pipeline.cpp` |
| **C-13** | Downscale `ImageCache`: kernel generico per tipo di buffer (u8/f32) selezionato una volta per chiamata, branch invariante `buffer.pixels ? … : …` eliminato da campione e da canale. Aritmetica invariata (accumulo `double`, stesso ordine) | `image_cache.cpp` |
| **P4 (2° lotto, pomeriggio)** | Parallelismo intra-nodo su righe disgiunte via `parallel_for_tracked` (telemetry onesta) invece di `tbb::parallel_for` grezzo: conversione float→RGBA8 del **write PNG** (parzializzata rows, bit-identica), loop blend/draw dei bridge blend2d, loop tint/crop di `draw_image`, loop C-2 di `multi_source_node` (gruppi, maschere, composito). In più: `sha256_string` via EVP (hardware-sha256: su `ImageCache` 33 MB risparmia ~140 ms di job one-shot), include TBB nei TU ospite (non nei fragment inclusi in `namespace`). Esito misurato §5b/P4: peak 0–1→**17–18**, wall −24%, PNG 6/6 bit-identici | `prepared_asset_digest.cpp`, `image_writer_png_detail.hpp`, `image_writer.cpp`, `blend2d_bridge_core.cpp`, `blend2d_bridge_transforms_fb.cpp`, `image_renderer_draw_detail.hpp`, `image_renderer.cpp`, `multi_source_node.cpp` |

**Non implementato in questo lotto:**
- **C-10** (espressioni riparse ogni frame): il fix vero richiede la compilazione
  in AST/bytecode (Path B già descritto in `animated_value.hpp`); la variante
  "vars senza heap" sposta poco (il costo dominante è il re-parse) e introdurrebbe
  stato mutabile condiviso. È il prossimo intervento architetturale.
- **C-1…C-4, C-7…C-9**: richiedono golden/decisioni (C-1 cambia i numeri del
  blur, C-2 va misurata sul corpus, C-3 è un riscrittura del rasterizer) o
  toccano il lane Vulkan/tooling.

**Verifica (esito reale, 2026-10-27 pomeriggio):** compilazione pulita in
RelWithDebInfo (`linux-fast-dev-release`) e Debug (`linux-fast-dev`). Suite
di test mirate Debug: **verdi** backends_software, cache, effects,
animation+helpers, timeline, render_job_contract, tbb_workers, compositor,
memory, io, cli, daemon_render_concurrency, steady_state_allocation_gate,
common_performance_gate. **3 fallimenti residui, tutti ambientali e su path non
toccati dai lotti: `test_text_shaping_corpus` (copertura glifi corpus),
`test_text_warm_cold` (roundtrip MP4 reale via CLI+ffmpeg),
`test_effect_gpu_capability` (censimento capability Vulkan =48). Oltre alle
suite: output del job family **bit-identico** per sha256 (6/6 PNG, §5b/P4).
Nessun verde senza test verdi: lotto chiuso.
