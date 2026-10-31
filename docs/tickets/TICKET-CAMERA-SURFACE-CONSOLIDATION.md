# TICKET-CAMERA-SURFACE-CONSOLIDATION — Snellimento della superficie camera (CM3D ↔ C3D)

- **Stato:** OPEN — censimento e piano. **Nessun cambio di contratto:** le zone CLOSED
  C1–C7 di [`chrononmotion/docs/BOUNDARY_C3D.md`](../../../../ChrononMotion3D/docs/BOUNDARY_C3D.md)
  e i gate pinned (proiezione < 0.02 px, bake roundtrip bit-deterministico, gate schema
  camera-field) restano intatti. Questo ticket riorganizza le *superfici di authoring e
  consumo* attorno alle autorità esistenti e rimuove vene legacy; non ridefinisce il confine.
- **Priorità:** P2 — subordinato al censimento **A** di
  [TICKET-AE-CAMERA-FUNCTIONAL-PARITY](TICKET-AE-CAMERA-FUNCTIONAL-PARITY.md)
  (che confluisce qui, non duplica); non possiede gate di release camera.
- **Repository:** `ChrononMotion3D` (autoring), `Chronon3d` (consumo), `ChrononTemplate`
  (solo come destinazione dei pack editoriali, senza toccarne le API).
- **Owner:** manutentore del confine ChrononMotion3D / Chronon3d.

---

## 1. Diagnosi — il problema è la superficie, non l'architettura

Il confine camera funziona: Motion possiede rig/gerarchie/traiettorie, Chronon3d guarda
proiezione/depth/DoF/shutter, un solo bake, parità pinned. Il difetto è che
*attorno* a quelle autorità si è accumulato un vocabolario parallelo senza criterio.

**Inventario verificato nel working tree (2026-10-31):**

| Repo | Superficie camera | Numeri |
|---|---|---|
| `ChrononMotion3D/include/chrononmotion/motion/` | 13 header (`Camera*`, `DocumentaryCameraPath`, `TitleCameraRecipe`) | ~1.664 righe |
| `Chronon3d/include` + `src` | ~25 file camera/DOF (projection ×5 varianti, scene/camera ×4, model/camera ×5, per-pixel DOF ×3, adapter, log, policy) | da censire consumer-by-consumer (S4) |
| `ChrononTemplate` (pack) | `TitleCameraPack`, `CameraMotionPack`, `DocumentarySnapshotPack`, `UiCameraChoreography`, `WordLensPack`, … | quattro layer = legale, ma è il *posto* dove le recipe devono finire |

### Censimento consumer locale — 2026-10-31 (PARTIAL)

Ricerca nel working tree di `ChrononMotion3D`, `Chronon3d` e `ChrononTemplate`,
include diretti per basename e ricerca dei simboli per i due header raggiunti via
umbrella. I conteggi escludono documenti e il file stesso, ma includono i `.cpp`
che implementano l'header; non dimostrano assenza di consumer esterni o include
indiretti. **Nessun header pubblico viene rimosso sulla base di questo grep.**

| Header Motion | Include diretti nel workspace | Consumer rappresentativi / classificazione |
|---|---:|---|
| `CameraRig.hpp` | 62 | authority; lowering, tool e pack |
| `CameraMotion.hpp` | 13 | applier/preset; `Presets.hpp`, `DocumentarySnapshotPack` |
| `CameraFocusSequence.hpp` | 4 | applier; `WordLensPack`, `DocumentaryCameraPath` |
| `CameraFraming.hpp` | 5 | geometria; recipe/sequence e test |
| `CameraPanelChoreography.hpp` | 4 | applier; `UiCameraChoreography` |
| `CameraRecipe.hpp` | 10 | applier; `TitleCameraPack`, `CameraMotionPack` |
| `CameraScreenSpace.hpp` | 16 | geometria; framing, sequence, pack UI e test |
| `CameraSequence.hpp` | 4 | applier; `SceneCameraPack` |
| `CameraShake.hpp` | 3 | procedurale; tool `emit_food_story_examples` |
| `CameraTrajectory.hpp` | 3 | applier; umbrella `Motion.hpp` e test |
| `CameraValidation.hpp` | 4 | validazione; `UiCameraChoreography` e test |
| `DocumentaryCameraPath.hpp` | 4 | applier; `DocumentarySnapshotPack` |
| `TitleCameraRecipe.hpp` | 4 | applier; `TitleCameraPack`, `DidoneTitlesPack` |
| `AspectReframe.hpp` | 1 | incluso da `Motion.hpp`; `autoReframeRig` usato indirettamente da test e tool |
| `EyeTrace.hpp` | 1 | incluso da `Motion.hpp`; `EyeTraceDirector::bakeInto` usato indirettamente da test e tool |

I conteggi della tabella e la lista originale «13» non coincidono: qui sono
contati anche `AspectReframe` ed `EyeTrace` (15 totali). La ricerca sugli include
non basta a dichiarare un'API inutilizzata. Su C3D, `camera_projection_matrix`,
`camera_projection_clip`, `camera_projection_frustum` e `camera_projection_resolver`
sono dichiarati in `cmake/Chronon3DPublicHeaders.cmake`; clip/frustum/resolver
sono inclusi anche da test. `camera_2_5d_projection` ha consumer runtime e test;
`camera_change_policy` è usata dal delta compiler, da scene setup e dalla tile
policy; `publish_dof_provenance` è invocata dal compositing e usata dal DoF.
`camera_shot_report` è usato dall'overlay diagnostico, `camera_state_builder`
dal `scene_builder`. Nessuna rimozione sicura è dimostrata dal solo censimento.

**S3 bloccato da consumer reali:** Motion `RenderPlanLoweringCamera.cpp` deriva
`dof_aperture` da f-stop e `RenderPlanEmission.cpp` lo serializza; in C3D
`render_plan_camera_adapter.hpp` lo assegna a `camera.dof.aperture` e il decoder
lo valida. Non è quindi sicuro cancellare il campo o la conversione prima di
migrare produttore, schema e consumer e di verificarne l'equivalenza.

**La prova che la proliferazione è accidentale.** Dieci dei tredici header CM3D sono
*applier* con la stessa firma — scrivono chiavi nel rig, non definiscono meccaniche:

```text
applyFocusSequence(CameraRig& rig, ...)         // CameraFocusSequence.hpp (53)
applyTitleCameraRequest(CameraRig& rig, ...)    // TitleCameraRecipe.hpp (75)
applyPanelChoreography(CameraRig& rig, ...)     // CameraPanelChoreography.hpp (34)
applyDocumentaryCameraPath(CameraRig& rig, ...) // DocumentaryCameraPath.hpp (51)
applySequence(CameraRig& rig, ...)              // CameraSequence.hpp (50)
authorCameraTrajectory(CameraRig& rig, ...)     // CameraTrajectory.hpp (90)
applyCameraTrack / applyRecipe(CameraRig& ...)  // CameraRecipe.hpp (97)
apply(CameraRig& rig, ...) + ~30 shot            // CameraMotion.hpp (574)
bakeInto(CameraRig& rig)                        // EyeTrace
autoReframeRig(const CameraRig& ..., CameraRig& out, ...) // AspectReframe
sampleCameraShake(spec, frame, duration)        // CameraShake.hpp (38) — procedurale, non applier
```

Il modello che intendevano è già quello giusto — `presets::`"holds no engine. Every
preset writes keyframes into a Layer's (or a CameraRig's) channels". La proliferazione
è stata un file per mossa editoriale, con categorie ("recipe", "sequence",
"trajectory", "path", "choreography", "shake", "eye trace") che sono sinonimi,
non significati architetturali.

## 2. Regola di censimento e classificazione

Ogni tipo/funzione camera-facing, nei due repo, entra in **uno** dei cinque bucket:

1. **Authority** in un solo posto per lato: `CameraRig` (CM3D);
   `camera_contract.hpp` + `camera_projection_contract.hpp` + `Camera2_5D`/DoF path (C3D).
2. **Applier** — dati o funzione che scrive chiavi nel rig/layer. Hanno il diritto di
   esistere come *dati* (`presets::`, tabella waypoint), non come header per mossa.
3. **Servizi geometrici** — input rig → output misura/feed-forward:
   `CameraScreenSpace`, `CameraFraming`, `AspectReframe`, `CameraValidation`.
4. **Contratto producer→consumer** — lowering, emission, decoder, provider, scheduler.
   Toccato solo se un campo vero passa il confine.
5. **Diagnostica** — debug overlay, shot report, camera_2_5d log, GPU timing per pass.

**Regola ammissibile di vita-morte:** ogni file deve avere ≥1 consumatore reale
(test, tool, pack, job). Grep per include nel repo; a zero consumer → candidato
dismissione (git history lo conserva).

## 3. Target end state (prima/dopo)

### Lato `motion/` (CM3D)

| Cosa resta | Cosa diventa | Cosa esce |
|---|---|---|
| `CameraRig.hpp` — authority unica | collasso in un solo tipo waypoint/traiettoria assoluta + mosse relative (dolly/pan/tilt/orbit/roll/shake) → già `presets::` | `CameraRecipe`, `CameraPanelChoreography`, `TitleCameraRecipe`, parts di `CameraMotion`, `DocumentaryCameraPath` → **preset composti** (dati in `Presets_*`) o **pack in `ChrononTemplate`** |
| `presets::` (già scritta correttamente) | la schiera di shot editoriali (handoff CameraMotion, bokeh …) è *contenuto*, non meccanismo: dati | `EyeTrace` o come preset composto o pack; se resta, una riga in `BOUNDARY_C3D.md` giustifica perché |
| `CameraValidation.hpp` | resta (bucket 3) | — |
| `CameraScreenSpace.hpp`, `CameraFraming.hpp` | restano (bucket 3) | `AspectReframe` → merge/registro se non ha consumer esclusivi |
| lowering + provider + emission | bucket 4 — si tocca SOLO per rimuovere vene (S3) | — |

Target conseguito: da 13 header a **6** (CameraRig, Presets, Motion, CameraValidation,
CameraScreenSpace, CameraFraming), senza perdere un singolo preset o pack. Ogni test
esistente resta, cambia solo l'include.

### Lato Chronon3d (snellimento)

| Azione | Base | Verifica |
|---|---|---|
| Congelare la verità: `camera_contract.hpp` + `camera_projection_contract.hpp` come unici punti pinned di ammissione | `motion_projection_parity.cpp` < 0.02 px; `motion_camera_bake_roundtrip.cpp` bit-determinismo | nessun altro sito ridichiara le stesse gamme |
| Audit delle 5 varianti di projection (`camera_projection_{matrix,clip,frustum,resolver}`, `camera_2_5d_projection`) | grep consumer standalone per ognuna | public API solo quelle con consumer fuori `src/`; le altre in `namespace detail` o merge; la tabella consumer va in `docs/evidence/` |
| Dismissione legacy DOF: field `dof_aperture` (C5 — LEGACY TRANSPORT, solo derivata) e sito preview `gain = 0.672/f_stop` (C1) | verificare che **nessun producer authorizza** e nessun consumer legge il campo come authored | rimozione del sito + commento LEGACY; il fail-closed C2/C3 non cambia |
| Diagnostica (`camera_2_5d_log.cpp`, `camera_shot_report`, `camera_state_builder`, `camera_change_policy`, `composite_dof_provenance`) | grep consumer | merge/dismissione con tabella; la diagnostica concatenate al testo `PLAN-FAIL` non si tocca senza |
| `render/` CM3D (threepp harness) | fuori perimetro — il perpetuo `check_render_scope.cmake` dice harness; il plate GPU è discusso solo nel §1 del ticket AE | — |

## 4. Collegamento al port threepp (e al ticket AE)

Il port da threepp corrente (physical camera: exposure da aperture/shutter/ISO,
intrinsics OpenCV, lens distortion; G-buffer metric depth + motion vectors;
EffectComposer DoF/bloom; glTF loader animato/skinning/meshopt; serializzazione scena)
è "attrezzi, non piano":

- alimenta **D** (ottica + fuoco animato) e **F** (DoF/alpha oracle) del ticket AE;
- il loader glTF animato sblocca i rig futures ("narrow glTF motion import");
- **non da riprendere:** Vulkan ray-tracing, PhysX, sensors, editor, python, splats,
  controls — è la metà game/sim che il confine esiste per non riportare;
  **DLSS SDK proprietario e asset non-MIT = mai.**
- onere: `THREEPP_TEST_COMPATIBILITY.md` e il ledger `CHRONONMOTION.md` aggiornati
  nella stessa PR del port, e guardrail (`check_motion_boundary.cmake`) verde.

## 5. Piano ordinato (gate di verifica per step)

- [ ] **S0 — Guardrail anti-rigrow prima di tutto.** Nuova sezione in
  `BOUNDARY_C3D.md` (e allineamento `ARCHITECTURE.md`): "ogni header `Camera*` nuovo in
  `motion/` è (a) preset-dato o (b) servizio geometrico con riga nel registro; mosse
  editoriali note = pack, non header; ogni campo che passa il confine = gamme
  fail-closed identiche, unità authored unica, default che non arminino kernel assenti".
  Gate: `chrononmotion_boundary` PASS; doc aggiornata nella stessa PR.
- [ ] **S1 — Inventario file-by-file con tabella consumatori** → `docs/evidence/`
  camera-surface-consolidation. Confluisce col censimento **A** del ticket AE: *una sola
  tabella, in entrambi i ticket con link, mai due censimenti divisi*.
- [ ] **S2 — Collasso applier → dati (CM3D).** Un meccanismo: CameraRig + waypoint +
  `presets::`. Ogni eliminazione header: (1) grep degli include per mappare i consumer;
  (2) migrazione del chiamante al preset/pack; (3) include dei test aggiornati;
  (4) `-Wall -Wextra -Werror` + ctest `release`/`release-fast` verde. **Vietato** creare
  un secondo resolver/sampler: una sola autorità di valutazione (regola del ticket AE).
- [ ] **S3 — Dismissione legacy DOF.** Dopo la prova pre-S3 (grep che dimostri che
  nessun producer authorizza ancora `dof_aperture` o il gain 0.672), rimuovere il sito,
  il commento LEGACY e il codice ponte;
  aggiornare `BOUNDARY_C3D.md` C1/C5 (e `render_plan_layer_types.hpp`) nella stessa PR.
- [ ] **S4 — Audit projection/diagnostica C3D.** Tabella consumer per ciascun file;
  spostare in `namespace detail` o dismettere; nulla si elimina senza grep consumer a zero.
- [ ] **S5 — Coordinamento demolition-debt ATTIVI:** integer transform bake (tutti i
  producer → resolved samples) e view video di compatibilità (exit = §3 G del ticket AE);
  le migrazioni pack **non cominciano prima del fix F3 Z convention**
  (`planPosition` vs `planAnimationPosition`) o il showcase finisce dietro la carta di nuovo.
- [x] **S6 — Allineamento documenti.** `FOLLOWUP_TICKETS.md` indicizza questo ticket;
  le voci F1/F2 non sono più elencate come blocker aperti: il ticket camera §5 e
  l'[evidenza del 30-10](../evidence/closeout-20261030/README.md) documentano F1
  risolto localmente e F2 chiuso come scelta di lane. Il ticket camera complessivo
  resta PARTIAL e i suoi gate di accettazione rimangono aperti.

## 6. Definition of Done (gate vincolanti)

- [ ] Da 13 → ≤6 header come da target §3; ogni mossa editoriale esiste come dato
  (preset o pack), non come header di meccanismo; tabelle consumer in evidence.
- [ ] `grep -r Camera` in `motion/` non rivela nessuna firma `apply*(CameraRig&, ...`
  fuori da presets/waypoint single mechanism (e regressioni).
- [ ] C3D: projection pinned unica; legacy DOF field/site rimossi con grep-prova;
  diagnostica classificata in evidence.
- [ ] Ogni step: ctest `release`/`release-fast` verde (CM3D), `chronon3d_camera_tests`
  + suite camera 82/82 su working tree attuale non regredita (C3D), boundary gate verde,
  producer-consumer canary (`check_render_plan_consumer_canary.sh`) output invariato.
- [ ] Docs `BOUNDARY_C3D.md` + `CHRONONMOTION.md` aggiornati **nella stessa PR**
  di ogni step (regola di manutenzione del confine).
- [ ] Niente degrado di certificazione: nessun test rimosso; i pack mantengono
  output bit-identico al prima (roundtrip scena import/export byte-identici).

## 7. Regola di chiusura e anti-riapertura

Il ticket chiude quando la superficie è ≤ target e **zero regole del guardrail
necessitano eccezioni manuali**. Si riapre solo per una regressione riproducibile che
fallisce un gate esistente. Nuove mosse editoriali non riaprono: entrano come preset
dati o pack, con una riga nel registro se toccano meccanismo.

## 8. Fuori perimetro esplicito

Parità funzionale/certificazione AE (ticket AE); contratto ottica e DoF fail-closed
(CAMERA-DEFINITIVE-CLOSURE); shaped bokeh/kernel (ticket separato); espressioni;  `.aep` importer; MeshLayer/ADR Object3D-Layer; plate GPU; prestazioni encoder/DoF
  (la scelta di lane è già chiusa in CAMERA-DEFINITIVE-CLOSURE §5 F2); GUI. Il ticket non tocca endpoint
encoder, scheduler calendar né schema V3 salvo dove S3/S5 lo prescrivono.
