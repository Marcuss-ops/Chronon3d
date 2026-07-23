# Chronon3D — Current Status

> **Snapshot implementazione:** `main@04c1cb48` — observed origin/main HEAD (2026-07-15) post pipeline `RenderRequest → RenderJob`, registry prepare/construct single-pass, asset isolation/fail-loud, installed-authoring consumer, header closure gate, RenderJob matrix, font bootstrap autenticato/checksum-pinned e robust canonical signature helper nel gate CLI render. Baseline verde certificata `main@7eb5c2ba` **11/11 PASS** ✅. **Current lineage non certificata**: GitHub non ha ancora pubblicato una run completa verde sullo stesso lineage. Feature freeze V0.1 revocato 2026-07-06. Linux-only. Cronologia dettagliata in [`docs/ARCHIVE/CURRENT_STATUS_HISTORY.md`](docs/ARCHIVE/CURRENT_STATUS_HISTORY.md).

## Active Blockers (top 3)

| ID | Area | Stato | Scheda |
|---|---|---|---|
| TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX | docs | OPEN | [TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX](tickets/TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX.md) |
| TICKET-125-TEST-AGGREGATOR | testing | OPEN | [TICKET-125](tickets/TICKET-125-test-aggregator.md) |
| TICKET-TEXT-SPEC-MIGRATION | text | OPEN | [TICKET-TEXT-SPEC-MIGRATION](tickets/TICKET-TEXT-SPEC-MIGRATION.md) |

Indice completo (9 blocker sintetici): [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md). Cronologia ticket chiusi: [`docs/CHANGELOG.md`](docs/CHANGELOG.md).

## Stato generale per area

| Area | Stato | Note sintetiche |
|---|---|---|
| CLI V3 unification | WIRED / NOT RUN | `render` è l’unico comando per still/sequence/video; alias e planner legacy rimossi. Suite focalizzata `chronon3d_render_job_contract_tests` copre mode, range, step, estensioni, video settings e video-disabled; matrice video ON/OFF/core-only aggiunta ma non ancora osservata verde. |
| Push infrastructure | WIRED | `tools/monitor_push_divergence.sh` cron-friendly 5-min cadence; ADR-022 advisory gate. |
| Text V1 Cert Step 11 (finale) | DEFERRED-VPS | BLOCKED on this VPS per TICKET-BUILD-ROT-CASCADE-CAMERA 409-error + TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV; macchina-verifica DEFERRED-WBH. |
| Cert sequence (Test #4/#8/#9/#13/#14) | WBH-DEFERRED | Per `docs/cert_sequence_wbh_protocol.md`; VPS cannot run. |
| Text V1 Cert Step 8+9 | DEFERRED-VPS | HARDER env-block than Step 7; spec-variant user centroid LOOSER than DoD §9 lock. |
| Text V1 Cert Step 10 (negative-font) | COMMITTED-VPS-DEFERRED | cat-1 source committed; rebuild DEFERRED-WBH. |
| Acceptance Suite | PASS | 20/20 contract tests landed. |
| Camera V1 | FAIL (cert) | AE-parity 35/35 PASS. Continuous-time motion params landed. Legacy adapters removed. Cert: CAMERA_FUNCTIONAL_FAIL. |
| Executor | P2 OPEN (cat-5 forward-point) | Tile-prune skip-unification chaser-chore tracked. |
| Glow Final (ChrononGlowFinalAE) | Done/Ready | Certified `1cb9cff2`; DoD §9 closed via 19px-sliver regression lock. |
| Product Launch demo (Test #1) | PARTIAL | Composition + JSON landed; orchestrator `== Product demo ==` TODO body. |
| Sanitizer gates (P2-A) | PARTIAL | 7 subsystems + ASAN/UBSAN/TSAN_OPTIONS wired; full ctest DEFERRED-WBH. |
| Text Rendering Core V1 | PASS | FreeType + HarfBuzz + FriBidi + shaping + layout + glyph cache + animator + selector certified; vedi [TICKET-TEXT-PRODUCTION-STATUS-CORRECTION](tickets/TICKET-TEXT-PRODUCTION-STATUS-CORRECTION.md). |
| Text Production / CapCut-grade V1 | PARTIAL | Subtitle wired: ≥8 presets + 10 test, SRT/VTT/JSON adapters, word-timing-quality, per-word karaoke highlight. Geometric ink-bbox fix landed (verdict Fase 1 forward-point). **Nuovo blocker**: `chronon3d_golden_matrix_subtitle_tests` passa ma emette 192/192 celle vuote (`ink_pixels == 0`) — false-green da indagare prima di certificare la matrice. Vedi [TICKET-GOLDEN-MATRIX-SUBTITLE-EMPTY-FRAMES](tickets/TICKET-GOLDEN-MATRIX-SUBTITLE-EMPTY-FRAMES.md). 5/20 general presets golden; macchina-verifica DEFERRED-WBH. Vedi anche [TICKET-TEXT-PRODUCTION-STATUS-CORRECTION](tickets/TICKET-TEXT-PRODUCTION-STATUS-CORRECTION.md) + [TICKET-TEXT-BBOX-OVERFLOW](tickets/TICKET-TEXT-BBOX-OVERFLOW.md) + [TICKET-TIMED-WORD-BINDING](tickets/TICKET-TIMED-WORD-BINDING.md) + [TICKET-WORD-TIMING-QUALITY](tickets/TICKET-WORD-TIMING-QUALITY.md). |
| Test hardening (false-green audit) | DONE | 6 falsi-verde fixes landed (anti-falso-verde dimension assertions, UTF-8 REQUIRE fb, auto-fit ink bbox, alignment isolation [EXPECT_FAIL], typewriter monotonic F0/F1/F5/F10/F20/F30, clipping outside_count==0). Vedi [TICKET-FALSE-GREEN-TEST-AUDIT](tickets/TICKET-FALSE-GREEN-TEST-AUDIT.md). |
| Text Health | PASS | `chronon3d_text_health_tests` PASS (1/1) on main. |
| Text API migration (Blocco 5.1/5.2) | PARTIAL | `centered_text`/`glow_text`/`TextSpec` overloads deprecated; 100+ caller bulk migration OPEN. |
| Authoring facade | WIRED / GUARDED | `asset(path)` è context-typed e kind-free; due `RenderEngine` con root distinti, CWD ostile, font/image logical refs e missing-image fail-loud coperti da test. Gate statico vieta root globali, fallback CWD, resolver nelle composizioni e mega-header. |
| Timeline props | WIRED | `PropsCodec`/`PropsSchema` typed composition props landed; registry resolve ora trasporta il costruttore preparato senza una seconda decode/factory pass. |
| Render job execution | WIRED / GUARDED | Pipeline unica `RenderRequest → RenderJob → execute_render_job(const RenderJob&)`; `ResolvedRenderJob`, conversioni legacy e executor separati vietati dal gate. Suite focalizzata e workflow matrix aggiunti; esecuzione CI NOT RUN/NOT OBSERVED. |
| SDK C++ installabile | PASS baseline / WIRED extension | Gate #10 storico PASS. Nuovo FILE_SET authoring disgiunto, closure gate e consumer installato `check_assets` implementati; nuova estensione non ancora certificata su CI. |
| SDK cross-language | NOT RUN | C ABI e formato `.chronon` da progettare. |
| Render runtime | PASS baseline / WIRED fail-loud | Runtime per-instance certificato nella baseline storica; il bridge SDK propaga `NodeExecutionError` come `RenderError::RuntimeFailure`, con test missing-image non ancora osservato. |
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
| Glow certification (Test GLOW-CERT) | WIRED (HARNESS-COMPLETE) | 13 TEST_CASEs; macchina-verifica DEFERRED-WBH. |
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
| Camera full cert (Test GLOW-CERT sibling) | WIRED | `verify_camera_full_linux.sh` 7-section FAIL-LOUD. |
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
