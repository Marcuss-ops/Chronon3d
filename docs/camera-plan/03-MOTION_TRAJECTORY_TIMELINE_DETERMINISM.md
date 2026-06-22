# Chronon3D Camera — Movimento, timeline e determinismo con semantica After Effects

## Missione

Rendere movimento, camera cuts, trajectory, motion blur e constraint deterministici usando il solo percorso compilato esistente.

```text
CameraSourceSpec
  + CameraModifierSpec
  + OrientationSpec
  + CameraConstraintSpec
  -> compile_camera()
  -> CameraProgram
  -> evaluate(SampleTime, CameraSession)
```

Non devono esistere evaluator separati per rig, preset, camera layer o helper imperative.

## Anchor già presenti su main

| Responsabilità | Anchor canonico esistente |
|---|---|
| Source variant | `CameraSourceSpec` |
| Static camera | `StaticCameraSource` |
| Keyframe camera | `PoseTracksSource` |
| Orbit/track/dolly | `OrbitMotion` |
| Trajectory | `TrajectoryMotion` e `CameraTrajectory` |
| Preset reference | `RegisteredMotionRef` |
| Modifier | `CameraModifierSpec` |
| Idle | `IdleOscillation` |
| Handheld deterministico | `HandheldNoise` |
| Orientation | `OrientationSpec` |
| Constraint | `CameraConstraintSpec` |
| Stateful metadata | `CameraEvaluationDependency` |
| Sessione | `CameraSession` |
| Cut e transizioni | `ShotTimeline` |
| Campioni shutter | `temporal::generate_temporal_samples()` |
| Camera layer | `LayerKind::Camera` |

Non ricreare questi elementi con suffissi V2 o namespace alternativi.

## Base camera comune

Gli evaluator source oggi inizializzano campi differenti. Introdurre una sola funzione interna:

```cpp
Camera2_5D make_camera_from_base(
    const CameraBaseSpec& base,
    const CameraEvalContext& ctx);
```

Deve inizializzare:

- enabled e modalità One/Two Node canonica;
- position e local rotation;
- Point of Interest;
- projection tramite il dispatch esistente;
- `LensModel`;
- `DepthOfFieldSettings`;
- motion blur;
- parent/target metadata;
- flags di animazione e gerarchia.

Ogni source modifica soltanto i canali di cui è proprietaria.

## PoseTracksSource

`PoseTracksSource` esiste già.

### P0 da correggere

- dopo aver applicato la projection base forza ancora Zoom;
- può annullare FOV e PhysicalLens;
- usa rotation Euler anche quando l'orientation look-at dovrebbe possedere la base orientation;
- il target è separato dal modo One/Two Node canonico;
- i canali DOF animati non coprono ancora l'intero contratto focus/blur.

### Implementazione

- campionare Position, local Rotation, Point of Interest e i canali della sola projection attiva;
- non scrivere Zoom/FOV non pertinenti;
- usare `CameraFocusMode` condiviso;
- preservare `LensModel`, parent, motion blur e metadata dalla base;
- valutare tutti i canali a `SampleTime`.

## OrbitMotion e Camera Tools

`OrbitMotion` è già la source canonica che deve sostituire entrambi i rig.

### Bug P0 reale

L'evaluator corrente:

- somma `track` direttamente in world space;
- applica dolly con `pos.z += dolly`;
- non usa il basis camera-target.

### Correzione

```cpp
forward = normalize(target - orbit_position);
right = normalize(cross(reference_up, forward));
up = cross(forward, right);

position += right * track.x;
position += up * track.y;
position += forward * track.z;
position += forward * dolly;
```

Semantica:

- Orbit ruota la camera intorno al Point of Interest.
- Track XY sposta camera e POI lungo right/up.
- Track Z sposta lungo la linea camera-POI.
- One-Node usa il forward della propria orientation.

Le funzioni equivalenti ai Camera Tools devono trasformare `CameraDescriptor`/`OrbitMotion`; non devono mantenere un altro rig.

### Migrazione rig

1. parity test fra rig moderno e `OrbitMotion`;
2. rig moderno delega all'adapter descriptor;
3. rig legacy delega allo stesso adapter;
4. rimozione delle due implementazioni autonome;
5. mantenimento del solo `OrbitMotion`.

## Look At Selected e Look At All

Usare il framing solver e i bounds risolti esistenti:

```cpp
CameraDescriptor look_at_bounds(
    const CameraDescriptor&,
    std::span<const WorldBounds>,
    FramingStrategy);
```

- Selected usa solo i bounds forniti dal caller.
- All usa tutti i layer world rilevanti.
- aggiorna Point of Interest e, se richiesto, Zoom/dolly tramite il solver esistente.
- non introduce un secondo framing engine.

## TrajectoryMotion

`CameraTrajectory` esiste già, ma l'evaluator compiled corrente perde proprietà della base e usa valori hardcoded.

### Bug P0 reale

Nel path trajectory vengono ancora impostati valori come:

```text
zoom = 1000
fov = 50
point_of_interest_enabled = true
```

senza preservare in modo completo projection, lens, DOF, motion blur, parent e local rotation.

### Correzione

1. iniziare da `make_camera_from_base()`;
2. sovrascrivere position con il sample trajectory;
3. applicare target/POI soltanto quando presente o richiesto dalla modalità;
4. conservare la projection attiva;
5. propagare roll e tangente;
6. applicare `OrientationSpec` una sola volta.

### Arc-length per segmento

Non usare una LUT globale per correggere un segmento già scelto tramite durata.

```cpp
struct CompiledTrajectorySegment {
    SegmentKind kind;
    f32 start_frame;
    f32 duration_frames;
    f32 geometric_length;
    ArcLengthTable local_arc_lut;
};
```

Ordine:

1. selezione segmento tramite tempo;
2. tempo locale;
3. remap tramite LUT locale;
4. sample position/tangent;
5. target e roll.

### Builder

Tutti i segmenti di movimento devono poter conservare target e roll:

```cpp
move_to(position, target, roll)
linear_to(position, target, roll)
bezier_to(handles, position, target, roll)
catmull_rom_to(position, target, roll)
hold_for(frames)
```

### Validazione compiler

- trajectory pointer non nullo;
- almeno un segmento quando richiesto;
- durata totale positiva;
- durate finite;
- punti/handle finiti;
- indici validi;
- tangent fallback;
- LUT valida;
- fingerprint stabile.

## OrientAlongPath

`OrientAlongPath` esiste nella variant ma l'evaluator è ancora uno stub.

Implementare nello stesso `CameraProgram`:

- tangente del sample;
- reference-up deterministico;
- keep horizon;
- look-ahead;
- roll keyframed;
- banking opzionale;
- fallback su Hold/tangente nulla.

Non creare un processore esterno.

## Modifier

`IdleOscillation` e `HandheldNoise` esistono già.

### Da completare

- aggiungere test compiled per `HandheldNoise`;
- assicurare che il noise dipenda da seed e tempo assoluto;
- includere tutti i parametri nel fingerprint;
- applicare i modifier nell'ordine strutturale dichiarato.

### Future extension

`AdditivePoseTrack` o `CameraShake` possono entrare nella stessa `CameraModifierSpec` solo quando esiste un use case. Non creare un secondo modifier stack.

Ordine canonico:

```text
1. base camera
2. source
3. modifiers
4. orientation
5. constraints
6. framing opzionale
7. final validation
8. runtime snapshot
```

## Active camera e ShotTimeline

Non aggiungere `CameraLayerSpec`.

La semantica active camera deve essere compilata dai layer reali:

- `LayerKind::Camera`;
- `Layer::visible`;
- `Layer::from`;
- `Layer::duration`;
- `Layer::active_at()`;
- ordine layer canonico.

### Compilazione

- scegliere la camera topmost attiva per ogni intervallo;
- compilare il descriptor payload del layer una sola volta;
- generare `CameraShot` con `Cut` quando cambia la camera;
- usare la default camera descriptor nei gap;
- rispettare il local time del layer;
- non mantenere un resolver parallelo nel renderer.

`ShotTimeline` è già il runtime canonico. Va esteso, non sostituito.

## Transizioni

La parità After Effects di base usa Cut tra camere. SmoothBlend, Push, WhipPan e FocusHandoff sono estensioni esplicite Chronon3D.

Centralizzare l'interpolazione in una funzione comune, estendendo i tipi esistenti:

```cpp
Camera2_5D interpolate_camera(
    const Camera2_5D& from,
    const Camera2_5D& to,
    f32 t,
    const CameraTransitionSpec& spec);
```

Non duplicare position/rotation/focus interpolation nelle cinque classi.

### Risultato strutturato

Estendere il resolver esistente affinché propaghi:

- camera;
- `ok`;
- active shot/camera id;
- stato transition;
- diagnostica dei due programmi coinvolti.

Non ignorare `CameraProgramResult::ok` e diagnostics.

## Constraint e stateful dependency

`CameraEvaluationDependency` e la classificazione di `DampedFollowConstraint` esistono già.

### Lavoro restante

- testare la metadata classification;
- validare range dei constraint al compile-time;
- assegnare slot soltanto ai constraint stateful;
- correggere `KeepLastValidCamera` affinché mantenga l'ultimo snapshot completamente valido;
- definire reset su Cut;
- propagare diagnostics.

## Determinismo random-access

Per `Stateless`, la camera deve essere valutabile direttamente a qualsiasi `SampleTime`.

Per `RequiresHistory`, usare una sola strategia runtime:

```cpp
struct CameraStateCheckpoint {
    Frame frame;
    CameraSession session;
};
```

Procedura:

1. checkpoint precedente;
2. clone sessione;
3. pre-roll deterministico;
4. evaluation del sample;
5. sessione isolata per worker.

È vietato far dipendere il risultato dall'ultimo frame casualmente richiesto sul worker.

Definire:

- intervallo checkpoint;
- reset su Cut;
- pre-roll durante overlap;
- invalidazione su fingerprint;
- comportamento con time remap.

## Sub-frame evaluation

Ogni source, modifier, orientation e constraint deve ricevere `SampleTime`.

Testare:

- frame interi;
- metà frame;
- frame rate razionali;
- shutter samples;
- time remap;
- retry identico;
- ordine frame casuale.

## Motion blur

`MotionBlurMode` esiste già, ma `MotionBlurSettings` conserva anche il legacy `bool enabled`.

### Correzione

- `MotionBlurMode` diventa l'unico selettore;
- `enabled` resta solo nell'adapter legacy e poi viene eliminato;
- `TemporalAccumulation` rivaluta lo stesso `CameraProgram` per ogni sub-frame;
- `VelocityApproximation` resta mutuamente esclusiva.

Gli offset shutter sono frazioni di frame:

```cpp
sub_frame = center_frame
          + window_start_normalized
          + sample_t * exposure_normalized;
```

Non dividerli per FPS prima di sommarli a un valore espresso in frame.

Il pose averaging non deve essere descritto come motion blur visibile; il blur corretto deriva dall'accumulo dei framebuffer.

## Focus dinamico

Il focus dinamico deve utilizzare il solo `CameraFocusMode` condiviso e `DepthOfFieldSettings`:

- PointOfInterest;
- TargetLayer;
- LockToZoom;
- ManualDistance one-shot.

Nessun evaluator focus separato.

## Stereo opzionale

La stereo camera è P2. Se implementata, deriva da un master `CameraProgram` e da una sola configurazione stereo. Non crea un terzo rig o due programmi authoring indipendenti.

## Cosa eliminare

- evaluator autonomi dei due rig;
- dolly world-Z;
- track world-space dichiarato locale;
- valori projection hardcoded nel trajectory path;
- LUT globale incoerente con le durate;
- OrientationAlongPath stub;
- RNG stateful;
- stateful dependency nascosta;
- transizioni con interpolazione duplicata;
- perdita diagnostica nella timeline;
- sessioni create a ogni frame;
- active-camera resolver fuori dal scene/layer compiler;
- `MotionBlurSettings::enabled` dopo migrazione.

## Test obbligatori

### Source

- Static, PoseTracks, Orbit, Trajectory e catalog ref;
- PoseTracks preserva projection;
- Orbit local basis;
- trajectory conserva tutti i campi base;
- arc-length locale;
- target/roll;
- OrientAlongPath.

### Camera tools

- Orbit conserva distanza dal POI;
- Track XY sposta camera e POI;
- Track Z segue la linea camera-POI;
- One-Node usa il proprio forward;
- Look At Selected/All usa il framing solver esistente.

### Active camera

- topmost camera layer vince;
- visible false ignorato;
- from/duration rispettati;
- switch produce Cut;
- gap usa default descriptor;
- local time corretto.

### Determinismo

- sequenziale vs frame diretto;
- ordine casuale;
- due sessioni parallele;
- checkpoint parity;
- retry;
- sub-frame;
- HandheldNoise ripetibile.

### Motion blur

- 180° = mezza finestra frame;
- 360° = un frame;
- stessa finestra in frame a 24/30/60 fps;
- accumulo framebuffer reale;
- camera statica invariata.

## Definition of Done

- tutti gli evaluator partono dalla stessa camera base.
- `OrbitMotion` è il solo motore dei rig.
- trajectory conserva projection/lens/DOF/motion blur/parent.
- OrientAlongPath funziona.
- HandheldNoise è coperto da test compiled.
- active camera deriva da `LayerKind::Camera` e confluisce in `ShotTimeline`.
- stateful random-access è checkpointed.
- motion blur usa unità corrette e un solo selettore.
- timeline propaga diagnostics.
- nessun nuovo motion registry o evaluator parallelo.

## Riferimento funzionale

Audit del 2026-06-22 basato sulla guida ufficiale Adobe After Effects relativa a camera layer, Point of Interest, Orbit, Track XY, Track Z e Look At Layers. L'implementazione deve riusare esclusivamente le source, i layer e la timeline già presenti in Chronon3D.