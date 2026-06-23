# Verdetto aggiornato — Camera Chronon3D vs After Effects

> **Snapshot funzionale camera analizzato:** `main@25049b2`, 23 giugno 2026.
>
> **Ultima baseline eseguita:** `main@446a60e2`.
>
> **HEAD ricontrollato:** `main@14dbc415`, 23 giugno 2026.
>
> Questo documento sostituisce il precedente audit, ormai superato da sub-frame
> sampling, Physical Lens, CameraProgram, shot timeline, checkpoint/pre-roll e
> altre implementazioni atterrate successivamente.
>
> Matrice dettagliata: [`CAMERA_FEATURE_MATRIX.md`](CAMERA_FEATURE_MATRIX.md).
> Stato prodotto complessivo: [`CURRENT_READINESS.md`](CURRENT_READINESS.md).
> Prova operativa: [`baselines/main-446a60e2-baseline.md`](baselines/main-446a60e2-baseline.md).

## Verdetto attuale

Per il solo comparto camera e animazione 2.5D destinato al motion graphics,
Chronon3D è stimato al **70–75% di Camera Production V1**.

Per una parità molto ampia con After Effects, includendo framing avanzato,
clipping completo, DOF più fisico, orientation/path completa e migrazione totale
dei percorsi legacy, la stima è **55–60%**.

Queste percentuali sono stime ingegneristiche. Non indicano una baseline CI
verde e non sostituiscono test eseguiti sul commit candidato.

I commit di stabilizzazione successivi allo snapshot funzionale migliorano o
ripuliscono confini renderer/CMake, ma non dimostrano da soli una maggiore parità
camera. L’ultima baseline osservata è 3/4 verde e complessivamente rossa a causa
di un target che non compila.

## Cosa esiste oggi

### Architettura compilata

```text
CameraDescriptor
    → compile_camera()
    → CameraProgram
    → CameraProgram::evaluate(CameraEvalContext, CameraSession)
    → Camera2_5D
    → projection contract
    → renderer
```

Sono presenti:

- descriptor variant-based;
- programma immutabile;
- sessione per render job;
- fingerprint deterministico;
- metadata stateless/requires-history;
- checkpoint e pre-roll per accesso non sequenziale.

### Projection e ottica

- Zoom;
- FOV;
- Physical Lens;
- focal length e sensor size;
- gate fit;
- focal X/Y;
- pixel aspect e anamorphic contract;
- focus distance e aperture;
- temporal motion blur e fondazioni DOF.

### Movimento e timeline

- pose tracks;
- orbit motion;
- trajectory motion;
- sample time sub-frame;
- idle oscillation;
- handheld noise deterministico;
- constraint tipizzati;
- shot timeline;
- Cut, Smooth Blend, Push, Whip Pan e Focus Handoff.

## Correzioni recenti

Il codice corrente include correzioni per:

- PoseTracks che preserva la projection variant attiva;
- orientation applicata una sola volta;
- OrbitMotion con track/dolly nel basis locale della camera;
- `MotionBlurMode` come unica fonte di verità;
- focal X/Y e gate fit;
- checkpoint/pre-roll;
- descriptor camera di default nella composition.

Queste correzioni non devono essere descritte tutte come ✅ finché i relativi
regression test non collegano ed eseguono sul commit candidato.

## Stato reale dei blocker

### TICKET-039 — primo blocker globale osservato

La baseline `446a60e2` si arresta in `src/runtime/render_engine.cpp` perché
`RenderEngine::Impl` usa il vecchio accessor `SoftwareRenderer::settings()`,
mentre il confine renderer conserva `render_settings()` come percorso canonico.

Questo errore compare prima della certificazione dei target camera. La prima
azione di stabilizzazione non è quindi aggiungere una funzione camera, ma
ripristinare la compilabilità attraverso l’API canonica e rieseguire il target
che ha scoperto il problema.

### TICKET-038 — secondo blocker globale noto

Dopo TICKET-039 può riemergere il rot già documentato nel visual test testuale
relativo a lambda capture e deduzione `auto`. Anche questo deve essere chiuso per
produrre una nuova baseline credibile.

### TICKET-029 — blocker specifico camera

TICKET-029 resta aperto sulla type visibility di
`camera_program_compiler.cpp` e impedisce link ed esecuzione completi dei test
scene/camera. Di conseguenza, diversi fix camera restano “code-fix landed /
test-blocked”, non “verificati”.

TICKET-029 è quindi necessario per Camera Production V1, ma non è più corretto
presentarlo come l’unico o il primo blocker globale del repository.

## Gap reali residui

### 1. Percorso authoring unico

Coesistono ancora:

- camera runtime impostata direttamente;
- `AnimatedCamera2_5D`;
- rig moderni e legacy;
- compiled descriptor/program path.

Le nuove composizioni devono convergere sul percorso compilato. Gli altri
percorsi devono diventare adapter temporanei con parity test e fase di rimozione.

### 2. Orientamento lungo path

`OrientAlongPath` è dichiarato, ma la chiusura produttiva richiede:

- tangent provider reale;
- fallback deterministico;
- look-ahead;
- keep horizon;
- banking;
- comportamento sui segmenti Hold;
- test di continuità.

### 3. Framing solver

Mancano o sono parziali:

- bounds reali dei layer;
- multi-target;
- rule-of-thirds;
- dead-zone;
- hysteresis;
- look-ahead;
- Dolly Zoom sul projection contract canonico.

### 4. Clipping

Serve completare il clipping di segmenti, quad e poligoni che attraversano near
e far plane. Non usare sentinel numerici come geometria valida.

### 5. Depth of Field

Il sistema dispone di focus/aperture e fondazioni per-pixel, ma per un modello
più vicino ad AE/camera fisica servono:

- Circle of Confusion;
- near/far blur separati;
- forma e rotazione dell’iride;
- anamorphic bokeh;
- focus breathing opzionale.

### 6. Timeline e diagnostica

La shot timeline esiste, ma la propagazione diagnostica completa e il
random-access determinism devono essere protetti da test bloccanti.

## Ordine corretto di lavoro

1. chiudere TICKET-039;
2. chiudere TICKET-038 se riemerge nella catena di build;
3. chiudere TICKET-029 e collegare i target camera;
4. rieseguire tutti i regression test camera recenti;
5. produrre golden/parity del compiled path;
6. completare `OrientAlongPath`;
7. chiudere trajectory e shot diagnostics;
8. completare framing e clipping essenziali;
9. migrare preset/composizioni legacy;
10. verificare una camera compilata da consumer SDK esterno;
11. soltanto dopo aggiungere ottica premium e multi-camera avanzata.

## Definition of Done

Camera Production V1 è chiusa quando:

- tutte le nuove composizioni usano descriptor/program;
- la sessione è posseduta dal render job;
- nessun compile o catalog lookup avviene per frame;
- projection e orientation hanno un solo contratto;
- `OrientAlongPath` è operativo;
- scene/camera tests collegano ed eseguono;
- parity/golden sono bloccanti;
- legacy ha adapter testati e piano di rimozione;
- un consumer fuori-tree renderizza usando una camera compilata.
