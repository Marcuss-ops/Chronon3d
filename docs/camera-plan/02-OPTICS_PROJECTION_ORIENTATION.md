# Chronon3D Camera — Optics, Projection e Orientation

## Missione

Unificare prospettiva, lente fisica, orientamento e look-at in un solo contratto matematico. Il renderer, il framing solver, le transizioni e i tool diagnostici devono produrre lo stesso risultato per gli stessi input.

## Problemi da chiudere

1. `ProjectionSpec` rappresenta Zoom e FOV, ma non una lente fisica completa.
2. `Camera2_5D` conserva contemporaneamente projection mode, optics mode, zoom, FOV e lens, permettendo stati ambigui.
3. alcuni evaluator copiano la lente senza attivare `PhysicalLens`.
4. il percorso pose può forzare Zoom anche quando viene animato il FOV.
5. `GateFit::Stretch` non può essere rappresentato correttamente con un solo valore focale.
6. il framing solver usa formule FOV proprie invece del projection contract.
7. look-at può essere codificato sia nel POI sia in una rotazione Euler derivata dal POI, producendo doppia applicazione.
8. conversioni frequenti quaternion -> Euler -> quaternion introducono discontinuità e gimbal behavior.

## Contratto ottico canonico

Sostituire la proiezione ambigua con una variant esaustiva:

```cpp
struct ZoomProjection {
    AnimatedValue<f32> zoom{1000.0f};
};

struct FovProjection {
    AnimatedValue<f32> vertical_fov_deg{50.0f};
};

struct PhysicalLensProjection {
    AnimatedValue<f32> focal_length_mm{50.0f};
    AnimatedValue<f32> sensor_width_mm{36.0f};
    AnimatedValue<f32> sensor_height_mm{24.0f};
    GateFit gate_fit{GateFit::Fill};
    f32 pixel_aspect{1.0f};
    f32 anamorphic_squeeze{1.0f};
};

using ProjectionSpec = std::variant<
    ZoomProjection,
    FovProjection,
    PhysicalLensProjection
>;
```

La variant è la fonte autorevole. Non mantenere un selettore separato che può contraddirla.

## Snapshot runtime normalizzato

Durante la compilazione o valutazione, la variant deve produrre un valore normalizzato:

```cpp
struct EvaluatedProjection {
    CameraOpticsMode mode{CameraOpticsMode::Zoom};
    f32 focal_x_px{1000.0f};
    f32 focal_y_px{1000.0f};
    RectF active_viewport{};
    f32 near_plane{0.1f};
    f32 far_plane{100000.0f};
};
```

Il renderer e i solver devono consumare `EvaluatedProjection` o una funzione equivalente del projection contract. Non devono reinterpretare `zoom`, `fov_deg` e `lens` autonomamente.

## Gate fit corretto

Il projection contract deve calcolare separatamente asse X e Y:

```cpp
struct ProjectionScale {
    f32 focal_x_px;
    f32 focal_y_px;
    RectF active_viewport;
};
```

### Fill

- il sensore riempie il viewport;
- una dimensione può essere croppata;
- nessuna barra visibile.

### Overscan/Fit

- l'intero sensore resta visibile;
- `active_viewport` può avere letterbox o pillarbox;
- proiezione e safe area devono usare l'active viewport, non sempre il canvas intero.

### Stretch

- focal X e focal Y possono differire;
- il contratto deve dichiarare esplicitamente la distorsione;
- nessun codice deve fingere che Stretch sia equivalente a Fill.

### Anamorphic

- `anamorphic_squeeze` modifica l'asse orizzontale;
- il desqueeze deve essere parte del projection contract;
- non aggiungere una seconda pipeline anamorfica post-projection.

## Near e far plane

Aggiungere near e far plane al contratto canonico della camera runtime o della proiezione valutata.

Il clipping deve avvenire nello spazio camera prima della divisione prospettica.

Da implementare:

- point clipping;
- segment clipping;
- quad/polygon clipping;
- gestione di primitive che attraversano il near plane;
- risultato diagnostico per valori degeneri;
- nessun sentinel numerico usato come geometria valida.

## Orientamento canonico

Separare orientamento base e offset artistico:

```cpp
struct CameraOrientationState {
    Quat base_orientation{identity_quat()};
    Vec3 local_euler_offset_deg{0.0f};
};
```

### Base orientation

Può essere prodotta da:

- fixed orientation;
- look-at point;
- look-at layer;
- orient along path;
- hierarchy parent.

### Local offset

Contiene soltanto:

- tilt;
- pan;
- roll;
- offset artistico additivo.

### Composizione

Definire e testare un ordine unico, per esempio:

```text
world_orientation = parent_orientation
                  * base_orientation
                  * local_offset_orientation
```

L'ordine deve essere documentato e non può cambiare fra rig, descriptor, transition e framing.

## Look-at

Quando è attivo un POI:

- il POI determina `base_orientation`;
- non scrivere contemporaneamente la stessa rotazione look-at in Euler;
- roll e offset locali vengono applicati dopo il look-at;
- usare un reference-up deterministico per assi degeneri;
- mantenere continuità quando il forward attraversa l'asse verticale.

Rimuovere ogni percorso che:

1. calcola look-at quaternion;
2. lo converte in Euler;
3. abilita anche il POI;
4. ricostruisce un secondo look-at in `view_matrix()`.

## OrientAlongPath

Implementare realmente la variante già dichiarata.

La source evaluation deve poter restituire metadati:

```cpp
struct EvaluatedCameraSource {
    Camera2_5D camera;
    std::optional<Vec3> path_tangent;
    std::optional<Vec3> path_normal;
    std::optional<Vec3> path_target;
};
```

`OrientAlongPath` deve supportare:

- tangent normalization;
- fallback sulla precedente direzione valida;
- keep horizon;
- reference-up configurabile;
- look-ahead in unità di tempo o distanza;
- roll keyframed;
- banking opzionale;
- comportamento esplicito su segmenti Hold.

Non introdurre un secondo path orientation processor esterno a `CameraProgram`.

## Framing solver

Il framing solver deve usare esclusivamente:

```cpp
camera_math::project_world_point(...)
camera_math::focal_from_camera(...)
```

o una loro evoluzione canonica.

Eliminare:

- formule locali basate sempre su `tan(fov/2)`;
- assunzione che il viewport attivo coincida sempre con il canvas;
- conversioni screen/world duplicate;
- dead-zone chiamate in gradi ma confrontate con distanze world-space.

Aggiungere strategie reali:

```cpp
ZoomOnly
ZoomAndAim
DollyOnly
DollyAndAim
DollyZoom
RuleOfThirds
```

Il solver deve funzionare in modo coerente con Zoom, FOV e PhysicalLens.

## Depth of field

Separare nettamente:

- configurazione della lente;
- selezione del focus target;
- calcolo della profondità focale;
- rendering del blur.

Il focus deve avere un solo owner:

```cpp
ManualDistance
PointOfInterest
TargetLayer
LockToZoom
```

Non reintrodurre booleani legacy paralleli come selettori autorevoli.

Per il modello fisico aggiungere in una fase successiva:

- circle of confusion;
- near/far blur separato;
- aperture blade count;
- blade rotation;
- anamorphic bokeh ratio;
- focus breathing opzionale.

Queste feature devono estendere `LensModel` o un unico `DepthOfFieldModel`, non creare un secondo lens subsystem.

## Cosa eliminare

- doppia selezione `projection_mode` + `optics_mode` non normalizzata;
- FOV animato ignorato perché la source forza Zoom;
- lente fisica copiata ma non attivata;
- formule di proiezione replicate nei solver;
- doppio look-at;
- interpolazione Euler come rappresentazione primaria;
- `GateFit::Stretch` simulato con un solo focal scalar;
- campi ottici inutilizzati o accettati dall'API senza effetto reale.

## Test obbligatori

### Projection parity

Gli stessi input devono produrre pixel identici attraverso:

- `project_world_to_screen`;
- layer projection;
- projection context;
- framing projection;
- renderer software.

### Ottica

- Zoom parity;
- FOV verticale;
- PhysicalLens su viewport 16:9, 4:3 e verticale;
- Fill;
- Overscan;
- Stretch;
- pixel aspect;
- anamorphic squeeze;
- valori degeneri;
- near/far clipping.

### Orientamento

- one-node;
- two-node;
- target sopra e sotto la camera;
- attraversamento asse verticale;
- roll locale dopo look-at;
- parent orientation;
- orient along path;
- hold segment;
- shortest-arc transition;
- assenza di doppia rotazione.

### Framing

- Zoom, FOV e PhysicalLens equivalenti a parità di focal pixels;
- safe area sull'active viewport;
- dead-zone angolare reale;
- target dietro la camera;
- bbox che attraversa il near plane;
- convergence report con errore residuo reale.

## Criteri di chiusura

- una sola `ProjectionSpec` autorevole;
- un solo projection contract;
- focal X/Y e active viewport disponibili;
- PhysicalLens realmente raggiungibile dal percorso compilato;
- `OrientAlongPath` implementato;
- look-at applicato una sola volta;
- framing senza matematica prospettica duplicata;
- test parity bloccanti;
- rimozione dei campi ottici che non hanno più responsabilità canonica.