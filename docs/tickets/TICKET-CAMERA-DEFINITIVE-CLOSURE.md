# TICKET-CAMERA-DEFINITIVE-CLOSURE — Chiusura Definitiva Pipeline Camera (Chronon3d + ChrononMotion3D)

- **Stato**: CHIUSO PER IL PERIMETRO CAMERA (2026-10-30). F1 risolto; F2 chiuso come scelta di lane (residuo software documentato); restano aperti **F3** e la verifica del video showcase completo (sezione 4).
- **Priorità**: P0
- **Repository**: `ChrononMotion3D`, `Chronon3d`
- **Build**: sempre `-j16`.

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
- **F3 — Incoerenza Z `planPosition` / `planAnimationPosition`** (punto 5, sezione 3): unificare la convenzione e aggiungere un test di regressione in `ChrononMotion3D`. Allineare anche l'ancora dell'evidenziatore, che parte ~20 px a sinistra del testo.
