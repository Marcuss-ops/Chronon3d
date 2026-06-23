# Chronon3D — Current Product Readiness

> **Snapshot funzionale analizzato:** `main@25049b2` — 23 giugno 2026.
>
> **Ultima baseline eseguita e documentata:** `main@446a60e2`.
>
> **HEAD ricontrollato:** `main@844bc7c0` — 23 giugno 2026.
>
> Questo documento distingue la copertura funzionale dalla prontezza operativa.
> Le percentuali sono stime ingegneristiche, non risultati CI. Una funzione è
> dichiarata **verificata** solo quando esistono build, test o consumer esterni
> eseguiti con successo sul commit indicato.

## Come leggere gli stati

| Stato | Significato |
|---|---|
| ✅ Verificato | Implementato e coperto da una prova eseguibile osservata. |
| 🟡 Implementato/parziale | Codice presente, ma con limiti, migrazione incompleta o test bloccati. |
| 🔴 Bloccante | Impedisce di dichiarare stabile il sottosistema o la release. |
| 🔵 Pianificato | Design o roadmap presenti, implementazione non completata. |

## Sintesi esecutiva

Chronon3D è un motore C++20 headless, CPU-first e code-first per motion graphics.
Non è una copia generale di After Effects e non deve diventare un editor GUI.
Il traguardo realistico è offrire rendering deterministico, testi animati,
camera 2.5D cinematica, compositing ed esportazione tramite SDK e pacchetti
riutilizzabili.

| Obiettivo | Completezza stimata | Stato reale |
|---|---:|---|
| Testi per titoli, kinetic typography e sottotitoli | 60–65% | Fondazioni ampie; visual target attualmente rosso, workflow produttivi e word timing da chiudere. |
| Parità testuale molto ampia con After Effects | 35–40% | Variable fonts, ICU globale, Text 3D, MSDF ed expression selector non sono produttivi. |
| Camera AE-style per motion graphics 2.5D | 70–75% | Percorso compilato avanzato; certificazione test, migrazione legacy e framing/ottica restano aperti. |
| Parità camera molto ampia con After Effects | 55–60% | Clipping completo, DOF più fisico, orientamento/path e solver avanzati non sono tutti chiusi. |
| SDK C++ installabile | 80–85% | Target e package CMake esistono; serve prova end-to-end reale sul commit candidato. |
| SDK multipiattaforma e cross-language | 30–40% | Manca un C ABI stabile e manca un formato dichiarativo pubblico per le animazioni. |

Le stime servono per pianificare il prodotto. Non autorizzano claim come
“release-ready”, “baseline verde” o “parità AE completa”.

## Evidenza operativa corrente

L’ultima baseline documentata su `main@446a60e2` ha prodotto:

| Controllo | Esito |
|---|---|
| Software renderer boundary gate | ✅ PASS |
| `linux-full-validation` configure | ✅ PASS |
| `linux-lean-dev` configure | ✅ PASS |
| Build `chronon3d_text_preset_visual_tests` | ❌ FAIL |

Il verdetto netto è **3/4 verde ma baseline rossa**. La build si arresta su
TICKET-039 nel confine `RenderEngine`/`SoftwareRenderer`. TICKET-038 è il
blocker globale successivo noto. TICKET-029 resta un blocker specifico dei test
camera.

L’HEAD corrente contiene commit successivi alla baseline, incluso il ritiro
dell’alias non namespaced `Chronon3D_SDK`, ma non è ancora coperto da una nuova
full validation sullo stesso commit.

## 1. Testo e kinetic typography

### Presente nel codice

- FreeType, HarfBuzz e FriBidi.
- Shaping, bidirezionalità, layout, wrap, overflow e auto-fit.
- `TextSpec`, `TextRunSpec` e `TextDocument` come modelli composabili.
- Span di stile e struttura a paragrafi.
- Animazione per glifo con posizione, scala, rotazione, skew, anchor, opacity,
  blur, fill, stroke, tracking, baseline shift e character offset.
- Selector per glifo, grapheme, carattere, parola e riga.
- Valutazione con `SampleTime`.
- Text-on-path già presente da consolidare, non da riscrivere.
- Registry centrale dei preset e percorso canonico di composizione dei preset.
- Golden/sentinel iniziali per un sottoinsieme di preset testuali.

### Stato delle prove

Il target `chronon3d_text_preset_visual_tests` è registrato ma l’ultima baseline
non lo compila. Il primo errore osservato è TICKET-039; TICKET-038 può emergere
subito dopo. Finché il target non compila ed esegue, i sentinel esistenti non
costituiscono una certificazione prodotto completa.

### Parziale o non ancora produttivo

- Rich text completo nello stesso paragrafo, inclusi animator per span e
  identità semantica per parola.
- Pipeline transcript/SRT/JSON con word timing.
- Highlight parola corrente, karaoke fill, active word pop e gestione automatica
  delle righe.
- Wiggly, Wave e Random selector completi.
- Visual regression sistematica per tutti i preset, aspect ratio e frame rate.
- Motion blur testuale ottimizzato e verificato su bounding box dinamiche.
- Preset produttivi documentati e parametrizzati per consumer esterni.

### Pianificato o sperimentale

- Variable fonts animate.
- ICU opzionale per segmentazione e line breaking globale.
- Color emoji/fallback completo.
- Expression Selector produttivo.
- Text 3D con extrusion/bevel.
- MSDF, morph e displacement avanzati.

### Criterio per Text Production V1

Text Production V1 è chiuso quando:

1. il visual regression target compila ed esegue;
2. almeno 20 preset generali e 8 preset subtitle sono verificati;
3. esiste input word-timing JSON/SRT;
4. styling per parola e highlight sono produttivi;
5. ogni preset ha golden su 16:9 e 9:16, testo corto/lungo e almeno tre timestamp;
6. il consumer SDK installato usa i preset senza includere header interni;
7. output seriale e parallelo è deterministico.

## 2. Camera

### Percorso canonico presente

```text
CameraDescriptor
    → compile_camera()
    → CameraProgram
    → CameraProgram::evaluate(CameraEvalContext, CameraSession)
    → Camera2_5D
    → CameraProjectionSource / projection contract
    → renderer
```

Sono presenti:

- descriptor variant-based per source, orientation, modifier e constraint;
- programma compilato immutabile;
- sessione camera per render job;
- Zoom, FOV e Physical Lens;
- focal length, sensor size, aperture e focus distance;
- Pose Tracks, Orbit Motion e Trajectory Motion;
- look-at point/layer e tipo `OrientAlongPath`;
- idle oscillation e handheld noise deterministico;
- constraint tipizzati;
- valutazione sub-frame e motion blur temporale;
- shot timeline e transizioni Cut, Smooth Blend, Push, Whip Pan e Focus Handoff;
- fingerprint deterministico del descriptor;
- checkpoint/pre-roll per stato camera in accesso non sequenziale.

### Stato corrente da non sovrastimare

Il repository conserva ancora percorsi sovrapposti: `Camera2_5D` diretta,
`AnimatedCamera2_5D`, rig moderni/legacy e il percorso compilato. Le nuove feature
devono entrare soltanto in `CameraDescriptor → CameraProgram`; gli altri percorsi
sono adapter temporanei o debito di migrazione.

La certificazione dei fix camera dipende dalla catena reale dei blocker:

1. TICKET-039, che interrompe la build globale prima dei target camera;
2. TICKET-038, blocker globale successivo noto;
3. TICKET-029, rottura specifica di type visibility/link dei test camera.

Un fix compilato isolatamente resta 🟡 finché il relativo test non collega ed
esegue sul commit candidato.

### Gap principali

- completare `OrientAlongPath` con tangent provider, look-ahead, keep-horizon e banking;
- chiudere trajectory/base-state preservation e propagazione diagnostica della shot timeline;
- rimuovere i percorsi legacy dopo adapter e regression parity;
- completare clipping near/far di segmenti, quad e poligoni;
- framing solver con multi-target, rule-of-thirds, dead-zone, hysteresis e bounds reali;
- DOF con Circle of Confusion, near/far separati e bokeh più fisico;
- golden camera e test parity bloccanti sul percorso compilato.

### Criterio per Camera Production V1

1. tutte le nuove composizioni usano `CameraDescriptor` o `CameraProgram`;
2. `CameraSession` è posseduta dal render job;
3. nessun lookup o compilazione avviene per frame;
4. orientation e projection hanno un solo contratto matematico;
5. TICKET-039, TICKET-038 e TICKET-029 non bloccano la catena richiesta;
6. i test camera compilati collegano ed eseguono in CI;
7. adapter legacy hanno parity test e una fase di rimozione;
8. consumer SDK esterno può costruire e usare una camera compilata.

## 3. SDK ed esportazione delle animazioni

### Presente

- header pubblici installabili;
- singolo archivio aggregato `libchronon3d_sdk_impl.a`;
- `Chronon3DConfig.cmake` e `Chronon3DTargets.cmake`;
- target pubblico canonico `Chronon3D::SDK`;
- alias legacy `Chronon3D_SDK` rimosso;
- central registry CMake per build, aggregation, install ed export;
- progetto consumer esterno basato su `find_package(Chronon3D CONFIG REQUIRED)`;
- estensioni tramite `ExtensionModule` e `ExtensionContext`.

### Limite della prova corrente

Il consumer installato dimostra il confine CMake e il link di un simbolo SDK.
Non dimostra ancora il workflow prodotto completo:

```text
install package
→ find_package
→ register external animation pack
→ create runtime
→ render text + camera + effects
→ write image/video
```

Questa prova deve diventare un gate release.

### Mancante per altri programmi e linguaggi

Il vecchio C API è stato rimosso. Per Python, C#, Node.js, Rust, Blender, Unreal
o altri host servono due superfici distinte:

1. **C ABI stabile** con handle opachi per runtime, package, render job,
   framebuffer, errori e diagnostica;
2. **formato dichiarativo versionato** per composizioni e animazioni, ad esempio
   un pacchetto `.chronon` con manifest, scene, asset, font e parametri.

Le composizioni C++ basate su callback/lambda possono essere distribuite come
plugin binari. Non possono essere trasformate automaticamente in un formato dati
senza un modello dichiarativo esplicito.

## 4. Blocker release attuali

Chronon3D resta **pre-stabile** finché non esistono prove sullo stesso commit per:

- chiusura TICKET-039 e compilazione del target scopritore;
- chiusura TICKET-038 se riemerge;
- build core;
- test core;
- build/test lean;
- no-content build/test;
- architecture e renderer-boundary gates bloccanti;
- collegamento ed esecuzione dei test scene/camera, incluso TICKET-029;
- consumer SDK esterno che renderizza una composizione reale;
- full-validation build/test;
- documenti sincronizzati con commit, comandi e risultati.

L’assenza di un workflow fallito non equivale a una baseline verde.

## 5. Ordine prodotto consigliato

### Milestone A — Baseline verificata

Chiudere TICKET-039, TICKET-038 se necessario, i residui TICKET-009 e
TICKET-029; quindi rieseguire tutti i profili sullo stesso commit e registrare
gli esiti.

### Milestone B — Text Production V1

Rich text produttivo, word timing, subtitle pipeline, preset e golden.

### Milestone C — Camera Production V1

Percorso compilato unico, orientation/path, framing essenziale, parity e rimozione
progressiva del legacy.

### Milestone D — SDK Product V1

Consumer esterno reale, documentazione pubblica, versionamento, esempi e release
artifact Linux/Windows.

### Milestone E — Interoperabilità

Schema `.chronon`, C ABI e primo binding Python. Gli altri binding devono usare
lo stesso ABI, non reimplementare il runtime.

## 6. Fonti canoniche di dettaglio

- Stato operativo e blocker: [`STATUS.md`](STATUS.md)
- Ordine immediato: [`NEXT_STEPS.md`](NEXT_STEPS.md)
- Prove baseline: [`baselines/main-446a60e2-baseline.md`](baselines/main-446a60e2-baseline.md)
- Roadmap: [`ROADMAP.md`](ROADMAP.md)
- Feature inventory: [`FEATURES.md`](FEATURES.md)
- Testo: [`TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`](TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md)
- Camera: [`CAMERA_FEATURE_MATRIX.md`](CAMERA_FEATURE_MATRIX.md) e [`camera-plan/`](camera-plan/)
- Ticket: [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md)

Quando una fonte di dettaglio contraddice questo documento, prevalgono il codice
e la prova eseguita più recente; il documento in conflitto deve essere aggiornato
nella stessa PR che modifica lo stato.
