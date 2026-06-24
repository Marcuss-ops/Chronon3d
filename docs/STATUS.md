# Chronon3D — Current Status

> **Snapshot:** `main@25c6b5cd` — 24 giugno 2026.
> Ultimo lavoro audit-driven: [`baselines/main-25c6b5cd-render-aggregator-deps-fixed.md`](baselines/main-25c6b5cd-render-aggregator-deps-fixed.md) (Blocco 1, wiring verificato).
>
> Stato prodotto canonico: [`CURRENT_READINESS.md`](CURRENT_READINESS.md).
> Questo documento descrive blocker e prove operative. Non certifica una baseline
> verde finché tutti i gate richiesti non sono osservati sullo stesso commit.
>
> **Ultimo aggiornamento agenti:** Agent 1 (renderer/backend) **COMPLETED**,
> Agent 2 (CMake/SDK/baseline) **COMPLETED** — merged in `ccabb574`.
> TXT-00 build/link: **GREEN**; test esecuzione bloccato da FontEngine mancante.

## Stato generale

Chronon3D è avanzato ma **pre-stabile**. Le fondazioni di rendering, testo,
camera e packaging SDK sono presenti; il lavoro immediato è trasformarle in un
percorso unico, verificato e consumabile fuori dalla source tree.

| Area | Stato | Gap principale |
|---|---|---|
| Render graph compilato | 🟡 Avanzato | Baseline completa e determinismo scheduler da verificare sul commit candidato. |
| Software backend | 🟢 Stabile | Confine rifattorizzato (Agent 1); gate, core, lean e full validation da certificare insieme. |
| CMake/SDK registry | 🟢 Stabile | Registry centralizzato, aggregate archive, install consumer funzionante (Agent 2). |
| Precomp / execution scope | 🔴 Aperto | Nested execution, lease, child arena e concorrenza richiedono chiusura verificata. |
| Text Production V1 | 🟡 60–65% stimato | TXT-00 build/link verde; test bloccato da FontEngine runtime. Word timing, rich text, preset e visual regression ancora aperti. |
| Camera Production V1 | 🟡 70–75% stimato | scene_tests link sbloccato via PR 2 (`fb1b7e97`, TICKET-029 risolto); migrazione legacy e feature di path/framing/ottica ancora aperte. |
| SDK C++ | 🟡 80–85% stimato | Package presente; manca consumer esterno che renderizzi una composizione reale. |
| SDK cross-language | 🔵 30–40% stimato | C ABI e formato dichiarativo `.chronon` assenti. |
| Expressions V2 | 🧪 Sperimentale | Non installato, non esportato e non integrato nel percorso produttivo. |
| V3 tile-first | 🔵 Pianificato | Non deve precedere la chiusura della baseline e dei percorsi V1. |

Le percentuali sono stime di copertura funzionale e non vanno usate come stato
di test o claim di release.

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

- **TXT-00:** build/link rc=0, test esecuzione bloccato da FontEngine non disponibile nell'ambiente ctest;
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

Il blocker TICKET-029 (che bloccava il link step di `chronon3d_scene_tests`) è
stato chiuso upstream via PR 2 (`fb1b7e97`): il link step ora collega e i fix
camera che compilano possono procedere verso la fase di verifica macchina
end-to-end. TICKET-029 è ora `🟢 Resolved` in `docs/FOLLOWUP_TICKETS.md` (per il
dettaglio della 6-fix bundle che include anche `8547b2e9 fix(scene+tests): TICKET-029
align test doctest pattern + camera compiler includes`). I ticket camera devono
passare da “code-fix landed” a “verified” soltanto dopo che i test siano
effettivamente eseguiti e `RC=0` sul commit candidato — la verifica end-to-end
resta il prerequisito per dichiarare una baseline verde anche con il blocker
TICKET-029 risolto.

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

- build core verde;
- test core verdi;
- build lean verde;
- test lean verdi;
- architecture gate verde e bloccante;
- renderer-boundary gate verde e bloccante;
- no-content build/test verde;
- scene/camera test link + run verde;
- install consumer esterno reale verde;
- full-validation build/test verde;
- documenti aggiornati con commit, comandi e risultati.

L’assenza di workflow o log verificabili non equivale a successo.

Stato attuale dei blocker principali:
- **TICKET-039** (SoftwareRenderer::settings): **RISOLTO** dal fix Agent 2.
- **TXT-00 linker strutturale** (~30 simboli): **RISOLTO** dal fix Agent 2; build/link rc=0.
- **TXT-00 test esecuzione**: bloccato da FontEngine non disponibile in ctest.
- **FontEngine runtime**: dipendenza di ambiente da risolvere per chiudere TXT-00.

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