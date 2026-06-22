# Chronon3D Camera — Integration, Tests e Rimozione Legacy

## Missione

Collegare il sistema camera compilato al normale render path, costruire una baseline verificabile e rimuovere progressivamente le API duplicate senza lasciare bridge permanenti, alias confusi o due comportamenti concorrenti.

Questo documento è il gate finale dei tre work package precedenti.

## Regola principale

Una feature camera non è completata quando compila isolatamente. È completata soltanto quando:

1. viene descritta tramite `CameraDescriptor`;
2. viene compilata tramite `compile_camera()`;
3. viene valutata tramite `CameraProgram` e una sessione per-job;
4. entra nel `SceneBuilder` o nel render pipeline canonico;
5. produce output verificato;
6. ha test random-access e sub-frame;
7. non richiede una seconda API legacy per funzionare.

## Integrazione nel render job

Il render job deve possedere lo stato camera mutabile:

```cpp
struct CameraRenderState {
    camera_v1::CameraSession program_session;
    camera_v1::ShotTimelineSession timeline_session;
    camera_v1::FramingSession framing_session;
    CameraCheckpointStore checkpoints;
};
```

Il nome e la posizione esatta possono seguire l'architettura runtime esistente, ma l'ownership deve restare per-job/per-sessione.

È vietato:

- stato camera globale;
- sessioni statiche;
- una sessione condivisa fra render concorrenti;
- sessioni create dentro i nodi;
- sessioni create e distrutte per ogni frame quando esistono constraint stateful.

## Integrazione CameraApi e SceneBuilder

Aggiungere un solo percorso moderno:

```cpp
scene.camera().descriptor(descriptor);
scene.camera().program(program);
scene.camera().preset("camera.hero_push", catalog);
scene.camera().timeline(timeline);
```

### Responsabilità di CameraApi

- selezionare il programma o timeline;
- non compilare ripetutamente a ogni frame;
- non possedere stateful session globale;
- propagare errori di compilazione;
- produrre diagnostica comprensibile;
- conservare compatibilità tramite adapter dichiarati, non tramite overload duplicati indefiniti.

### Responsabilità del render path

- valutare al `SampleTime` richiesto;
- fornire transform snapshot e viewport;
- risolvere sessione corretta;
- gestire stateful pre-roll/checkpoint;
- propagare la camera valutata alla scena;
- usare il projection contract canonico;
- invalidare cache con il fingerprint camera corretto.

## Compilazione fuori dall'hot path

La compilazione camera deve avvenire:

- durante la compilazione della composizione;
- durante il caricamento del template;
- oppure esplicitamente prima del render.

Non deve avvenire dentro:

- `render_frame()`;
- loop tile;
- singoli nodi;
- singoli layer;
- sub-sample motion blur.

Durante il temporal accumulation si rivaluta lo stesso `CameraProgram`; non lo si ricompila.

## Cache e fingerprint

Il fingerprint camera deve includere tutti i dati semanticamente rilevanti:

- source;
- keyframe e easing;
- projection variant;
- lens e gate fit;
- orientation;
- modifier;
- constraint e ordine;
- failure policy;
- parent/target reference;
- motion blur settings;
- timeline e transition spec;
- versione del formato compilato.

Non includere:

- indirizzi di memoria;
- puntatori;
- ordine accidentale di allocazione;
- stato della sessione runtime.

Il programma deve dichiarare se il risultato dipende da:

- solo tempo;
- transform esterni;
- history;
- viewport;
- asset o metadata esterni.

## Diagnostica

Unificare la diagnostica in tipi strutturati:

```cpp
struct CameraDiagnostic {
    CameraDiagnosticSeverity severity;
    CameraDiagnosticCode code;
    std::string message;
    std::string camera_id;
    std::optional<i32> shot_index;
    std::optional<SampleTime> sample_time;
};
```

Codici minimi:

- invalid source;
- preset not found;
- circular preset reference;
- invalid projection;
- invalid lens;
- invalid trajectory;
- target not found;
- parent not found;
- constraint failure;
- state history unavailable;
- non-finite camera;
- clipping degeneracy;
- transition mismatch.

Non perdere la diagnostica passando da `CameraProgram` a `ShotTimeline` o dal builder al renderer.

## Piano test canonico

### Test unitari compiler

Creare e collegare esplicitamente:

```text
tests/scene/camera/test_camera_program_compiled.cpp
```

Copertura minima:

- static source;
- pose tracks;
- orbit;
- trajectory;
- registered preset;
- preset mancante;
- ciclo A -> B -> A;
- projection Zoom;
- projection FOV;
- projection PhysicalLens;
- modifier;
- orientation;
- tutti i constraint;
- failure policy;
- metadata;
- fingerprint;
- invalid descriptor.

Il file non deve essere soltanto menzionato nei commenti: deve esistere ed essere incluso nel target CMake.

### Test projection parity

Costruire una tabella di casi con input e output atteso e verificare che tutte le superfici passino dal contratto canonico.

Confrontare:

- projection helper;
- layer projection;
- projection context;
- framing solver;
- renderer software;
- debug overlay.

### Test golden render

Aggiungere scene minime, non content pack pesanti:

1. one-node static;
2. two-node look-at;
3. orbit;
4. Bézier trajectory;
5. physical 24mm;
6. physical 135mm;
7. rack focus;
8. motion blur temporal;
9. shot transition;
10. framing multi-target.

Per ogni scena registrare:

- frame specifici;
- hash deterministico;
- tolleranza quando necessaria;
- viewport;
- frame rate;
- preset e fingerprint.

### Test determinismo

Obbligatori:

- seriale vs parallelo;
- sequenziale vs frame diretto;
- ordine frame casuale;
- due render job simultanei;
- retry dello stesso frame;
- sub-frame ripetuto;
- tile vs full frame quando applicabile;
- checkpoint restore;
- cache hit vs cache miss.

### Test API migration

Per ogni adapter legacy mantenuto temporaneamente:

```text
legacy input -> descriptor adapter -> compiled output
```

Deve essere confrontato con il risultato legacy atteso fino al momento della rimozione.

Questi test non devono giustificare la permanenza del legacy. Servono soltanto a rendere sicura la migrazione.

## Gate CI anti-duplicazione

Aggiungere uno script camera boundary, per esempio:

```text
tools/check_camera_architecture.sh
```

Il gate deve fallire se trova nuovi utilizzi fuori dalle allowlist di migrazione:

- nuovi include di `animated_camera_2_5d.hpp` nelle composizioni moderne;
- nuovi utilizzi di `camera_rig::CameraRig`;
- nuovi preset che restituiscono `AnimatedCamera2_5D`;
- nuovi registry camera;
- nuove funzioni di projection math fuori dal contratto canonico;
- nuove compilazioni camera dentro hot path;
- nuove sessioni statiche/globali;
- nuovi `Camera2_5D` costruiti direttamente nei content pack moderni;
- nuovi evaluator source separati dal `CameraProgram` canonico.

Il gate non deve impedire test unitari o file di migrazione esplicitamente allowlisted.

## Inventario legacy obbligatorio

Prima di eliminare file, produrre una tabella aggiornata:

| Elemento | Stato | Sostituto canonico | Adapter | Fase rimozione |
|---|---|---|---|---|
| `AnimatedCamera2_5D` | legacy | `PoseTracksSource` | sì, temporaneo | fase 3 |
| `CameraRig` moderno | migrazione | `OrbitMotion` | sì | fase 2 |
| `camera_rig::CameraRig` | legacy | `OrbitMotion` | sì, minimo | fase 2 |
| helper motion imperative | legacy | preset descriptor | sì/no | fase 2 |
| camera motion registry | rimosso | `CameraCatalog` | no | completato |
| constraint class hierarchy | rimosso | variant constraint | no | completato |
| legacy motion blur alias | compatibility | temporal accumulation | sì | fase 3 |

La tabella deve essere aggiornata durante il lavoro. Non dichiarare rimosso ciò che è ancora incluso dallo SDK.

## Strategia di rimozione

### Fase 1 — Freeze legacy

- nessuna nuova feature sulle API legacy;
- deprecation annotation dove possibile;
- documentazione indirizzata al percorso compilato;
- boundary gate in modalità bloccante per nuovi utilizzi.

### Fase 2 — Adapter-only

- `CameraRig` delega a `OrbitMotion`;
- `AnimatedCamera2_5D` viene convertita in `PoseTracksSource`;
- preset legacy restituiscono descriptor tramite adapter;
- rimuovere implementazioni duplicate dopo parity test.

### Fase 3 — SDK cleanup

- rimuovere header legacy dallo SDK pubblico;
- spostare adapter in compatibility package opzionale, se necessario;
- eliminare alias e typedef non più usati;
- rimuovere test del comportamento legacy sostituendoli con test adapter/descriptor.

### Fase 4 — Delete

- eliminare file senza call site;
- eliminare liste CMake residue;
- eliminare commenti che rimandano a test o percorsi non esistenti;
- eliminare documentazione duplicata;
- aggiornare changelog e migration guide.

## Cosa non fare

- non rinominare il legacy in `V2` lasciando intatta la duplicazione;
- non introdurre `CameraManager` globale;
- non creare `CameraProgram2`;
- non mantenere due cataloghi;
- non mantenere due modelli di constraint;
- non aggiungere overload indefiniti per ogni vecchio tipo;
- non conservare file vuoti come falso segnale di compatibilità;
- non lasciare commenti che indicano test inesistenti;
- non rimuovere una API prima che esista adapter, parity test e migrazione dei call site;
- non mescolare la rimozione legacy con nuove feature cinematografiche nella stessa PR.

## Suddivisione PR consigliata

### CAM-01 — Compiled camera tests

- aggiungere il test compiled mancante;
- collegarlo a CMake;
- registrare failure reali;
- nessuna nuova feature.

### CAM-02 — Compiler validation

- compile context;
- cycle detection;
- errori strutturati;
- metadata e fingerprint.

### CAM-03 — Optics and orientation

- projection variant completa;
- physical lens;
- look-at singolo;
- orient along path.

### CAM-04 — Source parity

- base camera comune;
- orbit locale;
- trajectory completa;
- modifier deterministici.

### CAM-05 — Render integration

- CameraApi moderna;
- render job session;
- cache integration;
- diagnostica.

### CAM-06 — Timeline and state

- transition spec;
- timeline result;
- checkpoint/pre-roll;
- random-access parity.

### CAM-07 — Legacy adapters

- rig -> orbit;
- animated camera -> pose tracks;
- preset migration;
- parity test.

### CAM-08 — Legacy deletion

- rimozione file e include;
- SDK cleanup;
- CMake cleanup;
- documentazione finale.

Ogni PR deve partire da `origin/main` aggiornato, avere un solo obiettivo e non mescolare feature, refactor e cancellazione massiva.

## Definition of Done globale

Il sottosistema camera è considerato canonicalizzato quando:

- `CameraDescriptor -> CameraProgram` è l'unico percorso moderno;
- `SceneBuilder` e render pipeline usano quel percorso;
- sessioni camera sono per-job;
- projection e orientation hanno una sola fonte matematica;
- motion, trajectory e timeline sono deterministici;
- test compiled, parity, golden e random-access sono bloccanti;
- il gate vieta nuove dipendenze legacy;
- `AnimatedCamera2_5D` e i due rig non mantengono implementazioni autonome;
- lo SDK non esporta API ritirate;
- non esistono file placeholder che rimandano a implementazioni o test assenti;
- documentazione, codice, test e CMake descrivono lo stesso stato.