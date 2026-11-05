# TICKET-CAMERA-3D-V1-CLOSURE — chiusura bounded di camera e 3D (V1)

- **Stato**: **CHIUSO E ARCHIVIATO — tracker storico camera e 3D V1.** Chiusura
  amministrativa il 2026-11-04, insieme alle quattro schede camera. Le matrici
  restano come verdetto storico; nessun `PARTIAL` o `NOT RUN` è promosso a
  `PASS`. I residui non certificati vivono nel master 2.0 e in v2.1.
- **Data di apertura**: 2026-11-04.
- **Sostituisce come authority di chiusura**: nessuna scheda. Le schede
  proprietarie restano la specifica; questo file è il registro dei verdetti.
- **Non è**: parità After Effects. La matrice B–J di
  [`TICKET-AE-CAMERA-FUNCTIONAL-PARITY`](TICKET-AE-CAMERA-FUNCTIONAL-PARITY.md)
  è demandata a v2.1; qui si chiude il **contratto Chronon V1**, non la
  compatibilità AE.

> **Nota di ricostruzione (2026-11-05).** L'originale era un file untracked mai
> committato; questa copia è ricostruita dal transcript
> `freebuff-chat-2026-11-04T17-48-12.348Z.md` e dalle schede archiviate.
> Matrici e verdetti sono quelli finali; nessun residuo è stato promosso.

## 1. Regola di evidenza

| Tag | Significato |
|---|---|
| `SUPPORTED` | Implementato e verificato localmente con evidenza nominata; resta la certificazione same-SHA se il gate la richiede. |
| `PARTIAL` | Implementato in parte o verificato solo su una lane/fixture; il residuo è elencato. |
| `REJECTED` | Escluso per decisione, con fail-closed tipizzato e test. |
| `MISSING` | Nessuna implementazione; va implementato o dichiarato fuori perimetro con una decisione. |
| `NOT RUN` | Gate esistente ma non eseguito in questo checkout. |

Nessun `PARTIAL` diventa `SUPPORTED` per età. Un `PASS` locale non è una
certificazione same-SHA: la distinzione resta quella di
[`docs/stato/README.md`](../../../../docs/stato/README.md).

## 2. Matrice camera V1

| # | Capacità | Stato | Evidenza locale | Residuo / gate |
|---|---|---|---|---|
| C-01 | Camera libera / one-node | `SUPPORTED` | `CameraRig`, `motion_camera_v1`, `chronon3d_camera_tests` 93/93 (68.186 assert), 2026-11-04 | Nessuno sul contratto; copertura nel corpus Mixed 3D |
| C-02 | Camera con target / look-at / roll | `SUPPORTED` | `resolveWorldPose`/`sampleWorld`, `camera_bake_roundtrip` 185/0, slerp sub-frame | Matrice degenerazioni (target coincidente, collinearità) non tabellata |
| C-03 | Null e parent multi-livello | `PARTIAL` | parent world + rig test; shear/reflection rifiutati | Catene arbitrarie, pivot/anchors e reparent semantica non certificati (B–J) |
| C-04 | Rig cinematografici | `PARTIAL` | dolly/orbit/pan/tilt/framing/trajectory in `CameraRig` | **dolly-zoom `MISSING`**; invarianti di framing non tabellati |
| C-05 | Ottica (FOV/focale/sensore/near/far/aspect) | `PARTIAL` | thin-lens, gate sensore [1,100] mm, FOV 50°, near/far nel `CameraPlan`; golden fisico combinato 5 assert (2026-11-04) | Lens shift e near/far animati su video limite; precedenza FOV↔focale dichiarata ma non tabellata |
| C-06 | Fuoco / DoF circolare | `SUPPORTED` | kernel disco + highlight weighting, parità CPU/Vulkan 1,35e-07; C4/C5/C6; 150/150 strict-native | Griglia CoC a tre profondità e near/far bleeding restano B–J |
| C-07 | Bokeh avanzato (lamelle) | `REJECTED` | `dof_shape_unsupported` rifiuta `iris_blades` a entrambi i confini; highlight gain ammesso | Nessuno: esclusione fail-closed con test |
| C-08 | Layer 2.5D (alpha/depth/occlusione) | `PARTIAL` | P0-05 144/144, P0-06 24/24 ordini, P0-07 cutout 24 frame, canary occlusion 87+46 assert | Occlusione+DoF su scena animata completa; fault-injection nativa end-to-end |
| C-09 | Tempo (easing, hold, wrap, subframe, shutter) | `PARTIAL` | `ResolvedFramePlan` bit-exact, 29 casi negativi; shutter 180°/8 | I job passano ancora il calendario a frame interi: parità sub-frame end-to-end non dimostrata |
| C-10 | Sequencer / cut camera attiva | `SUPPORTED` | `CameraSamplePlan.cut` sul wire, decoder (`render_plan_decoder_support.cpp`), `camera_cut_frames` nel `CompiledProgram` (`compiler.cpp:240`), fingerprint v41, `sample_resolved_camera` nel runtime; test decoder cut e caso tipizzato **1 caso, 40/40 assert** (hold a 1.999, cut a 2.0), 2026-11-04 | Nessuno sul contratto; il sequencer autorevole resta CM3D |
| C-11 | Determinismo camera/layer | `PARTIAL` | provider live vs offline full-scene PASS; raw deterministico | Certificazione cross-backend del corpus |
| C-12 | Preview / export tipizzato | `PARTIAL` | FullGraph/DirectYUV/GOP consumano `CompiledProgram` | `preview_scale` (Draft 3D) non implementato: `TICKET-DRAFT3D-PREVIEW` |

## 3. Matrice 3D V1

| # | Capacità | Stato | Evidenza locale | Residuo / gate |
|---|---|---|---|---|
| D-01 | glTF animato skin/morph + clip library | `SUPPORTED` | CM3D reader/bake, fixture textured 31/31 frame, MP4 SHA stabile, CTest CM3D 67/67 | CI same-SHA; materiali di produzione oltre la fixture |
| D-02 | Mesh inline textured/alpha + depth per-pixel | `SUPPORTED` | strict-native 31/31, `chronon3d_backend_registry_tests`, canary mesh/piano 87/46 | Copertura PBR/produzione; corpus overlay ampio (`SCALE-P0`) |
| D-03 | Ordine di profondità e compositing (image/plane/mesh/text) | `SUPPORTED` | P0-06: 24/24 ordini, hash identico per backend; P0-05 image-under-mesh 144/144 | Nessuno sul contratto V1 |
| D-04 | PBR metallic-roughness | `PARTIAL` | fattori, base/emissive, alpha, double-sided, texture esterna; shading software | Parità Vulkan del subset PBR; golden metallo/plastica/gemma |
| D-05 | Luci direzionali | `SUPPORTED` | parità Vulkan direzionale 3/3 casi, 69 assert | Nessuno sul contratto V1 |
| D-06 | Ombre renderer | `PARTIAL` | `ShadowNode::execute_native` (verificato); T05 5/5 off + 5/5 on, gate 30/30 | Qualità ombre, A/B su produzione approvata, repeatability strict |
| D-07 | Shadow catcher catch-only | `SUPPORTED` | `Material2_5D::shadow_catcher`, wire `shadow_catcher`, builder; il grafo parte da trasparente e applica l'alpha del receiver con `TrackMatteNode`; test catch-only 8/8 assert e frame 1920×1080 | Certificazione same-SHA |
| D-08 | Light rig multi-luce (spot/point) | `PARTIAL` | Tipi e cone/penumbra software (`lighting_eval.hpp`), array tipizzato con decoder fail-closed, ombre per-spot nel graph builder; test 4 spot indipendenti 28/28 assert | Lighting loop multi-luce Vulkan strict-native e parità MAE/p99: richiedono host GPU |
| D-09 | IBL / HDRI | `PARTIAL` | byte `HDRIENV1`, decoder, cubemap+mip+SH, diffuse e specular software; probe 2.965/2.965 pixel; **harness consumer del golden PASS** (154/154 assert: SH, radianza per texel/mip, energia per mip, bilineare) | Sampling cubemap strict-native e parità hardware |
| D-10 | Particelle / bokeh environment | `PARTIAL` | contratto bounded 4096/64 MiB, deterministico, lane software; golden 4/4 (empty/full-budget/clipped/bokeh-depth) + random access 1 vs 4 worker, 5 casi/23 assert, suite 217/217 | Parità Vulkan byte-identica e certificazione host GPU |
| D-11 | Raster mesh / performance 3D | `NOT RUN` | percorso compute O(pixel × triangoli) | `P2-01` profiling 10/100/1k/10k triangoli; decisione raster graphics `P2-02` |
| D-12 | Corpus di accettazione Mixed 3D | `NOT RUN` | — | Master §7 punto 3: glTF skinned+morph, opaca, alpha-cutout, plane3d, image dietro, camera rotating+dolly |

## 4. Cosa è stato chiuso in questa tornata (2026-11-04)

- **D-06 / premessa shadow**: `ShadowNode::execute` instrada su
  `execute_native` quando `require_native_gpu` è attivo
  (`include/chronon3d/render_graph/nodes/shadow_node.hpp:136`); evidenza
  2026-10-26 (regressione 15 assert, backend registry 1/1). Le tre schede che
  descrivevano il nodo come fail-closed sono state allineate.
- **D-09 / golden IBL**: aggiunto
  `tests/renderer/lighting/test_hdri_specular_golden.cpp`, che legge il golden
  del producer (`ChrononMotion3D/tests/hdri_specular_golden.json`) e il payload
  `HDRIENV1` committato, e confronta SH, sonde di radianza per faccia/mip/texel
  ed energia media per mip. Esito consumer: **154/154 assert PASS**.
- **C-10 / camera cut**: `CameraSamplePlan.cut` sul wire, decoder,
  `camera_cut_frames` nel `CompiledProgram`, fingerprint v41 e
  `sample_resolved_camera` nel runtime; test decoder cut e caso tipizzato
  **40/40 assert** (hold a 1.999, cut a 2.0).
- **D-07 / shadow catcher catch-only**: `Material2_5D::shadow_catcher`, campo
  wire, `ClearNode` trasparente e `TrackMatteNode` sull'alpha del receiver;
  caso catch-only **8/8 assert** e frame 1920×1080.
- **D-08 / spot e point light**: tipi, contributo software con attenuazione e
  cone, array `LayerPlan::light` con decoder fail-closed e ombre per-spot;
  4 spot indipendenti **28/28 assert**.
- **C-01/C-05**: `chronon3d_camera_tests` **93/93 (68.186 assert)** e golden
  fisico combinato **5 assert**.

## 5. Blocchi esterni (valgono per entrambe le matrici)

1. **CI same-SHA** — nessun runner verde sullo SHA esatto: ogni `SUPPORTED`
   resta locale finché non arriva (master P0-02).
2. **Reviewer umano** — P1-16 `BLOCKED`: senza nomina, C7 e la review visiva
   del corpus non si chiudono.
3. **Host GPU dedicato** — certificazioni strict-native di ombre, IBL,
   particelle, corpus.
4. **Content pack** — `CHRONON3D_BUILD_CONTENT=OFF`: i gate che lo richiedono
   restano `NOT RUN`.

## 6. Criteri di chiusura V1

1. Ogni riga è `SUPPORTED` con gate verde **oppure** `REJECTED` con test
   fail-closed **oppure** ha una decisione scritta che la sposta fuori V1.
2. Nessuna riga resta `PARTIAL` senza residuo nominato e owner.
3. `C-10` e `D-07` sono `SUPPORTED` localmente; `D-08` è `PARTIAL` con il solo
   residuo Vulkan strict-native nominato. Il sotto-caso dolly-zoom di `C-04`
   resta `MISSING` e va in v2.1: nessuna riga è promossa per età.
4. Il corpus Mixed 3D (D-12) gira come gate unico e non come somma di fixture.
5. CI same-SHA verde + review umana: senza questi due, la chiusura è locale
   per definizione e va dichiarata tale.

## 7. Ordine operativo

1. Decisione di scope su C-04 (dolly-zoom), D-08 e D-11/D-12.
2. D-06 shadow → D-08 spotlight → D-09 IBL strict-native (catena del
   `ROADMAP-ESECUZIONE` Fase 1/2a).
3. D-04 PBR Vulkan + D-10 particelle parity.
4. C-08/C-09 residui (occlusione+DoF su scena animata, calendario sub-frame).
5. D-12 corpus Mixed 3D come gate unico.
6. Fase 6: CI same-SHA e review.

## 8. Schede proprietarie

- Camera: [`TICKET-CAMERA-DEFINITIVE-CLOSURE`](TICKET-CAMERA-DEFINITIVE-CLOSURE.md)
  (chiusa amministrativamente), [`TICKET-AE-CAMERA-FUNCTIONAL-PARITY`](TICKET-AE-CAMERA-FUNCTIONAL-PARITY.md)
  (B–J a v2.1), [`TICKET-CAMERA-SURFACE-CONSOLIDATION`](TICKET-CAMERA-SURFACE-CONSOLIDATION.md)
  (chiusa), [`TICKET-DOF-OCCLUSION-ACCEPTANCE`](TICKET-DOF-OCCLUSION-ACCEPTANCE.md)
  (chiusa).
- 3D: [`TICKET-SHADOW-NATIVE-CATCHER`](../TICKET-SHADOW-NATIVE-CATCHER.md),
  [`TICKET-SPOTLIGHT-RIG`](../TICKET-SPOTLIGHT-RIG.md),
  [`TICKET-ENVIRONMENT-LIGHT-IBL`](../TICKET-ENVIRONMENT-LIGHT-IBL.md),
  [`TICKET-CINEMATIC-PARTICLES-BOKEH`](../TICKET-CINEMATIC-PARTICLES-BOKEH.md),
  [`TICKET-LIGHTS-SHADOWS-PARTICLES`](../TICKET-LIGHTS-SHADOWS-PARTICLES.md),
  [`TICKET-DRAFT3D-PREVIEW`](../TICKET-DRAFT3D-PREVIEW.md).
- Release: [`TICKET-CHRONON-2.0-MASTER`](../TICKET-CHRONON-2.0-MASTER.md) P0/P1/P2.

## 9. Chiusura amministrativa (2026-11-04)

Il perimetro camera e 3D V1 è **chiuso amministrativamente** per decisione
owner, insieme alle quattro schede camera. Il tracker non è più un'autorità
viva: conserva le matrici e i verdetti come prova storica. In particolare:

- C-10 e D-07 restano `SUPPORTED` solo per l'evidenza locale già citata; la
  certificazione same-SHA non è stata eseguita.
- D-08, D-09, D-10, C-03, C-04, C-05, C-08, C-09, C-11, C-12, D-04, D-06 e
  D-11/D-12 restano `PARTIAL` o `NOT RUN`: nessuna riga è stata promossa.
- C7/review, corpus Mixed 3D, parità AE B–J, S2/S5 e DoF avanzato restano
  tracciati nel [`TICKET-CHRONON-2.0-MASTER`](../TICKET-CHRONON-2.0-MASTER.md)
  (P0-10, P1-10, P1-11, P1-16) e in v2.1.
- Una regressione futura apre una nuova scheda in `../` che cita questa; questo
  file non si riapre.
