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
- **Stato 2026-10-31:** S0 (guardrail), S1 (censimento consumer) e S4
  (projection detail) completati. Il censimento A locale è completato; S3 passa
  build, gate decoder/DoF/camera e tre sentinels raw software byte-identici.
  In S5, Optical Studio, Word Rack Focus, i producer map 2.5D enumerati e
  Documentary Showcase ora richiedono campioni risolti per i layer visuali
  animati; Particle e Adjustment sono ammessi dal consumer (l'Adjustment del
  Documentary Showcase è statico e senza track Motion). Corretto anche il producer WordLens:
  prima impostava iris poligonali (8 lamelle), che il kernel C3D rifiuta; il
  highlight gain 1.25 è invece supportato dal bokeh circolare. Il pack usa ora
  i default circolari. Nel working tree corrente `applyFocusSequence` accetta
  gain in [0, 8] e rifiuta le iris; build mirata aggiornata e 5 CTest camera/
  authoring PASS il 2026-11-02. La suite completa resta un gate della DoD.
  F3 è PASS sul canary 2.5D con ordine depth verificato. Il 2026-10-31 tutti
  gli applier camera Motion sono stati instradati in `CameraRigTrackBatch`, con
  flush bounded per i percorsi campionati; il writer è stato poi spostato da
  `CameraRig.hpp` all'header source-private
  `src/chrononmotion/motion/CameraRigTrackBatch.hpp`. I 7 test camera/authoring
  mirati passano dopo lo spostamento; il canary confronta i campioni con le
  chiamate dirette `Track::add`. Il ticket
  resta aperto: il passaggio a dati preset tipizzati, la riduzione degli
  header pubblici e il confronto lowering-per-lowering non sono conclusi. Nessun
  test è stato rimosso.

Il main aggiornato documenta anche test mirati DoF (36/36) e decoder/shutter
(165 asserzioni); la suite camera completa ha invece tre casi rendering/golden
falliti nel worktree isolato. Il gate generale resta aperto.

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
Al censimento iniziale `camera_shot_report` era usato solo dall'overlay
diagnostico. La cleanup corrente rimuove report e overlay insieme ai caller, e
rimuove `evaluated_projection`; il test golden usa ora direttamente il
contratto di proiezione. `camera_state_builder` resta: l'audit completo ha
trovato 19 test client, e il builder crea solo un valore renderer-ready.
`camera_projection.hpp` resta usato dai test di proiezione e stabilizzazione.

**S3 — migrazione wire implementata:** CM3D non serializza
più `camera.dof.aperture`; schema, `CameraPlan`, fingerprint e decoder C3D non
accettano più il campo. Il renderer ricava l'unità interna del kernel da
`f_stop`. I test decoder coprono il rifiuto della vecchia chiave; tre sentinelle
raw software sono byte-identiche secondo l'evidenza locale. I gate mirati di build,
contract camera, DoF kernel, decoder e `chrononmotion_authoring` sono PASS
(risultati nel README evidence).
La stessa cleanup ha eliminato i campi typed duplicati `dof_f_stop` e
`dof_iris_blades`: il decoder normalizza i rispettivi alias wire nei campi
camera-level e rifiuta valori in conflitto; CM3D non emette più gli alias.

### Demolition debt — alias wire annidati f-stop/iris

- **Owner:** manutentore del confine CM3D/C3D.
- **Reason:** piani RenderPlan v2 già salvati possono avere `dof.f_stop` e
  `dof.iris_blades`; i nuovi producer emettono solo i campi camera-level.
- **Exit condition:** corpus supportato e producer downstream non contengono più
  le due chiavi annidate; la scansione del corpus/versione viene archiviata qui.
- **Equivalence gate:** i piani migrati decodificano con gli stessi campioni
  camera e superano il gate raw-pixel della sezione 6.
- **Removal scope:** proprietà alias nello schema, normalizzazione/conflitto nel
  decoder e i test compatibilità associati.
- **Status:** `ACTIVE` — input legacy accettato, emissione nuova già canonica.

### Demolition debt — traduzione nel tool `camera_plan_bake`

- **Owner:** manutentore del tool C3D.
- **Reason:** CLI e SDK Docker lo distribuiscono come convertitore di vecchi
  piani camera in campioni risolti; la lettura del guadagno legacy avviene solo
  in questo processo di migrazione, non nel modello o decoder renderer.
- **Exit condition:** fixture e consumer del convertitore usano `f_stop` e il
  manifest SDK non richiede più il tool per migrare input storici.
- **Equivalence gate:** bake roundtrip bit-deterministico e frame equivalence
  del piano prima/dopo la conversione.
- **Removal scope:** conversione `aperture`→`f_stop` in `tools/camera_plan_bake.cpp`,
  fixture legacy e target/packaging CLI soltanto dopo il gate.
- **Status:** `ACTIVE` — il tool è mantenuto perché ha consumer di packaging.

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
| Dismissione wire DoF `dof_aperture` (C5) e campi typed duplicati | producer CM3D non serializza l'apertura legacy; tipo C3D con una sola autorità per f-stop/iris; gli alias nested wire normalizzano e verificano conflitti | source migration implementata; runtime/pixel gate ancora richiesto; l'apertura interna `DepthOfFieldSettings` resta parte del kernel |
| Diagnostica (`camera_2_5d_log.cpp`, `camera_shot_report`, overlay, `evaluated_projection`) | grep consumer dopo rimozione dei soli caller diagnostici | overlay/report/snapshot rimossi; `camera_state_builder` mantenuto per 19 test client; projection API coperta da test e dall'audit S4 |
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

- [x] **S0 — Guardrail anti-rigrow prima di tutto.** Nuova sezione in
  `BOUNDARY_C3D.md` (e allineamento `ARCHITECTURE.md`): "ogni header `Camera*` nuovo in
  `motion/` è (a) preset-dato o (b) servizio geometrico con riga nel registro; mosse
  editoriali note = pack, non header; ogni campo che passa il confine = gamme
  fail-closed identiche, unità authored unica, default che non arminino kernel assenti".
  Gate: `chrononmotion_boundary` PASS; doc aggiornata nella stessa PR.
  **Eseguito 2026-10-31:** sezione S0 in `BOUNDARY_C3D.md`, regola nel ledger
  `CHRONONMOTION.md` e matrice camera in
  `Chronon3d/docs/architecture/RUNTIME-OWNERSHIP-MATRIX.md`.
- [x] **S1 — Inventario file-by-file con tabella consumatori** →
  [docs/evidence/camera-surface-consolidation](../evidence/camera-surface-consolidation/README.md).
  Confluisce col censimento **A** del ticket AE: *una sola tabella, in entrambi i
  ticket con link, mai due censimenti divisi*.
- [ ] **S2 — Collasso applier → dati (CM3D).** Un meccanismo: CameraRig + waypoint +
  `presets::`. Ogni eliminazione header: (1) grep degli include per mappare i consumer;
  (2) migrazione del chiamante al preset/pack; (3) include dei test aggiornati;
  (4) `-Wall -Wextra -Werror` + ctest `release`/`release-fast` verde. **Vietato** creare
  un secondo resolver/sampler: una sola autorità di valutazione (regola del ticket AE).
  **Prerequisiti chiusi localmente:** censimento A e canary F3 Z/depth; la migrazione
  può iniziare senza attendere altri gate camera. Il solo fatto che nove helper
  scrivano su `CameraRig` non prova che siano intercambiabili: traiettoria continua,
  handoff/cut, focus, shake e ricette editoriali hanno semantiche diverse. Il target
  provvisorio 13→6 si accetta solo con migrazione a dati tipizzati e confronto output;
  spostare dichiarazioni in un umbrella non conta come consolidamento.
  **Avanzamento 2026-11-02:** due header autonomi (`CameraSequence.hpp`,
  `CameraShake.hpp`) sono stati assorbiti in `CameraMotion.hpp`; consumer
  implementation/test aggiornati. I test sequence, shake, authoring e bake
  passano (373 check authoring, 832 bake), e il boundary guardrail Motion è
  verde. È una riduzione incrementale di superficie: il target 13→≤6 e la
  migrazione degli applier a dati tipizzati con confronto lowering restano aperti.
- [x] **S3 — Dismissione legacy DOF.** La migrazione wire è implementata nel
  working tree: producer, schema, tipo, decoder e fingerprint non trasportano
  più `dof_aperture`; l'adapter deriva il parametro interno del kernel da
  `f_stop`. C1/C5 in `BOUNDARY_C3D.md` sono aggiornati. Build, gate decoder,
  DoF e camera, `chrononmotion_authoring` e raw software frame 30/75/149
  byte-identici sono PASS. Le spelling nested legacy `f_stop`/`iris_blades`
  restano input-only e sono un demolition debt separato.
- [x] **S4 — Audit projection/diagnostica C3D.** Tabella consumer per ciascun file;
  spostare in `namespace detail` o dismettere; nulla si elimina senza grep consumer a zero.
  **Eseguito 2026-10-31:** `camera_projection_{matrix,clip,frustum}` spostati in
  `include/chronon3d/math/detail/` e riclassificati nel manifest SDK; resolver e
  contratti restano gli unici punti pinned. La cleanup successiva ha rimosso i
  wrapper diagnostici senza consumer indipendenti, mantenendo il value builder
  chiamato dai test; il censimento aggiornato è in
  `docs/evidence/camera-surface-consolidation/`.
- [ ] **S5 — Coordinamento demolition-debt ATTIVI:** i piani materializzati V3 dei
  producer Template passano ora dal wrapper con `requireResolvedLayerSamples`; lo
  stesso vale per Optical Studio, Word Rack Focus, camera-backed Map Flyover,
  Documentary Showcase e Showreel camera-free. Motion consente il campionamento layer
  anche senza CameraRig. I layer visuali Particle e Adjustment sono ammessi dal
  consumer. Documentary emette 151 campioni sui 14 layer visuali animati; Adjustment
  resta statico e senza track Motion. Word Lens emette 361 campioni per ciascuno di 8
  layer visuali, Showreel 151 per ciascuno di 11 testi. Inventario/prove in
  [`resolved-sample-migration.json`](../evidence/camera-surface-consolidation/resolved-sample-migration.json).
  La view video di compatibilità è stata rimossa; il gate di equivalenza resta aperto (§3 G del ticket AE). S2
  può procedere ora che F3 ha anche la prova di ordine/profondità 2.5D.
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
- [x] C3D: projection pinned unica; legacy DOF field/site rimossi con grep-prova;
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
(CAMERA-DEFINITIVE-CLOSURE); iris poligonale/kernel shaped-bokeh (ticket separato; il kernel circolare con highlight weighting è supportato); espressioni;  `.aep` importer; MeshLayer/ADR Object3D-Layer; plate GPU; prestazioni encoder/DoF
  (la scelta di lane è già chiusa in CAMERA-DEFINITIVE-CLOSURE §5 F2); GUI. Il ticket non tocca endpoint
encoder, scheduler calendar né schema V3 salvo dove S3/S5 lo prescrivono.
