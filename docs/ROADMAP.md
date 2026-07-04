# Chronon3D — Active Roadmap

La roadmap è organizzata per milestone prodotto. Non avviare una milestone
successiva per nascondere blocker della precedente.

Stato corrente: [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md). Criteri di release: [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md).

> **Snapshot macchina-verificato (2026-07-04):** `main@2895bd88` — post-FF-pull origin/main; **§3.1 Esecuzione del piano commitato (commit `a8414705`)** at 2026-07-04: TICKET-A3-CACHE-LEASE (CameraSessionLease rollback real) **CHIUSO**.  **8/11 atomic PASS + 1 FAIL (g4) + 1 PASS* (g8) + 1 NOT RUN (g10)** osservato post-§3.1 atomic audit run: g1, g2, g3, g5, g6, g7, g9, g11 tutti `exit 0`; **g4 FAIL** per abs-path leak in `docs/tickets/TICKET-GATE-10-PHASE-4-BLACK-FU4.md:75` (`cd /home/pierone/Pyt/Chronon3d`) — propagazione TICKET-GATE-4-LEAK cluster (vedi `FOLLOWUP_TICKETS.md`); **g8 PASS*** (warn-mode; 89 drift findings, ↓ da 170 post-M1.5#6 tightening pass); **g10 NOT RUN** (heavy full-build requirement; carry-over rot install_consumer_test.sh, TICKET-GATE-10-PHASE-4-FIX parziale).  **8 atomic PASS ≠ 11/11: feature freeze ANCORA ATTIVO** (revoca richiede 11/11 PASS macchina-verificato sullo stesso commit, vedi `AGENTS.md` §Feature Freeze + `CURRENT_STATUS.md` §Certificazione corrente).  M2 Camera V1 ADR-013 ✅ documented+accepted; A3 cluster source-code **1/8 chiuso** (TICKET-A3-CACHE-LEASE).  M3 SDK V1 **NOT green** (gate #10 carry-over rot).  Storico baseline `>9/11` macchina-verificata: [`docs/baselines/main-aaf70032-baseline.md`](docs/baselines/main-aaf70032-baseline.md) (10/11 pre-fix log).  Ultima 11/11 macchina-verificata osservata: `main@1078ab46` (post-Fase A+B2+B3, 2026-07-04; conservata in `CURRENT_STATUS.md`, table "Gate audit snapshot — `main@1078ab46`").  Per i dettagli di ogni milestone vedi le sezioni M0–M6 sotto.

### Fase A1–A2 — Migrazione header interni + unificazione backend (2026-07-03)

- **A1** (`1bd92daf`): Rimossi 4 symlink legacy (`render_session.hpp`, `session_services.hpp`, `scene_hasher.hpp`, `scene_program_store.hpp`) → accessibili solo via path `internal/`. Creato `tools/check_header_standalone_compile.sh` — compila standalone ogni header pubblico del manifest.
- **A2** (`8fdb0de8`): Unificata costruzione backend — `render_engine.cpp` e `cli_render_utils.cpp` ora usano entrambi `attach_software_backend()` come factory canonica. Rimosse ~65 linee duplicate di SoftwareBackendServices + make_software_backend + ProcessorSourceExtras.

### Fase B2+B3 — Deprecation global state (2026-07-03)

- **B2** (`2d3cc2dc`): `process_wide_assets_root()` / `process_wide_resolver()` marcati `[[deprecated]]` in `render_runtime.hpp`. Migration path: `RenderRuntime::resolver()` per-engine. Eliminazione effettiva bloccata da ~24 call sites (CLI, test, content modules) → Phase C.
- **B3** (pre-esistente): `shared_text_layout_cache()` marcato con deprecation banner in `text_run.hpp` / `text_run.cpp`. Migration path: `RenderSession::layout_cache` per-session. Eliminazione bloccata da ~35 call sites → Phase C.

### Fase C2 — Factory unificata RenderRuntime (2026-07-03)

- **C2** (in corso): `RenderRuntime::create(RuntimeConfig)` → `Result<RenderRuntime, RuntimeBuildError>`. `RuntimeConfig` wrappa `Config` + `optional<assets_root>`. `attach_backend()` rafforzato `[[deprecated]]` con suppression nei bridge interni.

### Fase C — Completamento doc (2026-07-02)

- **C2** (`d8a228f7`): Costruttore `RenderEngine::Impl` unificato (`optional<path>`).
- **C3** (`3b4dbdc6`): Pipeline canonica documentata (Definition→Compiler→Evaluator→GraphCompiler→Executor).
- **C1** (doc-only): `CompiledTextRun` pianificato in `text_run.hpp`; blocked by feature freeze.

## M0 — Baseline verificata

### Obiettivo

Produrre un commit candidato sul quale build, test, gate, consumer e documenti
riportano lo stesso stato.

### Lavori

1. Completato: link/run di `chronon3d_scene_tests` (TICKET-029).
2. Rieseguire i regression test dei fix camera recenti.
3. Completato: `SoftwareRenderer::settings()` regression (TICKET-039).
4. Completato: lambda/auto compile rot in text preset visual (TICKET-038).
5. Chiudere i gap Precomp, execution scope e identity/session che bloccano la baseline.
   - P1 #3 (parziale): `RenderSession::layout_cache` sostituisce il singleton `shared_text_layout_cache()`. Migrazione callsite post-baseline.
6. Eseguire core, lean, no-content e full-validation sullo stesso commit.
7. Rendere architecture e renderer-boundary gate realmente bloccanti.
8. Eseguire install consumer sullo stesso commit.
9. Registrare comandi ed esiti osservati in `docs/baselines/`.

### Gate di uscita

- nessun test richiesto skipped per nascondere un errore;
- nessun gate con `continue-on-error` sul percorso candidato;
- tutti i profili richiesti verdi sullo stesso commit;
- documenti sincronizzati.

## M1 — Text Production V1

### Obiettivo

Generare titoli, citazioni, keyword highlight e sottotitoli animati affidabili
per pipeline video automatizzate.

### Lavori

1. Completare rich text e styling per parola end-to-end.
2. Introdurre timed text/SRT/JSON e word timing.
3. Implementare highlight, karaoke, word pop e subtitle layout policies.
4. Completare Wiggly/Wave/Random selector richiesti dai preset.
5. Stabilizzare almeno 20 preset generali e 8 subtitle.
6. Aggiungere golden 16:9/9:16, testo corto/lungo e più timestamp.
7. Verificare 24/30/60 fps e determinismo seriale/parallelo.
8. Esporre esempi pubblici tramite `Chronon3D::SDK`.

### M1.5 — Text Pipeline Decomposition Backlog (post-feature-freeze, PLANNED)

> **Origine:** audit di leggibilità/maintainability del 2026-07-03 sull'orchestrator
> testo. NON avviabile fino a `11/11 PASS` sullo stesso commit (feature freeze
> attivo, vedi [`AGENTS.md`](../AGENTS.md)). Il backlog è codificato qui, in un
> canonical, per evitare documenti paralleli vietati da
> [`DOCUMENTATION_GOVERNANCE.md`](DOCUMENTATION_GOVERNANCE.md).
>
> **Regola di stato osservabile:** PASS / FAIL / PARTIAL / NOT RUN / BLOCKED /
> PLANNED. Tutti i 15 item sono attualmente `PLANNED` (nessun codice toccato
> durante il freeze). Avvio rigido: prima mini-sequenza `1 → 2 → 3 → 4`.

| #  | File / area target                                              | Pri    | Stato    | Vincolo architetturale                                                                                  |
|----|------------------------------------------------------------------|--------|----------|----------------------------------------------------------------------------------------------------------|
| 1  | `src/render_graph/nodes/TextRunNode.cpp`                        | P0     | PARTIAL  | `execute()` → orchestratore breve; canale `Result<OwnedFB, NodeExecutionError>`; nessun falso successo. Refactor committato: 3 helpers in `src/render_graph/nodes/text_run/` (execution / transform / diagnostics). `chronon3d_graph_nodes` lib compile clean (LIB_EXIT=0). Return-channel contract locked da `tests/render_graph/nodes/test_text_run_node_return_channel.cpp`. `chronon3d_render_graph_tests` target ha blockers pre-esistenti (`camera_change_policy.cpp::Camera2_5D::projection_mode`) fuori scope M1.5#1 — M1.5#1 PARTIAL ⇄ full baseline verde ancora in attesa del gate #10. |
| 2  | `src/text/text_run_driver.cpp`                                  | P0     | PLANNED  | Refactor committato: orchestratore breve + 3 helpers in `src/text/driver/` (text_state_sampler / text_font_state / text_layout_rebuild). Nuova struct pubblica `EffectiveTextState{text, FontLayoutIdentity, TextDirection, Bcp47LanguageTag, TextShapingFeatures}` in `include/chronon3d/text/text_run_driver.hpp`; operator== confronta tutti i 5 campi in declaration order. Il fast-path in `apply_active_state_to_text_run_shape` partecipa ora a TUTTE le dimensioni di `TextLayoutCacheKey::digest()` (non più solo `source_text + FontLayoutIdentity` come pre-M1.5#2 / P0-2). `compute_effective_font` preserva P0-2 (font overrides senza `font_path.empty()` gate). `chronon3d_text_core` lib compile clean (`LIB_EXIT=0`); nuovo test `tests/text/test_effective_text_state.cpp` locka semantica 5-campi + crossfade lifecycle; `tests/text/test_text_run_driver.cpp` invariato. Nessun nuovo singleton/registry/cache (cache flows via `TextRunShape::cache` o `TextLayoutCache*` esplicito); `process_wide_*` e `shared_text_layout_cache()` confermati rimossi (Fase B2+B3). Test target `chronon3d_core_tests` ancora bloccato da `TICKET-camera-policy-pre-existing` (`Camera2_5D::projection_mode`) — fuori scope M1.5#2. Stesso blocker pre-esistente di M1.5#1 — separato come ticket indipendente (one-commit-per-responsibility). |
| 3  | `include/chronon3d/text/text_run.hpp`                           | P0/P1  | PLANNED  | Sub-header `layout/shape/cache/hash/identity`; singleton rimosso da header pubblico.                    |
| 4  | `src/text/text_run.cpp`                                         | P0     | PLANNED  | `FontLayoutIdentity` usato in cache-key + layout-hash + shaping-hash + keyframe + prewarm + font-spans. |
| 5  | `src/text/text_run_builder.cpp`                                 | P1     | PLANNED  | Pipeline canonica: validate → resolve → shape → failure-policy → compose → font-spans → cache-store.    |
| 6  | `src/backends/software/processors/text_run/text_run_processor.cpp` | P1 | PLANNED  | Scratch allocator / `TextRenderResources`; niente vettori temporanei allocati per draw.                 |
| 7  | `include/chronon3d/backends/text/text_render_resources.hpp`     | P1     | PLANNED  | Aggregatore leggero; cache/font/atlas/scratch in sub-header separati.                                    |
| 8  | `src/text/text_resolver.cpp`                                    | P1     | PLANNED  | Unico entry `ResolvedTextTree`; nessun secondo resolver in backend o builder.                            |
| 9  | `src/backends/software/processors/text/software_text_processor.cpp` | P1  | PLANNED  | Svuotamento progressivo dopo migrazione callsite a `TextRunNode`; rimozione del processor legacy.        |
| 10 | `include/chronon3d/text/rich_text.hpp`                          | P1     | PLANNED  | Eliminazione completa dopo migrazione `RichTextLine → TextDocument` / `draw_rich_text → text_run(...)`. |
| 11 | `src/backends/text/text_rasterizer_render.cpp`                  | P1     | PLANNED  | Classificare utility riusabili (`blend2d_glyph_conversion`, `freetype_outline_conversion`) vs legacy.    |
| 12 | `src/backends/software/software_backend.cpp`                    | P2     | PLANNED  | Factory + dispatch + text + effects in file separati; main = ctor/dtor/accessors.                        |
| 13 | Registry preset testuali                                        | P2     | PLANNED  | Factory per categoria (basic/kinetic/cinematic/social); registro centrale unico invariato.               |
| 14 | `docs/FOLLOWUP_TICKETS.md` open-blockers housekeeping           | P2     | PLANNED  | Separare `Done`/`PARTIAL`/`TEST-ONLY DONE`; promuovere back-log da "open" a "recently closed".            |
| 15 | `docs/tickets/TICKET-P1-ACTION-PLAN.md`                         | P2     | PLANNED  | Convertire in matrice Implementazione / Test / Migrazione / Rimozione legacy (vedi pasted text §15).    |

### Regole vincolanti durante l'esecuzione del backlog M1.5

- **Prima mini-sequenza post-baseline (P0):** `1 → 2 → 3 → 4` — sono i file che oggi
  combinano maggiore fragilità, falsa riuscita, e identità/cache incoerenti.
- **Limiti guida** (suggerimento operativo, non obbligo meccanico):
  - header pubblico: preferibilmente < 250–300 righe;
  - orchestratore: < 350–450 righe;
  - funzione normale: < 80 righe;
  - una sola responsabilità architetturale per file;
  - nessun **nuovo** singleton, registry, resolver, sampler, cache, service locator;
  - ogni estrazione deve essere coperta da test prima **e** dopo;
  - un commit per responsabilità, direttamente su `main` (no branch di feature).
- **Pre-push gate:** `tools/wrap_push.sh origin main` (atomic FF-merge + gate
  `check_main_clean.sh`); vedi TICKET-076.
- **Sincronizzazione documentale:** ogni chiusura aggiorna `CURRENT_STATUS.md` (stato)
  e `FOLLOWUP_TICKETS.md` (storico) nello **stesso** commit (vincolo gate #7 `check_doc_sync.sh`).
- **Matrice di avanzamento** (Implementazione / Test / Migrazione / Rimozione legacy)
  per item — popolata in `docs/baselines/text-decomp-matrix.md` dopo lo sblocco freeze.

### Non-goal M1

- Text 3D;
- MSDF;
- morph avanzato;
- supporto globale ICU completo;
- nuovo renderer testuale parallelo.

## M2 — Camera Production V1

### Obiettivo

Rendere `CameraDescriptor → CameraProgram` l'unico percorso authoring nuovo e
coprire i movimenti cinematografici necessari al motion graphics 2.5D.

### Lavori

1. Completare i ticket camera P0/P1 ancora test-blocked o parziali.
2. Completare `OrientAlongPath`, look-ahead, keep-horizon e banking.
3. Preservare correttamente stato base/projection in tutte le source.
4. Chiudere diagnostica e determinismo della shot timeline.
5. Portare framing a multi-target, bounds-aware, rule-of-thirds e dead-zone.
6. Completare clipping near/far necessario al renderer.
7. Migliorare DOF senza creare un secondo sistema ottico.
8. ~~Aggiungere parity test e golden sul percorso compilato.~~ **Chiuso da C1–C7** (golden in `tests/scene/camera/golden_projection_test.cpp`, SHA `eb1ce8e5`; `FocalPx` + `ViewportRect` + `focal_xy_from_camera` + `effective_viewport` con offset; anamorphic_squeeze SOLO X; sentinel regression subrect-fits-canvas).
9. Migrare preset e composizioni, poi deprecare/rimuovere authoring legacy.
### M2.A3 — Agent3 ticket cluster: compiler semantics, session policy, cache lease, framerate explicit (PLANNED, post-freeze)

Origine: prompt operativo **Agent3 — Compiler, Session, Errori, Legacy Cleanup** (2026-07-04). Si applica ai file `camera_v1/*` (`include/chronon3d/scene/camera/camera_v1/*.hpp` + `src/scene/camera/camera_v1/*.cpp`) — **non** al type-level deletion già coperto da [ADR-011](adr/ADR-011-camera-legacy-deletion.md) (AnimatedCamera2_5D / dual CameraRig / CameraShotProfile / camera_descriptor_adapters / Camera2_5D::projection_mode). Stato attuale: tutti i sotto-ticket sono **PLANNED** (nessun codice toccato durante freeze). Workstream design-FROZEN 2026-07-04.

Lavori (uno per ticket, commit atomico su `main` — no branch, AGENTS.md §Feature Freeze):

1. **TICKET-A3-METADATA** — `camera_v1::compile_camera()`: rimuovere il `return` anticipato dopo graft del `RegisteredMotionRef`; late-rebuild `failure_policy_` / `time_dependent_` / `evaluation_dependency_` post-graft.
2. **TICKET-A3-SESSION-POLICY** — `CameraFailurePolicy::KeepLastValidCamera` realmente recupera `CameraSession::last_valid_camera` (oggi segue il ramo di `Stop`).
3. **TICKET-A3-CACHE-LEASE** — `CameraSessionCache::acquire()` non aggiorna `last_evaluated_frame` prima del completamento della evaluate; lease `commit()` on success only; failed evaluation non muta cache.
4. **TICKET-A3-CTX-FRAMERATE** — `CameraEvalContext::at(Frame, FrameRate)` + `CameraMotionContext::at(Frame, FrameRate)` audit call-site per residui `FrameRate{30,1}` hardcoded.
5. **TICKET-A3-PRE-ROLL-FPS** — `preroll_session_for_frame(...)` riceve `FrameRate` esplicito (signature già presente in `camera_session_cache.hpp`; verificare che ogni call-site passi fps non-default).
6. **TICKET-A3-DAMPED-HISTORY** — `DampedFollowConstraint` forza sempre `ProgramKind::RequiresHistory` durante la fase di metadata classification del compiler.
7. **TICKET-A3-LOOKAT-DIAGNOSTIC** — `LookAtLayer` senza `transforms` (`CameraEvalContext::transforms == nullptr`) emette `CameraDiagnostic{Code::MissingTransforms, Severity::Warning}` invece di silent fallback.
8. **TICKET-A3-ADR-013** — ADR-013 (camera_v1 semantics + framerate explicit + cache-lease contract) + ticket-to-code chain verso [ADR-011](adr/ADR-011-camera-legacy-deletion.md) per chiudere il DoD gate (h). Doc-only.

Done-Definition (8 gate, vedi matrice in `docs/FOLLOWUP_TICKETS.md` Agent3 cluster): tutti i gate (a)..(h) sono PASS verificato allo stesso commit. Gate di uscita addizionali:

- `tools/check_camera_architecture.sh` rimane 6/6 PASS dopo ogni commit;
- `tests/scene/camera/` LINK + RUN;
- `tools/wrap_push.sh origin main` GATE_PASS prima di ogni push.

Allineato con `docs/camera-plan/04-INTEGRATION_TESTS_AND_LEGACY_REMOVAL.md` PR breakdown (CAM-02 compiler validation / CAM-05 render integration / CAM-06 timeline+state). Avvio rigido post-freeze: `1 → 2 → 3 → 4 → 5 → 6 → 7`, ADR-013 last.

### Gate di uscita

- nuove composizioni senza `AnimatedCamera2_5D` o rig legacy;
- sessione posseduta dal render job;
- nessuna compilazione o lookup catalog per frame;
- test camera bloccanti verdi;
- consumer esterno usa una camera compilata.

## M3 — SDK Product V1

### Obiettivo

Distribuire Chronon3D come SDK C++ installabile e documentato, non soltanto come
repository sorgente.

### Lavori

1. Consumer esterno reale: extension pack, runtime, testo, camera, render e file output.
2. API pubblica minima documentata e header interni esclusi dagli esempi.
3. Compatibility/deprecation policy.
4. Versionamento package e verifica `find_package`.
5. Artifact Linux riproducibili.
6. Esempi separati dalla source tree.
7. Smoke test install/configure/build/run in CI.

### Gate di uscita

- un progetto vuoto può installare e usare `Chronon3D::SDK`;
- render di almeno una composizione reale fuori-tree;
- nessun link diretto a target interni;
- documentazione e artifact associati allo stesso tag.

## M4 — Pacchetti animazione e interoperabilità

### Obiettivo

Permettere ad altri programmi di caricare e usare animazioni Chronon3D senza
compilare direttamente il core C++.

### Lavori

1. Definire schema versionato `.chronon`.
2. Serializzare composition, layer, animation tracks, text, camera, effects e asset refs.
3. Loader con validazione, diagnostica e migration version.
4. Definire C ABI con handle opachi.
5. Esporre runtime, package, render job, framebuffer ed error API.
6. Creare primo binding Python.
7. Definire plugin ABI versionato per pack C++ non serializzabili.

### Regola

Python, C#, Node, Rust e altri binding devono appoggiarsi allo stesso C ABI.
Non duplicare il runtime in ogni binding.

## M5 — Global text ed effetti premium

Solo dopo M0–M4:

- ICU opzionale;
- variable fonts;
- color glyph/emoji;
- Expression Selector produttivo;
- Text 3D;
- MSDF;
- morph e displacement;
- ottica/DOF più avanzati.

## M6 — V3 tile-first

V3 è una futura evoluzione interna. Non deve interrompere la chiusura di Text,
Camera e SDK V1 né introdurre una pipeline parallela prima che i contratti V1
siano verificati.

## Vincoli permanenti

- non reintrodurre executor su raw graph o `ExecutionPlanCache`;
- non creare registry, resolver, sampler o cache paralleli;
- non costruire executor dentro i nodi;
- non indebolire gate per adattarli al codice;
- non promuovere `experimental/` perché compila in opt-in;
- non aggiungere GUI/browser al core headless;
- una PR deve risolvere un problema chiaro e avere test mirati.
