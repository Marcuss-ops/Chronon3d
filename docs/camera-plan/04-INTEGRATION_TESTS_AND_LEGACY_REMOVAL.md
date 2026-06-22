# Chronon3D Camera — Integrazione, test e rimozione legacy per parità After Effects

## Missione

Trasformare il piano camera in una sequenza eseguibile basata sul codice realmente presente su `main`.

Non ricreare componenti già esistenti. Ogni PR deve dichiarare:

1. anchor esistente riutilizzato;
2. gap reale corretto;
3. duplicazione eliminata;
4. test aggiornati;
5. file legacy che possono essere rimossi dopo parity.

## Baseline verificata su main

### Già presente — non ricreare

- `CameraDescriptor` come authoring canonico;
- `ProjectionSpec` con Zoom, FOV e PhysicalLens;
- `LensModel` e `LensPresets`;
- `CameraCatalog` e preset descriptor;
- `CameraProgram` e `compile_camera()`;
- `CameraCompileContext` con cycle detection;
- fingerprint deterministico del descriptor;
- `CameraEvaluationDependency`;
- `StaticCameraSource`, `PoseTracksSource`, `OrbitMotion`, `TrajectoryMotion`;
- `IdleOscillation` e `HandheldNoise`;
- `OrientationSpec`, incluso `OrientAlongPath` come tipo;
- `CameraConstraintSpec`;
- `EvaluatedProjection`;
- `CameraProjectionSource`;
- `camera_projection_contract.hpp`;
- `ShotTimeline` e transizioni;
- `CameraSession` e `ShotTimelineSession`;
- `LayerKind::Camera`;
- `Layer::visible`, `from`, `duration`, `active_at()`;
- `Layer::uses_2_5d_projection`;
- test compiled camera già esistente e già incluso in CMake;
- adapter descriptor legacy già incluso nel target camera.

### Gap reali — implementare

- consolidare `CameraRigMode` e legacy `RigMode` in un solo `CameraNodeMode`;
- agganciare il payload `CameraDescriptor` a `LayerKind::Camera` senza side list;
- compilare active camera dal normale layer stack verso `ShotTimeline`;
- default camera come normale `CameraDescriptor` di composizione;
- correggere `PoseTracksSource` che forza Zoom;
- correggere double look-at nel compiled path;
- implementare `OrientAlongPath`;
- applicare Orbit track/dolly nel basis locale;
- preservare projection/lens/DOF/motion blur/parent nel trajectory path;
- completare focal X/Y, gate fit, anamorphic e clipping nel projection contract;
- spostare `CameraFocusMode` nel common contract;
- evolvere `DepthOfFieldSettings` senza creare un secondo modello;
- eliminare il legacy motion-blur boolean;
- propagare diagnostics nella timeline;
- implementare checkpoint/pre-roll per stateful random-access;
- estendere i test già esistenti e correggere commenti di copertura non aggiornati.

## Livelli di parità

### AE-Core — P0

- One-Node e Two-Node;
- Point of Interest;
- active camera da `LayerKind::Camera`;
- default composition camera;
- world layer vs screen layer;
- Comp Camera come capability effetto;
- Zoom 1:1;
- focal length, film size e Angle of View coerenti;
- focus manuale, POI, layer e LockToZoom;
- Aperture/F-Stop e Blur Level;
- Orbit, Track XY, Track Z e Look At Layers.

### AE-Motion — P1

- parenting camera;
- proprietà animate;
- più camere e Cut;
- temporal motion blur;
- framing Selected/All;
- random-access parity;
- import/export descriptor.

### AE-Extended — P2

- stereo derivata da un master program;
- lens distortion;
- bokeh fisico;
- autofocus stateful;
- formati camera esterni.

Working view e mouse tool restano fuori dal core.

## Integrazione nel render job

Usare i tipi di sessione esistenti e aggregarli nel normale render-session state:

```cpp
struct CameraRenderState {
    camera_v1::CameraSession program_session;
    camera_v1::ShotTimelineSession timeline_session;
    camera_v1::FramingSession framing_session;
    CameraCheckpointStore checkpoints;
};
```

`CameraRenderState` può essere un campo del render job/session esistente. Non deve diventare un manager globale.

Regole:

- una istanza per render job;
- nessuna sessione statica;
- nessuna condivisione mutabile fra worker;
- reset esplicito a inizio render;
- checkpoint invalidati dal fingerprint;
- compilazione fuori dall'hot path.

## Active camera con i layer esistenti

Non aggiungere:

- `CameraLayerSpec`;
- un array camera parallelo;
- `CameraApi::camera_layers()`;
- `LayerCameraMode`;
- un secondo resolver nel renderer.

### Implementazione

1. Il normale authoring layer crea `LayerKind::Camera`.
2. Il layer contiene/referenzia il `CameraDescriptor` mediante un payload tipizzato dedicato al kind Camera.
3. Il composition compiler usa il normale ordine layer.
4. `Layer::active_at()` decide visibilità e intervallo.
5. La camera topmost attiva viene convertita in segmenti `CameraShot`.
6. I cambi producono `Cut`.
7. I gap usano il default `CameraDescriptor` della composizione.
8. Il render path valuta soltanto la `ShotTimeline` compilata.

## World, screen e Comp Camera

Non creare un nuovo enum layer camera.

- `uses_2_5d_projection == true`: world/2.5D camera-aware.
- `uses_2_5d_projection == false`: screen/comp space.
- Comp Camera: capability dichiarata nel descriptor/catalogo effetto esistente.

Tutti consumano lo stesso `Camera2_5D`/`CameraProjectionSource` del frame.

## CameraApi

Mantenere soltanto:

```cpp
scene.camera().descriptor(descriptor);
scene.camera().program(program);
scene.camera().preset("camera.50mm", catalog);
scene.camera().timeline(timeline);
```

Le camera layer passano dal normale layer API.

`CameraApi`:

- non compila a ogni frame;
- non crea sessioni globali;
- non risolve lo stack layer nel renderer;
- propaga compile errors e diagnostics.

## Compilazione fuori dall'hot path

Consentita:

- composition compile;
- template load;
- comando pre-render esplicito.

Vietata dentro:

- `render_frame()`;
- loop tile;
- nodo/layer processor;
- sub-frame motion blur.

## Cache e fingerprint

Il fingerprint descriptor esiste già. Estenderne la copertura quando vengono aggiunti campi canonici:

- `CameraNodeMode`;
- focus mode e target;
- blur level;
- pixel aspect/anamorphic;
- camera layer local-time inputs quando la timeline viene compilata;
- versione del formato compilato.

Non includere stato sessione, puntatori o identità heap.

Separare:

- fingerprint del `CameraDescriptor`;
- fingerprint della `ShotTimeline`/active-camera schedule;
- checkpoint state, che non è parte del programma immutabile.

## Diagnostica

Evolvere i tipi diagnostici esistenti. Non creare un secondo canale di errori.

Codici/gap minimi:

- invalid source;
- empty descriptor id;
- preset not found;
- circular catalog reference;
- invalid node mode;
- invalid projection/lens;
- invalid DOF/focus target;
- null/empty/zero-duration trajectory;
- parent/target missing;
- parent cycle;
- constraint range/failure;
- history unavailable;
- non-finite camera;
- clipping degeneracy;
- transition mismatch;
- invalid default camera.

`ShotTimelineResolver` deve propagare diagnostics dei programmi valutati.

## Piano test canonico

### Suite compiled esistente

`tests/scene/camera/test_camera_program_compiled.cpp` esiste ed è già incluso in `tests/scene_tests.cmake`.

Non ricrearlo.

### Prima attività

- aggiornare il commento iniziale ormai non allineato al codice;
- verificare cosa è coperto realmente;
- aggiungere test mancanti nello stesso file o in file mirati quando il perimetro cresce;
- non lasciare checklist che dichiarano assenti feature già presenti.

### Copertura da aggiungere/verificare

- PhysicalLensProjection;
- HandheldNoise;
- cycle detection;
- descriptor fingerprint;
- CameraEvaluationDependency;
- One/Two Node canonical mode;
- PoseTracks non forza Zoom;
- double-look-at regression;
- Orbit local basis;
- trajectory field preservation;
- OrientAlongPath;
- DOF focus modes;
- active camera layer schedule;
- timeline diagnostics;
- checkpoint parity.

## Test semantica After Effects

### Node mode

- One-Node ignora POI;
- Two-Node usa POI;
- roll/pan/tilt locali applicati una volta;
- conversione dei due enum rig legacy verso il modo canonico.

### Zoom e ottica

- depth = Zoom -> 1.0;
- depth = 2 × Zoom -> 0.5;
- descriptor preset e descriptor diretto equivalenti;
- FOV e PhysicalLens preservati nei PoseTracks;
- Angle of View coerente;
- focal X/Y e gate fit.

### DOF

- ManualDistance;
- LockToZoom;
- PointOfInterest;
- TargetLayer;
- target mancante;
- Blur Level 0/50/100;
- LensModel f-stop autorevole;
- legacy adapter parity.

### Camera operations

- Orbit intorno al POI;
- Track XY sposta camera e POI;
- Track Z segue il forward corretto;
- Look At Selected/All usa il solver canonico.

### Active camera

- solo `LayerKind::Camera`;
- stack order;
- `visible`;
- `from` e `duration`;
- Cut;
- default descriptor;
- local time;
- world/screen;
- effect Comp Camera.

## Projection parity

Gli stessi input devono produrre risultati equivalenti in:

- `camera_projection_contract`;
- `project_world_to_screen`;
- layer projection;
- projection context;
- framing solver;
- software renderer.

Usare `EvaluatedProjection` esistente come snapshot condiviso.

## Golden render

Scene minime:

1. One-Node Zoom;
2. Two-Node POI;
3. camera layer switch;
4. default camera gap;
5. Orbit;
6. Track Z;
7. PhysicalLens wide;
8. PhysicalLens telephoto;
9. LockToZoom;
10. focus target layer;
11. temporal motion blur;
12. world content + screen overlay + Comp Camera effect.

Registrare frame, viewport, frame rate, camera id, descriptor fingerprint e timeline fingerprint.

## Determinismo

- seriale vs parallelo;
- sequenziale vs frame diretto;
- ordine frame casuale;
- due render job;
- retry;
- sub-frame;
- checkpoint restore;
- cache hit vs miss;
- temporal samples ripetibili;
- HandheldNoise ripetibile.

## Gate CI anti-duplicazione

Aggiungere o estendere `tools/check_camera_architecture.sh`.

Il gate deve impedire nuovi:

- `CameraProgramV2` o namespace camera parallelo;
- camera registry oltre `CameraCatalog`;
- lens preset registry oltre `CameraCatalog` + helper `LensPresets`;
- `FocusMode`/DOF struct concorrenti;
- node-mode enum fuori dal common contract;
- `CameraLayerSpec`/side list;
- `LayerCameraMode` parallelo;
- active-camera resolver nel renderer;
- projection math fuori dal contract;
- compiler call nell'hot path;
- sessioni statiche/globali;
- evaluator autonomi dei rig;
- direct `Camera2_5D` authoring nei contenuti moderni.

Allowlist soltanto per test e directory compatibility esplicita.

## Inventario legacy aggiornato

| Elemento | Stato | Sostituto canonico | Rimozione |
|---|---|---|---|
| `AnimatedCamera2_5D` | legacy | `PoseTracksSource` | dopo adapter parity |
| modern `CameraRig` | migrazione | `OrbitMotion` | dopo parity |
| `camera_rig::CameraRig` | legacy | `OrbitMotion` | dopo parity |
| `CameraRigMode` | duplicato | common `CameraNodeMode` | dopo call-site migration |
| legacy `RigMode` | duplicato | common `CameraNodeMode` | dopo call-site migration |
| `point_of_interest_enabled` authoring | transitorio | `CameraNodeMode` | runtime-only poi cleanup |
| `projection_mode` + `optics_mode` | doppio selector transitorio | `ProjectionSpec` compilata | dopo migration |
| `CameraFocusMode` nel rig | posizione errata | common focus enum | spostare e riusare |
| legacy DOF `focus_z/aperture/max_blur/use_physical_model` | legacy | `DepthOfFieldSettings` evoluto + `LensModel` | dopo parity |
| `MotionBlurSettings::enabled` | legacy | `MotionBlurMode` | dopo adapter migration |
| helper motion imperative | legacy | descriptor/source helper | dopo migration |
| direct `Camera2_5D` authoring | legacy | `CameraDescriptor` | solo test/compat |

## Strategia di rimozione

### Fase 1 — Freeze

- boundary gate;
- nessuna nuova feature legacy;
- deprecation annotation;
- inventario call site.

### Fase 2 — Canonical extension

- common node mode;
- focus/DOF consolidation;
- active camera su LayerKind Camera;
- source bug fixes;
- tests.

### Fase 3 — Adapter-only

- rig e AnimatedCamera delegano al descriptor;
- legacy selectors tradotti una sola volta;
- parity test.

### Fase 4 — SDK cleanup

- rimuovere header/alias legacy dalla superficie stabile;
- eventuale compatibility package separato;
- migrare call site e preset.

### Fase 5 — Delete

- eliminare implementazioni, CMake residue, placeholder e documentazione obsoleta.

## Sequenza PR corretta

### CAM-01 — Baseline test refresh

- aggiornare la suite compiled esistente;
- correggere commenti stale;
- aggiungere test per feature già implementate;
- nessuna nuova architettura.

### CAM-02 — Compiler hardening

- validazioni mancanti;
- recursive metadata/failure policy;
- diagnostics;
- fingerprint coverage.

### CAM-03 — Canonical node mode

- unificare `CameraRigMode` e `RigMode`;
- aggiungere modo al descriptor;
- One/Two Node parity;
- niente nuovo runtime.

### CAM-04 — Projection and DOF fixes

- PoseTracks projection;
- focal X/Y;
- DOF/focus consolidation;
- double look-at.

### CAM-05 — Motion and trajectory

- Orbit basis;
- trajectory field preservation;
- OrientAlongPath;
- Handheld tests.

### CAM-06 — Active camera layers

- payload su `LayerKind::Camera`;
- stack compiler;
- default descriptor;
- Cut timeline.

### CAM-07 — Render integration and state

- render-job sessions;
- timeline diagnostics;
- checkpoint/pre-roll;
- cache.

### CAM-08 — Legacy adapters

- rig/AnimatedCamera delegano al descriptor;
- parity.

### CAM-09 — Legacy deletion

- SDK/CMake/file cleanup.

Ogni PR parte da `origin/main` aggiornato, resta piccola e non mescola feature con cancellazioni massive.

## Cosa non fare

- non implementare componenti già presenti;
- non creare una camera AE parallela;
- non creare side list camera;
- non creare nuovi lens/focus/node catalogs;
- non spostare l'active-camera resolution nel renderer;
- non introdurre GUI nel core;
- non rimuovere legacy prima di adapter, parity e call-site migration.

## Definition of Done globale

- `CameraDescriptor -> CameraProgram` è l'unico percorso moderno;
- un solo node mode, projection model, lens model, focus mode e DOF model;
- `LayerKind::Camera` è l'unico camera-layer model;
- active camera confluisce in `ShotTimeline`;
- default camera è un normale descriptor;
- source bug P0 corretti;
- sessioni per-job;
- stateful random-access checkpointed;
- test compiled, parity, golden e determinism bloccanti;
- boundary gate impedisce regressioni legacy;
- rig e AnimatedCamera non mantengono evaluator autonomi;
- codice, CMake, test e documenti descrivono lo stesso stato.

## Riferimento funzionale

Audit del 2026-06-22 contro la documentazione ufficiale Adobe After Effects e contro il `main` corrente di Chronon3D. Le feature Adobe definiscono il comportamento; i tipi e registry esistenti di Chronon3D definiscono l'architettura.