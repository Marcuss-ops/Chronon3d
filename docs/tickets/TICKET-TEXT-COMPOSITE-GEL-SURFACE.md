# TICKET-TEXT-COMPOSITE-GEL-SURFACE — superficie 2.5D composta per testo glossy

## Stato

**OPEN — gap architetturale e parità visiva non chiusi.** Aggiornamento
2026-11-06: la suite diagnostica Vulkan 1280×720 ha T00–T08 PASS sui gate
tecnici misurati; T00–T04 ora espongono layout e diagnostiche intermedie. P1
ha PASS limitati per T09, T13 e T15; T10 FAIL di leggibilità al tracking estremo −30 px (coverage continua), T11 PASS di continuità dopo fix della rotazione affine, T12 PASS tecnico sulla sweep della luce animata (review ottica pending). T14 è PASS limitato: il compositing destination-aware applica trasmittanza Beer–Lambert RGB per canale al backdrop, oltre all’assorbimento RGB già presente nel corpo shader. I render raw strict Vulkan ON/OFF passano ora tramite readback esplicito dal surface GPU; la trasmittanza usa alpha come proxy di copertura/spessore, non un attachment di optical depth. `GEL-VISUAL` resta in attesa della review del
reference; `GEL-PERF` non passa ancora: il gate 4K/120fps non è stato eseguito; la diagnostica T16 DoF-on a 720p misura p95 GPU 2,1827 ms, oltre il budget 1,5 ms, e p95 render-thread CPU 275,16 ms. La prova
storica 2,345 ms GPU span / 52,446 ms/frame end-to-end non appartiene a questo
run.

## Obiettivo

Ottenere per il titolo “calories of ingredients” una resa 2.5D gonfia e lucida,
con faccia luminosa, bordi blu/ciano, ombra di contatto, glow e frange ottiche,
vicina al [riferimento fornito](https://drive.google.com/file/d/1Tr3N3MPSoFrC6-x_-hLLTBwQv6KAfGrD/view?usp=drive_link).
Il confronto è di resa ottica, non di mesh o simulazione fisica del gel.

## Diagnosi architetturale

La bombatura attuale viene valutata nel ciclo per-glifo di
`src/backends/vulkan/shaders/text_tile_raster.comp`: per ogni glifo si campiona
il suo atlas MTSDF, si ricostruiscono altezza/normale e si applica lo shading,
poi si compone il fill. In questo modo ogni glifo porta la propria superficie;
il renderer non ha un campo di distanza della sagoma composta del TextRun da
cui ricavare una cresta coerente sulle aste e sulle zone di contatto.

After Effects offre un riferimento concettuale diverso: gli stili di livello
operano sulla sagoma/alpha del livello e poi combinano rilievo, ombre, glow e
compositing. Per Chronon manca quindi una superficie condivisa fra il TextRun e
i suoi effetti, non un altro insieme di parametri per il solo riflesso. Il
percorso GPU deve conservare shaping, layout, atlas e cache esistenti; la nuova
superficie è una modalità del materiale gel, non un secondo renderer tipografico.

La pipeline attuale ha già effetti di layer dopo il rendering del layer nel
graph builder. Il contratto va mantenuto: il pass gel produce un layer RGBA
completo e il glow deve aggiungere l’alone lasciando intatto il testo sorgente.
Un probe locale con `style.glow` ha fatto sparire il titolo; quel difetto va
riprodotto in una fixture persistente e isolato prima di dichiarare glow valido.

## Lavoro richiesto

- [ ] Costruire, solo per il materiale gel, una maschera composta del TextRun
  nello spazio di output e un campo di distanza con banda interna sufficiente
  alla bombatura. Il campo deve rappresentare l’unione dei glifi senza perdere
  i contatori (`e`, `g`, `o`, `a`) o gli spazi fra lettere.
- [ ] Ricavare altezza e normali dal campo composto con profilo limitato e
  stabile vicino al bordo e all’asse mediano. Evitare cuspidi, normali non finite
  e highlight generati esclusivamente dal contorno.
- [ ] Applicare shading ciano glossy sul campo composto: cresta ampia sulla
  faccia, specularità controllata, terminatore morbido, rim e ombra di contatto.
  Mantenere una variante di unione morbida configurabile per glifi che si
  sovrappongono; non fondere gli spazi tipografici per default.
- [ ] Far consumare la stessa maschera agli effetti del layer. Verificare source
  over glow, alpha premoltiplicato e clipping sul pass Vulkan; il glow non deve
  sostituire né attenuare il testo.
- [ ] Completare e certificare trasmissione stilizzata dipendente dallo
  spessore e dispersione RGB limitata alla silhouette e alle alte luci. La
  trasmittanza RGB destination-aware è implementata sul compositing GEL con
  alpha come proxy per spessore/copertura; la dispersione e il match ottico
  finale restano aperti. Dichiarare esplicitamente le approssimazioni: non
  chiamare il risultato simulazione ottica fisica.
- [ ] Conservare un solo contratto d’authoring in Chronon3D e ChrononMotion3D,
  inclusi RenderPlan, validazione, fingerprint, camera 2.5D e preview. Nessun
  atlas/font pipeline duplicato.

## Gate di accettazione

Prima prova visiva a 1280×720 contro il riferimento; 4K viene dopo il gate
visivo. Salvare piani, raw, PNG, log GPU e confronto nella directory
`docs/evidence/gel-text-vulkan-smoke-20261105/`.

1. **Topologia:** `%&@$# æøå gggQQQ 88888888 ||||||||`; contatori leggibili,
   nessun picco o normale invertito quando aste e occhielli diventano sottili.
2. **Kerning/fusione:** “calories or ingredients”, tracking da +50 a −30 px e
   ritorno in 61 frame deterministici; niente cuciture a lama o flicker. Le
   lettere si fondono solo quando la modalità di fusione è abilitata.
3. **Serif e subpixel:** font arrotondato e serif ad alto contrasto, zoom
   0,05×→10×→0,05× e rotazione Z fino a 15°; nessuno scintillio visibile o
   variazione temporale instabile.
4. **Luce radente:** sweep luce 360° a elevazione bassa e alta; la cresta scorre
   senza discontinuità e il terminatore non crea tagli neri.
5. **Trasmissione:** scacchiera ad alto contrasto dietro al testo; attenuazione
   visibile dipendente dallo spessore, compositing alpha corretto e nessun
   semplice adesivo opaco.
6. **4K/120:** solo dopo il pass visivo; GPU timestamp sull’intera pipeline
   testo/post, 4K UHD e 120 frame/s effettivamente presentati. Obiettivo ≤1,5 ms
   GPU per post del testo sulla macchina target documentata; misurare a parte
   conversione/readback e non usarli come prova di frame rate end-to-end.

In ogni gate registrare esito PASS/FAIL/NOT RUN, macchina, backend, comando,
hash raw e frame ispezionati. I test unitari dello shader non sostituiscono la
review del risultato ottico.

## Evidenza disponibile e limiti

La prima evidenza 720p e i limiti già osservati sono registrati in
[`docs/evidence/gel-text-vulkan-smoke-20261105/README.md`](../evidence/gel-text-vulkan-smoke-20261105/README.md):
highlight facettati, fusione kerning fallita e assenza di pass gel dedicati per
bloom/dispersione. Il probe corrente conferma inoltre che il contorno per-glifo
non basta a raggiungere la resa richiesta. Il risultato non è accettato come
match visivo.

Checkpoint unitario 2026-11-05: il binario corrente
`build/chronon/linux-video-test/tests/chronon3d_renderer_core_tests`, filtrato
con `--test-case='*gel*'`, passa **6/6 casi e 736/736 assertion**. Copre il
profilo superficie numerico, normali finite, unione SDF matematica, packing
della luce, roughness, terminatore e trasmissione Beer–Lambert degli helper.
Non esercita la maschera composta del TextRun, il compositing glow, la resa
ottica integrata, la camera/depth né i gate performance; non modifica quindi
l’esito dei gate visivi sopra.

Checkpoint di integrazione 2026-11-05: ricompilati con `-j2` i target cache,
renderer core e `chronon3d_text_tile_raster_canary_tests`. L’intera suite
renderer core passa **215/215 casi e 73.350/73.350 assertion**; il canary
Vulkan TextRun passa CTest **1/1**. Questi risultati verificano contratti
funzionali e shader/canary, ma non superano il FAIL visivo documentato sotto.
Il target Motion `chrononmotion_authoring` è stato poi ricompilato con
`-j1` e CTest passa **1/1** sullo stesso checkout; copre l’emissione e la
validazione dei parametri gel, non la resa runtime camera/depth.
Anche `chronon3d_c_abi_tests` è stato ricompilato; il filtro `*gel*` passa
**2/2 casi e 17/17 assertion** per decoder e fingerprint RenderPlan. Questo
filtro non è una certificazione dell’intera suite C ABI.

Aggiornamento diagnostico 2026-11-05, dopo rebuild del CLI `linux-video-test`:
il frame Vulkan `gel-01-core-bloom-20261105/gel-01-diagnostic.png` è stato
rigenerato e ispezionato. La riproduzione alle 15:24 UTC è byte-identica al raw
archiviato e conferma tutte e quattro le righe, inclusa la baseline bianca;
la precedente nota che la riga mancasse era errata e viene ritirata. CORE e
GLOSSY mostrano testo ciano leggibile ma piatto, senza bombatura o cresta
speculare leggibile; BLOOM aggiunge alone senza cambiare la superficie. Esito
visuale: **FAIL — volume gel non raggiunto**. Il raw RGBA 1280×720 (1 frame) ha
SHA-256
`7182e60794ec1dea5203459a48a7af1babca3f8169c96d1183e4f145b1bacc06`.
Il sidecar riprodotto è conservato come
[`gel-01-diagnostic-repro-1524.timing.json`](../evidence/gel-01-core-bloom-20261105/gel-01-diagnostic-repro-1524.timing.json)
e registra GPU span 2,345 ms e frame end-to-end 52,446 ms. Il primo
supera l’obiettivo 1,5 ms; il secondo include conversione/readback e non è una
misura di presentazione 120 fps. Restano non passati il confronto con il
riferimento e i gate di topologia, kerning, subpixel, luce radente e
trasmissione.

## Dipendenze e confini

- Riutilizzare `TextRun`, atlas MTSDF, `EffectCatalog`,
  `RenderSurfaceRegistry` e render graph canonici.
- Coordinare l’authoring con ChrononMotion3D; implementazione GPU e gate visivi
  appartengono a Chronon3D.
- Mantenere separati i ticket shadow/spotlight: questa scheda non li chiude né
  sostituisce la loro certificazione.
- Valutare un generatore SDF o Jump Flood solo se il campo composto non si può
  ottenere correttamente dal percorso esistente; nessuna dipendenza esterna è
  presupposta da questa scheda.

### Coerenza del profilo GPU — 2026-11-05

Confrontando l'oracolo `text::gel::rounded_height()` con
`shade_composite_gel_surface()`, il compute shader usava una miscela di profili
non presente nell'oracolo e una pendenza differente. Il compute ora usa
`h/r = u(2-u)` e `dh/du = 2(1-u)`, come il profilo C++ e il suo test numerico.
Il compile/validate/optimize dello shader Vulkan è passato e la canary GPU
`chronon3d_text_tile_raster_canary_tests` passa **1/1 caso, 105/105 assertion**.
Questa canary copre il percorso tile MTSDF, ma non è una review ottica gel. Il
render CLI sul nuovo shader è stato rifatto dopo il completamento della build
esterna autorizzata: il target era aggiornato e il render Vulkan 1280×720 a un
frame è terminato con exit 0. Il PNG risultante conferma ancora una superficie
ciano piatta, senza volume o cresta speculare leggibile; esito ottico FAIL.
Questo run diagnostico non è un confronto con il riferimento né una misura di
stress/performance. Nessun PASS visivo o prestazionale è dedotto da questa
verifica.

### GEL-TEXT stress test P0 — 2026-11-05 18:48 UTC

Checkpoint iniziale (storico, superato dagli aggiornamenti P0/P1 qui sotto):
eseguito il confronto corretto `calories or ingredients` a 1280×720 e un
frame, con stesso font/layout e backend `--backend vulkan`; il sink raw è stato
convertito in PNG con `tools/raw_frame_to_png.py`. T01 coverage, T05 core-only
e T07 glow-on/off passano i rispettivi gate osservati. T00 resta **NOT RUN**
come contratto completo perché il raw video route non ha emesso il dump delle
posizioni glyph. T02–T04 restano **NOT RUN** perché il RenderPlan non espone i
dump SDF/height/normals; T06/T08 e P1/P2 non sono stati eseguiti. Il primo
passaggio difettoso non è quindi localizzato e `GEL-VISUAL`/`GEL-PERF` non sono
PASS. Dati, hash, output e comandi sono in
[`docs/evidence/gel-text-stress-20261105/README.md`](../evidence/gel-text-stress-20261105/README.md).

Il raw T00/T01 è byte-identico. T05 produce una faccia ciano piena con box e
coverage quasi coincidenti col flat. Con il solo glow attivo, il core interno
resta byte-invariato e l'alone si estende fuori dal corpo. Queste verifiche
localizzano il comportamento osservato, ma non certificano il campo composto:
restano necessari i dump intermedi e il confronto visivo con il reference.
Il filtro `chronon3d_renderer_core_tests --test-case='*gel*'` passa 7/7 casi,
742/742 assertion; la canary Vulkan aggiornata passa 105/105 assertion.

### P0 diagnostic harness update — 2026-11-05 19:50 UTC

The stress-test run added RenderPlan `gel_debug_mode` values for composite SDF,
height, normals and shader-side normal validity heat map. The mode participates
in RenderPlan decoding/fingerprinting and uses the same TextRun geometry. T02
now has a raw-derived signed-distance visualization; its positive region agrees
with the T01 composed coverage mask at IoU 0.996591 (82 XOR pixels). T03 shows a
broad filled dome; eroded-core height median is 0.694 and p95 is 0.939. The
8-bit T04 normal visualization cannot alone certify the 0.01 vector-length
tolerance; mode 4 classified all 18,806 pixels in the eroded coverage core as
finite and within `abs(length(N)-1) < 0.01` (zero invalid pixels).

### M1 T02 Euclidean distance oracle — 2026-11-06

`tools/gel_sdf_oracle.py` compares the T02 debug field with an independent
center-sampled signed Euclidean distance transform from the T01 0.5-coverage
mask. The T02 encoding radius is 7.3125 px (`5.2 × 90/64`, unit layer scale).
Across 32,393 samples in the [−3,+4] px band, MAE is 0.230 px, p95 absolute
error 0.458 px, and 98.24% are within 0.5 px; signed-region IoU is 0.996591.
The T04 mode-3 normal map agrees directionally with the negative oracle
gradient over 16,465 core-band samples (mean cosine 0.99595, p05 0.98131,
median angular error 2.22°, p95 11.10°; no opposed samples); mode 4 separately
reports 18,806/18,806 finite, normalized core normals.
The error heatmap and full metrics are in
`docs/evidence/gel-text-stress-20261105/T02-distance-oracle-{error.png,metrics.json}`.
This PASS is limited to the pixel-grid raster sample; vector-contour subpixel
accuracy and zoom/rotation stability remain open, so M1 is not fully certified.

A single-frame T06 render also confirms the lit filled face with specular and no glow; its raw exactly matches T07-off. T08 adds a layer shadow and chromatic aberration; its 720p raw keeps the title legible and preserves core alpha over 8,002 eroded interior pixels. The moving sweep remains P1 T12. The raw-video route now emits per-frame glyph-coordinate JSON from the same TextRun snapshot. T00 reports 23 glyphs; T00–T08 layout hashes, positions and world matrices compare identical. P1 is partially run (see the T09–T15 results below); P2 remains NOT RUN, and the integrated visual result still needs reviewer comparison against the reference. Raw data,
plans, debug PNGs and field metrics are in
[`docs/evidence/gel-text-stress-20261105/`](../evidence/gel-text-stress-20261105/).
The 13.2-second H.264 review proxy, including T04 validity, is uploaded to the requested Drive folder;
it is not a pixel oracle. The `linux-video-test` build completed 350/350 steps with `-j14`. The C ABI gel
filter exposed a stale test expectation that rejected debug mode 4; that test
now rejects out-of-range mode 6 and the C ABI gel filter passes 2/2 cases,
34/34 assertions, inclusi transmission ablation e diagnostica mode 5. The real GPU canary passes 105/105 assertions. The release preset was repaired to resolve
its stale dependency paths by using the populated test vcpkg package tree.


### P1 diagnostic update — T09/T10 (2026-11-05)

Ran topology sample T09 and 61-frame tracking sweep T10 on explicit Vulkan/raw RGBA. T09 (`%&@$# æøå gggQQQ 88888888`) visually retains the tested counters and special glyphs; this is a visual-only PASS without an automated topology oracle. T10 fails at the sampled frame 30 (tracking −30 px): the phrase visibly collapses into overlapping glyphs, and raw-derived coverage above RGB threshold 40 spans only x=189–428 (240 px). See `docs/evidence/gel-text-stress-20261105/T10-030.png`, the 61-frame `T10.rgba`, and its plan/log. The first observed failing sample is frame 30; intermediate frames were not separately reviewed to find onset. The existing JSON layout sidecar captures the base TextRun snapshot, not evaluated animated offsets, so it cannot explain the runtime tracking animation. (This sentence is historical; T11 and T15 have subsequent results below.)


T15 deterministic seek subtest also ran on the same 150-frame animated tracking plan: two full linear renders were byte-identical, and isolated frames 90/0/149/45 in repeated shuffled order matched the corresponding linear raw frames exactly. Results are in `docs/evidence/gel-text-stress-20261105/T15-seek-metrics.json`. Historical note: the T11 failure and T12 unrun status in this initial P1 report are superseded by the 2026-11-06 reruns at the end of this ticket; T13 and T14 have results below.


### P1 T11 subpixel/zoom result — 2026-11-05 (superseded by 2026-11-06 fix)

The 61-frame 0.05×→10×→0.05× scale sweep with Z rotation to +15°/−15° has a measured blank frame at frame 19: zero pixels above RGB>40. Frame 30 returns with 23,519 foreground pixels. This initially appeared as a temporal transform/clipping FAIL; the subsequent affine-transform fix removed the zero-coverage frames, as documented below. Full raw, plan, log, sample PNGs and per-frame coverage metrics are under `docs/evidence/gel-text-stress-20261105/T11*`.


### P1 T13 camera result — 2026-11-05

Rendered 61-frame flat and gel variants with the same 3D-enabled text plane and perspective camera sample path (subtle dolly/orbit and return; near/far/focus fixed). Alpha coverage masks are identical across all 61 frames (zero XOR), so the gel material did not move projected geometry in this sample. See `T13.plan.json`, `T13-gel.plan.json`, raw files and `T13-metrics.json`. The T16 follow-up below now exercises the GEL camera-depth/DoF integration across three Z planes and an interpolated focus sweep; the separate debug-depth/occlusion/bokeh-shape visual suite remains open.


### P1 T14 source-over correction — 2026-11-06 (superseded)

Il fallimento storico del 2026-11-05 è conservato nel manifest come `T14_before_fix`. Il fragment shader ora ricava un alpha del corpo dalla densità Beer-Lambert e lo passa al successivo SourceOver; non legge più il destination alpha nel pass TextRun, che precede il compositing del backdrop. Render Vulkan/raw delle stesse plan ON/OFF: 26.492 pixel diversi nella ROI del titolo `[189,323,1089,409]`, 75.041 canali RGB diversi, delta massimo 106. SHA ON `9f991ac870e050fafde0f9dc6ff12f30f87b07aeb874002602ef5397580b70bb`; OFF invariato `a6f918535553f187cfa11d1e903b557e2a9246cc37550353bce3894ebd858467`. Questo soddisfa il gate di visibilità locale dell’ablation T14.

Limite esplicito: la soluzione è alpha scalare source-over derivato dall’assorbimento, non una trasmissione volumetrica fisica. Evidenze ON/OFF raw e PNG e differenza ×8: `docs/evidence/gel-text-stress-20261105/T14-{on,off}-after.*` e `T14-after-difference-x8.png`. Il rerun T07 conferma 9.854 pixel nel core eroso, alpha invariato su tutti, luminanza lineare media +2,938 (min −0,213, max +11,673) e nessuno shift del core; il bounding box esterno cresce per il glow. Metriche in `T07-after-core-metrics.json`.

### P1 T14 destination-aware RGB transmission and strict raw readback — 2026-11-06

`CompositeNode` now routes only GEL TextRun layers with transmission enabled through a channel-dependent Beer–Lambert backdrop branch. The Vulkan and software compositors preserve source-over alpha; backdrop RGB is attenuated per channel, with optical thickness approximated from source alpha. ON/OFF strict Vulkan raw renders both complete via explicit download from the retained GPU execution-slot surface. Each output is 1280×720 RGBA8 (3,686,400 bytes); ON SHA-256 `d35f357ba94db5aeeff9269c5a7f08591680f75f351d18005e4a7d41b4de1439`, OFF `7037051684471ed2d128ba152677e747209ff5291ce69129510c6a5f49252eaa`. The ON/OFF captures are byte-identical to the prior T14 proof; 26,534 RGB pixels differ inside the text ROI, zero outside, and alpha is unchanged. Both strict logs report `explicit GPU surface readback completed`; see `T14-{on,off}-strict-readback.{rgba,png,log}` and `T14-destination-transmission-metrics.json`. Build: `cmake --build build/chronon/linux-video-release --target chronon3d_cli -j14`, 578/578.

The first strict attempt exposed that raw sinks had no GPU slot and therefore no device surface to download. Raw Vulkan jobs now retain a native render slot and use a synchronous readback at the raw-writer boundary; other sink contracts are unchanged. The result is destination-aware stylized transmission, not physical volume transmission or RGB dispersion. The full visual gates and human reference review remain open.

### P1 T14 transmission ablation result — storico, prima del fix (2026-11-05)

Added a `gel_transmission` boolean to v2/v3 RenderPlan style, the text material, fingerprints and Vulkan run flags. Default ON preserves the former shader path; OFF is the diagnostic ablation. C ABI decoder/fingerprint tests pass. Rendered ON/OFF on a 24-pixel black/near-white checkerboard through strict Vulkan raw RGBA. The backgrounds are present and the text is visible, but ON and OFF raw frames are byte-identical (SHA-256 `a6f91853…58467`), max RGB delta 0 and alpha XOR 0. This is a FAIL: transmission is not observable in the current graph route. Diagnostic mode 5 independently visualizes the transmission run bit in red and destination alpha in green. Over 22,235 one-pixel-eroded core samples, ON sets the flag in all 22,235 while destination alpha remains zero in all 22,235; OFF clears the flag in all pixels. This confirms the TextRun tile shader runs before the checkerboard backdrop is composited into its destination image. Evidence: `T14-debug-on/off.rgba`, PNGs and `T14-diagnostic-metrics.json`. The follow-up is to expose the composited backdrop to the gel pass or move local transmission to a stage after background composition, then rerun T14. Esito storico, superato dal rerun del 2026-11-06 riportato sopra.

### Material relief iteration — 2026-11-06

A material-only iteration increases the SDF dome normal scale to 5.0 and
strengthens directional diffuse/specular response while keeping the TextRun,
coverage field, camera, and layout unchanged. Built with `-j14`; Vulkan strict
raw frame at 1280×720 is in
[`T06-relief-5.png`](../evidence/gel-text-stress-20261105/T06-relief-5.png),
with full provenance in
[`T06-relief-5.run.json`](../evidence/gel-text-stress-20261105/T06-relief-5.run.json).
The before/after proxy is in the requested Drive folder:
[GEL-TEXT-relief-iteration-20261106.mp4](https://drive.google.com/file/d/1OnHuL0SCg6JUgje7Cbqej0EWKG5MdStq/view?usp=drivesdk).
This produces more apparent rounded relief but remains an unreviewed material
prototype; it does not implement extruded mesh thickness or close the separate
camera/depth work. T10 resta il solo FAIL di leggibilità P1: −30 px comprime la frase ma non perde coverage. T11 ora passa la continuità e T12 passa la variazione tecnica degli highlight; la review ottica resta aperta. T14 passa il gate ON/OFF locale con le limitazioni dell’alpha scalare sopra registrate.


### P1 follow-up — T11 affine transform e T12 GEL light sweep — 2026-11-06

La matrice 2D animata del layer ora viene scomposta in scala e rotazione e gli offset per-glifo applicano la base affine completa; ciò elimina la sparizione della frase causata dalla rotazione ignorata. Il rerun T11 Vulkan/raw è conservato in `T11-transform-fix.rgba`, con metriche in `T11-transform-fix-metrics.json`: 61 frame, nessun frame a zero coverage. Ai frame di zoom massimo il testo oltrepassa i limiti del viewport, comportamento distinto dalla dropout precedente.

Aggiunta la proprietà RenderPlan `light_direction` (v2/v3), compilata come animazione GEL `Vec3` e campionata per frame nel TextRun, sia nel runner diretto sia nel batch fused. T12 usa una sweep azimutale di 61 frame; raw Vulkan SHA-256 `fd3b8e7811b591f74534321a8edc2c498519e26542c499a0ec82673ba7be6a17`. Fra frame 0 e 30 cambiano 26.369 pixel RGB nella ROI, media delta assoluta 3,44842. PNG campione: `T12-light-sweep-0.png`, `-30.png`, `-60.png`; plan e metriche sono affiancati. Questo certifica il binding e l’effetto dei parametri sullo shading, non ancora la corrispondenza ottica col reference.


### P1 T16 — GEL depth and focus sweep — 2026-11-06

T16 places “calories”, “or” and “ingredients” on three Z planes, animates camera focus from 700 to 1700 through 1200, and renders Vulkan/raw with DoF ON and OFF. The engine's existing render graph stamps positive camera-space layer depth into the per-pixel DoF provenance buffer; thin-lens CoC consumes the interpolated camera focus. The ON/OFF raw outputs differ across all sampled focus stages (34,400–87,521 RGB pixels). See `T16-dof-{on,off}.plan.json`, `T16-dof-metrics.json`, raw frames and PNGs. This verifies the integrated gel-depth/DoF route and focus sweep; the near word is deliberately enlarged beyond the viewport. Standalone depth visualization, detailed foreground/background occlusion, and bokeh highlight shape are still open, so M2/M4 remain partial rather than fully certified.


### Diagnostica prestazioni P2 — T12/T16 720p — 2026-11-06

La sweep GEL T12 misura p95 GPU 0,2825 ms e p95 render-thread 22,07 ms a 1280×720. T16 con DoF ON sale a p95 GPU 2,1827 ms e p95 render-thread 275,16 ms; DoF OFF: 0,7714 ms GPU e 241,06 ms render-thread. Sono misure diagnostiche raw, non presentazione video. Il budget del ticket è ≤1,5 ms GPU a 4K/120fps: T16 supera già il budget GPU a 720p, quindi P2 resta FAIL/non certificato; non si deduce una prestazione 4K. Dati frame-level nei sidecar T12/T16.


### P1 follow-up — Beer–Lambert RGB sul corpo e rerender Vulkan — 2026-11-06

Il vecchio helper C++ calcolava trasmissione Beer–Lambert, ma il fragment shader non attenuava il colore del corpo. Aggiunta la helper `gel::absorb()` e il corrispondente termine shader `exp(-sigma_a * dome)` con sigma `(1.2, 0.4, 0.05)`: lo spessore cresce da zero al bordo a uno all’asse mediano, l’assorbimento cresce nel nucleo; specular e Fresnel rimangono emissivi/non attenuati. Il gate shader/canary passa 105/105 e il filtro GEL passa 8/8 casi, 748/748 assertion dopo build `-j14` (558/558 step). T06 e T12 raw aggiornati mostrano 23.7k pixel RGB cambiati per frame rispetto allo shader precedente; T14 ON/OFF mantiene alpha identica (zero alpha pixel variati) e 26.534 pixel RGB cambiano nella ROI. Hash e sample PNG sono in `beer-lambert-rgb-render-metrics.json` e nei file `*-absorption*`. Limite dichiarato: la trasmissione RGB destination-aware è implementata come attenuazione stilizzata derivata dall’alpha sorgente, non da un attachment di spessore volumetrico indipendente.


### T18 — preview 2.5D integrata — 2026-11-06

Il render Vulkan strict-native usa un nuovo plate statico di sala d’attesa in stile documentario, testo GEL su piano abilitato 3D, shadow navy sotto il testo e sweep camera prospettica di quattro secondi da sinistra al centro e verso destra. Clip H.264: [T18 2.5D orbit, 4 s](https://drive.google.com/file/d/1ihWxf2Et4l2mLzZBq9sGaYUfc3QLMhi9/view?usp=drivesdk); still raw: [frame 60](https://drive.google.com/file/d/1sHC1HOoy-rCRP8DM_h-fOSJGEsrBVNTO/view?usp=drivesdk); manifest e plan sono nella stessa cartella Drive. Il plate è un’immagine generata, non footage originale. Il render mostra compositing su sfondo, materiale GEL e movimento parallax 2.5D; non dichiara estrusione fisica, approvazione visiva finale o chiusura M2/M4. Restano aperti il depth dump/occlusion gate, la diagnostica bokeh, la review ottica e il gate P2 4K/120.
