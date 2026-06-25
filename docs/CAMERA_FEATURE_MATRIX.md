# Camera Feature Matrix — Chronon3D

> **Snapshot funzionale camera analizzato:** `main@25049b2`, 23 giugno 2026.
>
> **Ultima baseline eseguita:** `main@446a60e2`.
>
> **HEAD ricontrollato:** `main@14dbc415`, 23 giugno 2026.
>
> Stato presente: [`CURRENT_STATUS.md`](CURRENT_STATUS.md).
> Prova operativa: [`baselines/main-446a60e2-baseline.md`](baselines/main-446a60e2-baseline.md).
> Piano canonico: [`camera-plan/`](camera-plan/).
>
> Una feature presente nel codice ma con test non eseguibili è 🟡, non ✅.

## Legenda

- ✅ **Verificato** — implementato e coperto da prova eseguita osservata.
- 🟡 **Implementato/parziale** — codice presente, ma limiti, migrazione o test aperti.
- 🔴 **Bloccante** — impedisce la chiusura della Camera Production V1.
- 🔵 **Pianificato** — design presente, implementazione non completata.
- ⚪ **Legacy** — mantenuto soltanto per compatibilità/migrazione.

## Valutazione complessiva

| Obiettivo | Completezza stimata | Nota |
|---|---:|---|
| Camera Production V1 per motion graphics 2.5D | 70–75% | Percorso compilato avanzato; certificazione test e migrazione legacy ancora aperte. |
| Parità camera molto ampia con After Effects | 55–60% | Framing, clipping, DOF e path/orientation avanzati non sono tutti completi. |

Le percentuali sono stime ingegneristiche, non risultati CI. I commit di
stabilizzazione successivi a `25049b2` non giustificano da soli una variazione
della stima funzionale camera; cambiano invece lo stato di verificabilità del
repository.

## Stato della verifica corrente

L’ultima baseline documentata su `446a60e2` ha prodotto tre controlli verdi e un
target build rosso. La build si arresta attualmente su TICKET-039 nel confine
`RenderEngine`/`SoftwareRenderer`; TICKET-038 resta il blocker successivo noto.
Di conseguenza, i target camera non sono ancora raggiunti dalla baseline completa.

TICKET-029 resta un blocker specifico della compilazione/link dei test camera,
ma non deve essere descritto come l’unico o il primo blocker globale del
repository.

## 1. Architettura canonica

| Feature | Stato | Evidenza / limite |
|---|---|---|
| `Camera2_5D` snapshot runtime | ✅ | Tipo runtime usato da projection e renderer. Non deve essere l’authoring primario futuro. |
| `CameraDescriptor` authoring | 🟡 | Presente con source/orientation/modifier/constraint variant; migrazione composizioni non completa. |
| `compile_camera()` | 🟡 | Produce `CameraProgram`; la certificazione dei regression test è bloccata dalla catena TICKET-039/TICKET-038 e poi da TICKET-029. |
| `CameraProgram` immutabile | 🟡 | Entry point compilato presente e metadata dependency disponibili. |
| `CameraSession` per render job | 🟡 | Stato separato e checkpoint/pre-roll presenti; integrazione e prove complete da chiudere. |
| Fingerprint descriptor | 🟡 | Hash deterministico implementato; includere nei gate di regressione. |
| Default descriptor in `Composition` | 🟡 | Integrazione presente; consumer e full-validation da verificare. |
| Unico percorso authoring | 🔴 | Coesistono ancora camera diretta, `AnimatedCamera2_5D`, rig moderni/legacy e compiled path. |

Percorso obbligatorio per nuove feature:

```text
CameraDescriptor
    → compile_camera()
    → CameraProgram
    → evaluate(CameraEvalContext, CameraSession)
    → Camera2_5D
    → projection contract
    → renderer
```

## 2. Projection e ottica

| Feature | Stato | Evidenza / limite |
|---|---|---|
| Zoom projection | ✅ | Contratto esistente e test storici. |
| Vertical FOV projection | 🟡 | Supportata; fix recente evita che PoseTracks la sovrascriva con Zoom. Regression run da certificare. |
| Physical Lens projection | 🟡 | Variant e `LensModel` presenti; percorso e parity completa da verificare. |
| Focal length / sensor size / f-stop | 🟡 | Modello presente e animabile in parti del sistema; ownership da rendere unica. |
| Focal X/Y separati | 🟡 | Contratto implementato; full parity renderer/solver da chiudere. |
| GateFit Fill | 🟡 | Presente nel lens/projection contract. |
| GateFit Overscan/Fit | 🟡 | Contratto presente; active viewport parity da certificare. |
| GateFit Stretch | 🟡 | Focal X/Y implementati; test end-to-end da certificare. |
| Pixel aspect | 🟡 | Contratto presente; propagation completa da verificare. |
| Anamorphic squeeze | 🟡 | Contratto/preset presenti; propagation e bokeh avanzato incompleti. |
| Near/far plane | 🟡 | Parametri/contratto parziali. |
| Clipping point | 🟡 | Fondazioni presenti. |
| Clipping segment/quad/polygon | 🔵 | Necessario per primitive che attraversano il near plane. |

## 3. Source e movimento

| Feature | Stato | Evidenza / limite |
|---|---|---|
| Static camera source | 🟡 | Presente nel compiled path. |
| Pose Tracks | 🟡 | Posizione, rotazione, target, zoom/FOV e DOF channels presenti. |
| Orbit Motion | 🟡 | Track/dolly corretti nel basis locale; nuovi test compilano isolatamente ma non sono ancora eseguiti. |
| Trajectory Motion | 🟡 | Tipo e trajectory path presenti; base-state preservation e test completi ancora aperti. |
| Arc-length LUT | ✅ | Implementazione e regression test documentati. |
| Sub-frame `SampleTime` | ✅ | Animated values, camera, composition e temporal samples usano il contratto sub-frame. |
| Idle oscillation | 🟡 | Modifier assoluto nel tempo presente. |
| Handheld noise | 🟡 | Modifier deterministico absolute-time presente. |
| Dolly zoom | 🟡 | Helper/preset esistenti; regression e percorso canonico da chiudere. |
| Product orbit / fly-through / dashboard tour | 🔵 | Preset prodotto, non primitive core; introdurre solo nel catalogo canonico. |

## 4. Orientamento

| Feature | Stato | Evidenza / limite |
|---|---|---|
| Fixed orientation | 🟡 | Variant presente. |
| Look-at point | 🟡 | Presente; fix recente evita doppia applicazione. Regression run da certificare. |
| Look-at layer | 🟡 | Variant e context dependency presenti. |
| Roll/local offset | 🟡 | Presente in source/rig; ordine canonico da proteggere con parity test. |
| Parent orientation | 🟡 | Fondazioni esistenti; integrazione unica da chiudere. |
| `OrientAlongPath` type | 🟡 | Variant dichiarata. |
| `OrientAlongPath` completo | 🔴 | TICKET-023: tangent provider, fallback, look-ahead, keep-horizon e banking non chiusi. |
| Quaternion continuity / shortest arc | 🟡 | Parti presenti; evitare conversioni Euler come rappresentazione primaria. |

## 5. Constraint e framing

| Feature | Stato | Evidenza / limite |
|---|---|---|
| LookAt constraint | 🟡 | Spec tipizzata presente. |
| KeepHorizon constraint | 🟡 | Spec tipizzata presente. |
| DampedFollow constraint | 🟡 | Stateful; richiede session/pre-roll corretti. |
| Distance constraint | 🟡 | Spec tipizzata presente. |
| RotationLimit constraint | 🟡 | Spec tipizzata presente. |
| Camera checkpoint/pre-roll | 🟡 | Implementato per random access; test completi da certificare. |
| Basic fit/framing | 🟡 | Helper/solver di base presenti. |
| Bounds reali dei layer | 🔵 | Non affidarsi a tabelle `layer_sizes`. |
| Multi-target framing | 🔵 | Da implementare nel solver canonico. |
| Rule-of-thirds | 🔵 | Da implementare nel solver canonico. |
| Dead-zone/hysteresis/look-ahead | 🔵 | Da implementare senza matematica prospettica duplicata. |
| DollyZoom solver | 🔵 | Deve usare lo stesso projection contract. |

## 6. Motion blur e depth of field

| Feature | Stato | Evidenza / limite |
|---|---|---|
| Temporal multi-sample motion blur | 🟡 | Pipeline e sub-frame sampling presenti; baseline deterministica completa da certificare. |
| `MotionBlurMode` source of truth | 🟡 | Bool legacy rimosso; regression run da certificare. |
| Shutter angle/phase/pattern/filter | 🟡 | Contratti presenti in più parti; verificare end-to-end e cache keys. |
| Depth buffer / per-pixel DOF foundation | 🟡 | Backend support presente. |
| Focus distance / aperture | 🟡 | Presenti e animabili. |
| Circle of Confusion fisico | 🔵 | Non ancora chiuso come modello canonico. |
| Near/far blur separati | 🔵 | Da implementare. |
| Iris blades/rotation/roundness | 🔵 | Modello/rendering non produttivo. |
| Anamorphic bokeh / focus breathing | 🔵 | Effetti avanzati post-V1. |

## 7. Shot timeline e transizioni

| Feature | Stato | Evidenza / limite |
|---|---|---|
| `ShotTimeline` | 🟡 | Sequenza di programmi camera presente. |
| Per-shot sessions | 🟡 | `ShotTimelineSession` presente. |
| Cut | 🟡 | Implementazione/factory presente. |
| Smooth Blend | 🟡 | Implementazione/factory presente. |
| Push | 🟡 | Implementazione/factory presente. |
| Whip Pan | 🟡 | Implementazione/factory presente. |
| Focus Handoff | 🟡 | Implementazione/factory presente. |
| Transition catalog | 🟡 | Catalogo DI, non singleton, presente. |
| Diagnostica completa | 🔴 | TICKET-027: non tutta la diagnostica viene propagata. |
| Random-access determinism | 🟡 | Checkpoint/pre-roll presente; integrazione timeline da certificare. |

## 8. Diagnostics e tool

| Feature | Stato | Evidenza / limite |
|---|---|---|
| Camera debug overlay | 🟡 | Safe area, target, bounds/path e viste diagnostiche presenti. |
| Camera shot validator | 🟡 | Validazioni di base presenti; soglie/options da canonicalizzare. |
| CLI path report | 🔵 | Non esposta come comando prodotto stabile. |
| CLI camera validate | 🔵 | Pianificata. |
| CLI debug video | 🔵 | Pianificata. |
| JSON report stabile | 🔵 | Schema/versionamento da definire. |
| Golden camera suite | 🔴 | Necessaria per dichiarare Camera Production V1. |

## 9. Catena di blocker per la certificazione camera

### TICKET-039 — blocker globale corrente

La baseline si arresta nel percorso `RenderEngine`/`SoftwareRenderer` prima di
raggiungere la certificazione completa dei target camera. Correggere il consumer
verso l’accessor canonico `render_settings()` e rieseguire il target scopritore.

### TICKET-038 — blocker globale successivo noto

Il visual test testuale contiene un rot di lambda capture/deduzione che potrebbe
manifestarsi subito dopo TICKET-039. Deve essere chiuso prima di dichiarare la
catena di build sufficientemente sana per una nuova baseline.

### TICKET-029 — blocker specifico camera

`camera_program_compiler.cpp` presenta una rottura di type visibility che blocca
link ed esecuzione dei test scene/camera. Finché è aperto, i fix camera recenti
con test compilati isolatamente restano 🟡.

Dopo la chiusura della catena, rieseguire almeno:

- projection variant preservation;
- single orientation application;
- orbit local basis;
- motion blur mode;
- checkpoint/pre-roll;
- projection focal X/Y e gate fit;
- composition default descriptor.

## 10. Definition of Done — Camera Production V1

La milestone è chiusa quando:

1. le nuove composizioni usano `CameraDescriptor` o `CameraProgram`;
2. `CameraSession` appartiene al render job;
3. non avvengono compile/catalog lookup per frame;
4. projection e orientation hanno un solo contratto;
5. `OrientAlongPath` è realmente operativo;
6. test scene/camera collegano ed eseguono in CI;
7. parity e golden sono bloccanti;
8. adapter legacy hanno regression parity e data/fase di rimozione;
9. il consumer SDK esterno usa una camera compilata;
10. nessun secondo registry, evaluator o projection contract è stato introdotto.
