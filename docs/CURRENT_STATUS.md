# Chronon3D — Current Status

> **Snapshot:** `main@24388800` — 25 giugno 2026. Linux-only.
>
> Ultima baseline macchina-verificata: `main@446a60e2` (3/4 ✅, registrata in [`baselines/main-446a60e2-baseline.md`](baselines/main-446a60e2-baseline.md)).
> La baseline sull'HEAD corrente non è ancora stata eseguita.
>
> Questo è l'unico documento canonico per lo stato presente del progetto.
> Per il futuro vedi [`ROADMAP.md`](ROADMAP.md).
> Per i requisiti di release vedi [`RELEASE_GATE.md`](RELEASE_GATE.md).
> Per i ticket vedi [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md).

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

| Area | Stato | Completezza stimata |
|---|---|---|
| Render graph compilato | 🟡 Avanzato | Baseline e determinismo scheduler da verificare sullo stesso commit. |
| Software backend | 🟡 Avanzato | Confine rifattorizzato (Agent-1 #49); gate e full validation da osservare insieme. |
| Precomp / execution scope | 🔴 Aperto | Nested execution, lease, child arena e concorrenza da chiudere. |
| Text Production V1 | 🟡 | 60–65% — word timing, rich text produttivo, preset e golden da chiudere. |
| Camera Production V1 | 🟡 | 70–75% — TICKET-029 risolto (link sbloccato); migrazione legacy e feature path/framing/ottica aperte. |
| SDK C++ installabile | 🟡 | 80–85% — consumer di rendering reale con testo+camera→PNG in fase di certificazione. |
| SDK cross-language | 🔵 | 30–40% — C ABI e formato `.chronon` da progettare. |
| Expressions V2 | 🧪 Sperimentale | OFF di default, non installato, non in SDK. |
| V3 tile-first | 🔵 Pianificato | Non prima della chiusura baseline e percorsi V1. |

Le percentuali sono stime di copertura funzionale, non risultati CI.

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

## Documenti correlati

- [`ROADMAP.md`](ROADMAP.md): milestone prodotto.
- [`RELEASE_GATE.md`](RELEASE_GATE.md): criteri di release.
- [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md): ticket e difetti tracciati.
- [`FEATURES.md`](FEATURES.md): inventario delle feature.
- [`CAMERA_FEATURE_MATRIX.md`](CAMERA_FEATURE_MATRIX.md): dettaglio camera.
- [`TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`](TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md): piano testo.
- [`baselines/main-446a60e2-baseline.md`](baselines/main-446a60e2-baseline.md): ultima baseline macchina-verificata.

Documenti archiviati: [`ARCHIVE/`](ARCHIVE/).
