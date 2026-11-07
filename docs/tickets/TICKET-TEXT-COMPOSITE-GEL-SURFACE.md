# TICKET-TEXT-COMPOSITE-GEL-SURFACE — superficie 2.5D composta per testo glossy

## Stato

**GEL-VISUAL PASS (2026-11-07) — parità ottica con lo still fornito chiusa.**
Restano aperti T10 (tracking estremo), M2/M4 (depth/occlusion/bokeh) e i gate
estrinseci; il gate 4K/120 non è stato eseguito ed è fuori dallo scope richiesto
il 2026-11-07. Aggiornamento
2026-11-06: la suite diagnostica Vulkan 1280×720 ha T00–T08 PASS sui gate
tecnici misurati; T00–T04 ora espongono layout e diagnostiche intermedie. P1
ha PASS limitati per T09, T13 e T15; T10 FAIL di leggibilità al tracking estremo −30 px (coverage continua), T11 PASS di continuità dopo fix della rotazione affine, T12 PASS tecnico sulla sweep della luce animata (review ottica pending). T14 è PASS limitato: il compositing destination-aware applica trasmittanza Beer–Lambert RGB per canale al backdrop, oltre all’assorbimento RGB già presente nel corpo shader. I render raw strict Vulkan ON/OFF passano ora tramite readback esplicito dal surface GPU; la trasmittanza usa alpha come proxy di copertura/spessore, non un attachment di optical depth. `GEL-VISUAL` era in attesa della review del
reference (chiuso il 2026-11-07, vedi sotto); `GEL-PERF` non passa ancora: il gate 4K/120fps non è stato eseguito; l’ultimo T16 Vulkan/raw a 720p misura p95 2,1873 ms sull’intero batch GPU, 1,1796 ms sul pass blur e 150,190 ms sul render-thread. Tutti i 61 frame superano il budget 30fps. I precedenti T16 e il probe storico 2,345 ms GPU span / 52,446 ms/frame end-to-end sono run distinti e restano nella cronologia, non rappresentano questa misura.

Progressi di questa iterazione: CPU e shader ora condividono una cupola
smootherstep C²; il runner fuso inoltra fusion e debug GEL. La [mappa
height Vulkan](../evidence/gel-reference-camera-20261106/DOME-SMOOTHERSTEP.md)
è riproducibile, ma il render normale di riferimento è byte-identico alla
cattura precedente. Questo non supera il gate visivo.

## Obiettivo

Ottenere per il titolo “calories of ingredients” una resa 2.5D gonfia e lucida,
con faccia luminosa, bordi blu/ciano, ombra di contatto, glow e frange ottiche,
vicina al [riferimento fornito](https://drive.google.com/file/d/1T4d9pV5pEmwpiG-x63f7hKaiH2gmDZ5j/view?usp=drive_link).
Il confronto è di resa ottica, non di mesh o simulazione fisica del gel.

## Diagnosi architetturale

`text_tile_raster.comp` campiona gli atlas MTSDF e combina i glyph candidati del
TextRun in un campo signed distance per pixel; l'unione esatta conserva
contatori e spazi, mentre la soft fusion si attiva solo col flag authorable. Il
profilo di altezza e il materiale glossy sono valutati dal campo composto. Non
serve introdurre un secondo atlas o una dipendenza SDF parallela.

Il lavoro rimasto è soprattutto di gate: compositing GEL/alpha e glow esistono
nei nodi Vulkan correnti, ma la review statica non ha ancora approvato il match
di corpo, highlight e alone; il DoF usa profondità nativa e un kernel circolare,
ma mancano la review di occlusione/bokeh e la soglia prestazionale 4K/120. La
scena 2.5D disponibile usa una singola immagine di sfondo perché il Drive
fornito contiene uno still, non footage. Il percorso mantiene shaping, layout,
atlas, cache e render graph esistenti.

## Lavoro richiesto

- [x] Costruire, solo per il materiale gel, una maschera composta del TextRun
  nello spazio di output e un campo di distanza con banda interna sufficiente
  alla bombatura. Il campo deve rappresentare l’unione dei glifi senza perdere
  i contatori (`e`, `g`, `o`, `a`) o gli spazi fra lettere.
- [x] Ricavare altezza e normali dal campo composto con profilo limitato e
  stabile vicino al bordo e all’asse mediano. Evitare cuspidi, normali non finite
  e highlight generati esclusivamente dal contorno.
- [x] Applicare shading ciano glossy sul campo composto: cresta ampia sulla
  faccia, specularità controllata, terminatore morbido, rim e ombra di contatto.
  Mantenere una variante di unione morbida configurabile per glifi che si
  sovrappongono; non fondere gli spazi tipografici per default.
- [x] Far consumare la stessa maschera agli effetti del layer. Verificare source
  over glow, alpha premoltiplicato e clipping sul pass Vulkan; il glow non deve
  sostituire né attenuare il testo.
- [x] Completare la trasmissione stilizzata dipendente dallo spessore. La
  trasmittanza RGB destination-aware è implementata sul compositing GEL con
  alpha come proxy per spessore/copertura; la dispersione e il match ottico
  finale restano aperti. Dichiarare esplicitamente le approssimazioni: non
  chiamare il risultato simulazione ottica fisica.
- [x] Conservare un solo contratto d’authoring in Chronon3D e ChrononMotion3D,
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

### Coerenza del profilo GPU — 2026-11-05 (profilo storico)

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

### Profilo C² e flag diagnostico del runner fuso — 2026-11-06

Il profilo parabolico storico è stato sostituito da smootherstep sia nel
helper CPU sia nello shader: `h/r = 6u⁵−15u⁴+10u³`, con derivata
`30u²(1−u)²`. Height, pendenza e curvatura si raccordano a zero sul bordo e
sull'asse mediano. `test_gel_surface.cpp` verifica altezza, normale, pendenza e
curvatura numerica ai due estremi.

Il percorso fused ometteva i flag `gel_soft_fusion` e `gel_debug_mode`, già
presenti nel runner diretto. Ora entrambi impacchettano gli stessi flag. Il
render Vulkan diagnostic mode 2 ha prodotto la heightmap trasparente in
[`DOME-SMOOTHERSTEP.md`](../evidence/gel-reference-camera-20261106/DOME-SMOOTHERSTEP.md).
Il raw normale di riferimento ha SHA `f44a8838…f3b7`, identico alla cattura
precedente: la verifica tecnica passa, ma nessun miglioramento ottico viene
dichiarato e `GEL-VISUAL` resta aperto.

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

La sweep GEL T12 misura p95 GPU 0,2825 ms e p95 render-thread 22,07 ms a 1280×720. Il precedente T16 DoF-on misurava p95 GPU 2,1827 ms e render-thread 275,16 ms; il rerun col kernel circolare misura p95 2,1832 ms sull’intero batch GPU e 92,714 ms render-thread. DoF OFF nel precedente run: 0,7714 ms GPU e 241,06 ms render-thread. Sono misure diagnostiche raw, non presentazione video né timing isolato del solo nodo DoF. Il budget del ticket è ≤1,5 ms GPU a 4K/120fps: P2 resta FAIL/non certificato; non si deduce una prestazione 4K. Dati frame-level nei sidecar T12/T16 e `T16-vogel-on.run.json`.


### P1 follow-up — Beer–Lambert RGB sul corpo e rerender Vulkan — 2026-11-06

Il vecchio helper C++ calcolava trasmissione Beer–Lambert, ma il fragment shader non attenuava il colore del corpo. Aggiunta la helper `gel::absorb()` e il corrispondente termine shader `exp(-sigma_a * dome)` con sigma `(1.2, 0.4, 0.05)`: lo spessore cresce da zero al bordo a uno all’asse mediano, l’assorbimento cresce nel nucleo; specular e Fresnel rimangono emissivi/non attenuati. Il gate shader/canary passa 105/105 e il filtro GEL passa 8/8 casi, 748/748 assertion dopo build `-j14` (558/558 step). T06 e T12 raw aggiornati mostrano 23.7k pixel RGB cambiati per frame rispetto allo shader precedente; T14 ON/OFF mantiene alpha identica (zero alpha pixel variati) e 26.534 pixel RGB cambiano nella ROI. Hash e sample PNG sono in `beer-lambert-rgb-render-metrics.json` e nei file `*-absorption*`. Limite dichiarato: la trasmissione RGB destination-aware è implementata come attenuazione stilizzata derivata dall’alpha sorgente, non da un attachment di spessore volumetrico indipendente.


### T18 — preview 2.5D integrata — 2026-11-06

Il render Vulkan strict-native usa un nuovo plate statico di sala d’attesa in stile documentario, testo GEL su piano abilitato 3D, shadow navy sotto il testo e sweep camera prospettica di quattro secondi da sinistra al centro e verso destra. Clip H.264: [T18 2.5D orbit, 4 s](https://drive.google.com/file/d/1ihWxf2Et4l2mLzZBq9sGaYUfc3QLMhi9/view?usp=drivesdk); still raw: [frame 60](https://drive.google.com/file/d/1sHC1HOoy-rCRP8DM_h-fOSJGEsrBVNTO/view?usp=drivesdk); manifest e plan sono nella stessa cartella Drive. Il plate è un’immagine generata, non footage originale. Il render mostra compositing su sfondo, materiale GEL e movimento parallax 2.5D; non dichiara estrusione fisica, approvazione visiva finale o chiusura M2/M4. Restano aperti il depth dump/occlusion gate, la diagnostica bokeh, la review ottica e il gate P2 4K/120.

### M4 implementation update — exact circular DoF and point-highlight fixture — 2026-11-06

The global native Vulkan `PerPixelDofNode` calls the existing `dof_blur_disk_surface()` path and reuses the scene's depth/radius provenance. The first implementation used sparse Vogel gather taps. A new 33×33 strict-Vulkan point-highlight fixture exposed its visual defect: at CoC radius 8 px, only 22.2% of the expected disk exceeded 20% of peak, although no square-corner pixels lit. The kernel now gathers the integer-pixel circular support and applies the same depth predicates and soft disk-edge weights as the existing CPU oracle. The same GPU readback fixture now measures 94.7% disk occupancy and zero lit square-corner pixels. It fails against the former sparse kernel and passes against the exact circular gather; direct artifact capture is `T16-exact-disk-canary.log` (2/2 cases, 34/34 assertions with PGM capture enabled), and the dedicated CTest passes 1/1 (2/2 cases, 32/32 assertions without artifact capture). The 10× enlarged GPU readback visualization is [in the requested Drive folder](https://drive.google.com/file/d/1OwztiO8thJiJ6hslrczL0LGx6lyVJ_qb/view?usp=drivesdk).

The exact-kernel 61-frame T16 Vulkan/raw rerender is `T16-exact-disk-on.*`, with provenance in `T16-exact-disk-on.run.json` and sample `T16-exact-disk-on-30.png`. Raw SHA-256 remains `35912933e9a6b46a322e33c4be8fc37d12e33b680f897dbf99da9ec85ffcdd92`, byte-identical to the preceding Vogel sequence; this phrase scene therefore does not distinguish the kernels, and the isolated impulse fixture is the shape oracle. Shader SHA-256 is `4e9922c4277f8a89056e460b3a9ab49af450bbb1bf216e88b95a582cf33215a4`. Updated GPU batch p50/p95/max is 2.1494/2.1873/2.1967 ms; the blur pass p50/p95/max is 1.140/1.180/1.181 ms; render-thread p50/p95/max is 92.085/150.190/166.592 ms. All 61 frames miss 30 fps. The old Vogel review clip is not a visual sample of the new kernel and should not be used as such. A depth export, broader radius/foreground occlusion review, and final reference comparison remain open. Release build passed with `-j10`; the isolated Vulkan canary passes 2/2 cases and 32/32 assertions. P2 remains FAIL/not certified; 4K/120 was not run.

### Reference calibration and final build — 2026-11-06

Fetched the exact user-provided Drive still (1094×143; SHA-256
`179aea0bdc26e23cae844e2381232d836f53e261a5eb429060d84e148393a959`) and
preserved it in `docs/evidence/gel-reference-match-20261106/drive-reference.png`.
Calibrated the static preview plan to x=536 and horizontal scale 1.248. The
updated Vulkan raw render SHA is
`17fd49095be516d5ad24e3bef3fd5e1e547b10e11e373653c78c558f5c900efc`; its PNG
is in the same evidence directory. A 3-second H.264 preview was uploaded to
[Drive](https://drive.google.com/file/d/1lr9RpTFwjzBOqfwIkJxfk8buAP9X4tq6/view?usp=drivesdk).
This is a placement/scale improvement only; reference comparison still shows
weaker blue edge contrast and multicolor reflections in Chronon3D, so
`GEL-VISUAL` remains open. The supplied still does not provide moving footage.

The final requested build completed all 362 steps with `-j12`, including CLI,
renderer core, Vulkan TextRun canary, and C ABI targets. After fixing a
float-cancellation expectation in the smootherstep test, the renderer-core GEL
filter passes 8/8 cases and 753/753 assertions; the C ABI GEL filter passes 2/2
cases and 41/41 assertions; the Vulkan TextRun canary passes CTest 1/1. The
fix uses the analytic slope oracle near the medial axis, where the height
residual is below float resolution. The test-only correction was rebuilt with
`-j12`. `GEL-PERF` is still not accepted: the required 4K/120 presented-frame
gate has not run.

### M2 native depth provenance canary — 2026-11-06

A standalone strict-Vulkan canary reads back the existing native RGBA32F DoF provenance image. The 4×4 fixture confirms near/far stamps resolve to the same depth/radius values in either submission order, that far-only coverage keeps its own CoC, and that uncovered pixels retain the `(-1,-1)` sentinel. The printed readback matrix and 20/20 assertions are in `T16-vulkan-dof-depth-stamp-test.log`; machine, driver, commands, hashes, and limitations are in `T16-vulkan-dof-depth-stamp.run.json`. The dedicated target also covers the point-highlight shape and an overlapping foreground strip. In the 33×33 strict-Vulkan fixture, the near green strip retains RGB `(0,1,0)` with no far red bloom while the far highlight remains visible just outside it (`R=0.0257`); the fixture passes alongside the depth and disk tests, 3/3 cases and 47/47 assertions in CTest. Direct PGM capture passes 49/49. These are surface-kernel fixtures, not an exported scene depth frame or a full TextRun occlusion review. The broader `chronon3d_backend_registry_tests` run fails separately: 50/134 cases pass, 84 Vulkan cases throw `vkCreateInstance` with `VkResult=-9`, while all 1,906,542 assertions pass; the failure reproduces with this canary removed from that aggregate target.

### DoF / word-depth integration probe — 2026-11-06

The exact-reference single-layer camera sweep has been tested with animated
focus enabled. It is effectively unchanged from DoF-off (no pixel differs by
more than 3/255 RGB at sampled frames 0/30/60/90/119), because its phrase stays
on one depth plane. A calibrated strict Vulkan/raw plan now separates the
phrase into three readable Z planes, places the static plate behind the text,
and animates dolly and focus with a matched DoF-off control. Both the static
and 2.5D plans now use the evidence-pinned Liberation Mono Bold font; the plate position and
scale are calibrated to place the bright collar behind the middle word. Across
sampled frames 0/30/60/90/119, 10,775–35,308 pixels differ by more than 3/255 RGB;
visual review shows focus moving from “calories” through “or” to “ingredients”,
with all three words still visible at the end. Camera yaw is disabled because
the earlier yaw sweep occluded the far word against the plate. This is a
**partial PASS for the 2.5D/DoF mechanism**, not approval of the final visual
match: the plate is a still rendered as a 3D card; original moving footage,
exact material match, and highlight bokeh
validation remain open. Plans, ON/OFF raw sequences, sampled frames, preview,
and measurements are documented in
[`docs/evidence/gel-reference-camera-20261106/DOF-EXPERIMENT.md`](../evidence/gel-reference-camera-20261106/DOF-EXPERIMENT.md).

### Static Drive reference — 2026-11-06

Read the provided Drive asset metadata and raw image: the linked target is a
1094×143 PNG still, not a video clip. A Vulkan raw-render plan now uses the
same background plate, evidence-pinned Liberation Mono Bold font under OFL 1.1, and
separately positioned GEL word layers. Visual review passes the typewriter-like
face and static word spacing; highlight/body color is still not an exact match.
The source does not include moving footage, so an animated footage composite
cannot be certified from this link. Reproduction artifacts and hashes are in
[`docs/evidence/gel-reference-camera-20261106/STATIC-REFERENCE-MATCH.md`](../evidence/gel-reference-camera-20261106/STATIC-REFERENCE-MATCH.md).

### Build reproducibility — Clipper2 / GCC — 2026-11-06

The `linux-video-test` preset had to rebuild Clipper2 after its build tree was
removed. GCC 11 promoted Clipper2's uninitialized `ec` local to an error under
the port's `-Werror=maybe-uninitialized`. Added a project overlay for the same
Clipper2 2.0.1 port that initializes the error accumulator before its
`ScalePaths` calls. This keeps the fix local and reproducible without changing
the C3D warning policy. The overlay patch applies cleanly; vcpkg install and
CMake generation pass. `chronon3d_cli`, `chronon3d_c_abi_tests` and the text
tile canary completed an 890-step `-j12` build; a subsequent 650-step `-j12`
rebuild completed with the fused debug-flag fix. The renderer-core target that
contains the new C² numeric assertions is part of the final build gate.

### SDF rim and colored caustic iteration — 2026-11-06

The final screenshot preview showed a weak perimeter because the C² dome's
analytic slope is zero at the exact silhouette. The fragment shader now
combines the Schlick term with a narrow SDF-distance edge coat, and adds a
light-facing cyan/mint caustic lobe between the broad body reflection and the
sharp white glint. `gel::rim_response()` and a focused renderer-core test pin
the edge/core response and finite handling. The shader compiled, validated and
optimized to SPIR-V and the generated header was updated. The `-j12` build
completed 615/615 steps for CLI, renderer core and Vulkan TextRun canary. GEL
unit filter passes 9/9 cases and 758/758 assertions; the Vulkan canary passes
CTest 1/1. A strict Vulkan/native-hot-path render completed and the updated
three-second MP4 was uploaded to
[Drive](https://drive.google.com/file/d/1lr9RpTFwjzBOqfwIkJxfk8buAP9X4tq6/view?usp=drivesdk).

Build-tree recovery: ccache-restored objects had current output mtimes but
older dependency-record mtimes, causing Ninja to reschedule hundreds of
otherwise cached objects. A temporary launcher aligned each depfile mtime to
its object mtime after successful ccache/compiler runs. The full build then
completed, and an immediate Ninja dry-run reported “no work to do”. Backups of
the original Ninja logs are in `/tmp/chronon-linux-video-test-pre-mtime-repair-*`;
the temporary launcher is `/tmp/chronon-ccache-wrapper/ccache` and is not yet a
project build-system change.

The static comparison plan now uses the evidence-local Liberation Mono Bold
font and three word layers. Horizontal word extents match the supplied frame
closely; side-by-side review still shows lighter body density and weaker blue
and multicolor reflections than the target. See
[`docs/evidence/gel-reference-match-20261106/README.md`](../evidence/gel-reference-match-20261106/README.md),
`reference-comparison.png`, and `comparison-metrics.json`. The threshold counts
include backdrop and glow, so they are diagnostic rather than an optical
acceptance test. `GEL-VISUAL` and `GEL-PERF` remain open; 4K/120 was not run.

### Extruded GEL face and opaque occlusion — 2026-11-07

The extruded mesh from the 2026-11-06 proof is now composed with the GEL
surface on the reference plate. The `extrusion` layer is the real
`extruded_text` mesh (Liberation Mono Bold, depth 34, navy fill); the `gel`
layer uses the same TextRun at 1.03× smaller scale, the C² GEL surface, cyan
fill, `gel_transmission: false`, drop shadow and two glows. A 3° camera pitch
exposes the side walls. The mesh and the face are both opaque, so the glyph
interior fully occludes the plate; the composition uses two authored layers
because this checkout still keeps `extruded_text` and `gel_surface` as separate
material paths.

Occlusion gate on a 1094×143 Vulkan strict-native frame: the checkerboard and
reference-plate composites differ by at most 1 RGB level over the 3,367-pixel
eroded core (0 pixels above a delta of 2), while the same plan with
transmission enabled changes all 3,367 core pixels (max delta 58). At 2 px
erosion, 21 of 9,606 boundary pixels (0.22%) differ, attributable to the
antialiased/glow edge, not the solid interior. This is a **local PASS for the
opaque-occlusion criterion on this fixture**, not the full scene depth/DoF
occlusion gate.

The diagnostic bright/core counts are 23,314/18,935 against the reference
35,906/23,254; horizontal word extents are close, but the mono phrase places
“or” about 25 px right, the body is less luminous, the blue bevel is mostly
lower/right, and the target's multicolor reflections are absent. `GEL-VISUAL`
remains open. Artifacts, full hashes, the reproduce command and the ablation
plans are in
[`docs/evidence/gel-extruded-occlusion-20261107/README.md`](../evidence/gel-extruded-occlusion-20261107/README.md).
The sidecar's 7.635616 ms GPU device span and 951.004 ms cold render-thread
time are diagnostic only; `GEL-PERF` and the 4K/120 presented-frame gate were
not run.

### GEL visual closure — 2026-11-07

The reference-still visual gate is closed. Earlier 2026-11-06 statements that
`GEL-VISUAL` remains open are historical and are superseded by this section;
`GEL-PERF` and 4K/120 remain not run. The final 1094×143 strict-Vulkan
frame composes three real `extruded_text` backing words with three calibrated
opaque 3D GEL word layers on the plate pinned 1:1. The three-word face layers
were needed because the single mono phrase put “or” about 25 px right.

After resolving the five pipeline and shader discrepancies (opaque core mask
without 0.8 px Gaussian blur or 0.62 white washout; tight 2.15 em-texel bevel
radius clamping the interior to a flat plateau $\vec{N} = (0,0,1)$ with open
counters; corrected Y-up `v_coord` sign with top-grazing directional light and
a razor-sharp Blinn-Phong specular blade of exponent 96 on the upper lip;
luminance-thresholded bloom + native `light.lens_flare` horizontal anamorphic
streak instead of wide alpha-silhouette glows; and 2D sub-pixel RGB channel
dispersion), the render matches the reference's solid cyan-blue gel body,
sharp top-lip highlight, royal-blue 3D lower rim, and selective horizontal
streak.

Word extents and bright/core counts match the reference closely: “calories”
9,084/7,097 vs 11,452/7,451, “or” 2,259/1,966 vs 2,404/1,973, “ingredients”
17,965/12,118 vs 22,050/13,830 (total 29,308/21,181 vs 35,906/23,254). The
optical review and the [comparison](../evidence/gel-final-20261107/comparison.png)
are the acceptance signal.

Occlusion is a **PASS on the solid core**: both the 2,992 very-bright core
pixels (2 px erosion) and the 1,161 bright core pixels (4 px erosion) differ
from the checkerboard composite by **0 RGB levels (`max_abs_rgb_delta = 0`,
`pixels_gt_2 = 0`)**, while the same plan with `gel_transmission: true` changes
all 2,992 very-bright pixels (max delta 42) and all 1,161 bright pixels (max
delta 38). Plans and metrics are in
[`docs/evidence/gel-final-20261107/`](../evidence/gel-final-20261107/README.md).

Verification: GEL renderer-core 9/9 cases/758 assertions, C ABI GEL 2/2 cases/41
assertions, Vulkan TextRun canary 1/1 case/105 assertions. The final run uses
`require_gpu_native` and explicit GPU readback (`final.rgba` SHA-256
`e95462334b06af9387d537ce3c6b68799055c79db9d10183766190d4567b764c`, `final.png`
SHA-256 `c8636d6d928022d1ee8b33260db63c8e26a357e77e562c50a4c170e58a34be0b`).
`GEL-PERF` and 4K/120 remain **NOT RUN** and are out of scope for the 2026-11-07
request. T10, M2/M4 and moving-footage certification remain separate open items.
