# Chronon3D — Current Status

> **Snapshot:** `main@41c3d5a6` — 2026-06-29. Linux-only.
>
> Ultima baseline macchina-verificata: `main@446a60e2` (3/4 ✅, registrata in [`baselines/main-446a60e2-baseline.md`](baselines/main-446a60e2-baseline.md)).
> **Baseline sull'HEAD corrente: NON CERTIFICATA** (nessun run macchina-verificato di tutti gli 11 gate registrato dopo `446a60e2`).
>
> Questo è l'unico documento canonico per lo stato presente del progetto.
> Per il futuro vedi [`ROADMAP.md`](ROADMAP.md).
> Per i requisiti di release vedi [`RELEASE_GATE.md`](RELEASE_GATE.md).
> Per i ticket vedi [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md).

## Preset CMake attivi

Configurazione osservata in [`CMakePresets.json`](../CMakePresets.json) al commit corrente:

- `linux-dev` — preset di sviluppo locale (default).
- `linux-asan` — instrumentazione ASAN + UBSAN.
- `linux-release-validation` — preset consumato da `.github/workflows/gates-full-validation.yml`.
- `linux-experimental-validation` — preset per la catena `experimental/expressions/` (opt-in).
- `linux-ci` — preset consumato da `tools/install_consumer_test.sh`.
- Varianti di profiling (`true`/`false`) — vedi file per la lista completa.

Build/test preset default: `jobs: 16`.

## CI gate osservati (lettura `.github/workflows/*.yml`)

### Gate bloccanti (failure = PR bloccato)

- `.github/workflows/gates.yml` — 6 cheap gates (`core-build`, `sdk-build`, `public-header-check`, `install+consumer`, arch-boundary, sw-renderer-boundary). Trigger: push + pull_request su `main`.
- `.github/workflows/ci.yml` — matrix Release + Debug + lean + scene/camera. Trigger: push + PR su `main`.
- `.github/workflows/gates-full-validation.yml` — full-validation con `paths:` filter (SDK/src/cmake). Trigger: push + PR.
- `.github/workflows/validation.yml` — validate secondario. Trigger: push + PR su `main`.

### Gate advisory (osservazione; non bloccante)

- `.github/workflows/visual_ci.yml` — visual regression con `continue-on-error: true` su più job (`diff-engine-test`, `visual-regression-test`).
- `.github/workflows/nightly-visual.yml` — schedule cron nightly + `workflow_dispatch`.
- `.github/workflows/profile-envelope.yml` — schedule weekly (lunedì 04:00 UTC) + dispatch.
- Step `renderer-boundary-observation` dentro `.github/workflows/gates.yml` — osservazione esplicita non bloccante (annotata come tale nel workflow) del confine renderer/software.

## Packaging SDK

[`tools/install_consumer_test.sh`](../tools/install_consumer_test.sh) — script end-to-end (7339 bytes, eseguibile). Configura + build + installa SDK in prefix temporaneo + richiede preset `linux-ci` + `cmake >= 3.25`.

- **Stato corrente**: `NOT RUN` per questo run di Step 5 (richiede macchina-verifica dedicata sul commit candidato). Syntax check (`bash -n tools/install_consumer_test.sh`) deferred a macchina-verifica.
- **Cosa serve per elevare a `PASS`**: run macchina-verificato di `tools/install_consumer_test.sh` su HEAD + exit `0` + log di configure + build su preset `linux-ci`.

## Install consumer

[`tests/install_consumer/`](../tests/install_consumer/) progetto consumer esterno che linka `Chronon3D::SDK` contro il prefix installato da `tools/install_consumer_test.sh`. Coperto dallo stesso script (vedi `## Packaging SDK` sopra).

- **Stato corrente**: `NOT RUN` (stessa giustificazione: macchina-verifica richiesta su commit candidato).
- **Cosa serve per elevare a `PASS`**: log del build del consumer in step separato + link riuscito + render di una composizione reale (testo + camera → PNG) fuori-tree.

## Come leggere gli stati

| Stato | Significato |
|---|---|
| ✅ Verificato | Implementato e coperto da una prova eseguibile osservata. |
| 🟡 Implementato/parziale | Codice presente, ma con limiti, migrazione incompleta o test bloccati. |
| 🔴 Bloccante | Impedisce di dichiarare stabile il sottosistema o la release. |
| 🔵 Pianificato | Design o roadmap presenti, implementazione non completata. |

## Stato generale

Chronon3D è un motore C++20 headless, CPU-first e code-first per motion graphics.
È avanzato ma **pre-stabile**.

| Area | Stato (osservato) | Note |
|---|---|---|
| Render graph compilato | 🟡 NOT RUN | Baseline e determinismo scheduler da verificare sullo stesso commit. |
| Software backend | 🟡 NOT RUN | Confine rifattorizzato (Agent-1 #49); gate e full validation da osservare insieme. |
| Precomp / execution scope | 🔴 NOT RUN | Nested execution, lease, child arena e concorrenza da chiudere. |
| Text Production V1 | 🟡 NOT RUN | word timing, rich text produttivo, preset e golden da chiudere. |
| Camera Production V1 | 🟡 NOT RUN | TICKET-029 risolto (link sbloccato); migrazione legacy e feature path/framing/ottica aperte. |
| SDK C++ installabile | 🟡 NOT RUN | consumer di rendering reale con testo+camera→PNG in fase di certificazione. |
| SDK cross-language | 🔵 NOT RUN | C ABI e formato `.chronon` da progettare. |
| Expressions V2 | 🧪 Sperimentale | OFF di default, non installato, non in SDK. |
| V3 tile-first | 🔵 Pianificato | Non prima della chiusura baseline e percorsi V1. |

Lo stato è osservabile (`PASS` / `FAIL` / `PARTIAL` / `NOT RUN`): mai una
stima di copertura. Un valore `PASS` senza output simultaneo sul commit
corrente è un falso positivo e va corretto.

## Fondazioni da preservare

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

## Testo

### Presente

- FreeType, HarfBuzz e FriBidi.
- `TextSpec`, `TextRunSpec`, `TextDocument`, span e paragrafi.
- Layout, wrap, overflow, auto-fit, gradienti, stroke e shadow.
- Animator per glifo e selector per glifo/grapheme/parola/riga.
- Sample time sub-frame.
- Text-on-path da consolidare.
- Registry canonico dei preset e sentinel iniziali.

### Parziale o aperto

- Word timing/SRT/JSON.
- Styling per parola end-to-end.
- Subtitle/highlight/karaoke pipeline.
- Wiggly/Wave/Random selector completi.
- Golden multi-resolution e multi-fps per tutti i preset.
- Consumer SDK che usa il percorso testuale pubblico.

### Pianificato (non prima di Text Production V1)

- Variable fonts, ICU, Text 3D, MSDF, expression selector produttivi.

## Camera

### Percorso canonico

```text
CameraDescriptor
    → compile_camera()
    → CameraProgram
    → CameraProgram::evaluate(CameraEvalContext, CameraSession)
    → Camera2_5D
    → CameraProjectionSource / projection contract
    → renderer
```

Sono presenti: descriptor variant-based, programma compilato immutabile, sessione camera per render job, Zoom/FOV/Physical Lens, Pose/Orbit/Trajectory, constraint, shot timeline e transizioni, checkpoint/pre-roll, fingerprint deterministico.

### Gap principali

- Completare `OrientAlongPath` con tangent provider, look-ahead, keep-horizon e banking.
- Chiudere trajectory/base-state preservation e diagnostica shot timeline.
- Rimuovere i percorsi legacy dopo adapter e regression parity.
- Framing solver con multi-target, rule-of-thirds, dead-zone.
- DOF con Circle of Confusion e bokeh più fisico.
- Golden camera e parity test bloccanti.

## SDK

### Presente

- `libchronon3d_sdk_impl.a` aggregato.
- Header pubblici installati.
- Package config/version/targets CMake.
- Target pubblico `Chronon3D::SDK`.
- Consumer esterno basato su `find_package`.

### Da completare

- Consumer fuori-tree che renderizza una composizione reale (testo + camera → PNG).
- C ABI stabile con handle opachi.
- Formato dichiarativo versionato `.chronon`.

## Baseline osservata (`main@446a60e2`, 2026-06-23)

L'ultima baseline macchina-verificata ha prodotto:

| # | Controllo | Esito | Note |
|---|---|---|---|
| BG-1 | Software renderer boundary gate | ✅ PASS | `tools/check_software_renderer_boundary.sh` exit 0 |
| BG-2 | `linux-full-validation` configure | ✅ PASS | Configure completato clean |
| BG-3 | `linux-lean-dev` configure | ✅ PASS | Configure completato clean |
| BG-4 | Build `chronon3d_text_preset_visual_tests` | ❌ FAIL | TICKET-039: `SoftwareRenderer::settings()` regression |

**Verdetto**: 3/4 ✅ — baseline non verde (registrata al commit `446a60e2`). I blocker noti sulla
compilazione del testo (TICKET-038, TICKET-039) sono chiusi nel grafo dei commit dell'HEAD corrente,
ma una nuova baseline macchina-verificata sull'HEAD non è ancora stata registrata.

### Progressi dalla baseline

- **🟢 TICKET-039** — `SoftwareRenderer::settings()` access regression risolta (commit `68c3e0f0`).
  TXT-00 build/link verde; `chronon3d_text_preset_visual_tests` cablato nel render aggregator (`44def204`).
- **🟢 TICKET-038** — lambda capture / auto deduction rot in `tests/text/test_text_preset_visual.cpp`
  risolta (commit `91debc36`).
- **🟢 TICKET-029** — link di `chronon3d_scene_tests` sbloccato (commit `fb1b7e97`). I fix camera atterrati
  sul codice possono ora procedere verso la verifica macchina.
- **🟢 TICKET-040** — Taskflow completamente rimosso (non più required, rot chiusa).
- **🟢 TICKET-006** — fix statico per il linkage `chronon3d_backend_text` (gen-exp guard in
  `tests/renderer_tests.cmake` + if-endif in `tests/scene_tests.cmake`). Verifica macchina deferred.
- **🟢 Content restructuring** — riorganizzazione `content/` in showcases/examples/experimental, staging completato.
- **🟢 Docs** — archivio `stabilization-plan/` e `refactor-roadmap/` in `ARCHIVE/`, single source of truth
  (`CURRENT_STATUS.md`, `RELEASE_GATE.md`), nuovo `AGENTS.md`.
- **🟢 Windows/MSVC** — supporto rimosso, progetto Linux-only.

## Blocker per baseline verde

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

L'assenza di workflow fallito non equivale a una baseline verde.

> **Certificazione corrente (2026-06-29, `4586d816`)**: NON CERTIFICATA.
> La regola di sopra si applica: serve un run macchina-verificato dei 11 gate sul commit candidato
> per promuovere la baseline a `CERTIFICATA @ <SHA>`.

## Documenti correlati

- [`ROADMAP.md`](ROADMAP.md): milestone prodotto.
- [`RELEASE_GATE.md`](RELEASE_GATE.md): criteri di release.
- [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md): ticket e difetti tracciati.
- [`FEATURES.md`](FEATURES.md): inventario delle feature.
- [`CAMERA_FEATURE_MATRIX.md`](CAMERA_FEATURE_MATRIX.md): dettaglio camera.
- [`TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`](TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md): piano testo.
- [`baselines/main-446a60e2-baseline.md`](baselines/main-446a60e2-baseline.md): ultima baseline macchina-verificata.

Documenti archiviati: [`ARCHIVE/`](ARCHIVE/).
