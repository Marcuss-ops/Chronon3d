# TICKET-CAMERA-DEFINITIVE-CLOSURE — Chiusura Definitiva Pipeline Camera (Chronon3d + ChrononMotion3D)

- **Stato**: **PARTIAL — gate end-to-end non superato** (checkpoint 2026-11-04); **alpha-edge §8.3 b2–b3 PASS locale** (2026-11-03: DoF osservabile, tutte le soglie preregistrate verdi). C0/C1 lifecycle, C3 occlusione mesh/piano, C4 kernel DoF, C5 parità raw software/Vulkan full-frame (congelata a MAE ≤2, p99 ≤10) ed equivalenza compile/render offline vs live provider runtime sull'intera scena Optical Studio v3 sono chiusi con evidenza locale nei checkpoint sotto; non restano aperti come fix isolati. Il pair corrente ha completato localmente 150/150 frame raw RGBA8 ON/OFF con soglie ottiche preregistrate verdi; DoF ON/shutter ha anche completato 150/150 frame strict-native Vulkan/NVENC 1080p senza single-flight, decode pulito, PTS monotoni, zero intervalli neri e zero errori validation/VUID. Restano review umana (C7), CI same-SHA e gate di occlusione nativa end-to-end sul corpus completo. Le prove locali non equivalgono al PASS end-to-end della sezione 8.
- **Aggiornamento 2026-11-03:** l'accettazione alpha-edge §8.3 b2–b3 è **PASS locale**
  sul contratto pre-registrato
  ([`docs/evidence/camera-alpha-edge-20261103/`](../evidence/camera-alpha-edge-20261103/README.md)).
  Il primo verdetto `BLOCKED_DOF_INERT` era un difetto dell'harness, non del renderer:
  `make_probe_plans.py` spegneva `camera.dof.enabled` solo nel blocco `camera`, mentre
  il piano porta 1350 `camera_samples` che il runtime valuta e che restavano con DoF
  attivo; i due probe erano quindi lo stesso render. Il generatore ora scrive il flag
  in `camera` e in ogni campione. Misura: DoF osservabile su 30/75/149 (delta medio
  3,19/3,87/4,09; max 81/87/87), G1 32.251/28.527/27.505 pixel a copertura parziale,
  G2 zero violazioni (overshoot max 3/0/0), G3 0/0/78 pixel contaminati (0,221% ≤
  0,5%), G4 246,55 px, zero validation error. C7 resta esterno: review umana senza
  reviewer nominato e CI same-SHA senza runner, con entrambi gli alberi **dirty**
  (C3D `b38f7e8f9`, 29 file; CM3D `83af9628`, 3 file).
- **Checkpoint locale 2026-10-30:** [report camera-closure-final](../evidence/camera-closure-final/README.md): camera 82/82, Motion 3/3, architettura 6/6 e nodo DoF 5/5; raw software/Vulkan e rack-focus ROI PASS. Corrette precisione double dei timestamp layer e copia DoF verso intermedi shutter. Due MP4 1080p caricati; shutter unico FAIL exit 139 dopo 75 frame, delivery segmentata in corso (workaround, non fix). Nessuna chiusura same-SHA/umana implicita. L'espansione funzionale AE-style ha un [ticket separato](TICKET-AE-CAMERA-FUNCTIONAL-PARITY.md).
- **Checkpoint locale 2026-10-31:** contratto unico di ammissione camera/ottica applicato ai due confini producer/compiler (test `Camera capability contract rejects unsupported optics at both compiler boundaries`), provider runtime passato al compiler canonico e risoluzione camera/layer sul calendario shutter; pubblicato anche il timing GPU per pass. I gate §8 restano aperti: la suite camera 82/82 e Motion 3/3 sono del 2026-10-30, il job shutter completo è ancora FAIL e la CI same-SHA è BLOCKED.
- **Priorità**: P0
- **Repository**: `ChrononMotion3D`, `Chronon3d`
- **Build**: parallelismo limitato alla memoria disponibile; non sovrapporre build pesanti senza tener conto del totale dei job.

### Reproduction checkpoint 2026-11-03 — current workspace C3D `b1b21cbe`

The animated-mesh collapse is reproducible on the checked-out software CLI
with a minimized fixture derived from
`ChrononMotion3d/docs/evidence/ticket-roundtrip-scaled-20261029/gltf/`. Its 31
mesh-animation samples were scaled by 100 into world-space pixels, centered at
the origin, and rendered on a 640×360 canvas. With 31 camera samples rotating
yaw from 0° to 0.02°, non-transparent pixel counts at frames 0/1/5/15/30 were
**501/327/171/0/0**. The same mesh-animation samples with camera rotation
fixed at 0° gave **501/452/477/452/477**. A fresh single-frame render at frame
15 with the rotating-camera plan was also empty. This reproduces the reported
camera/animated-mesh interaction independently of prior-frame reuse, but does
not yet locate the cause within camera placement, projection or mesh draw.

Reproduction ran with the existing `linux-video-fast-dev` CLI, software
backend, using temporary plans/raw files under `/tmp`. This is a minimized
diagnostic fixture, not the 150-frame release corpus. No source regression
test or Vulkan run has been made; P0-04/C6/C7 and this ticket remain
**PARTIAL**.

### Isolation checkpoint 2026-11-03 — yaw versus mesh deformation

The first reproduction conflated camera motion and mesh deformation. Four
frozen RenderPlans now separate them; the plans, commands, raw-frame counts and
limitations are recorded in
[`camera-mesh-yaw-isolation-20261103`](../evidence/camera-mesh-yaw-isolation-20261103/README.md).
On the same existing software CLI, frames 0/1/5/15/30 had non-transparent
pixel counts: animated mesh + fixed 0° camera **501/452/477/452/477**;
animated mesh + camera yaw 0°→0.02° **501/350/171/0/0**; frozen mesh + camera
yaw 0°→0.02° **501/402/376/376/375**; animated mesh + fixed 0.02° yaw
**375/328/171/0/0**. Camera motion alone therefore does not collapse this
fixture; mesh deformation under non-zero yaw does, even with a static camera.
This narrows P0-04 but does not identify whether deformation sampling,
placement, projection or clipping is responsible. The binary was pre-existing,
and there is still no source regression, fix, Vulkan run or 150-frame release
acceptance. P0-04/C6/C7 and the ticket remain **PARTIAL**.

### Source isolation and software regression checkpoint 2026-11-03

The four-plan isolation was followed by an end-to-end typed RenderPlan test and
temporary processor tracing. Deformed vertices and projected positions stayed
inside the viewport. The mesh processor instead took near/far from its default
`Camera{}` (`far_plane=1000`), while the resolved RenderPlan camera authored
`far=10000`; at non-zero yaw the animated triangle's camera-space depth crossed
the accidental 1000-unit limit and was clipped. The low-level rasterizer
passed the same yaw/deformation poses, excluding triangle coverage as the
cause.

The implementation now carries near/far through `Camera2_5D`, typed camera
sampling, projection context and the software/native mesh projection paths;
camera node-cache fingerprinting includes both fields. Regression coverage
checks visibility at frames 0/5/15/30 and camera sample parity, plus direct
lit/unlit rasterizer coverage. Built with
`ninja -C build/chronon/linux-fast-dev -j12 chronon3d_camera_tests chronon3d_renderer_core_tests`;
the focused tests passed (9, 34, and 8 assertions respectively). This is local
software evidence on a dirty worktree, not same-SHA CI or release certification.
P0-04, C6/C7 and this ticket remain **PARTIAL** until the full 0–149 animation,
skin/morph corpus, Vulkan/native validation, CI and human review pass.

Follow-up: the end-to-end regression now runs all 150 frames at 640×360 with
per-frame inline vertex animation and camera yaw interpolation from 0° to
0.02°. It passed all 150 visibility checks (301 assertions in the test case).
The test uses a minimized inline triangle, so it does not replace skinned+morph
glTF acceptance or the Vulkan/native and same-SHA release gates.

### Image-under-mesh checkpoint 2026-11-03

The first software canary used the legacy `render_compiled` compatibility
evaluator and was not evidence for the production typed program route. A
corrected `compiler::compile()` → `render_program()` regression reproduced
P0-05: 2D image composition reduced mesh coverage from 144 to 109 red pixels
(35 missing, 25 interior), while a 3D plane preserved all 144. The cause was
near-coplanar depth rounding (1000.0 vs 1000.000061), which defeated authored
submission order. The compositor now groups depths within its existing
occlusion epsilon and restores submission order within each bounded group.
The typed test passes 16/16 assertions after the fix: 144/144/144 red pixels,
zero coverage or interior mismatches. Evidence and fixture are in
[`image-mesh-occlusion-20261103`](../evidence/image-mesh-occlusion-20261103/README.md).

Post-fix remote A4000 Vulkan raw captures of both minimized plans now exactly
match their software captures byte-for-byte (49,152-byte RGBA8, matching
SHA-256 for image-under-mesh and plane3d). The route used Vulkan with
`gpu-hot-path-mode auto` and raw readback; it does not certify strict-native
no-readback video. Original showcase assets, strict-native output, same-SHA CI
and human review remain outstanding. P0-05 remains **PARTIAL**.

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
- **Shutter strict-native a risoluzione ridotta, 2026-10-31:** sulla build finale della CLI il piano offline Optical Studio 320×180 ha completato un unico job Vulkan/NVENC con validation attiva, `--gpu-hot-path-mode require_gpu_native --hardware nvenc --encoder-backend native`: exit 0, H.264, 150 frame a 30 fps, decodifica ffmpeg senza errori, zero intervalli blackdetect, zero errori Vulkan segnalati; SHA-256 `7c9444cac97dd02ea3801069a84e57d4650bfabdc1d7f7ce857149707a79b3d9`. Il log contiene 1.200 warning `detached reused extent` (non errori validation). Il precedente job 1080p exit 139 non è corretto né certificato da questo run 320×180, e non è approvazione visiva umana.
- **Triage warning, 2026-11-02:** in `../evidence/camera-closure-final/shutter-lifetime-fixed.log` tutti i 1.200 messaggi indicano lo stesso mismatch: superficie registrata 1920×1080 associata a framebuffer 24×24. `ensure_empty_native_surface` (`src/render_graph/nodes/native_surface.hpp`) distacca l'handle potenzialmente condiviso senza rilasciarlo e crea una nuova superficie con l'extent del framebuffer; il log non contiene stringhe `Validation Error` o `VUID-`. Si tratta di un warning del renderer, non di un errore di validation. Il controllo del log e il ramo di recupero non dimostrano da soli che il lifecycle asincrono sia sicuro: quel gate e il run 1080p restano aperti.
- **Diagnosi 2.5D locale:** un `Null` fra due layer 3D interrompeva il gruppo di ordinamento per profondità; una regressione software con il layer vicino inserito per primo riproduceva il piano lontano sopra quello vicino. `LayerPipelinePass` ora lascia il gruppo aperto per i Null non renderizzati; `Camera 2.5D: a non-rendering Null does not break depth sorting` verifica pixel, inversione della profondità e ritorno al frame precedente. La selezione di 10 test camera/ordine passa con 43 asserzioni. Non è una certificazione Vulkan o E2E Motion → video.
- **Optical Studio, diagnosi raw software limitata:** la fixture sharp 640×360 rigenerata è stata renderizzata con 1, 2, 3 e 4 piani ai frame 0/75/149 prima della correzione del Null. Il background isolato conserva rispettivamente 98.134, 99.213 e 102.094 pixel RGB identici nella composizione completa; ciascun piano aggiunto modifica dei pixel. Non dimostra occlusioni corrette né spiega la riproduzione storica del fondale mancante.
- **Mesh, prove software limitate:** il canary di estrusione e il test della texture con alpha passano (2 test, 9 asserzioni); non certificano occlusione per-pixel mesh/piani, DoF integrato, Vulkan o parità AE.
- **Correzione della fixture mesh/piano:** il canary originario usava `fill(Color::red())`, che aggiunge un rettangolo e non colora la mesh; i pixel rossi isolati non dimostravano quindi la copertura della mesh. La fixture corrente colora direttamente il nodo mesh, posiziona il triangolo nello spazio canvas e verifica entrambi gli ordini e l'alpha frazionario. La mesh isolata copre entrambi i probe; il compositing continua a fallire. La correzione locale della premoltiplicazione alpha delle mesh software non risolve l'occlusione per pixel.
- **(SUPERATO dal checkpoint 2026-11-02, C3):** questo bullet resta come storia del difetto, non come stato corrente. Il gate C3 è tracciato come chiuso con evidenza locale nella testa del ticket.
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
- [ ] Aggiungere una matrice di capability camera/DoF condivisa dal lowering Motion e dal consumer Chronon3D: ogni parametro supportato viene consumato; ogni combinazione non supportata (inclusa iris poligonale) viene rifiutata con errore tipizzato prima del render. Highlight weighting circolare è supportato nell'intervallo [0, 8]; nessun valore accettato e poi ignorato, clamp silenzioso o fallback non dichiarato.
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
- Acceptance DoF-on strict-native Vulkan/NVENC 150 frame v3 e controllo sharp sono stati completati nel checkpoint 2026-11-02 qui sotto. Restano aperti la parità software/Vulkan sul pair animato, DoF per-layer/alpha e F5 bokeh CPU/Vulkan.

- Diagnosi sul singolo frame 75 Optical Studio 1080p: `Transform` del layer 4 pubblica un framebuffer senza backing CPU né handle GPU; `Lighting` fallisce a valle. Il log `build/chronon/linux-video-test/optical-1080-diag.log` non distingue quale uscita anticipata di clipping sia stata presa: la causa esatta del frame non è ancora dimostrata.
- **NON VERIFICATO:** il working tree inizializza una superficie nativa trasparente sulle tre uscite anticipate di clipping di `Transform`, compreso il target encoder riutilizzato; aggiunta la regressione `strict Vulkan clipped transforms publish transparent native surfaces`. Nessun PASS finché il binario non viene ricompilato e il test e il frame 1080p non vengono rieseguiti con validation.

#### Checkpoint locale 2026-11-02 — pair v3 1080p completo

- DoF ON/shutter strict-native Vulkan/NVENC senza `CHRONON3D_VULKAN_SINGLE_FLIGHT`: 150/150 frame, H.264 1920×1080/30, decode pulito, nessun intervallo nero, zero Vulkan validation error/VUID. Video SHA-256 `c78cfaf096e368b72d32a1a44237e367427e5618a64e434174b4617ab85ac3d6` (`optical_current_pair_v3_dof_nosf_1080p.mp4`).
- Controllo sharp OFF con la stessa configurazione: 150/150 frame e stessi controlli di output PASS. Video SHA-256 `a5101c29c80197b07d065e7c2bc85631088e1e4d84aa0e15cfcb316e67f9dac6` (`optical_current_pair_v3_sharp_nosf_1080p.mp4`). Log e sidecar timing sono accanto ai video in `../evidence/camera-closure-final/`.
- Il raw rack-focus Vulkan v3 resta PASS 9/9 secondo report e soglie preregistrate. I probe `v3-parity-*` con sink raw hanno prodotto frame neri e sono stati scartati: non sono una misura di parity. Il probe still sintetico precedente passa, ma non prova il pair animato; il gate raw software/Vulkan full-frame resta quindi aperto. I due MP4 confermano solo output/render native, non il provider live end-to-end né l'accettazione alpha-edge e DoF per-layer.
- I worktree Chronon3D e Motion erano dirty: nessuna certificazione CI sul pair esatto di SHA. Review umana dell'intero video ancora necessaria. Stato complessivo **PARTIAL**.

#### Verifica tecnica 2026-11-02 — sentinelle v3 e build test

- CLI Release ricostruita con `cmake --build build/chronon/linux-video-release --target chronon3d_cli -j14` (**PASS**). Nuovi sentinella strict-native Vulkan/NVENC QP0 (`lossless`) con validation: frame 30/75/149, zero Vulkan validation error. Software RGBA raw confrontato con il decode H.264 QP0: MAE `1,8158/1,4539/1,5042`, p99 `11/9/9`; frame 30 resta **FAIL** al limite congelato p99 ≤10. Misure, hash, log e sidecar in `../evidence/camera-closure-final/parity-v3-*`; riproduzione: `measure_parity_v3_lossless_sentinels.py`. Il decode NVENC è yuv420p/BT.709 limited, quindi è diagnostica normalizzata e non sostituisce la raw RGBA Vulkan richiesta.
- ROI preregistrate: p99 near `2/3/6`, middle `11/10/11`, far `6/6/2` per frame `30/75/149`. Near è il proxy ROI del piano foreground con alpha e resta nei limiti, ma la ROI non isola la sola banda del bordo: nessun PASS anti-halo viene dedotto. Casi CPU mirati alpha/occlusione/depth **5/5 PASS, 44/44 asserzioni**; log `../evidence/camera-closure-final/camera-alpha-depth-targeted-20261102.log`. I test API C3D provider pixel/shutter **2/2, 105/105 PASS** (`camera-provider-targeted-20261102.log`); Motion bake/provider `chrononmotion_camera_bake_roundtrip` **PASS**. Sono prove bridge/provider campionato, non callback Motion→C3D sul render completo.
- Rigenerazione v3 `offline` e `provider`: i RenderPlan sono byte-identici a SHA `c2467b44fed0f0d007af7b3e38a3dd57e13728295b506701d7d3c02306e376d6`. Prova di campionamento/materializzazione provider sul fixture completo, non del callback runtime passato direttamente al render C3D completo: quel gate resta PARTIAL.
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
- [x] Software oracle e Vulkan strict-native sono confrontati sugli stessi frame/ROI con metriche e soglie predefinite (MAE ≤ 2.0, p99 ≤ 10): frame 30 MAE 0.1740/p99 1, frame 75 MAE 0.1772/p99 2, frame 149 MAE 0.1523/p99 1; Vulkan validation layers: 0 errori su tutti i sentinella; evidenza `docs/evidence/camera-closure-final/parity-v3-raw-sentinels.json`.

### 8.4 Una sola accettazione end-to-end ripetibile

- [x] Usare una scena originale versionata e riproducibile di studio ottico: almeno 150 frame a 1920×1080/30 fps, piani grafici con parallasse, elemento foreground con alpha, rack focus, e un elemento grafico in entrata. Tutti gli asset/font sono inclusi o risolti da manifest con digest; niente dipendenze da file locali non versionati. I vecchi T16/MrBeast e T18/folla non sono obiettivi di questa acceptance. Fixture e output locale v3 documentati nel checkpoint 2026-11-02; l'accettazione visiva alpha-edge è ancora separata.
- [x] La stessa authoring scene Motion produce il RenderPlan offline e alimenta il provider runtime. Chronon3D compila entrambi senza correzioni manuali di keyframe/coreografia nel renderer (test C3D "Full Optical Studio v3 provider vs offline production render equivalence", evidenza `motion-provider-full-scene-parity.json`: fingerprint, nodi, layer, pass e parametri sentinella 30/75/149 identici; conflitti e assenza provider fail-closed verificati). Salvare piano, asset manifest, comandi, configurazione, log, frame raw di controllo, video decodificato, metriche e SHA delle due repository sotto `docs/evidence/camera-closure-final/`.
- [ ] Accettazione visiva dell'intero video e dei frame sentinella documentata da una persona/reviewer: framing e movimento corretti, testo/cutout leggibili, passaggio del fuoco evidente e senza artefatti bloccanti. Il video deve decodificarsi, avere tutti i frame previsti e nessun intervallo nero accidentale. Encoder/codec si registrano come gate di output distinti: un loro difetto non riapre il contratto camera se i frame raw superano i gate.
- [ ] I gate automatici e il render finale passano sul pair preciso di SHA di ChrononMotion3D e Chronon3D; CI obbligatoria verde in entrambi i repository e test di integrazione sul medesimo pair. Una build locale dirty o una CI di un SHA diverso non certifica la chiusura.

### 8.5 Regola di chiusura e anti-riapertura

- [x] Pubblicare un report finale con tabella `gate / comando / SHA CM3D / SHA C3D / host-device / risultato / evidenza`; ogni gate è `PASS` oppure il ticket resta `PARTIAL/BLOCKED`. Owner e dipendenze esterne hanno un responsabile e una data di sblocco; non si convertono in PASS per decisione editoriale. **Pubblicata il 2026-11-03** (§Report finale dei gate): i verdetti sono `OPEN`/`PARTIAL`/`BLOCKED`, e il ticket resta `PARTIAL`.
  Restano da nominare con data: **owner della review umana** del video (oggi nessuno ha firmato) e **owner dei runner CI** (blocco billing, `docs/stato/README.md` §Blocchi trasversali).
- [ ] Dopo il PASS complessivo, questa pipeline è chiusa per il contratto e la scena di accettazione sopra definiti. Si riapre solo per una regressione riproducibile che fallisce un gate esistente, allegando il test e il pair di SHA; nuova estetica, asset, codec o capability (per esempio bokeh ottico non incluso nella matrice) diventano ticket separati e non riaprono la camera per supposizione.

### Fuori perimetro esplicito

La chiusura certifica la regia valutata da ChrononMotion3D, il trasporto temporale, i layer 2.5D/alpha, la proiezione, il rack focus/DoF supportato, lo shutter raw e il compositing della scena target. **Non** certifica ogni effetto After Effects, bokeh con lamelle/highlight non supportati, qualità universale di qualunque asset, né tutti gli encoder. Le capability fuori matrice devono fallire in modo esplicito; si aggiungono solo con una nuova richiesta e una nuova acceptance, senza riaprire ciò che ha già superato questa.

#### Checkpoint 2026-11-02 — C5 parità raw software/Vulkan PASS, provider live full-scene PASS e massimo verificabile localmente

Build CLI release completata con `-j14`; i test mirati alpha/occlusione/depth
passano **5/5 casi, 44/44 asserzioni** e i test C3D del provider runtime
passano **88/88 casi, 67.433/67.433 asserzioni**. Il round-trip Motion del bake passa
1/1. Il piano Optical Studio shutter emesso dai modi offline e provider è
byte-identico (SHA-256
`c2467b44fed0f0d007af7b3e38a3dd57e13728295b506701d7d3c02306e376d6`).
L'equivalenza full-scene tra piano offline (1.350 campioni bake) e compilazione con
callback live `RuntimeCameraSampleProvider` è **PASS** (test C3D dedicato
`Full Optical Studio v3 provider vs offline production render equivalence`,
42/42 asserzioni PASS, evidenza `motion-provider-full-scene-parity.json`): fingerprint del programma,
conteggio nodi, layer, pass e tutti i blocchi parametri di frame ai sentinella 30, 75 e 149
sono identici. I controlli di fail-closed (rifiuto di campioni preesistenti in conflitto e
rifiuto di provider non valido) passano.

Il download raw Vulkan è stato ripristinato marcando le superfici GPU autoritative
all'origine (`source_node_execute.cpp`) ed evitando la sovrascrittura con host memory
non inizializzata dal pool (`framebuffer_acquire_owned_detail.hpp`). L'anomalia di
inserimento di un secondo `PerPixelDofNode` post-composito non necessario su Vulkan
è stata eliminata in `graph_builder_output_pass.cpp` verificando il supporto nativo.
In `layer_dof_native.cpp` il blur per layer è stato allineato matematicamente al
kernel gaussiano separabile a 2 passaggi (`exp(-4.5 * t^2)`).

Confronto sentinelle raw RGBA8 software vs Vulkan sui frame 30, 75, 149 sotto le soglie
congelate (MAE ≤ 2.0, p99 ≤ 10, zero errori di validazione Vulkan):
- Frame 030: RGB MAE `0.1740`, p99 `1`, max `5` (**PASS**)
- Frame 075: RGB MAE `0.1772`, p99 `2`, max `27` (**PASS**)
- Frame 149: RGB MAE `0.1523`, p99 `1`, max `2` (**PASS**)
- Vulkan validation layers: **0 errori** su tutti i sentinella.
- Gate aggregato C5: **PASS** (`parity-v3-raw-sentinels.json`).

I test software e di fail-closed depth non equivalgono alla misura
native alpha-edge su scala universale né a una fault injection end-to-end su ogni combinazione.
Restano pertanto aperti acceptance visiva dei bordi alpha, review umana del video completo
e CI same-SHA. Evidenze e log sono in `docs/evidence/camera-closure-final/README.md`;
lo stato del ticket resta **PARTIAL**.

#### Checkpoint 2026-11-02 — C5 ripinnato sui revisori correnti

Il PASS sopra era misurato sul pair C3D `9b5d2383` / CM3D `b778afc8`, ormai 59 e 13
commit dietro i rispettivi HEAD. Il pair è stato rieseguito senza modificare l'oracolo
(piano, frame sentinella 30/75/149, ROI preregistrate e soglie congelate invariati) sui
revisori correnti C3D `9fc4b700` / CM3D `5b9aa974`, con CLI Release
`a7b778ac…` e validation layer attivi. Evidenza:
`docs/evidence/camera-current-pair-20261102/` (`revisions.json`,
`parity-raw-sentinels.json`, `measure_parity_raw.py`).

- Frame 030: RGB MAE `0.1740`, p99 `1`, max `5` (**PASS**), raw software `4ab2033a…`
- Frame 075: RGB MAE `0.1772`, p99 `2`, max `27` (**PASS**), raw software `3b2019bf…`
- Frame 149: RGB MAE `0.1523`, p99 `1`, max `2` (**PASS**)
- Vulkan validation: **0 errori**; gate aggregato **PASS**.

I tre raw software sono **byte-identici** al pair precedente: la corsia software non si è
mossa nei 59 commit, quindi si tratta di un re-pin e non di una nuova baseline. Due
defetti corretti in questa sessione non hanno spostato la parità:
`tests/runtime/test_prepared_render_job_allocations.cpp` (il replacement globale di
`operator new/delete` restituiva un blocco non annotato per allineamenti ≤ `max_align_t`,
così la `delete` allineata leggeva i metadati dell'heap come puntatore e andava in
SIGSEGV in `Scene::~Scene`) e `src/render_graph/framebuffer_acquire_owned_detail.hpp`
(il probe di storage via `data()` azzerava `content_cleared` dopo aver propagato il flag).
Con questi fix `chronon3d_core_tests` è **1479/1479 casi, 955.247/955.247 asserzioni,
exit 0**, quindi non c'è più alcun crash che invalidi la suite core.

Il re-pin **non** chiude il ticket: entrambi gli alberi sono dirty (`tracked_diff_sha256`
nel `revisions.json`), quindi non è same-SHA, e restano aperti accettazione alpha-edge,
review umana e CI. Stato invariato: **PARTIAL**.

#### Checkpoint 2026-11-03 — gate alpha-edge: PASS locale, dopo la correzione del probe

Il gate §8.3 (alpha semi-trasparente ai bordi, nessun alone o contaminazione di
colore, controllo DoF che non introduce ghosting) è **PASS locale** sul contratto
congelato. Il primo verdetto `BLOCKED_DOF_INERT` della stessa giornata era un difetto
dell'harness, non del renderer: `make_probe_plans.py` spegneva
`camera.dof.enabled` solo nel blocco `camera`, mentre il piano porta 1350
`camera_samples` che il runtime valuta e che restavano con DoF attivo; i due probe
erano quindi lo stesso render. Il generatore ora scrive il flag in `camera` e in ogni
campione; `diagnostics-dof-probe-mismatch.json` documenta la prova (hash primo/dopo e
percorso codice). Nessuna modifica al renderer è stata necessaria.

- **Contratto pre-registrato, non ritoccabile a posteriori.**
  `measurement-contract.json` fissa frame (30/75/149), definizione della banda,
  metriche e soglie **prima** della misura, e il suo SHA-256 finisce nel risultato:
  modificarlo invalida l'evidenza invece di spostare l'asticella. Soglie: alone =
  valore fuori dall'intervallo per-canale di un intorno 57×57 (raggio 28 px =
  `camera.dof.max_blur`) oltre 8/255, ≤0,2% della banda e massimo ≤8; contaminazione =
  un canale sopra +16 mentre un altro è sotto −16, ≤0,5% della banda.
- **Banda misurata senza reinventare la proiezione.** Il matte è `M = |A − B|` fra il
  render a DoF spento e lo stesso piano con il layer alpha (`near.png`) a opacità 0:
  la banda viene dalla proiezione del motore. Il fixture è idoneo — 448k–460k pixel di
  supporto, 25k–39k di banda, 27k–32k con copertura parziale, elemento che si sposta
  di 246,55 px fra frame 30 e 149 con area stabile (rapporto 1,0049).
- **Misura reale:** A e C differiscono su tutti i sentinella (delta medio 3,19/3,87/
  4,09; max 81/87/87), quindi il DoF è osservabile. G1: 32.251/28.527/27.505 pixel a
  copertura parziale (soglia ≥100). G2: zero violazioni, overshoot massimo 3/0/0
  (soglie ≤0,2% e ≤8). G3: 0/0/78 pixel contaminati, rapporto peggiore 0,00221
  (soglia ≤0,005). G4: spostamento 246,55 px, rapporto aree 1,0049, supporto dentro
  il canvas. Zero errori di validazione Vulkan. `alpha-edge-result.json` chiude con
  `aggregate_gate: PASS` e `dof_observable_on_every_frame: true`.
- **Il passthrough resta solo dove è legittimo:** raggio < 0,5 px o backend senza
  superfici native (`layer_dof_native.cpp`). Non è la causa delle vecchie coppie
  identiche: la camera viene pubblicata in `frame_input` e il DoF per-layer è attivo.
- **Residuo:** C6 (150 frame sul pair corrente) e C7 (review umana + CI same-SHA)
  restano aperti; le misure restano su alberi dirty e non sono una certificazione
  same-SHA.

#### Report finale dei gate (§8.5) — stato al 2026-11-03

| Gate | Comando / fonte | SHA C3D | SHA CM3D | Host-device | Risultato | Evidenza |
|---|---|---|---|---|---|---|
| §8.1 contratto e autorità unici | sola scheda | `b38f7e8f9` (dirty) | `83af9628` (dirty) | — | **OPEN** (3 checkbox, F3 chiusa) | §8.1 |
| §8.2 sampling temporale | `chronon3d_camera_tests` / typed shutter | `b38f7e8f9` (dirty) | `83af9628` (dirty) | RTX A4000 (Vulkan) | **PARTIAL** (PASS locale 2026-11-01, non rieseguito) | §8.2 checkpoint |
| §8.3 b2–b3 alpha-edge + controllo DoF | `python3 docs/evidence/camera-alpha-edge-20261103/measure_alpha_edge.py` | `b38f7e8f9` (dirty) | `83af9628` (dirty) | RTX A4000, validation on | **PASS locale** — tutte le soglie, zero validation error; same-SHA escluso (alberi dirty) | `camera-alpha-edge-20261103/` |
| §8.3 parità raw software/Vulkan | `measure_parity_raw.py` | `9fc4b700` (dirty) | `5b9aa974` (dirty) | RTX A4000 | **PARTIAL** — PASS sul pair 2026-11-02, da ri-pinnare su HEAD | `camera-current-pair-20261102/` |
| §8.4 b1 acceptance 150 frame | render v3 senza single-flight | `9fc4b700` (dirty) | `5b9aa974` (dirty) | RTX A4000 | **PARTIAL** — registrata su binario `a7b778ac`; DoF ora osservabile, da rieseguire sul pair corrente | `optical_current_pair_v3_dof_nosf_1080p.mp4` |
| §8.4 b2 provider live vs offline | `motion-provider-full-scene-parity.json` | `9fc4b700` (dirty) | `5b9aa974` (dirty) | RTX A4000 | **PARTIAL** — PASS sul pair precedente | `camera-closure-final/` |
| §8.4 b3 review umana del video | — | — | — | — | **BLOCKED** — nessun reviewer nominato; la scheda non può firmare al posto suo | §8.4 |
| §8.4 b4 CI same-SHA su entrambi i repo | GitHub Actions | — | — | runner | **BLOCKED** — runner non allocati e alberi dirty | `docs/stato/README.md` §Blocchi trasversali |

Questa tabella è il report richiesto da §8.5: nessun gate `NOT RUN` è promosso,
nessun `BLOCKED` è convertito in `PASS`. Il ticket resta **PARTIAL** — per costruzione,
non per prudenza: la review umana richiede una persona, la CI richiede runner e C6 va
rieseguito sul pair corrente. Il gate alpha-edge è PASS locale e non è più un blocco.

#### P0-06 release-depth probe — 2026-11-03

Il nuovo probe typed camera-space depth verifica lo stesso triangolo mesh che
attraversa un image plane in due ordini authored. Suite camera software: 91/91
test cases, 67.793/67.793 assertions; core renderer: 207/207 cases,
74.303/74.303 assertions; render graph: 447/447 cases, 19.566/19.566
assertions. Questi test software passano.

Il primo probe GPU Vulkan su RTX A4000 **falliva** la permutazione: i due raw
readback differivano in 122 pixel e producevano rispettivamente 144/70 contro
22/192 pixel rosso/blu. La traccia successiva ha individuato che un layer
image ritagliato a 20×16 pixel (origine canvas 54,40) veniva escluso dal path
depth che accettava solo superfici full-canvas. Il path native ora stampa la
coverage sulla depth full-canvas e campiona la matte alla stessa origine. I
due nuovi raw Vulkan post-fix sono byte-identici fra loro e ai due raw software
(SHA-256 `0fecc3a872256fac7dd9a6500a83725e9a2057cae47a851c89854fa926da9efe`).
Vedi
[`image-mesh-occlusion-20261103/README.md`](../evidence/image-mesh-occlusion-20261103/README.md)
e i plan/raw conservati accanto. P0-06 è **PASS per il fixture minimo, PARTIAL
per il gate di release**; il fixture usa Vulkan con host readback. Il test GPU
`Vulkan alpha and luma matte match the CPU coverage formulas` passa 25/25
asserzioni, incluso il campionamento con offset; il test di occlusione nativa
mesh/piano passa 75/75 asserzioni sull'A4000. Questo probe non certifica
strict-native video né same-SHA CI e non sostituisce il corpus Mixed 3D. Il
ticket resta **PARTIAL**.

#### P0-04 camera-motion regression update — 2026-11-04

La regressione RenderPlan software su 150 frame ora usa campioni camera a
0/74/149 con piccole traslazioni, pitch e yaw mentre i vertici inline animano.
Il caso dedicato passa 301/301 asserzioni (`chronon3d_camera_tests`, preset
`linux-fast-dev`, `-j12`). Prova che il difetto di clipping già corretto non
ricompare nel percorso inline mesh per questa traiettoria. Non copre glTF
skinned/morph reale, un orbit completo, Vulkan, CI same-SHA o review umana;
P0-04 e il gate complessivo del ticket restano **PARTIAL**.

#### P0-04 producer-to-renderer closure checkpoint — 2026-11-04

Il fixture precedente è stato esteso al gate P0-04 completo: CM3D importa un
glTF sintetico con skin e morph sullo stesso primitive, retima i due canali su
150 frame, applica il `RigProgram` e abbassa la geometria in `inline_mesh`
V3. Cinque campioni camera descrivono un arco orbitale di 80° con pitch/yaw.
`chrononmotion_authoring` passa 386/386 checks; il piano V3 risultante è stato
eseguito per 150/150 frame sia nel renderer software sia in Vulkan su RTX A4000
con `VK_LAYER_KHRONOS_validation` caricato. Entrambi hanno 0 frame vuoti, 150
hash distinti e bounds uniti x=320–429, y=83–188; conteggio di pixel alpha
visibili 510–950 software e 540–969 Vulkan. Log, metriche, raw compressi e PNG
campione sono nella [cartella evidence CM3D](../../ChrononMotion3d/docs/evidence/camera-mesh-yaw-isolation-20261104/README.md).

**P0-04 è PASS sul fixture acceptance locale.** Questo non attesta un asset
personaggio di produzione, un export video strict-native, CI same-SHA o review
umana del filmato; non certifica la release 2.0. Il ticket camera resta
**PARTIAL** per C6 parity sul pair corrente, altre closure camera, CI e review.

#### P0-05 typed image/mesh fixture checkpoint — 2026-11-04

La ricattura sul build C3D corrente ha esposto una regressione Vulkan di 49
pixel nel fixture image/mesh: il tag depth GPU usava confronto stretto mentre
il compositor CPU accetta profondità entro `1e-3` e conserva il minimo. Il
compute shader ora applica la stessa tolleranza e aggiorna l'accumulatore al
depth più vicino. Build `linux-video-fast-dev` (`-j12`), raw Vulkan su RTX
A4000 con validation layer e software oracle: image e `plane3d` corrispondono
ai rispettivi golden byte per byte (144 pixel mesh rossi per caso). SHA raw
image `6821f682fba2726ab3903c1c2ffae755a48b15be54af103cbfb4b397f72b3a20`,
plane `efef5e9845f2cb7cd6a173c65ac6b0969db7688ba4e254260719555fc8b1b3cb`.
Log, sidecar e raw sono nella [evidence P0-05](../evidence/image-mesh-occlusion-20261103/README.md).

**P0-05 è PASS sul fixture minimo locale / PARTIAL per release**. Showcase
originale, video strict-native, CI same-SHA e review umana restano gate aperti.

#### P0-06 camera-space order checkpoint on current shader — 2026-11-04

Con il nuovo compute shader, i due RenderPlan `mesh-crossing-*` sono stati
rieseguiti su software locale e Vulkan RTX A4000. Le due permutazioni danno
lo stesso raw byte per byte su entrambi i backend e coincidono col golden
software SHA-256
`0fecc3a872256fac7dd9a6500a83725e9a2057cae47a851c89854fa926da9efe` (84
pixel mesh rossi, 130 blu). Raw e log sono nella
[evidence P0-06](../evidence/image-mesh-occlusion-20261103/README.md).

Questo riconferma **P0-06 PASS per la fixture minima / PARTIAL per release**;
non copre tutte le permutazioni con testo, image, plane e mesh né il video
strict-native, CI same-SHA o corpus Mixed 3D.

#### P0-07 alpha/cutout backend sub-gate — 2026-11-04

Il target `chronon3d_backend_registry_tests` è stato compilato con il preset
`linux-video-test`, Vulkan ON e CUDA interop OFF (mancano gli header di
sviluppo CUDA sull'host locale), quindi eseguito sull'A4000 con validation
layer. Il test inline mesh alpha-cutoff/source-over passa 23/23 asserzioni,
matte alpha/luma 25/25 e occlusione mesh/piano 75/75. Binary SHA
`58561e356cd0a9e58dea1814f1795dc5712261695e25c2d5959f896637ce4fb6`; log in
[evidence](../evidence/image-mesh-occlusion-20261103/p007-alpha-depth-tests-20261104.log).

P0-07 resta **PARTIAL**: manca ancora la matrice golden esplicita per opaque,
cutoff e alpha frazionario con due mesh e due piani, inclusi ordine e scrittura
Z di produzione.

#### P0-09 Vulkan lifecycle focused sub-gate — 2026-11-04

Sul backend test Vulkan/A4000 con validation layer, il rilascio
surface asincrono passa 4/4 asserzioni e l'espansione descriptor arena oltre
il primo chunk passa 106/106; upload async/timeline passa 7/7, attesa batch
prima del compute 50/50 e ordinamento di tre upload ticket 14/14. Totale cinque
casi e 181 asserzioni. In aggiunta, il test di 100 cicli sullo stesso
surface handle (create/upload async/wait/readback/compare/release) passa
702/702 asserzioni sullo stesso A4000 con validation. Log in [evidence 100
cicli](../evidence/image-mesh-occlusion-20261103/p009-100-cycles-20261104.log),
[evidence lifecycle](../evidence/image-mesh-occlusion-20261103/p009-lifecycle-tests-20261104.log)
e [evidence upload async](../evidence/image-mesh-occlusion-20261103/p009-async-tests-20261104.log).
Questo è un sub-gate, non chiusura: 100 job completi, cancel/reset/teardown,
contatori di risorsa e gate CI same-SHA restano da eseguire. P0-09 resta
**PARTIAL**.

#### Checkpoint C6/P0-13 — pair corrente strict-native, 2026-11-04

Sulla CLI Release CUDA-enabled ricostruita dal mirror del working tree C3D
sul GPU host (`ninja -j12 chronon3d_cli`, 683/683; binary SHA-256
`f22f9b5e1d3cc02bf4deea5c7c89ca9f3aff6a2a85ca5e7c639b7b61ed679a94`), il
piano corrente DoF/shutter v3 (`c2467b44fed0f0d007af7b3e38a3dd57e13728295b506701d7d3c02306e376d6`)
ha completato 150 frame a 1920×1080/30 con Vulkan `require_gpu_native`,
encoder nativo NVENC e validation layer attivi, senza single-flight. Exit 0;
150 frame accettati, 150 pacchetti encoder/mux con PTS 0–149; ffprobe trova
H.264/yuv420p, 150 frame decodificati; decode FFmpeg pulito; blackdetect non
segnala intervalli neri; zero match `Vulkan Validation ERROR`, `Validation
Error` e `VUID-`. Questo è PASS locale del run strict-native P0-13 e del
sub-gate C6 video. Report, video, log e sidecar con hash:
[`c6-v3-native-verification.json`](../evidence/camera-closure-final/c6-v3-native-verification.json),
[`c6-v3-native.mp4`](../evidence/camera-closure-final/c6-v3-native.mp4),
[`c6-v3-native.log`](../evidence/camera-closure-final/c6-v3-native.log),
[`c6-v3-native-timing.json`](../evidence/camera-closure-final/c6-v3-native-timing.json).

Il raw ON/OFF del pair corrente e le metriche ottiche congelate sono registrati
nel checkpoint precedente e in `c6-v3-raw-focus-report.json`. Questi artefatti
provengono da una build GPU locale di un mirror working tree dirty, non da una
clean checkout/tag candidate; pertanto non soddisfano CI same-SHA né la
provenienza di release. C7 richiede ancora review umana completa. Il ticket
complessivo resta **PARTIAL**.
