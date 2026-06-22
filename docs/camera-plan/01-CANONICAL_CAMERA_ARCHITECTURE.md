# Chronon3D Camera — Architettura canonica con semantica After Effects

## Missione

Raggiungere una semantica camera familiare a After Effects senza creare un secondo motore camera, un manager globale o una gerarchia legacy parallela.

Il solo percorso moderno resta:

```text
CameraDescriptor
  -> compile_camera()
  -> CameraProgram
  -> evaluate(CameraEvalContext, CameraSession)
  -> Camera2_5D
  -> CameraProjectionSource
  -> camera_projection_contract
  -> renderer
```

Chronon3D rimane headless, CPU-first e deterministico. Working views, pannelli e mouse tool non appartengono al core.

## Anchor già presenti su main

Prima di aggiungere codice, usare e completare questi elementi esistenti:

| Responsabilità | Anchor canonico esistente |
|---|---|
| Authoring camera | `camera_v1::CameraDescriptor` |
| Compilazione | `camera_v1::compile_camera()` |
| Programma runtime | `camera_v1::CameraProgram` |
| Stato per render | `camera_v1::CameraSession` |
| Snapshot valutato | `Camera2_5D` |
| Vista di proiezione | `CameraProjectionSource` |
| Matematica prospettica | `camera_projection_contract.hpp` |
| Preset camera | `camera_v1::CameraCatalog` |
| Cut e transizioni | `camera_v1::ShotTimeline` |
| Layer camera | `LayerKind::Camera` |
| Intervallo e visibilità layer | `Layer::from`, `duration`, `visible`, `active_at()` |
| Partecipazione alla proiezione | `Layer::uses_2_5d_projection` |
| Ordine layer | contratto canonico dello scene/layer stack |

Non introdurre alternative agli anchor della tabella.

## Compatibilità comportamentale richiesta

Il percorso canonico deve supportare:

- One-Node Camera;
- Two-Node Camera con Point of Interest;
- Point of Interest ignorato dalla One-Node;
- camera attiva determinata dallo stack layer al sample time corrente;
- default composition camera quando nessuna camera layer è attiva;
- più camere nella stessa composizione con Cut deterministici;
- camera applicata ai layer world/2.5D;
- layer screen-space indipendenti dalla camera;
- effetti 2D che richiedono esplicitamente la Comp Camera;
- Zoom, preset focali e Depth of Field coerenti;
- operazioni equivalenti a Orbit, Track XY, Track Z e Look At Layers come funzioni pure.

## Un solo tipo di modalità camera

Oggi esistono almeno:

- `CameraRigMode` nel rig moderno;
- `camera_rig::RigMode` nel rig legacy;
- `point_of_interest_enabled` nello snapshot runtime.

Non aggiungere un terzo enum indipendente.

### Implementazione richiesta

1. Spostare o introdurre **un solo** enum canonico in `camera_common_types.hpp`:

```cpp
enum class CameraNodeMode {
    OneNode,
    TwoNode
};
```

2. Aggiungere `CameraNodeMode` a `CameraBaseSpec`.
3. Far derivare lo snapshot runtime da questa modalità durante `CameraProgram::evaluate()`.
4. Convertire `CameraRigMode` e `camera_rig::RigMode` in adapter/deprecated alias temporanei.
5. Rimuovere gli enum legacy dopo la migrazione dei call site.
6. Conservare `point_of_interest_enabled` soltanto come dettaglio runtime transitorio; non deve restare un secondo selettore di authoring.

### Semantica

- `OneNode`: orientation/rotation determina la vista; il Point of Interest non orienta la camera.
- `TwoNode`: il Point of Interest determina l'orientamento base; pan, tilt e roll sono offset locali applicati una sola volta.

## Active camera usando LayerKind::Camera

`LayerKind::Camera` esiste già. Non aggiungere `CameraLayerSpec`, un array parallelo di camere o `CameraApi::camera_layers()`.

### Modello richiesto

Un layer con `kind == LayerKind::Camera` deve contenere o referenziare un `CameraDescriptor` tramite una singola proprietà/payload tipizzato del layer. Se serve aggiungerla, usare forward declaration/PIMPL o il registry/payload pattern canonico del modello scene; non aggiungere una seconda collezione fuori da `Scene::layers()`.

### Risoluzione active camera

Il compiler della composizione deve:

1. attraversare i layer nell'ordine definito dal contratto layer esistente;
2. considerare soltanto `LayerKind::Camera`;
3. usare `Layer::active_at(frame)` come fonte per `visible`, `from` e `duration`;
4. scegliere il layer camera topmost attivo;
5. compilare ogni descriptor una sola volta;
6. trasformare gli intervalli risolti in `ShotTimeline` con transizione `Cut`;
7. usare il tempo locale del layer camera;
8. utilizzare la default camera nei gap.

Il renderer non deve avere un secondo active-camera resolver.

## Default composition camera

Non introdurre `DefaultCameraSpec` come nuovo modello concorrente.

La default camera deve essere un normale `CameraDescriptor` posseduto dalle impostazioni della composizione, compilato tramite `compile_camera()` e valutato dallo stesso runtime.

Il valore predefinito deve essere serializzabile e testabile; non deve essere nascosto come costante nel renderer.

## Layer world, screen e Comp Camera

Non aggiungere `LayerCameraMode`.

Usare i contratti esistenti:

- `uses_2_5d_projection == true`: il layer partecipa alla proiezione camera;
- `uses_2_5d_projection == false`: il layer resta in screen/comp space;
- un effetto che richiede la Comp Camera deve dichiarare una capability nel catalogo/descriptor degli effetti esistente.

La capability Comp Camera non deve diventare un nuovo tipo di layer o una seconda pipeline di proiezione.

## API moderna

Mantenere l'API concentrata sul percorso compilato:

```cpp
class CameraApi {
public:
    CameraApi& descriptor(const camera_v1::CameraDescriptor&);
    CameraApi& program(const camera_v1::CameraProgram&);
    CameraApi& preset(std::string_view, const camera_v1::CameraCatalog&);
    CameraApi& timeline(const camera_v1::ShotTimeline&);
};
```

Le camera layer vengono create attraverso il normale layer authoring con `LayerKind::Camera`, non attraverso un side channel in `CameraApi`.

La compilazione avviene prima del render. `CameraApi` non crea sessioni globali e non ricompila a ogni frame.

## Operazioni equivalenti ai Camera Tools

Implementare funzioni pure che modificano o restituiscono `CameraDescriptor`:

```cpp
CameraDescriptor orbit_camera(const CameraDescriptor&, Vec2 delta_deg);
CameraDescriptor track_camera_xy(const CameraDescriptor&, Vec2 delta_world);
CameraDescriptor track_camera_z(const CameraDescriptor&, f32 distance);
CameraDescriptor look_at_point(const CameraDescriptor&, Vec3 target);
CameraDescriptor look_at_layers(const CameraDescriptor&, Span<WorldBounds>);
```

Queste funzioni devono riusare `OrbitMotion`, il framing solver e il projection contract. Non devono introdurre classi UI o un nuovo rig.

## Compiler canonico

`compile_camera()` esiste già con:

- `CameraCompileContext`;
- cycle detection per `RegisteredMotionRef`;
- `CameraEvaluationDependency`;
- fingerprint deterministico.

Il lavoro restante è hardening, non una seconda implementazione:

- descriptor ID vuoti;
- source nulle o incoerenti;
- trajectory nulla, vuota o a durata zero;
- range dei constraint;
- projection/lens non finite;
- parent/target mancanti o ciclici;
- preservazione corretta della failure policy e dei metadata dopo la risoluzione ricorsiva;
- diagnostica strutturata completa.

## Ownership runtime

- `CameraProgram`: immutabile e condivisibile.
- `CameraSession`: una per render job.
- `ShotTimelineSession`: una per timeline e render job.
- `FramingSession`: una per render job.
- checkpoint stateful: posseduti dal render job.
- nessuno stato mutabile in singleton o cataloghi globali.

## Legacy

Adapter temporanei consentiti:

```cpp
CameraDescriptor descriptor_from_legacy(const AnimatedCamera2_5D&);
CameraDescriptor descriptor_from_legacy(const CameraRig&);
```

Gli adapter devono produrre soltanto descriptor, avere parity test e non mantenere evaluator autonomi.

### Deprecare subito

- nuovi utilizzi di `SceneBuilder::animated_camera()`;
- nuovi preset che restituiscono `AnimatedCamera2_5D`;
- nuovi utilizzi del rig legacy;
- helper motion che bypassano `CameraDescriptor`;
- costruzione diretta di `Camera2_5D` nelle composizioni moderne.

### Eliminare dopo parity

- implementazioni autonome di `AnimatedCamera2_5D` e dei due rig;
- `CameraRigMode` e `camera_rig::RigMode` dopo migrazione a `CameraNodeMode`;
- registry concorrenti con `CameraCatalog`;
- builder path non compilati;
- projection math duplicata;
- alias pubblici senza call site.

`Camera2_5D` resta come snapshot runtime, non come authoring API.

## Regole anti-duplicazione

È vietato:

- creare `CameraProgramV2`;
- creare `AfterEffectsCamera`;
- creare `CameraLayerSpec` separato dai layer;
- creare `LayerCameraMode` parallelo a `uses_2_5d_projection`;
- creare `DefaultCameraSpec` parallelo a `CameraDescriptor`;
- creare un secondo active-camera resolver;
- creare un secondo projection contract;
- mantenere due rig completi;
- compilare la camera nell'hot path;
- introdurre un `CameraManager` globale.

## Test obbligatori

- One-Node ignora il Point of Interest.
- Two-Node guarda il Point of Interest.
- `CameraRigMode` e legacy `RigMode` convertono correttamente nel modo canonico.
- camera topmost attiva selezionata tramite i layer esistenti.
- camera `visible == false` ignorata.
- `from` e `duration` rispettati.
- gap coperto dalla default camera descriptor.
- camera layer compilati in Cut corretti.
- layer con `uses_2_5d_projection` influenzato; screen layer invariato.
- effect capability Comp Camera usa il medesimo snapshot.
- due render job non condividono sessione.
- adapter legacy mantiene parity durante la migrazione.

## Definition of Done

- `CameraDescriptor -> CameraProgram` è l'unico percorso moderno.
- esiste un solo `CameraNodeMode`.
- `LayerKind::Camera` è l'unico modello di camera layer.
- active camera deriva dal normale scene/layer stack.
- default camera è un `CameraDescriptor`.
- nessuna collezione camera parallela.
- `CameraApi` usa programmi compilati.
- sessioni per-job.
- distinzione world/screen usa il contratto esistente.
- operazioni camera disponibili come funzioni pure.
- test di compatibilità bloccanti.

## Riferimento funzionale

Audit del 2026-06-22 basato sulla guida ufficiale Adobe After Effects dedicata a camera layer, active camera, One-Node, Two-Node e Point of Interest. Adobe definisce la semantica utente; gli anchor architetturali restano quelli già presenti in Chronon3D.