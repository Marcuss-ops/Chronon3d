# Chronon3D Camera — Ottica, proiezione e orientamento compatibili con After Effects

## Missione

Ottenere Zoom, Angle of View, Film Size, Focal Length, Depth of Field, One-Node, Two-Node e Point of Interest coerenti usando esclusivamente i contratti già presenti nel repository.

Non aggiungere una seconda `ProjectionSpec`, un secondo modello lente, un secondo tipo DOF o un secondo sistema di orientamento.

## Anchor già presenti su main

| Responsabilità | Anchor canonico esistente |
|---|---|
| Projection authoring | `camera_v1::ProjectionSpec` |
| Zoom | `camera_v1::ZoomProjection` |
| Field of View | `camera_v1::FovProjection` |
| Lente fisica | `camera_v1::PhysicalLensProjection` |
| Modello lente | `LensModel` |
| Preset lente interni | `LensPresets` |
| Snapshot normalizzato | `camera_v1::EvaluatedProjection` |
| Vista non owning | `CameraProjectionSource` |
| Matematica canonica | `camera_projection_contract.hpp` |
| Focus mode esistente | `CameraFocusMode` |
| DOF runtime esistente | `DepthOfFieldSettings` |
| Orientamento runtime | `Camera2_5D::resolve_look_at_orientation()` e `view_matrix()` |

Il lavoro consiste nell'estendere, consolidare e migrare questi anchor. Non ricrearli con nomi nuovi.

## Semantica After Effects richiesta

- Zoom rappresenta la distanza prospettica espressa in pixel/scene scale dal centro della lente al piano immagine.
- un layer a profondità camera uguale a Zoom ha scala prospettica 1.0;
- a profondità doppia ha scala 0.5;
- Angle of View deriva coerentemente da focal length e film/sensor size;
- i preset focali producono camera descriptor complete;
- Focus Distance può essere manuale, legata a Zoom, al Point of Interest o a un layer;
- Aperture/F-Stop e Blur Level hanno un solo owner canonico;
- One-Node ignora il Point of Interest;
- Two-Node usa il Point of Interest come orientamento base.

## ProjectionSpec: mantenere quella esistente

`camera_descriptor.hpp` contiene già:

```cpp
using ProjectionSpec = std::variant<
    ZoomProjection,
    FovProjection,
    PhysicalLensProjection
>;
```

`PhysicalLensProjection` contiene già un `LensModel`. Non duplicare `sensor_width`, `sensor_height`, focal length o gate fit direttamente nella variant.

### Da implementare

- validazione finita e positiva dei valori;
- normalizzazione coerente di `projection_mode` e `optics_mode` nello snapshot runtime;
- preservazione della variant attiva in ogni source evaluator;
- supporto animato per i canali ottici senza forzare Zoom;
- metadata per pixel aspect e anamorphic squeeze nel solo modello lente/proiezione canonico.

### Bug P0 reale

`PoseTracksSource` applica la projection base, poi forza nuovamente:

```text
projection_mode = Zoom
optics_mode = Zoom
```

Questo annulla FOV e PhysicalLens. Correggere l'evaluator affinché animi solo i canali della variant attiva e non sovrascriva la modalità.

## EvaluatedProjection: estendere quella esistente

`camera_v1::EvaluatedProjection` esiste già con:

- `focal_x_px`;
- `focal_y_px`;
- `principal_point_px`;
- `active_viewport`;
- `pixel_aspect`;
- `anamorphic_squeeze`;
- flag physical/anamorphic.

Non creare un nuovo snapshot nel renderer o nel framing solver.

### Da completare

- valorizzare realmente pixel aspect e anamorphic squeeze dal modello lente;
- rendere `focal_x_px` e `focal_y_px` indipendenti quando richiesto;
- aggiungere, solo se necessario allo stesso tipo, Angle of View X/Y e near/far plane;
- assicurare che renderer, projector e framing consumino lo snapshot invece di ricalcolare formule locali;
- invalidare lo snapshot quando cambia viewport o fingerprint camera.

## Projection contract

`camera_projection_contract.hpp` è la sola fonte di verità.

### Conservare

- convenzione left-handed già definita;
- Y screen verso il basso;
- centro viewport aggiunto dal contratto;
- `perspective_scale = focal / depth`;
- `CameraProjectionSource` come input.

### Correggere

1. `focal_from_camera()` restituisce ancora un solo scalar: introdurre nello stesso contratto una funzione/risultato X/Y, senza creare un secondo projector.
2. `GateFit::Stretch` non può essere rappresentato correttamente da un solo focal scalar.
3. Overscan deve esporre l'active viewport effettivo.
4. il punto non visibile usa ancora un sentinel numerico: separare validità e coordinate senza trattare il sentinel come geometria.
5. near/far clipping deve avvenire in camera space prima della divisione prospettica.

### Test fondamentale Zoom

```text
perspective_scale = zoom / camera_space_depth
```

Verificare:

- depth = Zoom -> 1.0;
- depth = 2 × Zoom -> 0.5;
- depth = 0.5 × Zoom -> 2.0.

## LensModel e preset

`LensModel` esiste già e possiede:

- focal length;
- sensor width/height;
- f-stop;
- close focus;
- gate fit;
- calcolo FOV;
- conversione a focal pixels.

`LensPresets` contiene già preset fisici.

### Non aggiungere

- `CameraLensPreset` come secondo tipo;
- un secondo lens registry;
- un catalogo focali parallelo a `CameraCatalog`.

### Implementazione richiesta

I preset compatibili con il workflow After Effects devono essere normali `CameraDescriptor` registrati nel `CameraCatalog`. Ogni descriptor può usare `LensModel`/`LensPresets` per costruire `PhysicalLensProjection`.

Il catalogo camera resta l'unica superficie pubblica di preset. `LensPresets` può restare un helper dati interno.

### Estensioni del modello lente

Aggiungere soltanto al modello canonico, quando implementate:

- film size measure: horizontal, vertical o diagonal;
- pixel aspect;
- anamorphic squeeze;
- aperture blade count/rotation;
- eventuale focus breathing.

Non duplicare questi campi in `Camera2_5D`, `ProjectionSpec` e DOF contemporaneamente.

## Depth of Field: evolvere il tipo esistente

Oggi esistono:

- `CameraFocusMode` nel rig moderno;
- `DepthOfFieldSettings` in `camera_common_types.hpp`;
- campi legacy `focus_z`, `aperture`, `max_blur`, `use_physical_model`;
- `LensModel::f_stop`.

Non aggiungere `FocusMode` o `DepthOfFieldSpec` separati.

### Implementazione richiesta

1. Spostare `CameraFocusMode` in `camera_common_types.hpp` come enum canonico condiviso.
2. Aggiungere a `DepthOfFieldSettings`:

```cpp
CameraFocusMode focus_mode;
std::string focus_target_name;
f32 blur_level_percent;
```

3. Usare `LensModel::f_stop` come f-stop fisico autorevole.
4. Definire una sola risoluzione della focus distance:

```text
ManualDistance  -> dof.focus_distance
PointOfInterest -> distance(camera, poi)
TargetLayer     -> distance(camera, resolved target layer)
LockToZoom      -> camera.zoom
```

5. Applicare Blur Level come moltiplicatore del blur fisico, con 100% come valore naturale.
6. Mantenere `focus_z`, legacy `aperture`, `max_blur` e `use_physical_model` soltanto negli adapter finché i call site non sono migrati.
7. Eliminare i campi legacy dopo parity test.

### Regola aperture

Non mantenere due aperture fisiche autorevoli.

- `LensModel::f_stop`: owner della lente fisica.
- legacy `DepthOfFieldSettings::aperture`: parametro del vecchio blur lineare, adapter-only.

Se viene aggiunto il diametro fisico dell'apertura, deve essere derivato da focal length e f-stop nello stesso `LensModel`.

## Comandi focus

Gli equivalenti After Effects devono modificare il medesimo descriptor/DOF:

```cpp
CameraDescriptor link_focus_to_point_of_interest(CameraDescriptor);
CameraDescriptor link_focus_to_layer(CameraDescriptor, std::string layer_id);
CameraDescriptor set_focus_to_layer(
    CameraDescriptor,
    std::string layer_id,
    SampleTime,
    const ResolvedSceneTransforms&);
```

- `link_*` mantiene il legame dinamico tramite `CameraFocusMode`.
- `set_focus_to_layer` campiona una volta e salva una `ManualDistance`.
- nessun evaluator focus parallelo.

## Orientamento canonico

Il runtime dispone già di helper look-at e view matrix. Il problema P0 è nel compiled path: `apply_orientation_spec_free()` calcola una rotazione Euler derivata dal look-at **e** abilita il Point of Interest; successivamente `view_matrix()` applica nuovamente il look-at.

### Correzione richiesta

Per `LookAtPoint` e `LookAtLayer`:

- impostare Point of Interest/Two-Node mode;
- non scrivere una seconda rotazione Euler derivata dallo stesso look-at;
- conservare in `rotation` soltanto gli offset locali artistici;
- usare un solo helper canonico per comporre parent, look-at e local offset.

Ordine:

```text
world_orientation = parent_orientation
                  * base_or_look_at_orientation
                  * local_offset_orientation
```

Non aggiungere un secondo `CameraOrientationState` se lo stesso contratto può essere espresso nei tipi canonici esistenti.

## OrientAlongPath

`OrientAlongPath` esiste già nella `OrientationSpec`, ma l'evaluator è ancora uno stub.

### Implementare nello stesso CameraProgram

- tangente normalizzata dalla trajectory sample;
- fallback deterministico sull'ultima tangente valida o sulla base orientation;
- keep horizon;
- reference-up stabile;
- look-ahead;
- roll keyframed;
- comportamento esplicito sui segmenti Hold.

Non introdurre un path-orientation processor esterno.

## Orbit, Track XY e Track Z

Usare il basis canonico:

```cpp
forward = normalize(point_of_interest - position);
right = normalize(cross(reference_up, forward));
up = cross(forward, right);
```

- Orbit ruota intorno al Point of Interest.
- Track XY sposta camera e POI lungo right/up.
- Track Z sposta lungo la linea camera-POI.
- One-Node usa il forward della propria orientation.

Le funzioni di authoring devono delegare a `OrbitMotion` e al framing solver, non creare un altro rig.

## Framing solver

Il solver deve consumare `EvaluatedProjection` e il projection contract esistenti.

Correggere:

- formule locali basate sempre su `tan(fov/2)`;
- safe area che ignora active viewport;
- dead zone in gradi confrontata con distanza world-space;
- convergence report che misura movimento applicato invece di errore residuo;
- target completamente dietro la camera;
- bbox che attraversano il near plane.

Strategie eventuali (`ZoomOnly`, `DollyZoom`, ecc.) devono estendere il solver esistente.

## Cosa eliminare

- selettori `projection_mode` e `optics_mode` contraddittori dopo la migrazione;
- forcing Zoom in `PoseTracksSource`;
- secondo lens preset catalog;
- secondo tipo DOF/focus;
- doppio look-at;
- formule prospettiche duplicate;
- Euler look-at come seconda rappresentazione primaria;
- sentinel numerici trattati come coordinate valide;
- campi DOF accettati ma senza effetto.

## Test obbligatori

### Projection

- Zoom 1:1, 0.5 e 2.0;
- Zoom/FOV/PhysicalLens equivalenti a parità di focal pixels;
- `PoseTracksSource` conserva FOV e PhysicalLens;
- focal X/Y;
- Fill, Overscan e Stretch;
- pixel aspect e anamorphic squeeze;
- near/far clipping;
- nessuna geometria sentinel valida.

### Lens preset

- preset risolto dal solo `CameraCatalog`;
- descriptor preset usa il medesimo `LensModel` del percorso diretto;
- nessun lookup lens registry runtime.

### DOF

- ManualDistance;
- LockToZoom;
- PointOfInterest;
- TargetLayer;
- target mancante con diagnostica/fallback definito;
- Blur Level 0, 50 e 100%;
- f-stop fisico autorevole;
- adapter legacy aperture parity.

### Orientamento

- One-Node ignora POI;
- Two-Node usa POI;
- roll locale applicato una volta;
- assenza di doppio look-at;
- parent orientation;
- OrientAlongPath;
- segmenti Hold;
- assi verticali degeneri.

## Definition of Done

- resta una sola `ProjectionSpec`.
- resta un solo `LensModel`.
- i preset pubblici vivono nel solo `CameraCatalog`.
- `EvaluatedProjection` è il solo snapshot normalizzato.
- `CameraFocusMode` è condiviso da descriptor, rig adapter e runtime.
- `DepthOfFieldSettings` è l'unico DOF model.
- PhysicalLens funziona nel compiled path.
- PoseTracks non forza Zoom.
- look-at viene applicato una sola volta.
- framing non duplica la matematica prospettica.
- test parity sono bloccanti.

## Riferimento funzionale

Audit del 2026-06-22 basato sulla guida ufficiale Adobe After Effects dedicata a Camera Settings, Zoom, focal presets, Point of Interest e Depth of Field. La semantica utente viene implementata estendendo esclusivamente gli anchor esistenti di Chronon3D.