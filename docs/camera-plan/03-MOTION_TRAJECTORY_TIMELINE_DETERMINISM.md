# Chronon3D Camera — Motion, Trajectory, Timeline e Determinismo

## Missione

Rendere ogni movimento camera compilabile, sub-frame aware, deterministico e compatibile con rendering random-access, parallelo e distribuito.

Il modello canonico deve essere:

```text
CameraSourceSpec
    + CameraModifierSpec
    + OrientationSpec
    + CameraConstraintSpec
    -> compile_camera()
    -> CameraProgram
    -> evaluate(time, session)
```

Non devono esistere evaluator separati per preset legacy, rig moderni o helper imperative.

## Sorgenti canoniche

Mantenere una sola variant per le sorgenti:

```cpp
using CameraSourceSpec = std::variant<
    StaticCameraSource,
    PoseTracksSource,
    OrbitMotion,
    TrajectoryMotion,
    RegisteredMotionRef
>;
```

Ogni nuova sorgente deve entrare qui o sostituire una variante esistente. Non creare gerarchie virtuali parallele o registry di motion object.

## Base camera comune

Ogni evaluator deve iniziare dallo stesso snapshot:

```cpp
Camera2_5D make_camera_from_base(
    const CameraBaseSpec& base,
    SampleTime time);
```

La funzione deve impostare in modo coerente:

- enabled;
- position e rotation base;
- projection e optics mode;
- zoom, FOV o lens;
- depth of field;
- motion blur;
- parent;
- point of interest;
- near/far plane;
- metadata di animazione.

Gli evaluator source-specific devono modificare soltanto i canali che possiedono.

## PoseTracksSource

### Da implementare

- posizione;
- orientation quaternion o rotation track normalizzata;
- target;
- projection track della variante attiva;
- focus distance;
- aperture;
- max blur;
- eventuale focal length fisica animata;
- sub-frame evaluation per tutti i canali.

### Da eliminare

- forzatura di Zoom quando viene usato il FOV;
- default hardcoded che ignorano `CameraBaseSpec`;
- duplicazione del focus resolver;
- conversioni Euler non necessarie.

## OrbitMotion

Il contratto deve usare un basis camera/target esplicito:

```cpp
forward = normalize(target - orbit_position);
right = normalize(cross(reference_up, forward));
up = cross(forward, right);

position += right * track.x;
position += up * track.y;
position += forward * track.z;
position += forward * dolly;
```

### Da correggere

- dolly applicato lungo Z globale;
- track dichiarato locale ma sommato in world space;
- divergenza matematica fra `CameraRig` e `OrbitMotion`;
- target hierarchy non risolto nello stesso punto del percorso canonico.

### Parity requirement

Durante la migrazione, lo stesso set di parametri in `CameraRig` e `OrbitMotion` deve produrre la stessa camera entro una tolleranza definita. Dopo il raggiungimento della parity, il rig deve diventare adapter verso `OrbitMotion` e non mantenere una seconda implementazione.

## TrajectoryMotion

### Struttura compilata

```cpp
struct CompiledTrajectorySegment {
    SegmentKind kind;
    f32 start_frame;
    f32 duration_frames;
    f32 geometric_length;
    ArcLengthTable local_arc_lut;
    std::unique_ptr<TrajectorySegmentSampler> sampler;
};
```

La selezione corretta è:

1. selezionare segmento tramite tempo;
2. calcolare tempo locale;
3. rimappare il tempo locale con la LUT locale;
4. campionare posizione e tangente;
5. produrre target, roll e metadati di orientamento.

Non usare una LUT globale per rimappare un segmento già scelto temporalmente.

### Builder

Tutti i metodi devono poter conservare le proprietà artistiche del punto finale:

```cpp
move_to(position, target, roll)
linear_to(position, target, roll)
bezier_to(handles, position, target, roll)
catmull_rom_to(position, target, roll)
hold_for(frames)
```

Non lasciare target e roll disponibili soltanto su `move_to()`.

### Validazione compile-time

- almeno due punti quando richiesto;
- indici validi;
- durata finita e positiva per i segmenti di movimento;
- hold con durata non negativa;
- handle finiti;
- nessun NaN/Inf;
- LUT non vuota;
- tangent fallback definito;
- fingerprint stabile.

## Modifier

La variant canonica deve estendersi senza sistemi paralleli:

```cpp
using CameraModifierSpec = std::variant<
    IdleOscillation,
    HandheldNoise,
    AdditivePoseTrack,
    CameraShake
>;
```

### HandheldNoise

Deve essere deterministico e time-addressable:

```cpp
noise(seed, absolute_time)
```

Non usare RNG mutabile avanzato frame dopo frame.

Parametri minimi:

- position amplitude;
- rotation amplitude;
- frequency;
- octave count;
- seed;
- low-pass smoothing;
- axis masks.

### CameraShake

Usare un modello dati, per esempio trauma/decay, ma la valutazione deve dipendere da tempo assoluto e eventi compilati, non dall'ordine di rendering.

### AdditivePoseTrack

Applicare offset dopo la source base e prima dei constraint. Documentare l'ordine esatto.

## Ordine di valutazione

Congelare il seguente ordine:

```text
1. base camera
2. source
3. modifiers
4. orientation
5. constraints
6. framing policy opzionale
7. final validation
8. runtime snapshot
```

Non permettere che un preset cambi arbitrariamente l'ordine.

## Constraint

Mantenere una sola variant dati:

```cpp
LookAtConstraint
KeepHorizonConstraint
DampedFollowConstraint
DistanceConstraint
RotationLimitConstraint
```

Nuovi constraint devono estendere questa variant e il compiler canonico.

### Da correggere

- `KeepLastValidCamera` deve restituire realmente l'ultima camera completamente valida;
- diagnostica non deve essere persa;
- valori min/max devono essere validati al compile-time;
- lo state slot deve essere assegnato dal compiler;
- constraint stateless non devono allocare slot runtime;
- constraint stateful devono dichiarare la dipendenza da history.

## Determinismo random-access

Ogni programma deve dichiarare:

```cpp
enum class CameraEvaluationDependency {
    Stateless,
    RequiresHistory
};
```

### Stateless

Può essere valutato direttamente a qualsiasi `SampleTime`.

Esempi:

- keyframe;
- orbit;
- trajectory;
- deterministic noise basato su tempo assoluto;
- look-at;
- limiti di distanza o rotazione puri.

### RequiresHistory

Richiede uno stato precedente.

Esempi:

- damped follow integrato;
- smoothing non analitico;
- autofocus con isteresi;
- collision avoidance con memoria.

Il renderer non deve trattare i due casi allo stesso modo.

## Strategia per stateful camera

Scegliere e implementare una sola strategia canonica.

### Strategia consigliata — checkpoint e pre-roll

```cpp
struct CameraStateCheckpoint {
    Frame frame;
    CameraSession session;
};
```

Per valutare un frame random-access:

1. trovare il checkpoint precedente;
2. clonare lo stato;
3. fare pre-roll con step deterministici;
4. valutare il sample richiesto;
5. non mutare sessioni condivise fra job.

Parametri da definire:

- checkpoint interval;
- pre-roll start;
- reset su cut;
- comportamento durante transition overlap;
- invalidazione quando cambia il fingerprint.

### Vietato

- dipendere implicitamente dal frame precedentemente richiesto;
- condividere la stessa `CameraSession` fra worker;
- rendere il risultato differente fra render sequenziale e retry;
- nascondere la dipendenza stateful dietro `is_animated`.

## Sub-frame evaluation

Tutti i source, modifier e constraint devono ricevere `SampleTime`.

Non convertire a frame intero salvo operazioni esplicitamente discrete.

Testare:

- frame interi;
- metà frame;
- shutter samples;
- frame rate razionali;
- tempo negativo quando permesso;
- time remap.

## Motion blur

Usare un solo contratto:

```cpp
MotionBlurMode::Off
MotionBlurMode::TemporalAccumulation
MotionBlurMode::VelocityApproximation
```

Eliminare progressivamente `bool enabled` come selettore autorevole.

### TemporalAccumulation

Il compositor deve:

1. generare sample temporali canonici;
2. valutare programma camera e scena a ogni sub-frame;
3. renderizzare framebuffer separati;
4. accumulare con pesi normalizzati.

### VelocityApproximation

Deve essere una pipeline differente e non attivabile contemporaneamente all'accumulo temporale.

### Correzione unità

Gli offset normalizzati dello shutter sono frazioni di frame. Non dividerli per FPS prima di sommarli a un valore espresso in frame.

```cpp
sub_frame = center_frame
          + window_start_normalized
          + sample_t * exposure_normalized;
```

Il pose averaging non deve essere descritto come motion blur visibile.

## ShotTimeline

La timeline deve contenere soltanto `CameraProgram` già compilati.

Aggiungere una transition spec dati:

```cpp
struct CameraTransitionSpec {
    CameraTransitionKind kind;
    EasingCurve position_easing;
    EasingCurve rotation_easing;
    EasingCurve focus_easing;
    DiscreteSwitchPolicy projection_switch;
    DiscreteSwitchPolicy hierarchy_switch;
    LensTransitionPolicy lens_policy;
};
```

Ogni transition deve passare da una sola funzione:

```cpp
Camera2_5D interpolate_camera(
    const Camera2_5D& from,
    const Camera2_5D& to,
    f32 t,
    const CameraTransitionSpec& spec);
```

Non duplicare la stessa interpolazione in Cut, SmoothBlend, Push, WhipPan e FocusHandoff.

## Risultato timeline strutturato

```cpp
struct ShotTimelineResult {
    Camera2_5D camera;
    bool ok{true};
    i32 active_shot{-1};
    bool in_transition{false};
    std::vector<CameraProgramDiagnostic> diagnostics;
};
```

Il resolver non deve ignorare `ok` e diagnostica del programma.

## Sessioni timeline

- una sessione per shot;
- reset esplicito su nuovo render;
- policy esplicita sui cut;
- nessuna mappa che cresce senza limiti;
- preallocazione quando il numero di shot è noto;
- nessuna sessione globale.

## Cosa eliminare

- evaluator separati per rig e orbit;
- LUT globale incoerente con le durate per segmento;
- dolly world-Z;
- track world-space quando dichiarato locale;
- motion blur controllato da due selettori;
- RNG stateful per handheld/shake;
- constraint history-dependent non dichiarati;
- transizioni con interpolazione duplicata;
- perdita della diagnostica nella timeline;
- camera session creata e distrutta a ogni frame.

## Test obbligatori

### Motion source

- static;
- pose tracks;
- orbit parity con adapter rig;
- trajectory linear/Bézier/Catmull-Rom/Hold;
- durate differenti;
- arc-length locale;
- target e roll per ogni segmento;
- orient along path.

### Determinismo

- stesso frame due volte -> stesso risultato;
- render sequenziale vs accesso diretto;
- ordine frame casuale;
- due sessioni parallele;
- checkpoint + pre-roll parity;
- retry del frame;
- sub-frame parity;
- deterministic handheld e shake.

### Motion blur

- 180 gradi = mezza finestra di frame;
- 360 gradi = un frame;
- stessa finestra in frame a 24/30/60 fps;
- accumulo framebuffer realmente multi-sample;
- static camera non modifica output;
- modalità mutually exclusive.

### Timeline

- cut;
- smooth blend;
- push;
- whip pan;
- focus handoff;
- projection switch;
- lens switch;
- overlap;
- gap validation;
- local shot time;
- diagnostica propagata;
- stateful constraint durante overlap.

## Criteri di chiusura

- source evaluator basati sulla stessa camera base;
- OrbitMotion equivalente al rig durante la migrazione;
- traiettorie con LUT locale per segmento;
- `OrientAlongPath` funzionante;
- stateful dependency dichiarata;
- random-access deterministico o gestito tramite checkpoint;
- motion blur con unità corrette;
- transizioni centralizzate;
- timeline con risultato strutturato;
- nessun nuovo motion registry o evaluator parallelo.