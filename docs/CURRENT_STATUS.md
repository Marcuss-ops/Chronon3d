# Chronon3D — Current Status

> **Snapshot:** `main@7dc553e5` — post `feat(cli): add shared flat props-file loader` (current HEAD, 2026-07-15; cumulative 39-commit lineage from `main@5246d7bb` spanning CLI V3 unification, Text Blocco 5.1/5.2 deprecations, Authoring facade, Timeline PropsCodec, RenderJob execution-complete, Camera continuous-time params, and Benchmark corpus). Baseline verde certificata `main@7eb5c2ba` **11/11 PASS** ✅. Feature freeze V0.1 revocato 2026-07-06. Linux-only. Cronologia dettagliata in [`docs/ARCHIVE/CURRENT_STATUS_HISTORY.md`](docs/ARCHIVE/CURRENT_STATUS_HISTORY.md).


## Active Blockers (top 3)

| ID | Area | Stato | Scheda |
|---|---|---|---|
| TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX | docs | OPEN | [TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX](tickets/TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX.md) |
| TICKET-125-TEST-AGGREGATOR | testing | OPEN | [TICKET-125](tickets/TICKET-125-test-aggregator.md) |
| TICKET-TEST-FONT-ASSET-PATH | assets | OPEN | [TICKET-TEST-FONT-ASSET-PATH](tickets/TICKET-TEST-FONT-ASSET-PATH.md) |

Per dettaglio: [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md). Cronologia ticket chiusi: [`docs/CHANGELOG.md`](docs/CHANGELOG.md).

## Stato generale per area

| Area | Stato | Note sintetiche |
|---|---|---|
| CLI V3 unification | PARTIAL | `preview`, `watch`, `create` landed; `still`/`video` deprecated aliases; props-file loader wired; macchina-verifica DEFERRED-WBH.
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
| Text Production V1 | PASS | Text Export V1 certified. Clip 06 closed. FU04 contract closed. |
| Text API migration (Blocco 5.1/5.2) | PARTIAL | `centered_text`/`glow_text`/`TextSpec` overloads deprecated; 100+ caller bulk migration OPEN. |
| Authoring facade | WIRED | Scene builder forwarders + context-typed `asset()` landed; macchina-verifica DEFERRED-WBH.
| Timeline props | WIRED | `PropsCodec`/`PropsSchema` typed composition props landed; macchina-verifica DEFERRED-WBH.
| Render job execution | WIRED | `RenderJob` execution-complete; separate `RenderJobPlan` retired; macchina-verifica DEFERRED-WBH.
| SDK C++ installabile | PASS | gate #10 PASS (sub-blocks A+B+C). |
| SDK cross-language | NOT RUN | C ABI e formato `.chronon` da progettare. |
| Render runtime | PASS | ImageCache + RenderSession::layout_cache landed. |
| Composition pipeline | PASS | Canonical pipeline documented; Sequence V2 + Asset Readiness code-complete. |
| CompositionDescriptor migration | PARTIAL | `add(name, factory)` deprecated (ADR-027); 200+ legacy callers remain; Chore B bulk migration OPEN.
| Video pipeline | PASS | Structured error reporting (13 codes); atomic output; 98 video tests pass. |
| CI infrastructure | PASS | Sanitizers nightly/weekly; renderer-boundary gate; test-hygiene 3 invariants; CI status JSON artifact. |
| Test coverage | PASS | 5×5 deterministic matrix; 5×5 SafeArea matrix; 5 layout TEST_CASEs. |
| Benchmark corpus | WIRED | 12-scene YAML corpus B00-B11 + sanity test harness landed; macchina-verifica DEFERRED-WBH.
| Auto-fit (ADR-018) | PARTIAL | engine-level DONE; canonical wrapper forward-pointed (ADR-gated). |
| Sistemi meta (Expressions V2 / V3) | PLANNED | V2 OFF di default; V3 subordinato a V1. |
| 10-point friction audit | DONE (2026-07-08) | Lineage closed. |
| SDK Product V1 (manifest + image-layer) | PASS | forward-points 0e+0f+0g+0h+ closed. |
| Glow certification (Test GLOW-CERT) | WIRED (HARNESS-COMPLETE) | 13 TEST_CASEs; macchina-verifica DEFERRED-WBH. |
| Fail-loud errors (Test #7) | WIRED | `check_first_principles_fail_loud.sh` + 5 fixtures + orchestrator section. |
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
| Diagnostics cert (Test P2) | WIRED | `verify_diagnostics_linux.sh` 10-class + 7-field contract. |
| Determinism spec completeness (amend) | WIRED | `verify_determinism_linux.sh` 4→6 invariants. |
| Compositing spec completeness (amend) | WIRED | `verify_compositing_effects_linux.sh` 10→14 effects. |
| Camera full cert (Test GLOW-CERT sibling) | WIRED | `verify_camera_full_linux.sh` 7-section FAIL-LOUD. |
| SDK consumer functional (Test P1 sibling) | WIRED | `verify_sdk_consumer_functional_linux.sh` 6-surface + 6-isolation. |
| Render runtime cert (Test P3) | WIRED | `verify_render_runtime_linux.sh` 4 distinct sha256. |
| Asset preflight cert (Test #7 sibling) | WIRED | `verify_asset_preflight_linux.sh` 10 sabotage scenarios. |
| Timeline functional cert (Test P1) | WIRED | `verify_timeline_functional_linux.sh` 10 TEST_CASEs. |
| Chronon product cert (orchestrator) | WIRED | `verify_chronon_product_linux.sh` 14 sub-gates. |
| Error handling cert | WIRED | `verify_error_handling_linux.sh` 10 scenarios. |
| Performance cert | WIRED | `verify_performance_linux.sh` 5 scenarios + leak test. |
| Sanitizer cert | WIRED | `verify_sanitizer_linux.sh` 0 OOB + 0 UAF + 0 UB + 0 data races. |
| TICKET-125 (Test aggregator Tests 8-18) | PARTIAL | 11-row index Tests 8-18 con PASS/FAIL criterion. Vedi [TICKET-125](tickets/TICKET-125-test-aggregator.md) + forward-points TICKET-TEST-9-PILOT-7GG + TICKET-TEST-13-INDEXING + TICKET-TEST-18-LONG-FORM-CONTENT. |
| Test 18 founder dashboard | OPEN | Weekly scorecard aggregator + 8 metriche. |

## Gate Audit — ultima verifica

**`main@ef9c83f1` — 14/14 + 1 forward-pointed (BLOCKED)** (2026-07-12, Global DoD Sign-off baseline): orchestrator `tools/verify_chronon_product_linux.sh` end-to-end dry-run: 14/14 executable sub-gates PASS + 1 forward-pointed `verify_diagnostics_linux` (TICKET-VERIFY-DIAGNOSTICS-LINUX) = `CHRONON_PRODUCT_FUNCTIONAL_BLOCKED` (exit 2). Per-gate log: `/tmp/chronon3d_product_cert.log`. Full audit: [`docs/baselines/main-ef9c83f1-baseline.md`](docs/baselines/main-ef9c83f1-baseline.md). §honest-limitation: BLOCKED reported honestly per AGENTS.md; the 15th gate (verify_diagnostics_linux) is IMPLEMENTED but uses `forward_point_gate` instead of `run_gate` (TICKET-VERIFY-DIAGNOSTICS-ORCHESTRATOR-WIREIN forward-point).

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
