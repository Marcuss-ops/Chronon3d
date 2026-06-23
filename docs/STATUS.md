# Chronon3D — Current Status

> **Snapshot funzionale analizzato:** `main@25049b2` — 23 giugno 2026.
>
> **Ultima baseline eseguita e documentata:** `main@446a60e2`.
>
> **HEAD ricontrollato:** `main@844bc7c0` — 23 giugno 2026.
>
> Stato prodotto canonico: [`CURRENT_READINESS.md`](CURRENT_READINESS.md).
> Ordine operativo: [`NEXT_STEPS.md`](NEXT_STEPS.md).
> Prova osservata: [`baselines/main-446a60e2-baseline.md`](baselines/main-446a60e2-baseline.md).

## Stato generale

Chronon3D è avanzato ma **pre-stabile**. Le fondazioni di rendering, testo,
camera e packaging SDK sono presenti; la baseline non è verde perché almeno un
target richiesto non compila.

| Area | Stato | Gap principale |
|---|---|---|
| Baseline repository | 🔴 Rossa | 3 controlli su 4 verdi; `chronon3d_text_preset_visual_tests` fallisce su TICKET-039. |
| Render graph compilato | 🟡 Avanzato | Build/test completi e determinismo scheduler da verificare sullo stesso commit. |
| Software backend | 🟡 Avanzato | Boundary gate verde sulla baseline; integrazione runtime ancora bloccata da TICKET-039. |
| Precomp / execution scope | 🔴 Aperto | Nested execution, lease, child arena e concorrenza richiedono chiusura verificata. |
| Text Production V1 | 🟡 60–65% stimato | Visual target non compilabile; word timing, rich text produttivo, preset e golden da chiudere. |
| Camera Production V1 | 🟡 70–75% stimato | Certificazione test bloccata dalla catena TICKET-039 → TICKET-038 → TICKET-029. |
| SDK C++ | 🟡 80–85% stimato | Package presente; manca consumer esterno che renderizzi una composizione reale. |
| SDK cross-language | 🔵 30–40% stimato | C ABI e formato dichiarativo `.chronon` assenti. |
| Expressions V2 | 🧪 Sperimentale | Non installato, non esportato e non integrato nel percorso produttivo. |
| V3 tile-first | 🔵 Pianificato | Non deve precedere la chiusura della baseline e dei percorsi V1. |

Le percentuali sono stime di copertura funzionale e non vanno usate come stato
di test o claim di release.

## Ultima baseline osservata

La baseline `main@446a60e2` ha registrato:

| Controllo | Esito |
|---|---|
| Software renderer boundary gate | ✅ PASS |
| `linux-full-validation` configure | ✅ PASS |
| `linux-lean-dev` configure | ✅ PASS |
| Build `chronon3d_text_preset_visual_tests` | ❌ FAIL |

Il risultato netto è **3/4 verde ma baseline rossa**. Configure-only non prova
che il codice compili, colleghi o esegua. L’HEAD corrente contiene commit
successivi, ma non è ancora coperto da una nuova full validation sullo stesso
commit.

## Blocker operativi ordinati

### TICKET-039 — blocker globale immediato

`src/runtime/render_engine.cpp` usa il vecchio
`SoftwareRenderer::settings()`, mentre il confine renderer conserva
`render_settings()` come accessor canonico. Il target visuale si arresta qui.

La correzione deve aggiornare il consumer al percorso canonico, non ripristinare
un alias duplicato.

### TICKET-038 — secondo blocker globale noto

Il visual test testuale contiene un problema di lambda capture/deduzione `auto`
che può emergere appena TICKET-039 viene chiuso. Serve build ed esecuzione del
target, non sola compilazione isolata.

### TICKET-009 — residui build/experimental

Restano da verificare i residui effettivi dopo il branch cleanup, incluso il rot
`CompileResult`/variant del percorso sperimentale. Il profilo stabile non deve
dipendere da Expressions V2.

### TICKET-029 — blocker specifico camera

La type visibility di `camera_program_compiler.cpp` impedisce link ed esecuzione
dei test scene/camera compilati. È necessario per certificare i fix camera, ma
non è il primo blocker globale osservato dalla baseline.

## Fondazioni valide da preservare

- `RenderGraph → FrameGraphCompiler → CompiledFrameGraph → GraphExecutor`.
- Executor su `RenderGraph` grezzo ritirati.
- Scheduler esplicito e ID/hash deterministici.
- `RenderRuntime` proprietario dell'infrastruttura engine-lifetime.
- Stato per-sessione per history, cache e camera.
- `AssetResolver` tipizzato e runtime-owned.
- Registrazione esplicita tramite `ExtensionModule` e `ExtensionContext`.
- Un solo registry CMake per build, aggregate archive, install ed export.
- `Chronon3D::SDK` come unico target pubblico documentato.
- Alias legacy non namespaced `Chronon3D_SDK` rimosso.
- `experimental/` escluso dallo SDK stabile.
- Camera canonica: `CameraDescriptor → compile_camera() → CameraProgram`.
- Testo canonico: `TextDocument/TextSpec → layout → animator stack → renderer`.

## Stato testo

### Implementato

- FreeType, HarfBuzz e FriBidi.
- `TextSpec`, `TextRunSpec`, `TextDocument`, span e paragrafi.
- Layout, wrap, overflow, auto-fit, gradienti, stroke e shadow.
- Animator per glifo e selector per glifo/grapheme/parola/riga.
- Sample time sub-frame.
- Text-on-path da consolidare.
- Registry canonico dei preset e sentinel iniziali.

### Ancora aperto

- rendere compilabile ed eseguibile il visual regression target;
- word timing/SRT/JSON;
- styling per parola realmente end-to-end;
- subtitle/highlight/karaoke pipeline;
- Wiggly/Wave/Random selector completi;
- golden multi-resolution e multi-fps per tutti i preset;
- consumer SDK che usa il percorso testuale pubblico;
- variable fonts, ICU, Text 3D, MSDF ed expression selector produttivi.

## Stato camera

Il percorso compilato esiste e comprende descriptor, programma immutabile,
sessione per render job, projection variant, lente fisica, pose/orbit/trajectory,
constraint, shot timeline e transizioni.

### Correzioni recenti presenti nel codice

- preservazione della projection variant nel pose path;
- orientation applicata una sola volta;
- orbit track/dolly nel basis locale della camera;
- `MotionBlurMode` come unica fonte di verità;
- checkpoint/pre-roll per accesso non sequenziale;
- focal X/Y, gate fit, anamorphic/pixel-aspect contract;
- descriptor di default integrato nella composition.

### Stato delle prove

I fix camera non possono essere promossi tutti a verificati finché la catena di
build non supera TICKET-039 e TICKET-038 e i target camera non superano
TICKET-029. La prova deve includere link ed esecuzione sullo stesso commit.

### Gap residui principali

- `OrientAlongPath` completo;
- trajectory/base-state preservation e diagnostica shot timeline;
- framing multi-target/rule-of-thirds/dead-zone/bounds-aware;
- clipping near/far completo;
- DOF più fisico;
- rimozione di `AnimatedCamera2_5D` e rig legacy come authoring primario;
- golden/parity test bloccanti.

## Stato SDK

### Presente

- `libchronon3d_sdk_impl.a` aggregato;
- header pubblici installati;
- package config/version/targets CMake;
- target pubblico `Chronon3D::SDK`;
- dipendenze transitive risolte nel package config;
- consumer esterno basato su `find_package`;
- estensioni tramite module/context.

### Prova ancora insufficiente

Il consumer corrente verifica package, link e un simbolo reale. Per la release
serve un consumer fuori-tree che:

1. installi e trovi Chronon3D;
2. registri un pack esterno;
3. costruisca testo e camera pubblici;
4. renderizzi almeno un frame;
5. scriva un file;
6. verifichi output e diagnostica.

### Interoperabilità mancante

Il C API storico è stato rimosso. Un SDK per Python, C#, Node, Rust o host come
Blender/Unreal richiede un C ABI stabile con handle opachi e un formato dati
versionato per le animazioni. Non creare binding diretti indipendenti che
replichino il runtime.

## Blocker per dichiarare una baseline verde

Servono tutte le prove seguenti sullo stesso commit:

- TICKET-039 chiuso e target scopritore compilato;
- TICKET-038 chiuso se riemerge;
- build core verde;
- test core verdi;
- build lean verde;
- test lean verdi;
- architecture gate verde e bloccante;
- renderer-boundary gate verde e bloccante;
- no-content build/test verde;
- scene/camera test link + run verde, incluso TICKET-029;
- install consumer esterno reale verde;
- full-validation build/test verde;
- documenti aggiornati con commit, comandi e risultati.

L’assenza di workflow o log verificabili non equivale a successo.

## Documenti di riferimento

- [`CURRENT_READINESS.md`](CURRENT_READINESS.md): stato prodotto e stime.
- [`NEXT_STEPS.md`](NEXT_STEPS.md): ordine operativo immediato.
- [`ROADMAP.md`](ROADMAP.md): milestone prodotto.
- [`FEATURES.md`](FEATURES.md): inventario delle feature.
- [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md): ticket e prove puntuali.
- [`CAMERA_FEATURE_MATRIX.md`](CAMERA_FEATURE_MATRIX.md): stato camera.
- [`TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`](TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md): piano testo.

Un’attività è completata soltanto quando codice, test, gate e documenti riportano
lo stesso stato.
