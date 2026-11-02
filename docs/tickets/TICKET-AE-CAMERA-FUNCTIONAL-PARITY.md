# TICKET-AE-CAMERA-FUNCTIONAL-PARITY — Camera 3D, Null, rig e DoF

- **Stato:** OPEN — specifica e piano; il contratto di ammissione ottica condiviso è atterrato il 2026-10-31, la parità funzionale AE non è certificata.
- **Priorità:** P1, dopo i gate locali di [CAMERA-DEFINITIVE-CLOSURE](TICKET-CAMERA-DEFINITIVE-CLOSURE.md).
- **Owner:** manutentore del confine ChrononMotion3D / Chronon3d; reviewer visivo da assegnare prima della certificazione.
- **Obiettivo:** parità funzionale di un sottoinsieme dichiarato di camera After Effects, NON pixel identici, compatibilità universale AE o stessa velocità presunta.
- **Repository coinvolte:** Motion per authoring/evaluation; Chronon3d per consumer/render; Template e RenderingGen per job/export e delivery.

## 1. Confine definitivo: staccare la regia, non duplicare il renderer

Motion possiede rig, gerarchie/Null, traiettorie, interpolazione/easing, point of interest/look-at, roll, scelte di lente e fuoco. Il job renderer possiede il calendario dei timestamp necessari; Motion restituisce camera e layer risolti a quegli stessi istanti. Il compilatore conserva dati immutabili, non callback mutabili nel render hot path.

Chronon3d conserva solo il contratto necessario a disegnare: view/projection, clipping, trasformazioni risolte, profondità, compositing/alpha, DoF supportato e integrazione shutter. Non ricostruisce look-at, parent o coreografia. Per il 2D puro la proiezione è identità/ortografica. Avere proiezione nel renderer NON equivale a mantenere una seconda authority di regia.

**Alternativa plate GPU, NON obbligatoria per questo ticket:** se anche proiezione/DoF devono uscire da C3D, il renderer Motion deve produrre superfici RGBA GPU di produzione. Servono gate separati per alpha premoltiplicato, spazio colore, depth/occlusioni con overlay, ownership/sincronizzazione, accesso fuori ordine e assenza di readback. `render/` CM3D è ancora un harness: non promuoverlo editorialmente e non eliminare DoF C3D prima del replacement verificato.

## 2. Inventario e matrice da congelare

Le righe qui sotto sono requisiti da censire. Le API presenti non implicano parità E2E; i PASS locali del ticket precedente restano limitati al suo contratto.

| Famiglia | Comportamento richiesto | Baseline osservata / verifica mancante |
|---|---|---|
| Camera libera / one-node | posizione e orientamento indipendenti, rotazioni locali/world esplicite | CameraRig position/orientation presenti; fissare semantica AE-style e test combinati |
| Camera con target / two-node | point of interest animato, look-at, roll senza doppia applicazione | target/roll presenti e regressioni locali; dichiarare target space e degenerazioni |
| Null e parent | catene multilivello, pivot/anchor, reparent con semantica dichiarata | parent world presente; shear/reflection/singularità non rappresentabili rifiutati; nessun supporto implicito |
| Rig cinematografici | dolly, truck/pedestal, pan/tilt, orbit attorno a pivot, crane e dolly-zoom | primitive CameraRig presenti; combinazioni e invarianti framing da certificare |
| Ottica | FOV/focale/sensore/zoom con precedenza unica, lens shift, near/far, aspect | contratto condiviso locale; confronti numerici e di proiezione ai limiti |
| Fuoco / DoF | ON/OFF, distanza lungo asse ottico, f-stop, rack focus, focus su target | DoF circolare presente; qualità depth/alpha/occlusioni da provare sulle fixture |
| Bokeh avanzato | lamelle e highlight | fuori matrice iniziale: rifiuto esplicito; implementazione eventuale in ticket separato |
| Layer 2.5D | immagini, testo/grafica, profondità e parallasse, opacity, alpha | campioni TRS/opacity risolti locali; niente claim su mesh/effetti non supportati |
| Tempo | easing non lineare, hold quando supportato, quaternion wrap, subframe, shutter phase/angle/samples | calendario comune locale 180°/8; altre configurazioni da censire e accettare/rifiutare |
| Sequencer | selezione camera attiva, cut senza interpolazione tra shot, intervalli | NOT CERTIFIED; censire API esistenti prima di aggiungerne |
| Determinismo | seek/reverse/random access, offline/provider equivalenti | gate locale piccolo presente; estendere al corpus integrato |
| Preview / export | stessa regia e stesso calendario, qualità esplicitamente diversa | nessun claim di pixel parity threepp/C3D senza modello ottico equivalente |

**Esclusioni iniziali:** importer `.aep`, espressioni AE arbitrarie, renderer Adobe identico, ogni effetto AE, materiali/ombre/mesh universali, iris poligonale e multipass cinematici non specificati. Il bokeh circolare supporta highlight weighting; capability non supportate falliscono prima del render con campo e motivo; nessun clamp/fallback silenzioso.

## 2.1 Censimento locale 2026-10-30

Le classificazioni seguenti riguardano il working tree dirty, non una release né una prova contro Adobe. Test ricompilati con `cmake --build ChrononMotion3D/build/release-fast --target chrononmotion_camera_v1 chrononmotion_camera_focus_sequence chrononmotion_camera_trajectory chrononmotion_camera_shake chrononmotion_camera_framing chrononmotion_camera_sequence chrononmotion_authoring chrononmotion_renderplan_easing_parity chrononmotion_projection_parity chrononmotion_camera_bake_roundtrip -j16` e 11/11 CTest PASS; log `../evidence/camera-closure-final/camera-inventory-tests.log`.

| Capacità | Stato locale e traccia |
|---|---|
| Camera libera/target, roll e parent | **PARTIAL:** `CameraRig.hpp` e `RenderPlanLoweringCamera.cpp`; `motion_camera_v1.cpp`, `motion_camera_bake_roundtrip.cpp`. Parent e look-at verificati per fixture, non catene arbitrarie, pivot, degenerazioni o confronto AE. |
| Dolly, pan, tilt, orbit, framing e traiettorie | **PARTIAL:** primitive `CameraRig.hpp`; `motion_camera_v1.cpp`, `motion_camera_trajectory.cpp`, `motion_camera_framing.cpp`; le combinazioni complete e dolly-zoom AE non sono certificate. |
| FOV/focale/ottica | **PARTIAL:** `camera_contract.hpp` è l'ammissione condivisa applicata ai due confini compiler (test dedicato, 2026-10-31); `RenderPlanLoweringCamera.cpp`, `motion_projection_parity.cpp` e `motion_authoring_renderplan.cpp`. Parità AE e limiti combinati ancora da verificare. |
| DoF e focus | **PARTIAL:** `motion_camera_focus_sequence.cpp`, `test_per_pixel_dof.cpp`, ROI/raw e integrazione software a due profondità con alpha 0,5 passano; fault injection strict-native e matrice completa alpha/depth restano aperte. |
| Iris blades / highlight weighting | **PARTIAL:** iris poligonale rifiutata; highlight weighting circolare ammesso e implementato nei kernel software/Vulkan. La parità integrata e l'equivalenza AE restano da provare. |
| Shutter e layer | **PARTIAL:** `camera_sample_provider.hpp`, `RenderPlanLowering.cpp`, `test_camera_motion.cpp`; test camera/layer e piano Optical Studio materializzato passano. Il job unico è ancora oggetto di diagnosi e la CLI non prova il callback provider live. |
| Camera sequence e cut | **PARTIAL:** `motion_camera_sequence.cpp` copre il componente Motion; nessuna certificazione di cut E2E su job C3D. |
| Bridge export | **IMPLEMENTATO, PARITÀ NON CERTIFICATA:** il percorso RenderPlan esporta FullGraph, DirectYUV e GOP da `CompiledProgram`; il bridge `make_video_compatibility_view` e il suo translation unit sono stati rimossi il 2026-10-31. Test raw/shutter e GOP/DirectYUV non eseguiti in questa passata. |

La prima esportazione MP4 sul percorso diretto ha trovato un gate residuo in
`validate_video_job`: richiedeva `CompiledComposition` anche quando il job aveva già il
`CompiledProgram` tipizzato. Il gate è stato corretto. Il 2026-10-31 il piano demo 2.5D è
stato renderizzato via RenderPlan diretto e letto con FFprobe: H.264, 1280×720, 30 fps,
150 frame / 5 s. Questo prova export e readback del demo, non la parità AE. G resta aperto
per le fixture/reference AE indipendenti e per le verifiche raw/shutter e GOP elencate sotto.


## 2.2 Tracciabilità del censimento A

Questa matrice chiude la ricognizione locale per l'intera superficie richiesta
dal punto A. `PARTIAL` significa che il percorso esiste e ha test locali, ma non
copre la matrice funzionale dichiarata; non equivale a supporto AE certificato.

| Famiglia | API / producer | Wire, compiler e consumer | Stato e prova locale |
|---|---|---|---|
| Camera libera e target | CM3D `CameraRig`; `RenderPlanLoweringCamera.cpp` risolve orientamento/look-at | `camera_samples[].camera`; C3D camera decoder → `CameraPlan` → projection resolver | `PARTIAL`; `motion_camera_v1`, `motion_projection_parity`, bake roundtrip |
| Parent e Null | gerarchia `MotionScene` + `CameraRig::setParent` | Motion campiona matrice world e abbassa pose risolte; C3D consuma camera/layer samples | `PARTIAL`; regressioni parent in `motion_camera_bake_roundtrip` e test rig |
| Traiettoria, rig e framing | `CameraTrajectory`, `CameraSequence`, `CameraFraming`, `CameraMotion` | applier CM3D → rig channels → camera samples; nessuna seconda evaluazione C3D | `PARTIAL`; camera trajectory/sequence/framing e `motion_authoring_renderplan` |
| Ottica e proiezione | `CameraRig` + `camera_contract.hpp` | schema V3 → decoder → contratto camera → projection/lens C3D | `PARTIAL`; decoder camera, contract e projection parity; reference AE e limiti combinati non presenti |
| Fuoco e DoF circolare | focus track CM3D → campi camera-level canonici | `f_stop`, focus, max blur; adapter → DoF node/kernel | `PARTIAL`; focus-sequence, DoF node, raw ROI e sovrapposizione di card opaca e alpha 0,5 in entrambi gli ordini passano in software; frame 30/75/149 byte-identici dopo la migrazione wire; matrice completa alpha, depth fault injection e strict-native restano aperti |
| Iris e highlight bokeh | campi camera-level CM3D/RenderPlan | `camera_contract_problem` rifiuta forma non supportata prima dell'esecuzione | `REJECTED`; test camera contract e DoF unsupported-shape |
| Layer 2.5D e profondità | layer/image/text/shape Motion + transform lowering | transform samples/track → scene projection, depth e compositing C3D | `PARTIAL`; authoring RenderPlan, canary e occlusione software di card sovrapposte in entrambi gli ordini passano; la matrice completa di alpha/depth e il gate strict-native non sono certificati |
| Tempo, subframe e shutter | Motion lowering/provider; scheduler shutter C3D | `camera_samples` e `transform_samples` con timestamp del calendario C3D | `PARTIAL`; bake roundtrip, temporal-surface e shutter tests; full fixture provider/offline non eseguita |
| Sequencer e camera attiva | `CameraSequence` esiste in CM3D, ma non c'è un consumer di selezione shot nel contratto RenderPlan | nessun campo/wire/compiler/consumer end-to-end per active-camera cut | `MISSING`; test Motion della sequence non prova un cut C3D |
| Preview/export tipizzato | piano tipizzato → preparazione CLI | FullGraph, DirectYUV e GOP consumano `CompiledProgram` direttamente | `PARTIAL`: export diretto implementato; parità raw/shutter, GOP e reference AE ancora da certificare |

La fonte file-by-file per gli header e gli include consumer rimane il
[censimento superficie camera](../evidence/camera-surface-consolidation/README.md);
questa tabella completa il collegamento ai confini wire e ai gate funzionali.
Non è stato usato un progetto/reference After Effects: il punto B resta aperto e
nessuna riga `PARTIAL` viene rinominata “parità AE”.


### 2.3 Regressione mirata DoF + occlusione 2.5D (2026-10-31)

La fixture sintetica `PR2-RG-DoF: nearest 2.5D card occludes the far plane in either layer order`
usa due piani colorati sovrapposti e verifica che la card più vicina resti davanti con DoF
attivo, invertendo l'ordine di authoring. I controlli ad alpha frazionario confrontano il
compositing nei due ordini e con DoF disattivato. Sono test del renderer 2.5D con geometria
procedurale semplice: non richiedono modelli 3D né asset esterni. Il binario locale
precompilato ha eseguito i 3 casi aggiunti: **3/3 casi, 21/21 asserzioni PASS**.
Quel binario è basato sul checkout locale `919ed8f5` (non sullo SHA finale del ticket); il
risultato è una regressione mirata, non certificazione same-SHA.

Questi test coprono ordine di profondità, alpha e interazione locale col pass DoF; non
certificano near/far bleeding su una griglia di fuoco, qualità del bokeh HDR o un pass AO.
Chronon3d non espone attualmente SSAO/HBAO/GTAO né occlusion culling come capability del
contratto camera. Nessun test di Ambient Occlusion va quindi dichiarato PASS: la sua
eventuale implementazione richiede un ticket/capability separata, con input depth/normal,
regole di bias e criteri immagine espliciti.


## 3. To-do ordinata e criteri di accettazione

- [x] **A — Censimento locale:** la matrice §2.1 e la tabella di tracciabilità §2.2 associano ogni famiglia ad API, producer, wire/schema, compiler, consumer e test, con stato `SUPPORTED` / `PARTIAL` / `REJECTED` / `MISSING`. Spazi e unità attestati dal contratto Motion→RenderPlan sono annotati; dove non esiste una semantica dichiarata (per esempio dolly-zoom AE e selezione camera attiva) resta `MISSING`, senza inferire compatibilità. Questo chiude l'inventario del codice locale, non certifica parità AE né sostituisce la reference numerica del punto B.
- [ ] **B — Reference funzionale:** versionare fixture e manifest numerici indipendenti; per le righe chiamate AE-parity acquisire riferimento AE documentato (versione/progetto/export/parametri). Senza riferimento usare “contratto Chronon verificato”, non “AE verificato”. Fissare tolleranze PRIMA del run.
- [ ] **C — Camera e Null:** certificare libera/target, parent a due livelli, pivot, rotazioni combinate, wrap ±180°, target coincidente/collinearità e pose invalide; test di snapshot e proiezione, errori espliciti per casi esclusi.
- [ ] **D — Ottica e regia:** dolly/orbit/pan/tilt/roll e dolly-zoom con invarianti numerici di framing; focale/FOV e focus animati; cut camera e confini shot senza interpolazione impropria.
- [ ] **E — Tempo unico:** consumer tipizzato di produzione, camera E layer agli stessi timestamp; 3 frame × 8 campioni minimo, easing non lineare, accesso fuori ordine, roundtrip double e raw deterministico. Phase/pattern/filter e budget devono essere trasportati oppure rifiutati se non rappresentabili.
- [ ] **F — DoF e alpha:** regressioni software mirate per ordine di occlusione 2.5D e alpha frazionario, inclusi i due ordini di authoring e controllo DoF OFF (**3/3 casi, 21/21 asserzioni PASS** sul binario precompilato locale `919ed8f5`; non same-SHA). Restano scene a tre profondità con metriche ROI di nitidezza, near/far bleeding, profondità attesa mancante end-to-end e confronto software oracle vs Vulkan strict-native entro soglie congelate; zero errori validation. Questi test non certificano AO/SSAO, non presente nel renderer.
- [ ] **G — Un solo percorso video:** portare l'export a consumare direttamente CompiledProgram senza view temporanea; confrontare raw, shutter e output rispetto alla baseline prima di rimuovere il ponte. Nessun nuovo executor.
- [ ] **H — Prestazioni misurate:** stesso hardware/driver/build, asset e qualità, 1080p30/150 frame minimo; warm/cold separati, almeno 5 run, mediana e dispersione per evaluation/compile/upload/render/DoF/shutter/readback/convert/encode. Registrare byte trasferiti, cache hit e peak VRAM. Definire budget dopo baseline, prima del fix; nessuna promessa AE-speed senza benchmark AE equivalente.
- [ ] **I — Delivery e certificazione:** video 1080p MP4, sentinel raw e decoded, manifest/digest, comandi ripetibili, CI verde sul pair preciso di revisioni e approvazione umana dell'intera sequenza. Upload non equivale a qualità accettata.
- [ ] **J — Rimozione e anti-resurrezione:** eliminare codice/bridge effettivamente sostituiti solo dopo equivalenza; gate architetturale che impedisce regia in C3D. Report finale riga per riga, senza estendere il perimetro dopo il PASS.

## 4. Demolition debt: view video di compatibilità

- **Owner:** manutentore compiler/video Chronon3d.
- **Reason:** il bridge è stato rimosso; manca ancora la certificazione di equivalenza per i consumer video tipizzati.
- **Exit condition:** tutti i job di accettazione video usano direttamente il programma tipizzato, senza conversione di view, con equivalenza raw e shutter e output contrattuale verificati.
- **Equivalence gate:** §3 E/F/G/I, fixture identiche e soglie congelate, software e strict-native, seek e ordine inverso.
- **Removal scope:** call site, adapter, translation unit e test esclusivamente del ponte sono stati rimossi; restano i gate di equivalenza e le API legacy non RenderPlan.
- **Status:** ACTIVE — codice bridge rimosso, gate di equivalenza ancora NOT RUN.

**Gate storico prima della rimozione del bridge (2026-10-31):** `tests/renderer/camera/test_camera_motion.cpp`, test
`Video compatibility view matches typed program raw camera and shutter frames`.
Una RenderPlan sintetica con due card 2.5D e camera campionata ai frame 0 e 2
usa shutter 180°/4; su ciascuno dei frame 0/1/2 il raw framebuffer del
`CompiledProgram` coincide byte-per-pixel con quello di
`make_video_compatibility_view` eseguito via `render_compiled`. Il test non
esercitava mux/encoder; il bridge è stato successivamente rimosso e la parità del percorso diretto resta da certificare.

## 5. Dipendenze e regola di chiusura

La chiusura attuale resta in CAMERA-DEFINITIVE-CLOSURE §8. Questo ticket non azzera i suoi gate e non riapre feature già certificate per nuove richieste estetiche. Review umana, riferimento AE e CI same-SHA hanno owner da assegnare e data di sblocco da concordare: finché assenti sono BLOCKED, mai PASS.

Il ticket chiude solo con matrice approvata, tutti i gate obbligatori PASS sul medesimo pair di revisioni, rimozioni verificate e benchmark pubblicati senza regressioni rispetto ai budget congelati. L'alternativa plate GPU richiede una decisione esplicita separata.
