# Chronon3D Camera — Canonical Architecture

## Missione

Portare il sottosistema camera a un solo percorso di authoring, compilazione, valutazione e rendering:

```text
CameraDescriptor
    -> compile_camera()
    -> CameraProgram
    -> CameraProgram::evaluate(CameraEvalContext, CameraSession)
    -> Camera2_5D
    -> CameraProjectionSource
    -> canonical projection contract
    -> renderer
```

Questo documento definisce la direzione architetturale obbligatoria. Le API esistenti che non seguono questo flusso possono restare soltanto come adapter di migrazione temporanei e devono avere una data o una fase di rimozione.

## Stato da preservare

Le seguenti fondazioni sono corrette e devono restare canoniche:

- `CameraDescriptor` come forma di authoring dati.
- `compile_camera()` come unico passaggio descriptor -> programma eseguibile.
- `CameraProgram` immutabile dopo la compilazione.
- `CameraSession` come stato mutabile per-job/per-render.
- `Camera2_5D` come snapshot runtime valutato per un istante.
- `CameraProjectionSource` come vista read-only verso il contratto di proiezione.
- `camera_projection_contract.hpp` come unica fonte matematica per world-to-camera e world-to-screen.
- `CameraCatalog` come catalogo dati di preset, senza registry globale concorrente.
- `ShotTimeline` come composizione di programmi camera già compilati.

## Problema corrente

Oggi convivono più percorsi sovrapposti:

1. `Camera2_5D` impostata direttamente.
2. `AnimatedCamera2_5D` valutata dal `SceneBuilder`.
3. `CameraRig` moderno.
4. `camera_rig::CameraRig` legacy.
5. `CameraDescriptor -> CameraProgram`.
6. helper e preset che restituiscono ancora tipi legacy.

Questi percorsi duplicano responsabilità e permettono risultati diversi per la stessa intenzione artistica.

## Architettura finale

### Authoring

Tutte le nuove composizioni devono costruire un `CameraDescriptor`.

```cpp
camera_v1::CameraDescriptor descriptor;
descriptor.id = "hero_push";
descriptor.base = ...;
descriptor.source = camera_v1::PoseTracksSource{...};
descriptor.orientation = camera_v1::LookAtPoint{...};
descriptor.constraints = {...};
```

Non aggiungere nuove funzioni che restituiscono direttamente `Camera2_5D`, `AnimatedCamera2_5D` o `CameraRig` per descrivere un movimento artistico.

### Compilazione

`compile_camera()` deve:

1. validare interamente il descriptor;
2. risolvere i riferimenti al catalogo;
3. rilevare cicli fra preset;
4. normalizzare projection e optics mode;
5. compilare traiettorie e lookup table;
6. assegnare gli slot dei constraint stateful;
7. calcolare dipendenze temporali ed esterne;
8. calcolare un fingerprint deterministico;
9. produrre un programma immutabile senza lookup runtime.

### Valutazione

`CameraProgram::evaluate()` deve essere l'unico entry point per ottenere la camera runtime.

```cpp
auto result = program.evaluate(eval_context, camera_session);
```

La funzione deve restituire:

- camera valutata;
- stato `ok`;
- diagnostica strutturata;
- indicazione delle dipendenze da history;
- eventuale fallback applicato.

### Runtime ownership

- `CameraProgram`: stato immutabile condivisibile.
- `CameraSession`: stato per singolo render job.
- `ShotTimelineSession`: stato per timeline nel render job.
- `FramingSession`: stato per solver nel render job.
- nessuno stato camera mutabile in singleton o registry globale.

## API da aggiungere

Integrare il percorso compilato nella normale API di composizione:

```cpp
class CameraApi {
public:
    CameraApi& descriptor(const camera_v1::CameraDescriptor& descriptor);
    CameraApi& program(const camera_v1::CameraProgram& program);
    CameraApi& preset(std::string_view id,
                      const camera_v1::CameraCatalog& catalog);
    CameraApi& timeline(const camera_v1::ShotTimeline& timeline);
};
```

Il builder non deve creare una `CameraSession` locale a ogni chiamata. Deve ricevere o risolvere la sessione dal contesto del render job.

Aggiungere un adapter esplicito per i casi legacy:

```cpp
CameraDescriptor descriptor_from_legacy(const AnimatedCamera2_5D&);
CameraDescriptor descriptor_from_legacy(const CameraRig&);
```

Gli adapter devono vivere in una directory di migrazione e non nel core canonico.

## API da eliminare o deprecare

### Deprecare subito

- nuovi utilizzi di `SceneBuilder::animated_camera()`;
- nuovi preset che restituiscono `AnimatedCamera2_5D`;
- nuovi utilizzi di `camera_rig::CameraRig`;
- helper imperative separati per dolly, pan, orbit o focus che bypassano `CameraDescriptor`;
- creazione di `Camera2_5D` direttamente nelle composizioni, tranne test matematici e adapter.

### Eliminare dopo la migrazione

- `camera_rig::CameraRig` legacy;
- duplicazione fra `CameraRig` moderno e `OrbitMotion`;
- vecchi namespace di preset che producono tipi legacy;
- alias pubblici non più necessari dopo la finestra di compatibilità;
- builder path camera non compilati.

### Mantenere

`Camera2_5D` deve restare come snapshot runtime, ma non come API primaria di authoring.

## Regole anti-duplicazione

È vietato:

- introdurre `CameraProgramV2` accanto a `CameraProgram`;
- introdurre un nuovo `CameraRegistry` oltre a `CameraCatalog`;
- creare un secondo projection contract;
- creare un secondo evaluator per descriptor;
- mantenere due rig completi con feature equivalenti;
- copiare la stessa logica source-specific in static, pose, orbit e trajectory evaluator;
- aggiungere service locator o singleton per accedere alla camera session;
- costruire o compilare il programma a ogni frame.

Ogni nuova source, modifier, orientation o constraint deve entrare nelle variant canoniche esistenti oppure sostituirle con una forma più completa nello stesso percorso.

## Implementazione richiesta

### Fase 1 — Base runtime comune

Creare una funzione interna canonica:

```cpp
Camera2_5D make_camera_from_base(
    const CameraBaseSpec& base,
    SampleTime time);
```

Ogni source evaluator deve partire da questo snapshot e modificare soltanto i canali di propria responsabilità.

### Fase 2 — Compile context

Introdurre:

```cpp
struct CameraCompileContext {
    const CameraCatalog* catalog{nullptr};
    std::vector<std::string> resolution_stack;
    std::unordered_set<std::string> visited;
    std::vector<CameraCompileDiagnostic> diagnostics;
};
```

La risoluzione dei preset deve essere iterativa o ricorsiva con cycle detection obbligatoria.

### Fase 3 — Program metadata

Il programma compilato deve esporre almeno:

```cpp
enum class CameraEvaluationDependency {
    Stateless,
    RequiresHistory
};

struct CameraProgramMetadata {
    bool time_dependent{false};
    bool has_external_dependencies{false};
    CameraEvaluationDependency evaluation_dependency{
        CameraEvaluationDependency::Stateless
    };
    u64 fingerprint{0};
};
```

### Fase 4 — Scene integration

Il render job deve possedere le sessioni camera e passarle alla valutazione. Il `SceneBuilder` deve soltanto descrivere o selezionare il programma, non possedere stato temporale persistente.

## Test obbligatori

- descriptor statico -> programma -> snapshot;
- stessa camera via preset e descriptor diretto produce snapshot equivalente;
- nessun lookup catalog durante `evaluate()`;
- programma condiviso fra due render job con sessioni separate;
- nessuna contaminazione di stato fra sessioni;
- cycle detection nei riferimenti preset;
- fingerprint uguale per descriptor semanticamente uguali;
- fingerprint diverso per canali differenti;
- adapter legacy produce lo stesso risultato prima della rimozione;
- il percorso canonico compila senza includere header legacy.

## Criteri di chiusura

Questo work package è chiuso quando:

- le nuove composizioni usano `CameraDescriptor` o `CameraProgram`;
- `CameraApi` accetta il percorso compilato;
- `CameraSession` appartiene al render job;
- `compile_camera()` valida e normalizza realmente il descriptor;
- non esistono nuovi preset legacy;
- esiste una lista esplicita di API deprecate con fase di rimozione;
- i test del percorso compilato sono bloccanti in CI;
- non è stato introdotto nessun nuovo registry o evaluator parallelo.