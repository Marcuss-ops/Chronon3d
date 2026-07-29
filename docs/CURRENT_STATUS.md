# Chronon3D — Current Status

> Ultima revisione semantica: 2026-07-29.
> Ultima baseline certificata: `main@7eb5c2ba`, 11/11 PASS.
> I commit successivi alla baseline non sono implicitamente certificati.
> Ultimo SHA osservato: `main@eabf459a` (Glow V1 e Camera 2.5D V1 verificati sui target disponibili; fixture Combined Product eseguito via CLI, certificazione globale ancora aperta).
>
> Feature freeze V0.1 revocato 2026-07-06. Linux-only.
> Cronologia dettagliata in [`docs/ARCHIVE/CURRENT_STATUS_HISTORY.md`](docs/ARCHIVE/CURRENT_STATUS_HISTORY.md).

## Active Blockers (top 2)

| ID | Area | Stato | Scheda |
|---|---|---|---|
| TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX | docs | OPEN | [TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX](tickets/TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX.md) |
| TICKET-125-TEST-AGGREGATOR | testing | OPEN | [TICKET-125](tickets/TICKET-125-test-aggregator.md) |

Indice completo dei blocker attivi: [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md). Cronologia ticket chiusi: [`docs/CHANGELOG.md`](docs/CHANGELOG.md).

## Stato generale per area

| Area | Stato | Note sintetiche |
|---|---|---|
| CLI V3 unification | WIRED / NOT RUN | `render --plan` è il percorso canonico; `render-plan` è un alias sottile. La compilazione typed e l’AudioMuxer condiviso sono presenti; macchina-verifica non eseguita. |
| Push infrastructure | WIRED | `tools/monitor_push_divergence.sh` cron-friendly 5-min cadence; ADR-022 advisory gate. |
| Text V1 Cert Step 11 (finale) | DEFERRED-VPS | BLOCKED on this VPS per TICKET-BUILD-ROT-CASCADE-CAMERA 409-error + TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV; macchina-verifica DEFERRED-WBH. |
| Cert sequence (Test #4/#8/#9/#13/#14) | WBH-DEFERRED | Per `docs/cert_sequence_wbh_protocol.md`; VPS cannot run. |
| Text V1 Cert Step 8+9 | DEFERRED-VPS | HARDER env-block than Step 7; spec-variant user centroid LOOSER than DoD §9 lock. |
| Text V1 Cert Step 10 (negative-font) | COMMITTED-VPS-DEFERRED | cat-1 source committed; rebuild DEFERRED-WBH. |
| Acceptance Suite | PASS | 20/20 contract tests landed. |
| Camera V1 | PASS focused | `main@eabf459a`: descriptor → program → session, projection, random access, framing, DOF, motion blur, LookAt, OrientAlongPath, camera groups e composition unification PASS; full Linux orchestrator non certificato. |
| Executor | P2 OPEN (cat-5 forward-point) | Tile-prune skip-unification chaser-chore tracked. |
| Glow V1 | PASS focused | `main@eabf459a`: gate Glow completo PASS — software CPU, animazione continua, bbox/ROI, alpha, landscape/portrait, video reale 60 frame, cold/warm e determinismo; suite globale non certificata. |
| Combined Product V1 | PASS focused / NOT CERTIFIED | `main@eabf459a`: `GlowCameraProductV1` compila e produce un frame CLI reale con testo Glow, camera orbit, parallax e bloom senza clipping; SDK/C ABI, seriale/parallelo, cold/warm, random order e video 60 frame non ancora certificati. |
| Product Launch demo (Test #1) | PARTIAL | Composition + JSON landed; orchestrator `== Product demo ==` TODO body. |
| Sanitizer gates (P2-A) | PARTIAL | 7 subsystems + ASAN/UBSAN/TSAN_OPTIONS wired; full ctest DEFERRED-WBH. |
| Text Rendering Core V1 | PASS | FreeType + HarfBuzz + FriBidi + shaping + layout + glyph cache + animator + selector certified; vedi [TICKET-TEXT-PRODUCTION-STATUS-CORRECTION](tickets/TICKET-TEXT-PRODUCTION-STATUS-CORRECTION.md). |
| Text Production / CapCut-grade V1 | PARTIAL | Canonical TextDefinition path, catalogo 20 preset generali + 8 subtitle, subtitle matrix 192/192 non-empty, alignment e auto-fit focused tests PASS. CapCut parity is now a strict release-blocking gate, but the repository still has zero blessed PNG exports from CapCut. Vedi [TICKET-CAPCUT-REFERENCE-CORPUS](tickets/TICKET-CAPCUT-REFERENCE-CORPUS.md). |
| Test hardening (false-green audit) | PASS | Auto-fit e alignment sono assertions bloccanti; il parity harness non accetta più reference mancanti o corpus vuoti. Vedi [TICKET-FALSE-GREEN-TEST-AUDIT](tickets/TICKET-FALSE-GREEN-TEST-AUDIT.md). |
| Text Health | PASS | `chronon3d_text_health_tests` PASS (1/1) on main. |
| Text API migration (Blocco 5.1/5.2) | PASS (focused) | `TextSpec`/`TextRunSpec`, reverse adapters, `animated_text()` e i 21 motion helper dedicati sono stati rimossi dal percorso produttivo; authoring, esempi e preset usano `TextDefinition`, `text_run()` e `LayerBuilder::motion()`. Resta tracciata soltanto l'eventuale riduzione futura dei transport interni `TextRunDefinition`/`PreparedText` in [TICKET-DEPRECATED-API-REMOVAL](tickets/TICKET-DEPRECATED-API-REMOVAL.md). |
| Animation / local-time / asset preparation | PASS (focused) | Un solo interpolate con `Extrapolate`, una `SpringConfig`, random deterministico per stagger, sequence su `FrameContext` locale e barriera `prepare_render()` verificati con test mirati; full baseline non ricalcolata su questo worktree. |
| Authoring facade | WIRED / GUARDED | `asset(path)` è context-typed e kind-free; due `RenderEngine` con root distinti, CWD ostile, font/image logical refs e missing-image fail-loud coperti da test. Gate statico vieta root globali, fallback CWD, resolver nelle composizioni e mega-header. |
| Timeline props | WIRED | `PropsCodec`/`PropsSchema` typed composition props landed; registry resolve ora trasporta il costruttore preparato senza una seconda decode/factory pass. |
| Render job execution | WIRED / GUARDED | Pipeline unica `RenderRequest → RenderJob → execute_render_job(const RenderJob&)`; `ResolvedRenderJob`, conversioni legacy e executor separati vietati dal gate. Suite focalizzata e workflow matrix aggiunti; esecuzione CI NOT RUN/NOT OBSERVED. |
| SDK C++ installabile | PASS baseline / WIRED extension | Gate #10 storico PASS. Nuovo FILE_SET authoring disgiunto, closure gate e consumer installato `check_assets` implementati; nuova estensione non ancora certificata su CI. |
| SDK cross-language | WIRED / NOT RUN | C ABI V1 usa engine + plan: `chronon_engine_create/destroy/last_error`, `chronon_abi_version`, `chronon_plan_compile_json`, `chronon_render_frame/file` e `chronon_buffer_free`. Il vecchio `chronon_context` e gli adapter JSON sono stati rimossi; smoke install/ctypes non eseguito per toolchain mancante. |
| Modular graph legacy path | PASS (source audit) | `use_modular_graph` non è presente nella superficie attiva `include/src/apps/tests`; il gate permanente è `tools/check_no_modular_graph.sh`. |
| Render runtime | PASS baseline / WIRED fail-loud | Runtime per-instance certificato nella baseline storica; `prepare_render()` orchestra preflight, resource preparation e warmup nei percorsi CLI e nella boundary `chronon3d::RenderEngine::render()`, con test fail-loud/idempotenza/null-renderer mirati. |
| Composition pipeline | PASS | Canonical pipeline documented; Sequence V2 + Asset Readiness code-complete. |
| CompositionDescriptor migration | PARTIAL | `add(name, factory)` deprecated (ADR-027); 200+ legacy callers remain; Chore B bulk migration OPEN. |
| Video pipeline | PASS | Structured error reporting (13 codes); atomic output; 98 video tests pass. |
| CI infrastructure | FAIL | Workflow RenderJob matrix, suite fast aggiornata e font bootstrap autenticato/checksum-pinned sono WIRED; le run recenti non autorizzano ancora una nuova baseline verde. |
| Test coverage | PASS baseline / WIRED additions | Baseline: 5×5 deterministic matrix, 5×5 SafeArea matrix e layout tests. Aggiunti asset isolation, installed consumer, single-pass registry e planner contract, ancora NOT RUN sul lineage corrente. |
| Benchmark corpus | WIRED | 12-scene YAML corpus B00-B11 + sanity test harness landed; macchina-verifica DEFERRED-WBH. |
| Auto-fit (ADR-018) | PARTIAL | engine-level DONE; canonical wrapper forward-pointed (ADR-gated). |
| Sistemi meta (Expressions V2 / V3) | PLANNED | V2 OFF di default; V3 subordinato a V1. |
| 10-point friction audit | DONE (2026-07-08) | Lineage closed. |
| SDK Product V1 (manifest + image-layer) | PASS baseline / WIRED authoring | forward-points 0e+0f+0g+0h+ closed nella baseline; authoring asset install extension attende verifica. |
| Glow certification (Test GLOW-CERT) | PASS | `main@05fdb4cd`: `tools/check_glow_certification.sh` PASS su 6 fasi e 3 run deterministici. |
| Fail-loud errors (Test #7) | WIRED | Gate esistente + nuovo confine SDK: un errore interno impedisce la restituzione di framebuffer parziali come successo. |
| Costo reale (Test #11 render-cost) | WIRED | `measure_render_cost.sh` + `docs/scorecard.csv` 9-col. |
| Manual touches per video (Test #19) | WIRED (HARNESS-COMPLETE) | `check_manual_touches_per_video.sh` + 4-phase thresholds. |
| Scale 100 batch (Test #12 wireup) | WIRED (HARNESS-COMPLETE) | Orchestrator wireup of Test #20 4-envelope gate. |
| Batch 100 videos acceptance (Test #20) | WIRED (HARNESS-COMPLETE) | 4-envelope PASS gate. |
| Video Completeness Spec §5 (60-frame) | WIRED (HARNESS-COMPLETE) | 12-col CSV + 6 assertions per frame. |
| Fix speed (Test #11 cronometro) | GATE-WIRED | `check_fix_cronograph.sh` + orchestrator section. |
| Sunset registry (Test #16) | GATE-WIRED | `docs/FEATURE_SUNSET.md` 3-non rule + 30gg scadenza. |
| Direct comparison (Test #17) | GATE-WIRED | `docs/product-tests/TEST-17-COMPARISON.md` 8 metriche × 3 prodotti. |
| Single source of truth (Test #12) | GATE-WIRED | 12/12 audits clean. |
| Packaging cert (Test P1) | WIRED | `verify_packaging_linux.sh` 7-section FAIL-LOUD. |
| Diagnostics cert (Test P2) | WIRED / NOT RUN | `verify_diagnostics_linux.sh` usa solo `render`, richiede 10 codici stabili e restituisce BLOCKED quando manca la verifica runtime; nessun PASS parziale. |
| Determinism spec completeness (amend) | PASS | Verified via chronon3d_cli on `BenchB01_StaticText1080p`: 5 identical renders of frame 30 and random-order sequence (30, 0, 60, 15, 30) produced identical SHA-256 hashes. |
| Compositing spec completeness (amend) | WIRED | `verify_compositing_effects_linux.sh` 10→14 effects. |
| Camera full cert (Test GLOW-CERT sibling) | FAIL | `verify_camera_full_linux.sh` eseguito sul lineage corrente: prima collisione overload di `interpolate`, poi filesystem pieno durante il link; nessun PASS certificato. |
| SDK consumer functional (Test P1 sibling) | WIRED | Consumer esterno esistente + nuovo `check_assets`: include authoring espliciti, image/font logical refs, due engine/root e CWD isolation. |
| Render runtime cert (Test P3) | WIRED | `verify_render_runtime_linux.sh` 4 distinct sha256. |
| Asset preflight cert (Test #7 sibling) | WIRED | `verify_asset_preflight_linux.sh` 10 sabotage scenarios. |
| Timeline functional cert (Test P1) | WIRED | `verify_timeline_functional_linux.sh` 10 TEST_CASEs. |
| Chronon product cert (orchestrator) | WIRED / NOT RUN | `verify_chronon_product_linux.sh` esegue una lista unica di 15 sub-gate reali; il diagnostics gate non è più forward-pointed. |
| Error handling cert | WIRED | `verify_error_handling_linux.sh` 10 scenarios. |
| Performance cert | WIRED | `verify_performance_linux.sh` 5 scenarios + leak test. |
| Sanitizer cert | WIRED | `verify_sanitizer_linux.sh` 0 OOB + 0 UAF + 0 UB + 0 data races. |
| TICKET-125 (Test aggregator Tests 8-18) | PARTIAL | 11-row index Tests 8-18 con PASS/FAIL criterion. Vedi [TICKET-125](tickets/TICKET-125-test-aggregator.md) + forward-points TICKET-TEST-9-PILOT-7GG + TICKET-TEST-13-INDEXING + TICKET-TEST-18-LONG-FORM-CONTENT. |
| Test 18 founder dashboard | OPEN | Weekly scorecard aggregator + 8 metriche. |
| Build (4346d7f068cb11ab26598417b13b5277b7e55ad6) | PARTIAL | chronon3d_cli verde post rebase-21ece2b3 (upstream rot #8 for-loop fix merged at apps/chronon3d_cli/commands/watch/register_watch_commands.cpp; our profiling.hpp include at src/backends/text/text_render_resources.cpp preserved); broader 11/11 macchina-verifica DEFERRED-WBH per AGENTS.md §rot-class-protection threshold (post-2nd-rebase [ahead 4, behind 0] vs upstream 4e203dde (chaser-chaser close cat-5 deferred)). |

| Layer transitions (TRN-03) | PASS | compute_progress centralizzato su `sample_transition()`; cache key include durata/delay/easing/direzione e parametri tipizzati; in/out coexist nel medesimo layer via due nodi seriali; catalogo fail-loud su ID sconosciuto. |
| Text transitions (TRN-04) | PASS | semantica Cut corretta (A fino al boundary di B); dissolve testuale con alpha complementari (incoming alpha = mix, outgoing alpha = 1 - mix); effetti fill/stroke/shadow/blur/material/font spans/bbox applicati simmetricamente; golden frames al 0/25/50/75/100%. |
| Camera transitions (TRN-05) | PASS | contratto di overlap reale con tempi locali per entrambi gli shot; transition_frames==1 mappa a cut istantaneo; stato sessione unificato in ShotTimelineSession/CameraSessionCache; CameraTransitionCatalog è l'unico percorso per creare transizioni. |
| Layer transition certification (TRN-06) | PASS | matrice minima eseguita e verde per 11 casi: 16:9/9:16, in/out, durate 1/2/10/30 frame, easing diverse, 4 direzioni, cache cold/warm, accesso sequenziale/casuale, alpha opaco/trasparente, frame chiave start/middle/end, determinismo scheduler seriale/parallelo. |
| Clip transitions (TRN-07) | PASS | ClipTransitionNode implementato con Cut/Dissolve certificati: p=0 restituisce A, p=1 restituisce B, p=0.5 blend matematico A*(1-p)+B*p nello spazio alpha premoltiplicato. Color/alpha policy documentata in clip_transition.hpp; ScaleToFit gestisce risoluzioni diverse; ErrorOnMismatch restituisce errore strutturato. SceneBuilder::clip_transition() integrato nel graph builder. |

## Gate Audit — ultima verifica

**`main@04c1cb48` — gate helper refactor** (2026-07-15): `tools/check_no_legacy_render_cli.sh` ora usa helper `check_canonical()` per rendere le verifiche di firma robuste ai ritorni a capo di clang-format. Nessuna nuova certificazione funzionale dichiarata.

**`main@8d63f407` — render/asset certification harness WIRED, NOT RUN** (2026-07-15): pipeline diretta `RenderRequest → RenderJob`, registry prepare/construct single-pass, workflow matrix video ON/OFF/core-only, suite RenderJob focalizzata, asset-root isolation, missing-image fail-loud, authoring FILE_SET closure, installed SDK consumer e Poppins bootstrap autenticato/checksum-pinned presenti. Nessuna run verde osservata sullo stesso lineage; nessun PASS dichiarato.

**`main@7878a627` — 15/15 gate eseguibili WIRED, NOT RUN** (2026-07-15): `verify_diagnostics_linux` è ora invocato con `run_gate` equivalente nella lista unica dell'orchestratore. Il gate diagnostico usa il comando `render` canonico e non può emettere PASS quando il binario runtime manca. Nessuna nuova certificazione dichiarata: serve una run WBH completa sullo stesso SHA.

**`main@ef9c83f1` — 14/14 + 1 forward-pointed (BLOCKED)** (2026-07-12, baseline storica): 14/14 sub-gate eseguibili PASS + 1 forward-pointed `verify_diagnostics_linux` = `CHRONON_PRODUCT_FUNCTIONAL_BLOCKED` (exit 2). Baseline: [`docs/baselines/main-ef9c83f1-baseline.md`](docs/baselines/main-ef9c83f1-baseline.md).

**`main@7eb5c2ba` — 11/11 PASS** (2026-07-06, certificata, regression-line preserved as historical reference). Baseline: [`docs/baselines/main-7eb5c2ba-baseline.md`](docs/baselines/main-7eb5c2ba-baseline.md).

## Come leggere gli stati

| Stato | Significato |
|---|---|
| PASS | Implementazione verificata contro prova eseguibile osservata. |
| FAIL | Comportamento non corretto osservato. |
| PARTIAL | Implementazione presente ma con limiti o copertura incompleta. |
| NOT RUN | Gate / prova non ancora eseguita. |
| BLOCKED | Bloccato da altro ticket o condizione esterna. |
| PLANNED | Design presente, implementazione non iniziata. |
| WIRED | Harness/impalcatura presente; verifica eseguibile differita o in attesa di risorse. |

## Link canonici

- [`docs/ROADMAP.md`](docs/ROADMAP.md) — milestone prodotto
- [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md) — requisiti permanenti di release
- [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) — indice blocker aperti (canonical)
- [`docs/CHANGELOG.md`](docs/CHANGELOG.md) — chiusure recenti e cronologia
- [`docs/CAMERA_FEATURE_MATRIX.md`](docs/CAMERA_FEATURE_MATRIX.md) — feature camera
- [`docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`](docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md) — piano testo
- [`docs/DOCUMENTATION_GOVERNANCE.md`](docs/DOCUMENTATION_GOVERNANCE.md) — contratto documentale
- [`docs/baselines/main-7eb5c2ba-baseline.md`](docs/baselines/main-7eb5c2ba-baseline.md) — baseline verde certificata
- [`docs/ARCHIVE/CURRENT_STATUS_HISTORY.md`](docs/ARCHIVE/CURRENT_STATUS_HISTORY.md) — cronologia estesa (Phase A–H, gate audit pre-`7eb5c2ba`)

## Text Simplicity Plan (M1.8 — PLANNED)

Piano dettagliato per raggiungere l'ergonomia di Remotion (17 commit in 5 fasi). Documenti: [`docs/TEXT_SIMPLICITY_ACTION_PLAN.md`](docs/TEXT_SIMPLICITY_ACTION_PLAN.md) (piano operativo) + [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) §M1.8 (17 ticket PLANNED) + [`docs/ROADMAP.md`](docs/ROADMAP.md) §M1.8 (milestone).

## Hygiene

Main-sync hygiene enforced da GATE-MNT-01 ([`tools/wrap_push.sh`](../tools/wrap_push.sh) + [`tools/check_main_clean.sh`](../tools/check_main_clean.sh)). `branch.main.rebase = true`.
