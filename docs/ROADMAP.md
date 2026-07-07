# Chronon3D — Active Roadmap

La roadmap è organizzata per milestone prodotto. Non avviare una milestone
successiva per nascondere blocker della precedente.

Stato corrente: [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md). Criteri di release: [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md).

> **Snapshot macchina-verificato (2026-07-04):** `main@2895bd88` — post-FF-pull origin/main; **§3.1 Esecuzione del piano commitato (commit `a8414705`)** at 2026-07-04: TICKET-A3-CACHE-LEASE (CameraSessionLease rollback real) **CHIUSO**.  **8/11 atomic PASS + 1 FAIL (g4) + 1 PASS* (g8) + 1 NOT RUN (g10)** osservato post-§3.1 atomic audit run: g1, g2, g3, g5, g6, g7, g9, g11 tutti `exit 0`; **g4 FAIL** per abs-path leak in `docs/tickets/TICKET-GATE-10-PHASE-4-BLACK-FU4.md:75` (`cd <REPO_ROOT>`) — propagazione TICKET-GATE-4-LEAK cluster (vedi `FOLLOWUP_TICKETS.md`); **g8 PASS*** (warn-mode; 89 drift findings, ↓ da 170 post-M1.5#6 tightening pass); **g10 NOT RUN** (heavy full-build requirement; carry-over rot install_consumer_test.sh, TICKET-GATE-10-PHASE-4-FIX parziale).  **8 atomic PASS ≠ 11/11: feature freeze ANCORA ATTIVO** (revoca richiede 11/11 PASS macchina-verificato sullo stesso commit, vedi `AGENTS.md` §Feature Freeze + `CURRENT_STATUS.md` §Certificazione corrente).  M2 Camera V1 ADR-013 ✅ documented+accepted; A3 cluster source-code **1/8 chiuso** (TICKET-A3-CACHE-LEASE).  M3 SDK V1 **NOT green** (gate #10 carry-over rot).  Storico baseline `>9/11` macchina-verificata: [`docs/baselines/main-aaf70032-baseline.md`](docs/baselines/main-aaf70032-baseline.md) (10/11 pre-fix log).  Ultima 11/11 macchina-verificata osservata: `main@1078ab46` (post-Fase A+B2+B3, 2026-07-04; conservata in `CURRENT_STATUS.md`, table "Gate audit snapshot — `main@1078ab46`").  Per i dettagli di ogni milestone vedi le sezioni M0–M6 sotto.

> **Snapshot (2026-07-07, post-Phase-H Soluzione B + MultiSourceNode consistency — verification PENDING, tickets remain PARTIAL):** TICKET-AE-CAM-PRECISION-COLLAPSE + TICKET-ae-cam-hash-collision Soluzione B atomic commit `d39b37f1` (cumulative 27 file) + MultiSourceNode consistency commit `853ace48` (3 sites mirror source_node pattern with `from_mat4(item.matrix, item.opacity)` for proper TRS decomposition, pre-empting empty-Transform-tr bug).  **Verification FAILED on working build host 2026-07-07**: 32/35 `chronon3d_ae_parity_tests` PASSED + 3 in-memory FB hash tests FAILED (AE_CAM_03/05/06 at `tests/visual/ae_parity/ae_parity_tests.cpp:230/303/341`) + 13 banned PNGs remain on disk (sha256 prefix `cc86d2b5e80287dc`) + 9-key `test_node_cache_ae_sweep` blocked at `ar` link step (system-level disk-quota exceeded).  Candidate root cause (Gemini source-read, NOT machine-verified): SourceNode round-2 fix at `src/render_graph/nodes/source_node.cpp:122/216` passes a default-constructed `chronon3d::Transform tr;` (scale=1,1,1) to `project_layer_2_5d`, propagating `layer_size=1x1` → 2D layers render as transparent-black → `framebuffer_hash` collisions.  Forward-fix path documented in `docs/tickets/TICKET-ae-cam-hash-collision.md` `## Verification gap` (Option 1: restore `m_uses_2_5d_projection` check; Option 2: pass `m_node.world_transform` instead of empty `tr`).  **Promotion to `DONE` is intentionally NOT triggered** — the user request was "Once verified, update ... to fully DONE"; verification did NOT pass, so tickets remain at `PARTIAL` (matrix-fix cluster DONE + hash-collision cluster OPEN).  `docs/CURRENT_STATUS.md` §Phase H blockquote documents the full evidence + forward-fix path.  Cat-2 AE-parity visual contract (gate #2) status: **BLOCKED** until SourceNode empty-Transform-tr fix lands + end-to-end re-bake produces 24 fresh-distinct PNGs + `check_ae_parity_golden_state.sh` transitions FAIL→PASS + 9-key test runs + PASSes.  AGENTS.md v0.1 Cat-1 (build corrective) + Cat-3 (no public API surface) + Cat-5 (doc-only alignment) freeze-compliant.  Zero new public symbols.

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
| 1  | `src/render_graph/nodes/TextRunNode.cpp`                        | P0     | DONE     | `execute()` → orchestratore breve; canale `Result<OwnedFB, NodeExecutionError>`; nessun falso successo. Refactor committato (commit `82d2b0e0`): 3 helpers in `src/render_graph/nodes/text_run/` (execution / transform / diagnostics). `chronon3d_graph_nodes` lib compile clean (LIB_EXIT=0 re-verified). Return-channel contract locked da `tests/render_graph/nodes/test_text_run_node_return_channel.cpp` (3 failure mode: ExecutionFailure, CapabilitiesOff, NullBackend + Success path). `chronon3d_render_graph_tests` target ha blockers pre-esistenti risolti (`camera_change_policy.cpp::Camera2_5D::projection_mode` chiuso in `ac514fea`; `text_unit_map` link rot tracked in TICKET-011 — fuori scope M1.5#1). Avvio rigido mini-seq post-baseline: `1 → 2 → 3 → 4`. |
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

## M1.6 — AE-Parity Cinematic Text Golden Expansion (PLANNED, post-baseline-verde)

> **Origine:** action-plan landing 2026-07-06 dalla strategia "Chronon3D vs After Effects per kinetic typography 2D". Formalizza l'espansione del floor AE-parity (5/20 IMPL shipped storicamente — Phases D) al target completo (20/20 IMPL + 288 PNG floor + 4 killer test + referee AE-side). NON avviabile fino a `TICKET-GOLDEN-CAPTURE` chiusura + gate #10 `install_consumer_test.sh` 11/11 PASS macchina-verificato sullo stesso commit (AGENTS.md v0.1 §Feature Freeze revoca).
>
> **Regola di stato osservabile:** PASS / FAIL / PARTIAL / NOT RUN / BLOCKED / PLANNED. Tutti i 17 ticket di questo workstream sono PLANNED al landing.

### Obiettivo

Portare Chronon3D al livello "After Effects-like per kinetic typography 2D automatizzata e timelined subtitle video" via:

- **Floor 1:** 20 scene cinematic AE-parity scene-builder IMPL × 6 AR/frame snapshots = 120 PNG floor
- **Floor 2:** + user-spec 12 golden tests × 6 PNG (4×1.5 già tracked + 20 PNG total) = ~140 PNG
- **Floor 3:** + motion-blur text 6 PNG (TICKET-MOTION-BLUR-TEXT) = ~146 PNG
- **Floor 4:** + 4 killer test visual regression (motion_blur + per_char_3D + wiggly_wave + subtitle_word_timing) = ~288 PNG floor verificato macchina
- **Floor 5:** referee pipeline AE-side → Chronon3D-side diff `mean_abs_diff` < 5/255 su almeno 10/15 scene cinematic
- **Floor 6:** consumer SDK `tests/install_consumer/` deve usare almeno 3 cinematic preset senza includere header interni (manifest-clean DoD §P3-H)

### Lavori (sequenza rigida, un commit per ticket atomicamente)

Sequenza di lavoro — 5 step incrementali. Ogni step è un commit atomic su `main` (AGENTS.md §GATE-MNT-01 + `tools/wrap_push.sh`); nessun branch di feature.

1. **AE-PARITY-CINEMATIC-06..20 IMPL** (15 ticket, ciascuno un `tests/text_golden/ae_parity/ae_NN_<id>.cpp` scene-builder + 6 TEST_CASE × AR × frame = 30 PNG per scene). Scopes: tracking_expansion, stroke_reveal, glow_pulse, blur_in, scale_pop, rotation_per_character, random_jitter, text_on_path, multiline_9_16_safezone, long_paragraph_wrap, mixed_font_rich_text, arabic_english_bidi, small_subtitle_readability, fast_motion_stress, karaoke_word_highlight. Harness reuse `verify_golden` da `tests/visual/support/golden_test.hpp` + canonical pipeline `composition(...) + SceneBuilder::layer(...) + LayerBuilder::text(...)`. ZERO `TextShape` / `rich_text` references (AGENTS.md v0.1 Cat-2 freeze-compliant).

2. **Killer test driver** (TICKET-AE-PARITY-KILLER-PER-CHAR-3D + TICKET-AE-PARITY-KILLER-WIGGLY-WAVE-EXPRESSION; cross-link TICKET-MOTION-BLUR-TEXT + TICKET-GOLDEN-30). Per ogni killer test: `tests/text_golden/ae_parity_killer/<name>.cpp` con 3 SUBCASEs (determinism seed + frame-by-frame delta + cross-run reproducibility). Cross-link a M5 milestone per per-char-3D (text 3D + Expressions V2 stable prerequisite).

3. **Floor deliverable: 288 PNG** (TICKET-AE-PARITY-FLOOR) post-CAPTURE-fix (`TICKET-GOLDEN-CAPTURE` chiusura prerequisita). `git ls-files HEAD ./test_renders/golden/text/ae_*.png` ≥ 120; consumato dal `tools/test_golden_driver.sh verify linux-release` con zero PNG drift.

4. **Referee pipeline** (TICKET-AE-PARITY-DRIVER). `tools/ae_parity_referee.sh`: per ogni AE-parity scene, `reference/after_effects/scene_NNN_frame_NN.png` (AE-side mock futuro) + `reference/chronon3d/scene_NNN_frame_NN.png` (engine output) + diff harness `mean_abs_diff + perceptual color metric` con lock a 5/255 threshold. Rigoroso: nessun claim di "AE-like" finché referee non passa su almeno 10/15 scene cinematic. Step forward-only: prima le 5 scene storiche (ae_01..05) devono passare referee come proof-of-concept.

5. **Cinematic preset registry + SDK certifier**. Promozione dei 22 preset (4 basic + 8 kinetic + 4 cinematic + 6 subtitle) da `builtin_text_presets()` (M1.5#13 Step 1/4) a `built_in_text_presets_v1()` API pubblica. Consumer SDK `tests/install_consumer/` deve linkare almeno 3 cinematic preset senza includere header `internal/`/`test/`/etc. (manifest-clean DoD §P3-H — `cmake/Chronon3DPublicHeaders.cmake` scope + `tools/install_consumer_test.sh` end-to-end 11/11 PASS). `docs/FEATURES.md` Text paragrafo aggiornato da "Parziali" → "Presenti" per cinematic kinetic typography.

### Gate di uscita

- tutti i 288+ golden PNG tracked in `test_renders/golden/` (gitignored-friendly, whitelisted);
- 4 killer test PASS macchina-verificato sullo stesso commit;
- referee pipeline `tools/ae_parity_referee.sh` exit 0 su almeno 10/15 scene cinematic NOT RUN (0 su 15), PARTIAL (1-9 su 15), PASS (10+ su 15);
- consumer SDK include `Chronon3D::SDK` 22 preset text senza header `internal/`;
- `docs/FEATURES.md` Text paragrafo aggiornato da "Parziali" a "Presenti" per cinematic kinetic typography;
- `docs/CHANGELOG.md` chiusura "M1.6 — AE-Parity Cinematic Text Golden Expansion" registrata.

### Non-goal M1.6

- complete global text support (emoji/CJK) — M5;
- text 3D production-grade — M5;
- Expression Selector production-grade (Wiggly/Wave solo smoke-test in M1.6);
- per-character 3D beyond demo — M5.

### Cross-link canonici

- Workstream cinematic track: ticket rows `TICKET-AE-PARITY-CINEMATIC-{01..20}` (01..05 DONE Phase D) + `TICKET-AE-PARITY-SUITE` (umbrella 5→20 transition tracker) + `TICKET-AE-PARITY-KILLER-*` (4 killer);
- Concurrency precond: `TICKET-GOLDEN-CAPTURE` (Phase C capture pipeline rot parent) + `TICKET-GATE-10-PHASE-4-FIX` (gate #10 FU5);
- Referee spec: roadmap stub `docs/adr/ADR-015-ae-parity-cinematic-expansion.md` (PLANNED, da stilare al primo commit cinematico successivo alla revoca freeze);
- Match RElease gate §Text Production V1: 20 preset generali + 8 subtitle verificati (TICKET-AE-PARITY-CINEMATIC tracks i 20 generali; i 8 subtitle sono cross-link a TICKET-GOLDEN-30 + TICKET-AE-PARITY-CINEMATIC-20 karaoke_word_highlight).

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

1. **TICKET-A3-METADATA** — `camera_v1::compile_camera()`: rimuovere il `return` anticipato dopo graft del `RegisteredMotionRef`; late-rebuild `failure_policy_` / `time_dependent_` / `evaluation_dependency_` post-graft.  **PARTIAL**: late-rebuild gia' implementato in camera_program_compiler.cpp (CAM-03 fix marker); test lock tests/scene/camera/test_camera_program_metadata_late_rebuild.cpp 5 SUBCASEs committed; test eseguito direttamente 1 TEST_CASE × 32 assertions PASS; 1/8 gate (a) cluster chiuso.
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
