# TICKET-CAMERA-DEFINITIVE-CLOSURE — Chiusura Definitiva Pipeline Camera (Chronon3d + ChrononMotion3D)

- **Stato**: **PARTIAL — closeout gate definito, non ancora superato** (checkpoint 2026-11-01). Le prove locali già elencate restano valide solo per il loro ambito; non equivalgono al PASS end-to-end della sezione 8. Aperture confermate: occlusione trasparente per-pixel, parità temporale camera/layer end-to-end, accumulo shutter strict-native GPU, accettazione 1080p, validazione Vulkan sul run completo e CI same-SHA. La capability validation condivisa è atterrata il 2026-10-31 (`camera_contract.hpp` in C3D, `a67934e` in Motion); resta la certificazione E2E.
- **Checkpoint locale 2026-10-30:** [report camera-closure-final](../evidence/camera-closure-final/README.md): camera 82/82, Motion 3/3, architettura 6/6 e nodo DoF 5/5; raw software/Vulkan e rack-focus ROI PASS. Corrette precisione double dei timestamp layer e copia DoF verso intermedi shutter. Due MP4 1080p caricati; shutter unico FAIL exit 139 dopo 75 frame, delivery segmentata in corso (workaround, non fix). Nessuna chiusura same-SHA/umana implicita. L'espansione funzionale AE-style ha un [ticket separato](TICKET-AE-CAMERA-FUNCTIONAL-PARITY.md).
- **Checkpoint locale 2026-10-31:** contratto unico di ammissione camera/ottica applicato ai due confini producer/compiler (test `Camera capability contract rejects unsupported optics at both compiler boundaries`), provider runtime passato al compiler canonico e risoluzione camera/layer sul calendario shutter; pubblicato anche il timing GPU per pass. I gate §8 restano aperti: la suite camera 82/82 e Motion 3/3 sono del 2026-10-30, il job shutter completo è ancora FAIL e la CI same-SHA è BLOCKED.
- **Priorità**: P0
- **Repository**: `ChrononMotion3D`, `Chronon3d`
- **Build**: parallelismo limitato alla memoria disponibile; non sovrapporre build pesanti senza tener conto del totale dei job.

### Avanzamento locale 2026-11-01 — fasi proiezione/focale

- **Fase 0 PASS:** la fixture della card 200×150 inclinata confronta i quattro angoli proiettati dal contratto camera con la matrice consumata dal render graph. Prima della migrazione al valore omografico il test falliva; dopo la migrazione passa.
- **Fase 1 PASS:** `ProjectedLayer2_5D` non espone più il TRS ricostruito dalla bbox; il placement usa la matrice omografica. Rimosso l'overload produttivo a 5 argomenti. Verifica: `chronon3d_core_tests` filtro camera **15 casi / 49 asserzioni PASS**, `chronon3d_renderer_core_tests` **204/204 casi e 74.269/74.269 asserzioni PASS**, `chronon3d_render_graph_tests` dalla root C3D **445/445 casi e 19.543/19.543 asserzioni PASS**.
- **Fase 2 PASS localmente:** rimosso `focal_length_from_fov`, separato il factory di proiezione 3D nativa da quello camera/layer, allineati i default FOV a 50°, introdotto `sensor_height_mm` nel RenderPlan e nel bridge CameraRig Motion; il test numerico verifica `fx == fy` su gate quadrato e `fx/fy == aspect` in Stretch. Motion authoring **1/1 PASS**, rig canary **1/1 PASS**, bridge Motion→RenderPlan C3D **10/10 casi e 160/160 asserzioni PASS**.
- **Fase 3 PASS locale — stato aggiornato 2026-11-01:** oltre ai cambi già elencati, il modello legacy gain/aperture è stato rimosso dai consumatori attivi; il thin-lens usa `f_stop`, focale, focus, sensore e limiti canonici; fingerprint e chiavi cache includono la semantica ottica aggiornata. Vedi il checkpoint F0–F3 più recente in fondo al ticket.
- Il build C3D è stato eseguito con `-j12` e ccache attivo (cache preesistente 10.149 hit). Il render graph è stato lanciato dalla root del repository per risolvere gli asset fixture; un primo avvio dalla build dir è stato scartato perché mancavano gli asset relativi.
- Questi PASS non chiudono il ticket: restano DoF per-layer, kernel bokeh, validazione/lifecycle Vulkan asincrona, acceptance 1080p, parity provider/runtime Motion→renderer, CI same-SHA e review. Il checkpoint storico del 2026-11-01 su software non certificava il percorso Vulkan, poi riaperto dai run nativi.

---

## 1. Problemi reali identificati (audit 2026-10-29) e risoluzione

| # | Repo | Problema | Fix | Verifica |
|---|------|----------|-----|----------|
| 1 | ChrononMotion3D | `CameraRig` con `parentId != kRoot` rifiutato in `lowerToRenderPlan`; `sample()` valutava solo la posa locale | `CameraRig::resolveWorldPose` / `sampleWorld` (compone la posa con la world matrix del parent); `RenderPlanLowering.cpp` la risolve per frame | `ctest`: **68/68 PASS** |
| 2 | Chronon3d | Sub-frame `camera_samples` interpolati con `glm::mix` sugli angoli di Eulero | `glm::slerp` sui quaternioni + `math::camera_rotation_euler` (`camera_pose.hpp`, `render_plan_compiler_animation_utils.cpp`) | `chronon3d_camera_tests`: **75/75 PASS** (65.807 asserzioni), test slerp attraverso il wrap |
| 3 | Chronon3d | Halo / color bleeding sui cutout nel DoF (gather pass pesava solo distanza e alpha, non CoC/profondità) | Peso depth/CoC-aware: `compute_pair_weight` (CPU Highway) e `dof_pair_weight` (`dof_blur.comp`) | Test `test_per_pixel_dof.cpp` + verifica visiva sul render del showcase (nessun alone attorno al soggetto) |
| 4 | Chronon3d | Ticket obsoleti (T05 shutter, SHADOW-NATIVE-CATCHER) | Allineati: il confronto RAW RGBA8 è 150/150 PASS; il fallimento era solo nel confronto MP4 lossy | vedi `FOLLOWUP_TICKETS.md` |

### Estensioni ChrononMotion3D necessarie al showcase
- `RenderPlanLoweringSamples.cpp`: `dof_focus_distance == 0` ammesso quando il DoF è spento; ammessi `LayerType::Adjustment` e `LayerType::Particles`.
- `RenderPlanEmission*.cpp`: emissione V3 per `Adjustment` (solo `{id,type,effects}`, come richiede lo schema) e per gli effetti `gaussian_blur`, `drop_shadow`, `chromatic_aberration`, `lens_distortion`.
- `tests/check_renderplan_camera_parity.cmake`: include `render_plan_camera_adapter.hpp` (`make_camera_value` è stato spostato lì).

---

## 2. Showcase documentario end-to-end (`ChrononMotion3D/tools/plan_documentary_showcase.cpp`)

Pipeline reale: `MotionScene → lowerToRenderPlan (CameraRig con parent) → renderPlanToJson → chronon3d_cli render --plan`.

1. **Carta + testo serif + evidenziatore**: placca carta con noise 2,8% e blur 0,8 px; testo FreeSerif con blur 0,6 px; barre ciano `#00A8FF` in **Multiply** con **Trim Paths** 0→100% (`out_expo`), sfalsate per riga; Null con scala 100→105% e drift.
2. **Cutout + DoF + polvere**: soggetto con drop shadow; rack focus dal documento al soggetto con diaframma che si apre (max blur 28 px); particelle in **Screen**; sfondo che si abbassa di opacità (0,52) all'ingresso del soggetto.
3. **Pop-up**: badge "MB" e tag "$10,000" con drop shadow + glow, `scaleOvershoot`, sfasati di 7 frame.
4. **Look cinema**: layer `Adjustment` in cima con `lens_distortion` (barrel), `chromatic_aberration` ±1,6 px e `vignette`.

**Verifica visiva** (frame estratti dal video codificato, non dalla lane PNG): frame 40 (stage 1 + 4: carta, serif, barre ciano Multiply, aberrazione, vignetta); frame 100 (stage 2 + 3 + 4: soggetto nitido senza alone, sfondo sfocato e scurito, badge e tag con glow, polvere).

---

## 3. Scoperte tecniche (cause radice trovate durante il showcase)

1. **Lane di render**: `--backend auto` sceglie Vulkan ibrido e impiega ~34 s/frame (121 frame ≈ 68 min) con frame corrotti (metà nera / metà verde). La lane valida è `--backend software --encoder-backend pipe --codec h264_nvenc --pipe-pixfmt nv12`.
2. **Path nativo `h264_nvenc` con backend software**: con `--encoder-backend native` (default) il frame esce con **piani YUV errati** (metà sinistra rosa, destra verde). Con `--encoder-backend pipe --pipe-pixfmt nv12` i colori sono corretti. Causa non ancora diagnosticata (follow-up F1).
3. **`--assets-root` deve essere un percorso assoluto** (`AssetResolver::mount`), altrimenti il job fallisce subito.
4. **La lane PNG non è pixel-truth** (avviso del CLI): non compone i layer di testo. Mai usarla per giudicare un frame.
5. **Convenzione Z incoerente nel lowering** (`RenderPlanLoweringSamples.cpp`): `planPosition` NON inverte la Z per `Text/Shape/Particles/Light`, mentre `planAnimationPosition` (track `position`, che sovrascrive la posizione base) la inverte per tutti. Effetto: un testo a Z motion negativa finisce **dietro** la carta. Workaround nel showcase: testo/barre a Z motion positiva (+1, +2, +3).
6. **Ottica**: se un `CameraRig` imposta sia `fov` sia `focal_length` il lowering sceglie `PhysicalLens` e il FOV viene ignorato. Con 50 mm su sensore 36 mm lo zoom era ~2,5× più stretto del previsto; nel showcase `setFocalLength(20.f)`.
7. **Y**: nel path 2.5D l'asse Y delle posizioni Motion è Y-up; le righe del showcase usano `-y`.

---

## 4. Stato dei criteri di chiusura

- [x] Step 1 — parenting `CameraRig` (68/68 CTest)
- [x] Step 2 — slerp sub-frame (75/75, 65.807 asserzioni)
- [x] Step 3 — DoF depth/CoC-aware CPU + Vulkan, senza alone
- [x] Step 4 — ticket/documenti allineati
- [~] Showcase completo 121 frame: render completo lanciato in background (`Chronon3d/output/documentary_showcase/full_render.log`). **Frame chiave verificati**; il video intero va controllato a fine render. Il costo per frame dipende dallo shot: la misura DoF della lane nativa è nella F2.

---

## 5. Follow-up aperti (non nascosti)

- [x] **F1 — Path nativo NVENC con backend software — RISOLTO 2026-10-30.** La staging CPU era `AV_PIX_FMT_YUV420P` mentre il frames context CUDA è NV12: `av_hwframe_transfer_data` copiava U/V planari nel piano UV interlacciato, producendo metà rosa / metà verde. Fix in `native_av_encoder_write.cpp`: staging NV12 (Y + UV interlacciato, `stride_uv = linesize[1]`) per l'output SDR; HDR/10-bit resta planare. Verifica: frame 0 del piano T05 produzione, backend software + NVENC nativo → mean RGB **53,5169** (identico al gate T05 caster-on), nessuna inversione di piani, `execution_path=full_graph_native`; MAE 1,5908/p99 95 contro il riferimento RGB storico (residuo di range/encoding, non il difetto F1). Evidenza: `docs/evidence/closeout-20261030/`.
- [x] **F2 — Prestazioni del software renderer con DoF — CHIUSO COME SCELTA DI LANE.** Stesso piano, 2 frame: software 1.706,57 ms vs Vulkan strict-native 304,66 ms (853,24 vs 152,30 ms/frame, **5,60×**), nessun fallback; parità pixel MAE **0,4424**, p99 **3**, RGB-L1 >8 sullo **0,0084%** dei canali. La lane nativa è la lane di produzione per il DoF; il costo del fallback software resta documentato e non è un blocco camera. Evidenza: `docs/evidence/closeout-20261030/`.
- **F3 — Z lowering e ordine 2.5D, RISOLTO LOCALMENTE 2026-10-31:** `planPosition` e `planAnimationPosition` applicano la stessa conversione Z; il canary ripetibile Motion→RenderPlan→C3D verifica la card PNG rossa davanti al frame 0 e dietro al frame 1 dopo l'attraversamento Z (pixel raw e digest in `docs/evidence/camera-surface-consolidation/camera-f3-depth-order.json`). La nota storica sull'highlighter T16 è fuori dalla scena target corrente, Optical Studio; non è un residuo del lowering Z.

---

## 6. Audit camera 2026-10-29 — verifiche 2026-10-30

| Punto audit | Stato 2026-10-30 | Evidenza |
|---|---|---|
| Provider runtime con look-at/roll fallisce | **RISOLTO** | `makeCameraSampleProvider` azzera `hasTarget`/`roll` dopo `CameraRig::sample`; `lowerCameraPose` continua a rifiutare le pose non risolte. Test `chrononmotion_camera_bake_roundtrip`: **185 check / 0 failure**, inclusi look-at+roll e sub-frame 15.5. |
| Interpolazione Eulero ±180° | **GIÀ RISOLTA** | `render_plan_compiler_animation_utils.cpp` usa `glm::slerp` + `camera_rotation_euler`, non `glm::mix`. |
| Contratto sub-frame camera/layer | **PARTIAL** | Il provider runtime CM3D supporta gerarchie parented; il bake può ricevere la lista dei timestamp dallo scheduler C3D e serializzarli senza perdita di precisione double. Il default resta a frame interi, i job non passano ancora il calendario e i layer rimangono campionati ai frame interi: la parità end-to-end non è dimostrata. |
| DoF senza profondità | **PARTIAL** | Il working tree rifiuta la profondità CPU mancante o malformata se il tracking è richiesto; test unitario mirato PASS. Mancano la verifica end-to-end del contratto sorgenti e la copertura native. |
| FOV/lente/DoF capability | **RISOLTO LOCALMENTE (2026-10-31)** | Contratto condiviso `render_plan/camera_contract.hpp` applicato ai due confini compiler (test dedicato) e importato dal lowering Motion (`a67934e`); iris/highlight sono rifiutati con errore tipizzato. Resta la certificazione end-to-end §8, non la validazione in sé. |
| Vulkan validation | **APERTO** | Gli errori layout GENERAL/UNDEFINED del 22-10 vanno rilanciati sulla build corrente. |
| CI same-SHA | **APERTO** | CI Chronon3D/Motion3D non verde sullo SHA corrente; nessuna chiusura ufficiale. |
| Video showcase C3D/T16 | **CARICATI** | 3 MP4 1920×1080/30 fps/150 frame, strict Vulkan/NVENC, frame 0 non nero, zero black interval. Link e SHA in `docs/evidence/camera-audit-20261030/README.md`. |
| Video CM3D/threepp | **CARICATI** | T18 crowd orbit, T17 product orbit + focus, T10 DoF; 1920×1080/30/150, pipeline MotionSceneBridge → threepp GL → PBO → CUDA → NVENC. Link in `docs/evidence/cm3d-camera-renders-20261030/README.md`. |

La camera non è quindi “chiusa una volta per tutte”: il bug concreto look-at/roll è chiuso e l'Eulero era già corretto, ma i gate di contratto sub-frame, DoF fail-closed end-to-end, capability validation, validazione Vulkan e CI restano aperti. Non serve una terza camera.

I render CM3D/threepp (T18/T17/T10) mostrano camera, luci/ombre e DoF nel renderer di prova CM3D. La migrazione desiderata è **plate 3D CM3D** + **compositing text/SVG C3D**, ma `ChrononMotion3D/tests/check_render_scope.cmake` definisce ancora `render/` come harness, non renderer di produzione. Il gate del plate RGBA/alpha GPU-native resta aperto (T13 animated alpha PARTIAL). Non rimuovere il DoF C3D prima di avere un sostituto verificato e compatibile.

## 7. Avanzamento migrazione CM3D — working tree 2026-10-30

- **PASS locale:** provider Motion per camera parented, con `MotionScene` dedicata, tempo frazionario e fingerprint fornito dal chiamante; l'overload senza scena e il bake standalone rifiutano rig parented. Corretto anche il doppio roll in `CameraRig::resolveWorldPose`. `chronontemplate_plan_lowering_test` verifica lo snapshot della gerarchia e l'accesso dopo la distruzione della scena originale; le scene non rappresentabili dal formato progetto sono rifiutate.
- **PASS locale:** bake di camera agli istanti espliciti e round-trip JSON `double`; `chrononmotion_camera_bake_roundtrip` confronta provider e bake su tre frame con otto tempi stratificati generati dallo scheduler C3D, look-at, roll, FOV e focus animati. Rimangono da collegare il calendario al job e da verificare il consumer e i layer end-to-end.
- **PASS locale circoscritto:** `chrononmotion_render_pixel_certification` verifica che `BokehPass` conservi uno sfondo trasparente, il soggetto e alpha frazionario ai bordi. Non certifica colorimetria, occlusioni o trasferimento GPU-native.
- **PASS locale circoscritto:** `chronontemplate_plan_lowering_test` verifica l'overload offline che inoltra al bake Motion i tempi espliciti forniti dal job: 8 tempi shutter C3D su tre frame, camera parented, confronto con provider runtime e rifiuto di liste vuote o duplicate. Non genera automaticamente il calendario del job, non prova la serializzazione fino al renderer e non modifica i layer.
- **PASS locale, working tree 2026-10-30:** `chronon3d_camera_tests` ricompilato e passato (78 casi, 65.937 asserzioni); include il confronto offline/provider ai tempi shutter sulla view di compatibilità, e i nuovi test di campionamento e rendering della camera nel percorso tipizzato. Successivamente il test aggiunto `Typed program temporal accumulation uses camera shutter samples` passa isolato (1 caso, 8 asserzioni): stesso frame centrale, camera animata contro camera ferma, 8 campioni e ripetizione deterministica. Non prova parità pixel offline/provider o layer end-to-end.
- **PASS locale circoscritto, working tree 2026-10-30:** `chrononmotion_camera_v1`, `chrononmotion_authoring`, `chrononmotion_camera_bake_roundtrip`, `chrononmotion_renderplan_camera_parity` e `chrononmotion_render_pixel_certification` passano (5/5 CTest). Il bake confronta 688 check su parent, look-at/roll, timestamp shutter e JSON; l'authoring include la regressione Z per Text/Shape/Particles. Rimangono da verificare l'evidenziatore e l'ordine/profondità della scena target.
- **PASS locale circoscritto:** il test `DOF rejects missing tracked depth but permits untracked passthrough` passa (1 caso, 4 asserzioni) per buffer CPU mancante/malformato con tracking richiesto; non certifica ogni combinazione di sorgenti o il percorso native.
- **PARTIAL — percorso tipizzato:** le modifiche locali compilano `camera_samples` nei metadati e li campionano in `RenderPipeline::render_program`, anche durante lo shutter. Il provider runtime non è collegato a questo percorso e la parità end-to-end, inclusi layer, job e accesso fuori ordine, resta da verificare. La view di compatibilità video usa invece `make_camera_evaluator`.
- **APERTO:** promuovere/certificare il renderer CM3D come componente di produzione, integrare pass DoF con plate animato alpha e compositing C3D GPU-native, collegare il calendario effettivo e garantire parità sub-frame per i layer, quindi ripetere Vulkan validation ed E2E/CI sullo SHA finale. Le modifiche non sono ancora committate: nessuna certificazione same-SHA è implicita.

---

## 7.1 Nuova scena originale — optical studio 2026-10-30

La scena `ChrononMotion3D/tools/plan_optical_studio.cpp`, con grafica generata da `tools/generate_optical_studio_assets.py`, sostituisce T16/MrBeast e T18/folla come obiettivo visivo camera. Le due vecchie fixture T16/CAM-04 restano compilabili manualmente, ma non sono più registrate come test CTest: la loro copertura non è automaticamente sostituita né certificata.

- Video diagnostico [Optical Studio 1080p](https://drive.google.com/file/d/1iI2WOkUDYd2EWEk06THIZFfFYxxXACd9/view?usp=drivesdk), 1920×1080, 30 fps, 150 frame, SHA-256 `11192a637cf97b35d8d0feee2c4e380a80da934b097c831a28a4e3adee0638b3`. Pipeline MotionScene con camera parented → lowering V3 → Chronon3D Vulkan/NVENC; nei frame decodificati 0/75/149 il fuoco passa tra i tre piani. Decodifica completa e nessun intervallo nero rilevato.
- **FAIL/PARTIAL:** il fondale appare isolato, ma scompare nella composizione a quattro piani; riprodotto con DoF disattivato e con backend software e Vulkan. La causa non è diagnosticata. I tentativi di modificare ordine, dimensione e tracce non risolvono. Non è un PASS di occlusione/alpha, né di parità sub-frame o CI.
- **DoF strict-native, fix locale 2026-10-31:** se il tracking è richiesto, un buffer di profondità solo CPU non è ammesso sulla lane GPU obbligatoria, nemmeno quando non contiene sorgenti di blur visibili. `DOF rejects*,PR2-Unit-DoF*`: 6 casi / 29 asserzioni PASS. Resta aperta la fault injection end-to-end native e la certificazione sulla scena completa.
- **Diagnosi locale successiva, 2026-10-31:** con piani Optical Studio rigenerati dal working tree Motion, renderer software, DoF spento e raw RGBA al frame 75, il fondale isolato è identico in 907.958 pixel su 2.073.600 nella composizione completa a 1920×1080 (99.213 su 230.400 a 640×360). La stessa diagnosi al frame 75 con backend Vulkan e validation abilitata (sink raw, non strict-native video) conserva 908.605 pixel RGB identici, senza errori Vulkan rilevati nel log. Il fondale quindi non scompare completamente in questi casi; la differenza rispetto alla riproduzione precedente non è ancora spiegata. `Camera projection preserves oversized image pixels outside the source canvas` copre anche un foreground con alpha frazionario (19 asserzioni PASS, software). Non prova gli altri frame, shutter, strict-native video, ordine di profondità o qualità dei bordi; il gate rimane PARTIAL.
- **Confronto piani materializzati, 2026-10-31:** la fixture shutter 1080p rigenerata offline e nella variante `provider` produce piani JSON equivalenti come valori dopo il parsing, inclusi 1.350 campioni camera. La variante provider sostituisce solo i campioni camera nel piano già compilato: non verifica provider live o campionamento layer indipendente.
- **Optical Studio integrato 320×180 con shutter, 2026-10-31:** ricompilati `chrononmotion_plan_optical_studio` e `chronon3d_cli` (`-j12`); i piani shutter 180°/8 offline e provider materializzato coincidono dopo parsing (1.350 campioni camera e 1.350 per ciascuno dei quattro layer immagine). Sei job Vulkan raw separati, frame 0/75/149, validation attiva: raw offline/provider bit-identici per ogni frame, nessun errore validation nel log. SHA-256 RGBA8: frame 0 `2255b1b079a647b723621211e9eb9b11ac636fb01939493f900a8906ed53d12d`; frame 75 `975cdd4e817552ae388bf4db814d817827a838072015b418f8e24d5ae58206e9`; frame 149 `1cc2849cd3a4478869e0cd15211659639a6d7204f17dae2911e130beb8fea59d`. Comandi: `chrononmotion_plan_optical_studio PLAN docs/evidence/camera-closure-final/assets 320 shutter offline|provider`, poi `CHRONON3D_VULKAN_VALIDATION=1 chronon3d_cli render --plan PLAN --assets-root "$PWD" --backend vulkan --video-sink raw --pipe-pixfmt rgba --encoder-backend pipe --start-frame N --end-frame N -o OUTPUT.mp4`. La variante provider modifica solo campioni camera nel piano offline, non usa callback live durante il job; il sink raw non certifica strict-native video, la sequenza completa o i gate 1080p. Non contiene mesh incrociata né certifica alpha/DoF per tale caso.
- **Shutter strict-native a risoluzione ridotta, 2026-10-31:** sulla build finale della CLI il piano offline Optical Studio 320×180 ha completato un unico job Vulkan/NVENC con validation attiva, `--gpu-hot-path-mode require_gpu_native --hardware nvenc --encoder-backend native`: exit 0, H.264, 150 frame a 30 fps, decodifica ffmpeg senza errori, zero intervalli blackdetect, zero errori Vulkan segnalati; SHA-256 `7c9444cac97dd02ea3801069a84e57d4650bfabdc1d7f7ce857149707a79b3d9`. Il log contiene 1.200 warning `detached reused extent` (non errori validation); restano da valutare. Il precedente job 1080p exit 139 non è corretto né certificato da questo run 320×180, e non è approvazione visiva umana.
- **Diagnosi 2.5D locale:** un `Null` fra due layer 3D interrompeva il gruppo di ordinamento per profondità; una regressione software con il layer vicino inserito per primo riproduceva il piano lontano sopra quello vicino. `LayerPipelinePass` ora lascia il gruppo aperto per i Null non renderizzati; `Camera 2.5D: a non-rendering Null does not break depth sorting` verifica pixel, inversione della profondità e ritorno al frame precedente. La selezione di 10 test camera/ordine passa con 43 asserzioni. Non è una certificazione Vulkan o E2E Motion → video.
- **Optical Studio, diagnosi raw software limitata:** la fixture sharp 640×360 rigenerata è stata renderizzata con 1, 2, 3 e 4 piani ai frame 0/75/149 prima della correzione del Null. Il background isolato conserva rispettivamente 98.134, 99.213 e 102.094 pixel RGB identici nella composizione completa; ciascun piano aggiunto modifica dei pixel. Non dimostra occlusioni corrette né spiega la riproduzione storica del fondale mancante.
- **Mesh, prove software limitate:** il canary di estrusione e il test della texture con alpha passano (2 test, 9 asserzioni); non certificano occlusione per-pixel mesh/piani, DoF integrato, Vulkan o parità AE.
- **Correzione della fixture mesh/piano:** il canary originario usava `fill(Color::red())`, che aggiunge un rettangolo e non colora la mesh; i pixel rossi isolati non dimostravano quindi la copertura della mesh. La fixture corrente colora direttamente il nodo mesh, posiziona il triangolo nello spazio canvas e verifica entrambi gli ordini e l'alpha frazionario. La mesh isolata copre entrambi i probe; il compositing continua a fallire. La correzione locale della premoltiplicazione alpha delle mesh software non risolve l'occlusione per pixel.
- **FAIL — mesh che attraversa un piano 2.5D:** il test `software 3D mesh crossing a 2.5D plane uses per-pixel depth` riproduce l'occlusione errata: la fixture corretta verifica che il triangolo rosso isolato copra entrambi i punti di prova; nel composito il pixel vicino risulta blu invece che rosso, oppure il pixel lontano rosso invece che blu, a seconda dell'ordine dei layer. Riproduzione: `build/chronon/linux-video-test/tests/chronon3d_renderer_core_tests --test-case='software 3D mesh crossing a 2.5D plane uses per-pixel depth'`. Il test resta **fallito**, non è una certificazione né un blocco fittiziamente chiuso. Su fixture RenderPlan separate 64×64, sia software sia Vulkan raw restituiscono tutta la mesh davanti oppure tutta dietro al piano invertendo l'ordine dei layer: la profondità interna dei triangoli non viene portata fino al compositing tra layer. Le quattro prove Vulkan strict-native video 320×320 hanno mostrato lo stesso limite, senza errori validation nel log. Servono superfici di profondità per mesh e piani, compositing depth-aware coerente con alpha/ordine 2D/maschere/effetti e DoF, e regressioni su entrambi i backend e sull'ordine inverso; il solo ordinamento per Z del layer non risolve una intersezione.
- **Mesh Vulkan raw, fix locale:** `SourceNode` usa il rasterizzatore inline-mesh nativo anche se la policy non richiede GPU strict, quando il backend supporta superfici native. Prima il caso isolato perdeva la mesh e registrava `InvalidImageLayout` (GENERAL/UNDEFINED); dopo il fix le quattro fixture raw con validation non riportano quell'errore e la mesh isolata ha 931 pixel rossi, contro 930 software (una differenza di copertura). Non è prova di occlusione corretta né di tutta la matrice Vulkan. Regressione dedicata e test raster UV/alpha: `chronon3d_backend_registry_tests --test-case='SourceNode renders inline mesh on Vulkan without strict native policy,Vulkan inline mesh rasterizes indexed UV triangles with alpha cutoff and source-over'`: **2 casi / 31 asserzioni PASS** dopo la correzione del setup camera del test.
- **Verifiche mirate dopo il fix clipping:** `chronon3d_camera_tests` sui casi typed shutter e immagine oversized: 2 casi / 176 asserzioni PASS; `chronon3d_render_graph_tests` sui casi `DOF rejects*,PR2-Unit-DoF*`: 6 / 29 PASS; 3 test mask rettangolare/invertita/testo: 3 / 10 PASS; test software texture alpha e canary estrusione: 2 / 9 PASS. Non provano maschere o DoF con occlusione per-pixel mesh/piano, che non è ancora implementata.
- **Mesh Vulkan, clipping near/far:** il percorso inline eliminava un intero triangolo se un suo vertice era fuori dalla profondità ammessa; il software ne ritaglia invece la porzione visibile. Il fix locale ritaglia nello spazio camera, interpola UV e luce ai nuovi vertici e triangola il risultato. La regressione Vulkan copre triangoli interni, attraversamento near e far, triangoli interamente fuori, confronto copertura con il raster software (massimo 32 pixel differenti su 64×64, soglia fissata nel test) e raster UV/alpha, in policy strict e non strict: **2 casi / 108 asserzioni PASS**. Prima del fix gli attraversamenti near e far fallivano entrambi. Non è una correzione dell'occlusione fra layer o prova di UV/luce dopo clipping con texture variabile.
- Il test `chrononmotion_t16_camera_truth` e il consumer `chrononmotion_cam04_renderplan_pixels` sono rimossi da CTest, non cancellati: occorre sostituire le loro verifiche automatiche con equivalenti sulla nuova fixture prima della chiusura.

---

## 8. Definition of Done definitiva — gate vincolante per chiudere questo ticket

Questa sezione è il criterio finale di chiusura. Le verifiche e i PASS locali delle sezioni precedenti sono evidenza parziale: **non spuntano automaticamente nessun gate qui sotto**. Il ticket resta aperto finché tutti i gate obbligatori sono `PASS` sul pair di revisioni dichiarato. `NOT RUN`, `BLOCKED`, demo caricata o processo terminato con exit code 0 non sono PASS.

### 8.1 Contratto e autorità unici

- [ ] Registrare nel contratto Motion → RenderPlan le unità e convenzioni effettive per coordinate/spazi (local/world e parent), asse Z, rotazioni, precedenza FOV/focale, focus distance, apertura e profondità dei layer. Motion risolve gerarchia, traiettorie, look-at, roll, easing e valori ottici; Chronon3D proietta e renderizza i valori ricevuti, senza ricostruire la regia.
- [x] F3: base e traccia animata usano la stessa conversione Z; il canary image-card Motion→RenderPlan→C3D verifica sample/bounds e ordine di profondità invertito tra frame 0/1. Evidenza: `docs/evidence/camera-surface-consolidation/camera-f3-depth-order.json`.
- [ ] Aggiungere una matrice di capability camera/DoF condivisa dal lowering Motion e dal consumer Chronon3D: ogni parametro supportato viene consumato; ogni combinazione non supportata (inclusi iris blades/highlight gain se non implementati) viene rifiutata con errore tipizzato prima del render. Nessun valore accettato e poi ignorato, clamp silenzioso o fallback non dichiarato.
- [ ] Verificare i limiti FOV/lente/focus una sola volta alla frontiera, con test sui limiti e sui valori non finiti; Motion e Chronon3D devono accettare/rifiutare lo stesso payload.

### 8.2 Sampling temporale comune per camera, layer e shutter

- [ ] Il job Chronon3D genera il calendario dei tempi effettivi richiesti dallo shutter e lo inoltra al provider/bake Motion. Per ogni timestamp Motion valuta camera **e layer**; il RenderPlan conserva i campioni con precisione sufficiente. Non è ammesso sostituire un easing authored con interpolazione lineare fra soli frame interi quando si dichiara equivalenza esatta.
- [ ] Confrontare snapshot offline serializzati e provider runtime agli stessi timestamp: almeno 3 frame (inizio, interno, fine), tutti gli 8 sottocampioni shutter del profilo di accettazione e accesso fuori ordine. Includere parent/Null, look-at, roll, rotazione attraverso ±180°, FOV/focale, focus, trasformazione/opacity dei layer e un easing non lineare. Le tolleranze numeriche sono dichiarate e congelate prima del run; non possono essere allargate per far passare il test.
- [ ] Eseguire gli stessi casi tramite il percorso tipizzato usato dai job di produzione, non soltanto la view di compatibilità o un helper isolato. Un payload non in grado di rappresentare quei tempi deve essere rifiutato o marcato esplicitamente approssimato; non può presentarsi come esatto.
- [ ] Sullo stesso backend, il rendering sequenziale e random-access dei frame di accettazione produce gli stessi snapshot e gli stessi pixel raw (bit-identici se il backend è deterministico; altrimenti entro una soglia numerica fissata e motivata prima del test). Verificare separatamente accumulo shutter raw e ordine dei campioni.

#### Checkpoint locale 2026-11-01 — calendario e percorso tipizzato

- Nel working tree il compiler rifiuta campioni camera serializzati mancanti quando il piano shutter contiene layer risolti; i piani legacy senza bake layer mantengono la precedente interpolazione. Il renderer tipizzato controlla inoltre gli istanti shutter effettivi rispetto ai campioni camera/layer prima dell'accumulo: un override incompatibile di phase, pattern o seed non deve interpolare silenziosamente il bake esatto.
- Verifiche parziali: smoke isolato del calendario, 27 istanti, PASS; controlli sintattici C++ su renderer e test, PASS. Dopo `cmake --build build/chronon/linux-video-test --target chronon3d_camera_tests -j12`, la regressione `Typed shutter offline and runtime cameras consume identical resolved layer samples` passa sul binario ricompilato (1 caso, 201 asserzioni). Non costituisce una certificazione del job reale o del pair di SHA.
- Accumulo typed strict-native tentato il 2026-11-01 su piano minimo Vulkan 320×180 con validation: **FAIL**. Il job non consegna frame, il sink rifiuta la superficie come non-GPU e il validation layer segnala `InvalidImageLayout` (immagini attese `GENERAL`, stato corrente `UNDEFINED`). Nessun output RGBA valido; log `build/chronon/linux-video-test/typed-native-shutter-minimal-f75.log`. Il nuovo ramo non è prova di shutter strict-native; accumulo GPU e lifecycle/layout delle superfici restano da correggere.
- Canary mesh/piano rieseguito dopo `cmake --build build/chronon/linux-video-test --target chronon3d_renderer_core_tests -j12`: **FAIL**, 34/46 asserzioni passate, 12 fallite. I due ordini di authoring e i casi con alpha confermano che l'occlusione per-pixel resta aperta; log locale `build/chronon/linux-video-test/camera-mesh-occlusion.log`.
- Diagnosi/fix locale 2026-11-01: il clone pooled del framebuffer copiava i pixel ma perdeva `depth_map`, così `CompositeNode` non riceveva la profondità rasterizzata dalla mesh. `FramebufferPool::acquire_from` e il passthrough identity di `TransformNode` ora propagano la stessa mappa insieme ai pixel. Il compositing CPU mantiene e riordina i contributi per-pixel con source-over. Canary invariata: **46/46 asserzioni PASS**, entrambi gli ordini e casi alpha; `build/chronon/linux-video-test/camera-mesh-occlusion-final.log`. Questo PASS è circoscritto al percorso CPU e non certifica alpha+DoF o Vulkan.

#### Verifica locale aggiornata 2026-11-01 — ownership e gate

- Ownership target aggiornata 2026-11-01: ChrononMotion3D è l’unica autorità semantica per camera, calendario, layer, alpha/depth, policy DoF/shutter e composizione; Chronon3d resta backend pixel/GPU con kernel, risorse e sincronizzazione, senza un secondo modello authoring. La migrazione non è completata e i gate di questo ticket restano obbligatori. Decisione canonica: [`ADR-CAMERA-PIPELINE-OWNERSHIP.md`](../../../ChrononMotion3D/docs/ADR-CAMERA-PIPELINE-OWNERSHIP.md); il confine nel codice corrente è descritto come legacy in [`BOUNDARY_C3D.md`](../../../ChrononMotion3D/docs/BOUNDARY_C3D.md).
- Verifiche locali rieseguite: `chronon3d_renderer_core_tests` **204/204 casi, 74.268 asserzioni PASS**; `chronon3d_camera_tests` **84/84 casi, 66.232 asserzioni PASS**; `chronon3d_render_graph_tests` **444/444 casi, 19.510 asserzioni PASS**; canary mesh/piano **46/46 asserzioni PASS**. Motion `chrononmotion_authoring`, `chrononmotion_camera_bake_roundtrip`, `chrononmotion_renderplan_camera_parity` **3/3 PASS** e `chrononmotion_boundary` **1/1 PASS** (run precedente). Strict-native typed è fallito con errori Vulkan/layout come riportato sopra.
- La build Release di `chronon3d_cli` è disponibile localmente, ma da sola non dimostra l'accettazione. I vecchi video 1080p nella cartella `camera-closure-final` appartengono a un pair di revisioni precedente e non valgono come prova per questo working tree.
- Restano da completare: integrazione dell'occlusione alpha con DoF e backend nativo; prova end-to-end offline/runtime e random-access sul pair corrente; accumulo shutter GPU strict-native con layout/lifecycle validi; acceptance originale 150 frame a 1920×1080 con metriche/manifest; zero errori Vulkan sul medesimo run; review umana e CI verde sullo stesso pair di SHA. Il ticket rimane **PARTIAL** finché ogni gate §8 non ha evidenza riproducibile.

#### Checkpoint locale successivo 2026-11-01 — batch typed e profondità DoF

- **Shutter typed Vulkan/NVENC, fix locale:** il parent temporale ora chiude il batch nativo di ogni campione dopo il weighted-add, anche sui percorsi di errore. Prima Optical Studio 320×180, frame 75, produceva un MP4 valido ma interamente nero; dopo il fix il frame decodificato è non nero e la validation non segnala errori in questo run. La regressione `Typed native shutter submits each sample before encoder handoff` passa **1 caso / 11 asserzioni**, su tinta unita 320×180 con validation. Non prova il run completo, DoF/alpha, o la parità generale.
- **Occlusione CPU → depth DoF:** la ricomposizione pubblica nel buffer DoF la profondità del contributo più vicino con alpha sopra la soglia di copertura e invalida gli hint ROI precedenti. Regressioni mirate: **7 casi / 47 asserzioni PASS**; canary mesh/piano invariata **46/46 PASS**. Una depth singola resta un'approssimazione per miscele trasparenti; non è prova di DoF multistrato o GPU.
- **Raw Vulkan, diagnosi:** lo stesso piano minimale con sink raw compatibile conclude con validation senza errori; il caso raw + `require_gpu_native` continua a essere rifiutato dal writer perché il sink non ha uno slot nativo. Il precedente `InvalidImageLayout` non è stato riprodotto e non è identificata la sua causa; non dichiararlo risolto.
- **Motion / typed:** con binari ricompilati, `chrononmotion_authoring`, `chrononmotion_boundary`, `chrononmotion_camera_bake_roundtrip`, `chrononmotion_renderplan_camera_parity` **4/4 PASS**; i due test typed shutter C3D **2 casi / 209 asserzioni PASS**. Non certificano provider live camera+layer o CI same-SHA. Il confronto indicativo frame 75 software raw / NVENC lossy 320×180 dà MAE RGB 2,23 e p99 23, senza soglia prefissata: è solo diagnostico.
- La chiusura resta **PARTIAL**: lo shader inline mesh Vulkan non pubblica la depth per-pixel verso il compositing; non c'è ancora equivalenza GPU per occlusione mesh/piano/alpha né integrazione DoF dimostrata. L'accettazione 1080p e la review umana restano distinte dai test locali.

#### Checkpoint locale — uscite vuote Transform strict-native

#### Checkpoint locale 2026-11-01 — proiezione canonica e policy ottica F0–F3

- **F0–F1 PASS locale:** il canary della card a dimensione nota verifica i quattro angoli contro il contratto canonico; proiezione/camera mirata: **15 casi / 49 asserzioni PASS**. Il placement usa la matrice omografica senza ricostruzione TRS e l'overload ambiguo a cinque argomenti è stato rimosso.
- **F2 PASS locale:** `sensor_height_mm` è trasportato nel piano e nel wire, il gate-fit è risolto dal contratto e i default FOV sono allineati. Il test adapter Motion sul pair locale verifica la parità dei parametri ottici e passa nel run riportato sotto; resta necessaria la verifica end-to-end sul pair finale.
- **F3 PASS locale limitato alla policy CPU/software:** rimosso il ramo legacy gain/aperture e il raggio DoF è derivato da `f_stop`, focale, distanza di fuoco, sensore e limiti authored. Il fingerprint del piano è versionato per il cambio semantico. DoF/render graph: **26 casi / 1.525 asserzioni PASS**; Motion adapter: **10 casi / 158 asserzioni PASS**. La chiave cache del nodo DoF ora include i parametri ottici e i limiti near/far; il test ha trovato e corretto l'omissione di focale e f-stop.
- Le fixture del piano di fuoco sono state rese esplicite (`focus_distance=500` per una card a 500 unità); le asserzioni di nitidezza e di zero lavoro DoF restano invariate. Build dei quattro target core/renderer/render graph/Motion adapter completata con `-j12` e `ccache` attivo; `git diff --check` pulito.
- Questo checkpoint **non** chiude F4/F5: il DoF resta post-composito con provenienza depth singola, i kernel non implementano highlight weighting o iris shape e il percorso strict-native Vulkan/1080p non è certificato. Restano obbligatori i gate §8 e lo stato complessivo rimane **PARTIAL**.

#### Checkpoint locale 2026-11-01 — pass strict-native 1080p e backend DepthGrade

- Il frame 75 del piano Optical Studio corrente (`optical-1080-current.plan.json`) ora completa strict-native Vulkan/NVENC a 1920×1080 con 8 campioni shutter, `CHRONON3D_VULKAN_SINGLE_FLIGHT=1` e validation layers attivi: **1 frame scritto, 0 errori validation**. Prima il job falliva su `DepthGrade`, che aveva solo il riferimento CPU e rifiutava il framebuffer GPU-only.
- `DepthGradeNode` ora abbassa la formula costante per-layer (fog, desaturazione e contrasto) al color-matrix Vulkan già esistente, preservando alpha e la semantica CPU; il source/output usano superfici distinte della stessa extent fisica. `chronon3d_render_graph_tests`: **445/445 casi, 19.546/19.546 asserzioni PASS** dopo questa modifica.
- Un primo tentativo 150 frame su quel binario è stato **annullato a 13/150** quando il controllo pair ha rilevato che il piano usato era antecedente all'ultimo emitter Motion; zero errori validation nei 13 frame parziali, ma non è acceptance. Il piano appena rigenerato dal pair corrente ha SHA `f13bbf8c4e0b4c152498e7f01cc6bb1c15af60ea909a26cb3f87ee19fb24f75f`; il suo frame 75 passa strict-native Vulkan/NVENC con zero errori validation. L'acceptance completa deve usare questo piano e verificare decode, frame count e manifest.
- Diagnostica lifetime: il render 320×180 con shutter e validation senza single-flight emette `VUID-vkDestroyImage-image-01000` durante il retirement di superfici CUDA-exportable. Lo stesso frame con `CHRONON3D_VULKAN_SINGLE_FLIGHT=1` completa con zero errori. Questo è un workaround diagnostico, non la correzione del retirement concorrente; il run 1080p in corso usa single-flight e non certifica il lifecycle asincrono di default.
- Il test mirato `DOF rejects incomplete native depth provenance` passa (**1 caso / 13 asserzioni**); il frame 0 320×180 senza sorgenti visibili DoF viene esportato in strict-native con single-flight e zero validation. La canary CPU mesh/piano resta separata: non equivale a occlusione alpha+DoF nativa.
- Controllo del binario `chronon3d_core_tests` fallisce per SIGSEGV in `SceneHasher: disabled camera is treated as static` (39/40 casi del filtro camera eseguiti, 232 asserzioni prima del crash). Questo run non è verde e richiede diagnosi; il test completo render-graph sopra è invece verde.
- F4 per-layer alpha/DoF, kernel F5 a disco con highlight weighting e parità numerica CPU/Vulkan, parità Motion→renderer sul pair finale, oracle raw con soglie preregistrate, CI same-SHA e review umana restano aperti. Stato complessivo: **PARTIAL**.

#### Verifica corrente del pair Motion→renderer — 2026-11-01

> Aggiornato dal checkpoint v2 qui sotto: i numeri `f13bbf...` e il FAIL near@30 descrivono il pair/fixture precedente e restano solo come cronologia.

- Ricostruiti i binari Motion `release-fast` dai sorgenti correnti (`-j14`, ccache): `chrononmotion_authoring` **374 checks / 0 failure**; `chrononmotion_camera_bake_roundtrip` **832 checks / 0 failure**. `chrononmotion_plan_optical_studio` ha prodotto il piano shutter 1920×1080, 150 frame, 8 campioni e `sensor_height_mm=24`; SHA-256 `f13bbf8c4e0b4c152498e7f01cc6bb1c15af60ea909a26cb3f87ee19fb24f75f`.
- `chronon3d_camera_tests`: **84/84 casi, 66.232 asserzioni PASS**; `chronon3d_render_graph_tests`: **445/445 casi, 19.546 asserzioni PASS** dopo il depth stamp nearest-first e il native DepthGrade. Il test isolato `SceneHasher: disabled camera is treated as static` termina con SIGSEGV dopo l'asserzione; va diagnosticato prima di considerare verde la suite core completa.
- Canary software mesh/piano/alpha rieseguito: **1 caso / 46 asserzioni PASS**, con entrambi gli ordini d'authoring.
- Build `chronon3d_cli` Release aggiornata con il piano camera attuale e shader Vulkan nearest-first. Il frame 75 corrente passa strict-native Vulkan/NVENC con single-flight e validation layers: **1/1 frame, 0 errori validation, render 3,30 s**.
- Run acceptance completo: **150/150 frame**, exit 0, H.264 1920×1080/30, decodifica senza errori, zero intervalli neri rilevati e zero Vulkan validation error; SHA-256 video `c0b77f72496b63611adfe57f11b8a71af36871422fb1a67b3ff8e3075d5afa7e`. Report, video, timing sidecar e log sono in `../evidence/camera-closure-final/strict-native-current-pair-report.json` e `optical_current_pair_strict_native_1080p.*`.
- Focus gate con controllo DoF OFF agli stessi frame 30/75/149: **FAIL**, mantenendo le soglie preregistrate. Near@30 rapporto ON/OFF `0,0773` contro `>=0,70`; middle@75 `1,0031` (PASS); far@149 `0,9783` (PASS); near@75 fuori fuoco `1,0297` contro `<=0,90`. La ROI near@30 nel controllo ha edge energy `0,0664` ed è visivamente vuota: la fixture corrente non colloca il target near nella ROI dichiarata. Il risultato non è sanabile modificando la soglia; occorre correggere e versionare fixture/proiezione, poi ripetere il gate.
- Il run full usa `CHRONON3D_VULKAN_SINGLE_FLIGHT=1`: prova l'output strict-native serializzato, ma non il lifecycle asincrono predefinito. L'errore lifetime Vulkan async resta aperto. F4 DoF per-layer alpha, F5 bokeh disk/highlight e parità numerica CPU/Vulkan, runtime provider Motion→renderer end-to-end, CI same-SHA e review umana restano aperti. Stato complessivo: **PARTIAL**.

#### Verifica pair v2 e fixture a linee — 2026-11-01

- Le tavole di misura sono state rigenerate con pattern line-pair ad alto contrasto centrati nelle ROI; asset digest e registro alpha sono aggiornati. Il pair offline/provider genera lo stesso piano, SHA-256 `15f650b4ed70a05401ff42385246493e60733a3054bc33f8b1ad84229876decd`; sono stati confrontati 1.350 campioni camera shutter e tutti i campioni trasformazione dei layer.
- Il strict-native Vulkan/NVENC DoF ON e il controllo sharp OFF terminano entrambi con 150/150 frame 1920×1080/30, decode pulito, nessun intervallo nero rilevato e zero errori validation. I run richiedono `CHRONON3D_VULKAN_SINGLE_FLIGHT=1`: accettazione serializzata PASS, retirement asincrono ancora NOT PASS. Hash e log sono in `../evidence/camera-closure-final/optical_current_pair_v2_*`.
- Focus raw RGBA Vulkan: **9/9 PASS** con gli stessi ROI e limiti preregistrati; rapporti focused near@30 `1.0000`, middle@75 `1.0000`, far@149 `0.9987`; le ROI defocused restano entro il limite `0.90` (massimo `0.5807`). Il controllo grafico ora ha dettaglio misurabile, quindi il precedente falso FAIL per ROI near vuota è superato.
- Il confronto software/Vulkan full-frame sullo stesso pair è **FAIL**, non una misura DoF valida: RGB MAE `28.99/27.59/27.12` e p99 `75/83/83`, contro i limiti congelati `2/10`. Il probe isolato che mostrava clear-color usava `--diagnostic` (layout preview) e non è valido per diagnosticare il renderer; un nuovo frame shuttered software senza quel flag è stato interrotto dopo oltre 80 s e 2,4 GB RSS. Causa software-side non ancora stabilita. Creare un probe focused a un solo campione per isolare la proiezione, poi ripetere il confronto preregistrato; soglie invariate.
- Probe software sintetici mirati rieseguiti sul binario corrente: card ai quattro angoli canonici **11/11 PASS**; immagine sovradimensionata con alpha **19/19 PASS**; matrice di proiezione condivisa Rect/Image **11/11 PASS**. Confermano le primitive base, non la parità pixel sul pair completo.
- Rimangono aperti: F4 DoF per-layer alpha prima del compositing, F5 kernel disco/highlight e parità kernel CPU/Vulkan, regressione software/Vulkan dopo il fix source, shutter strict-native GPU (il run video con shutter non è ancora certificato), lifecycle async Vulkan, occlusione alpha/DoF, provider end-to-end sul rendering completo, CI same-SHA e review umana. Il ticket resta **PARTIAL**.

#### Checkpoint v3 — background fixture dimensionato per il campo camera

- Il piano Optical Studio Motion ora dimensiona il background a **3840×2160** per il campo visivo stimato `3316×1865` (50°, distanza camera/plane 2000): overscan ~16%, invece di 8400×4725 per un PNG 1920×1080. Generatore Motion ricompilato con `-j14`; il pair provider/offline è byte-identico, piano SHA `c2467b44fed0f0d007af7b3e38a3dd57e13728295b506701d7d3c02306e376d6`, 1.350 campioni camera e layer; dry-run RenderPlan PASS, 1920×1080/30/150.
- Probe still sintetico software/Vulkan con DoF e shutter spenti soltanto nel piano temporaneo: RGB MAE `1,0374`, p99 `5`, Vulkan validation `0`. È prova della corrispondenza sul still, non del pair animato.
- Raw Vulkan rack-focus v3: **9/9 PASS**, soglie congelate. near@30 `0,9443`, middle@75 `0,8856`, far@149 `0,9956`; report e hash raw in `../evidence/camera-closure-final/strict-native-current-pair-v3-focus-report.json`.
- Acceptance DoF-on strict-native Vulkan/NVENC 150 frame v3 in corso: `../evidence/camera-closure-final/optical_current_pair_v3_dof_1080p.*`. Finché non termina con 150 frame decodificabili e zero validation, non è PASS. Controllo sharp 150 frame, parità software/Vulkan sul pair animato, DoF per-layer/alpha e F5 bokeh CPU/Vulkan restano aperti.

- Diagnosi sul singolo frame 75 Optical Studio 1080p: `Transform` del layer 4 pubblica un framebuffer senza backing CPU né handle GPU; `Lighting` fallisce a valle. Il log `build/chronon/linux-video-test/optical-1080-diag.log` non distingue quale uscita anticipata di clipping sia stata presa: la causa esatta del frame non è ancora dimostrata.
- **NON VERIFICATO:** il working tree inizializza una superficie nativa trasparente sulle tre uscite anticipate di clipping di `Transform`, compreso il target encoder riutilizzato; aggiunta la regressione `strict Vulkan clipped transforms publish transparent native surfaces`. Nessun PASS finché il binario non viene ricompilato e il test e il frame 1080p non vengono rieseguiti con validation.
- La build di verifica è temporaneamente bloccata da un'altra build non avviata dall'agente sullo stesso `build/chronon/linux-video-test`, con `-j14`, swap pieno e disponibilità RAM oscillante. Non avviare build sovrapposte o render pesanti; controllare nuovamente memoria e processi prima di procedere, con massimo `-j12` complessivi per la build avviata dall'agente.

#### Diagnosi 2026-11-01 — lifecycle delle superfici CUDA-exportable (chiusa da C1)

Il difetto riprodotto con validation (`VUID-vkDestroyImage-image-01000` al
retirement di superfici esportate, solo senza single-flight) ha una causa
verificata nel codice corrente:

- `Impl::wait_for_pending()` (`vulkan_backend_submission_private.cpp`) drena i
  fence dei frame slot e gli upload slot; **non attende nulla sul lato CUDA**.
- `VulkanBackend::release_surface()` decide il rinvio con
  `recording_slots_retired()`; il destroy effettivo passa da
  `release_surface_now()` → `prune_unused_slots_impl()`, che filtra solo
  `slot_in_use`/`slot_pinned`, cioè i lease di registrazione Vulkan.
- Il submit esporta la superficie segnalando `vulkan_to_cuda`; il consumer
  CUDA/NVENC consuma e segnala `cuda_to_vulkan`. Il backend riusa una superficie
  solo dopo aver atteso C2V alla riesumazione, ma **non attende alcun token C2V
  prima di distruggere**.
- `video_device_runtime_surfaces_detail.hpp` sostituisce `native_surface`/
  `source_surface` chiamando `release_surface()` al rebuild dello slot senza
  handshake di consumo: la stessa assunzione di ring asincrono che il validation
  layer viola.
- `CHRONON3D_VULKAN_SINGLE_FLIGHT=1` chiama `wait_for_pending()` all'inizio di
  ogni frame: serializza la pipeline e nasconde il difetto. Resta diagnostico.

Non esisteva un punto in cui il consumer dichiarasse "ho finito" in modo
osservabile dal backend. Il passo C1 lo introduce (sotto); l'exit 139 dopo il
frame 75 del job shutter 150 frame era un secondo difetto di lifetime, distinto
da questo VUID, e va verificato con un run proprio (ora coperto dal checkpoint
C2 sotto).

#### Chiusura 2026-11-01 — C0/C1 lifecycle, C3 occlusione nativa, C4 kernel

**C1 — contratto di consumo consumer→backend (fix applicato).** Il token è
`Image::export_handed_off`:

- viene alzato dal submit che consegna la superficie a CUDA/NVENC
  (`vulkan_backend_submission_private.cpp`, `vulkan_kernel_replay_private.cpp`);
- viene abbassato solo da `acquire_pending_cuda_surface()`, cioè quando il
  consumer restituisce la superficie (`vulkan_surface_authority.cpp`);
- `slot_export_owned()` ora governa la distruzione
  (`prune_unused_slots_impl`), il riuso di slot non pianificato (`ensure`,
  `bind_unplanned`) e la ri-materializzazione (`materialize_binding`, `ensure`),
  che falliscono con `std::logic_error` invece di distruggere memoria ancora
  mappata; il retirement dei transient di frame-slot non azzera più
  `initialized` sugli slot posseduti dal consumer.

Regressioni dedicate in `tests/backends/test_vulkan_surface_materializer.cpp`:
`a surface handed to CUDA is retained by the lazy prune`,
`export retention is independent of the warm-pool budget`,
`consumer-owned slots are never reused by an unplanned allocation`,
`re-materializing a consumer-owned slot fails closed`. Suite completa:
**32/32 casi, 481/481 asserzioni PASS**.

**C0 — riproduzione/documentazione e verifica del fix.** Il difetto
(`VUID-vkDestroyImage-image-01000` al retirement di superfici CUDA-exportable,
solo senza single-flight) è la diagnosi sopra, con i tre punti di codice citati.
Sull'albero corretto lo stesso job non lo riproduce più:

```
CHRONON3D_VULKAN_VALIDATION=1 chronon3d_cli render \
  --plan docs/evidence/camera-closure-final/optical_current_pair_v3.plan.json \
  --assets-root "$PWD" --backend vulkan --gpu-hot-path-mode require_gpu_native \
  --hardware nvenc --encoder-backend native \
  --start-frame 70 --end-frame 79 -o optical_current_pair_v3_c0_f70-79_nosf.mp4
```

Risultato: 10/10 frame 1920×1080 accettati dall'encoder nativo, exit 0,
**0 occorrenze del VUID**, **0 Validation Error**, `render_ms=34249`
(3,4 s/frame), nessuna variabile `CHRONON3D_VULKAN_SINGLE_FLIGHT`. Evidenza:
`docs/evidence/camera-closure-final/optical_current_pair_v3_c0_f70-79_nosf.log`
e `.mp4` (SHA-256 `ca7b87aa9322a0c97e11bfaaf82a16c7927f157ee74a9008991b5fbe868306e2`).
Coppia: C3D `b77ae37e887e7fa655de3d3a34e98eafb514c83f` con working tree dirty
(135 file modificati, nessuna certificazione same-SHA implicita).

**C3 — occlusione mesh/piano per-pixel nativa (fix applicato).** Il depth stamp
Vulkan (`dof_depth_stamp.comp`) considerava "già scritto" un pixel con
`g < 0`, quindi una superficie di profondità inizializzata a zero bloccava la
prima scrittura: la mesh veniva mascherata, il piano mai. Ora il test di
presenza è lo stesso predicato di `occlusion_tag.comp`
(`g >= 0 && r > 0`), e la depth per-pixel della mesh inline pubblicata dal
rasterizzatore resta nearest-first. Verifiche:
`Vulkan native occlusion holds a mesh half behind a 2.5D plane in both authoring
orders` **1 caso / 87 asserzioni PASS** (Vulkan, entrambi gli ordini d'authoring)
e `software 3D mesh crossing a 2.5D plane uses per-pixel depth`
**1 caso / 46 asserzioni PASS** (software, entrambi gli ordini).

**C4 — kernel bokeh a disco e highlight weighting.** La parità numerica CPU/Vulkan
con soglia preregistrata passa: `Vulkan disk bokeh kernel matches the CPU kernel
within the frozen tolerance` **1 caso / 1169 asserzioni PASS**, delta massimo per
canale `1.35e-07` contro la soglia `1e-3`, con `highlight_gain = 2.0` e
discriminazione disco/quadrato sul supporto. Contestualmente sono state corrette
sette aspettative DoF rimaste sul modello pre-thin-lens (`depth * aperture`):
ora sono derivate dal modello CoC documentato con `focus_distance` esplicita.
`chronon3d_camera_tests` **87/87 casi, 67.391/67.391 asserzioni PASS**.

### 8.3 Layer, profondità, alpha e DoF

Le vecchie scene T16/MrBeast e T18/folla sono esclusivamente evidenza storica: non sono obiettivi né criteri di accettazione camera. La nuova scena target è uno studio ottico originale, senza celebrità, personaggi o folla; i gate tecnici qui sotto restano obbligatori.

- [ ] La scena di accettazione contiene almeno tre piani distinti: background, grafica intermedia e un elemento foreground con alpha; testo o grafica sovrapposti. Le trasformazioni e l'ordine dei layer arrivano da Motion, mentre Chronon3D applica proiezione, occlusione, compositing e DoF.
- [ ] Verificare parallasse durante un movimento camera non nullo; ordine/occlusione coerenti; alpha semitrasparente ai bordi dell'elemento foreground; nessun pixel spurio, alone o contaminazione di colore visibile attorno a bordi e testo nei frame di accettazione. Le ROI e le soglie pixel vengono registrate con la fixture prima di valutare il risultato.
- [ ] La prova DoF include un controllo identico con DoF disattivato e un rack focus che attraversa almeno background/documento/soggetto. Per ogni stato, la ROI del piano a fuoco deve risultare nitida rispetto alle ROI fuori fuoco secondo metrica e soglia fissate sulla fixture prima del fix; il passaggio non deve introdurre ghosting o bleeding sui bordi alpha. Un semplice test `DoF ON != OFF` non è sufficiente.
- [ ] Definire il comportamento della profondità mancante: se il piano dichiara sorgenti/profondità DoF attese e il dato non arriva, il job fallisce con errore diagnosticabile; una scena senza sorgenti DoF è legale solo quando è esplicitamente dichiarata. Vietato il pass-through silenzioso che dichiara successo per una richiesta DoF non soddisfatta.
- [ ] Software oracle e Vulkan strict-native sono confrontati sugli stessi frame/ROI con metriche e soglie predefinite; la lane software può essere lenta, ma non si può sostituire la prova native con fallback CPU o backend ibrido non dichiarato. Vulkan validation layers: zero errori per il run di accettazione.

### 8.4 Una sola accettazione end-to-end ripetibile

- [ ] Usare una scena originale versionata e riproducibile di studio ottico: almeno 150 frame a 1920×1080/30 fps, piani grafici con parallasse, elemento foreground con alpha, rack focus, e un elemento grafico in entrata. Tutti gli asset/font sono inclusi o risolti da manifest con digest; niente dipendenze da file locali non versionati. I vecchi T16/MrBeast e T18/folla non sono obiettivi di questa acceptance.
- [ ] La stessa authoring scene Motion produce il RenderPlan offline e alimenta il provider runtime. Chronon3D renderizza entrambi senza correzioni manuali di keyframe/coreografia nel renderer. Salvare piano, asset manifest, comandi, configurazione, log, frame raw di controllo, video decodificato, metriche e SHA delle due repository sotto `docs/evidence/camera-closure-final/`.
- [ ] Accettazione visiva dell'intero video e dei frame sentinella documentata da una persona/reviewer: framing e movimento corretti, testo/cutout leggibili, passaggio del fuoco evidente e senza artefatti bloccanti. Il video deve decodificarsi, avere tutti i frame previsti e nessun intervallo nero accidentale. Encoder/codec si registrano come gate di output distinti: un loro difetto non riapre il contratto camera se i frame raw superano i gate.
- [ ] I gate automatici e il render finale passano sul pair preciso di SHA di ChrononMotion3D e Chronon3D; CI obbligatoria verde in entrambi i repository e test di integrazione sul medesimo pair. Una build locale dirty o una CI di un SHA diverso non certifica la chiusura.

### 8.5 Regola di chiusura e anti-riapertura

- [ ] Pubblicare un report finale con tabella `gate / comando / SHA CM3D / SHA C3D / host-device / risultato / evidenza`; ogni gate è `PASS` oppure il ticket resta `PARTIAL/BLOCKED`. Owner e dipendenze esterne hanno un responsabile e una data di sblocco; non si convertono in PASS per decisione editoriale.
- [ ] Dopo il PASS complessivo, questa pipeline è chiusa per il contratto e la scena di accettazione sopra definiti. Si riapre solo per una regressione riproducibile che fallisce un gate esistente, allegando il test e il pair di SHA; nuova estetica, asset, codec o capability (per esempio bokeh ottico non incluso nella matrice) diventano ticket separati e non riaprono la camera per supposizione.

### Fuori perimetro esplicito

La chiusura certifica la regia valutata da ChrononMotion3D, il trasporto temporale, i layer 2.5D/alpha, la proiezione, il rack focus/DoF supportato, lo shutter raw e il compositing della scena target. **Non** certifica ogni effetto After Effects, bokeh con lamelle/highlight non supportati, qualità universale di qualunque asset, né tutti gli encoder. Le capability fuori matrice devono fallire in modo esplicito; si aggiungono solo con una nuova richiesta e una nuova acceptance, senza riaprire ciò che ha già superato questa.
