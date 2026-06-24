# Chronon3D — Current Status (canonico)

> **Snapshot:** `main@83a4bf21` — 24 giugno 2026.
>
> Questo è il **documento canonico unificato** che sostituisce `CURRENT_READINESS.md`,
> `STATUS.md` e `NEXT_STEPS.md`. Ogni contraddizione con altri documenti deve essere
> risolta a favore di questo file e del codice eseguito più recente.

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
camera 2.5D cinematica, compositing ed esportazione tramite SDK.

| Obiettivo | Completezza stimata | Stato reale |
|---|---:|---|
| Testi per titoli, kinetic typography e sottotitoli | 70–75% | TXT-11 parser SRT/VTT/JSON completi; TXT-12 highlight/karaoke (active_word_pop, karaoke_fill); 20 preset nel visual harness. Golden hash da catturare. |
| Camera AE-style per motion graphics 2.5D | 80–85% | Percorso compilato completo; OrientAlongPath, DOF fisico, clipping, multi-target framing chiusi. |
| SDK C++ installabile | 80–85% | Target e package CMake presenti; consumer di rendering reale da certificare. |
| SDK cross-language | 30–40% | C ABI e formato dichiarativo `.chronon` da progettare. |

Le stime sono di copertura funzionale, non risultati CI. Non autorizzano claim
"release-ready", "baseline verde" o "parità AE completa".

## Stato aree

| Area | Stato | Gap principale |
|---|---|---|
| Render graph compilato | 🟡 Avanzato | Baseline completa da verificare sul commit candidato. |
| Software backend | 🟡 Avanzato | Gate, core, lean e full-validation devono risultare verdi insieme. |
| Precomp / ExecutionScope | 🟢 Chiuso (PR 6.0–6.7 + WP-7) | Nested execution, lease, child arena, recursion protection, test memoria/race implementati. Legacy `execute()` e arena-defaulting child ctor **rimossi** (WP-7 complete). `execute_with_scope()` è l'unico entrypoint. |
| Text Production V1 | 🟡 70–75% | Parser SRT/VTT/JSON (TXT-11); active_word_pop + karaoke_fill (TXT-12); 20 preset in harness visuale con 320 sentinel. Golden hash e rich text ancora da chiudere. |
| Camera Production V1 | 🟡 80–85% | TICKET-029 risolto; OrientAlongPath, DOF fisico, near/far clipping, multi-target framing chiusi. Migrazione legacy e golden suite da completare. |
| SDK C++ | 🟡 80–85% | Package presente; consumer esterno che renderizzi composizione reale da certificare. |
| SDK cross-language | 🔵 30–40% | C ABI e formato `.chronon` assenti. |
| Expressions V2 | 🧪 Sperimentale | Non installato, non esportato, non nel percorso produttivo. |
| V3 tile-first | 🔵 Pianificato | Non prioritario prima della chiusura baseline e percorsi V1. |

## Fondazioni valide da preservare

- `RenderGraph → FrameGraphCompiler → CompiledFrameGraph → GraphExecutor`
- Executor su `RenderGraph` grezzo ritirati
- `ExecutionScope` con child arena distinte, anti-ricorsione, kMaxScopeDepth=16
- Scheduler esplicito e ID/hash deterministici
- `RenderRuntime` proprietario dell'infrastruttura engine-lifetime
- Stato per-sessione per history, cache e camera
- `AssetResolver` tipizzato e runtime-owned
- Registrazione esplicita tramite `ExtensionModule` e `ExtensionContext`
- Un solo registry CMake per build, aggregate archive, install ed export
- `Chronon3D::SDK` come unico target pubblico documentato
- `experimental/` escluso dallo SDK stabile
- Camera canonica: `CameraDescriptor → compile_camera() → CameraProgram`
- Testo canonico: `TextDocument/TextSpec → layout → animator stack → renderer`

## CI e gate

10 gate pre-merge in `.github/workflows/gates.yml`:
1. `core-build` — build senza text/video/content/diagnostics
2. `sdk-build` — build con Text+Blend2D
3. `public-header-check` — ogni header pubblico compilato standalone
4. `install-consumer-check` — `cmake --install` + `find_package` esterno
5. `architecture-check` — regole statiche + skip ticket + boundary grep + gitignored dirs
6. `architecture-selftest` — 14 assertion sul boundary script
7. `audit-software-renderer` — inventario SoftwareRenderer
8. `filename-drift` — file citati devono esistere su disco
9. `profile-measurement` — metriche per profilo
10. `install-consumer-script` — test SDK end-to-end

Gates 12/13/14 (CMake registry, vcpkg parity, SDK surface) sono **bloccanti** (promossi da advisory).

## Testo e kinetic typography

### Presente
- FreeType, HarfBuzz e FriBidi
- Shaping, bidirezionalità, layout, wrap, overflow, auto-fit
- `TextSpec`, `TextRunSpec`, `TextDocument`, span e paragrafi
- Animator per glifo e selector per glifo/grapheme/parola/riga
- Sample time sub-frame, text-on-path, registry canonico preset, sentinel iniziali

### Da chiudere per Text Production V1
1. Rich text e styling per parola end-to-end
2. ✅ Word timing / SRT / JSON — parser SRT/VTT/JSON in `timed_text_parser.cpp` (TXT-11)
3. ✅ Highlight, karaoke — `active_word_pop` + `karaoke_fill` preset in registry (TXT-12)
4. Wiggly/Wave/Random selector completi
5. 🟡 20 preset generali in harness (160 sentinel ink pass); golden hash da catturare
6. Consumer SDK che usa percorso testuale pubblico
7. Determismo seriale/parallelo verificato

### Pianificato (non M1)
- Variable fonts, ICU opzionale, Text 3D, MSDF, expression selector produttivo, color emoji

## Camera

### Percorso canonico
```
CameraDescriptor → compile_camera() → CameraProgram → CameraSession → Camera2_5D → projection contract → renderer
```

### Presente
- Descriptor variant-based, programma compilato immutabile, sessione per render job
- Zoom, FOV, Physical Lens, Pose Tracks, Orbit Motion, Trajectory Motion
- Look-at point/layer, `OrientAlongPath`, idle oscillation, handheld noise
- Constraint tipizzati, valutazione sub-frame, motion blur temporale
- Shot timeline con transizioni, fingerprint deterministico, checkpoint/pre-roll

### Gap principali
- Rimuovere `AnimatedCamera2_5D` e rig legacy dopo adapter parity
- Diagnostica shot timeline e golden suite camera
- Consumer SDK reale con rendering end-to-end (camera + text + compositing)
- Golden camera e test parity per il percorso compilato

### Camera Production V1 gate
1. ✅ Nuove composizioni usano solo `CameraDescriptor` / `CameraProgram`
2. ✅ `CameraSession` posseduta dal render job; nessuna compilazione per frame
3. ✅ Orientation e projection hanno un solo contratto matematico
4. ✅ Test camera compilati collegano ed eseguono in CI (`chronon3d_scene_tests`)
5. ✅ Consumer SDK esterno può costruire e usare camera compilata (install_consumer/main.cpp)

Feature completate in questa sessione:
- ✅ `OrientAlongPath` — tangent via POI, keep-horizon, 3 test (TICKET-023)
- ✅ DOF fisico — CoC formula, near/far asimmetrici, 5 test in test_dof.cpp
- ✅ Near/far clipping — Sutherland-Hodgman + far-plane in camera_projection_resolver
- ✅ Multi-target framing — centroid pesato, rule-of-thirds, dead-zone/hysteresis, dolly
- ✅ `[[deprecated]]` su `AnimatedCamera2_5D` e `fit_camera_to_layers()`
- ✅ Adapter `AnimatedCamera2_5D → CameraDescriptor`
- ✅ WP-8 — percorsi `RenderGraph&` grezzi ritirati, `FrameGraphCompiler` single source of truth

## SDK

### Presente
- `libchronon3d_sdk_impl.a` aggregato, header pubblici installati
- `Chronon3DConfig.cmake`, `Chronon3DTargets.cmake`, target `Chronon3D::SDK`
- Consumer esterno basato su `find_package`, estensioni via `ExtensionModule`

### Da certificare
Consumer fuori-tree che:
1. Include solo header pubblici, linka solo `Chronon3D::SDK`
2. Registra `ExtensionModule` esterno
3. Costruisce testo e camera compilata
4. Crea runtime/backend, renderizza un frame, scrive PNG
5. Verifica dimensioni, hash o marker diagnostico

## Blocker per baseline verde

Servono tutte le prove seguenti sullo stesso commit:
- build core, test core, build/test lean, no-content build/test
- architecture gate e renderer-boundary gate verdi e bloccanti
- scene/camera test link + run verdi
- install consumer esterno reale verde
- full-validation build/test verde
- documenti aggiornati con commit, comandi e risultati

## Priorità immediate

### P0 — Ripristinare compilabilità baseline
1. ✅ TICKET-039: `SoftwareRenderer::settings()` → `render_settings()` in `render_engine.cpp` — già risolto
2. ✅ TICKET-038: lambda capture/deduzione `auto` in `test_text_preset_visual.cpp` — già risolto
3. TICKET-009 residuo: orphan cleanup, CompileResult rot, experimental isolation

### P1 — Verificare test camera
1. Rieseguire `chronon3d_scene_tests` e target camera compilati (TICKET-029 risolto, link ok)
2. Verificare projection variant, orientation, OrbitMotion, motion blur, checkpoint, focal X/Y

### P2 — Nuova baseline sullo stesso commit
Eseguire core, lean, no-content, architecture, renderer, scene/camera, install consumer, full-validation.

### P3 — Consumer SDK reale (fuori-tree, rendering end-to-end)
### P4 — Text Production V1
### P5 — Camera Production V1
### P6 — Export animazioni (schema `.chronon` + C ABI + binding Python)

## Roadmap milestone

| Milestone | Stato |
|---|---|
| M0 — Baseline verificata | 🔴 In corso |
| M1 — Text Production V1 | 🔵 Pianificato |
| M2 — Camera Production V1 | 🔵 Pianificato |
| M3 — SDK Product V1 | 🔵 Pianificato |
| M4 — Pacchetti animazione e interoperabilità | 🔵 Pianificato |
| M5 — Global text ed effetti premium | 🔵 Pianificato |
| M6 — V3 tile-first | 🔵 Pianificato |

Vedi [`ROADMAP.md`](ROADMAP.md) per i dettagli di ogni milestone.

## Ticket attivi

I ticket risolti (🟢 Done) sono archiviati in [`archives/resolved-tickets.md`](archives/resolved-tickets.md).
I ticket attivi (🟡/🔵) sono in [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md).
I nuovi ticket vanno creati come GitHub Issues.

## Documenti di riferimento

| Documento | Ruolo |
|---|---|
| [`README.md`](../README.md) | Entry point, quick start, architettura |
| [`CURRENT_STATUS.md`](CURRENT_STATUS.md) | **questo file** — stato canonico unificato |
| [`ROADMAP.md`](ROADMAP.md) | Milestone prodotto |
| [`FEATURES.md`](FEATURES.md) | Inventario feature |
| [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) | Ticket attivi (🟡/🔵) |
| [`archives/resolved-tickets.md`](archives/resolved-tickets.md) | Ticket risolti (🟢) |
| [`TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`](TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md) | Piano testo |
| [`CAMERA_FEATURE_MATRIX.md`](CAMERA_FEATURE_MATRIX.md) | Matrice camera |
| [`ARCHITECTURE_EVOLUTION_PLAN.md`](ARCHITECTURE_EVOLUTION_PLAN.md) | Evoluzione architettura |
| [`adr/`](adr/) | Architecture Decision Records |

> **Regola:** quando una fonte di dettaglio contraddice questo snapshot, prevale il codice
> e la prova eseguita più recente; il documento in conflitto deve essere aggiornato.
