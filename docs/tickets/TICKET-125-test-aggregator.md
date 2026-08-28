| **TICKET-125 — Engine certification aggregator (Tests 10-16 core subset)** | |
| --- | --- |
| **Stato** | PARTIAL (catalog landed; per-test runtime verification deferred to working build host per F2/TICKET-INFRA-F2-DIVERGENCE blocker) |
| **Priorità** | P0 (catalog coordination under the 11/11 canonical gate baseline `main@7eb5c2ba`) |
| **Problema** | I deliverable di ENGINE CERTIFICATION (Tests 10-16 core subset: determinism, fix cronograph, SSoT audit, indexing, scaling, sunset registry) sono distribuiti su commit/changelog/tickets eterogenei senza un indice unico che garantisca tracciabilità per push iterativo (Rule #5 SHA-triple). Manca un aggregatore canonico che dichiari per ogni test: deliverable osservabile, PASS/FAIL criterion eseguibile come `bash` one-liner on working build host, stato corrente osservabile, forward-pointed ticket per gap onesti. |
| **Evidenza** | AGENTS.md §"Test 8 — Test 18" specifiche verbatim sparse + `docs/CHANGELOG.md` prepended entries (Test 10 GATE-WIRED+selftest+PARTIAL VPS / Test 11 GATE-WIRED+§honesty PARTIAL / Test 12 GATE-WIRED+EXERCISABLE on VPS / Test 14 GATE-WIRED+selftest+PARTIAL VPS / Test 16 REGISTRY-COMPLETE) + `docs/FOLLOWUP_TICKETS.md` §Open Blockers rows per i forward-pointed (TICKET-INFRA-F2-DIVERGENCE P0 + TICKET-SUNSET-VERIFY P0) + `docs/CURRENT_STATUS.md` §Stato per area rows post-§Hygiene. |
| **Impatto** | Senza un aggregatore canonico, il push iterativo Rule #5 fallisce per mancanza di SHA-triple lookup atomico; i deliverable di certification rimangono sparsi su commit messages + CHANGELOG + tickets senza una single read-side entry point. Il push cadence PARTIAL già documentato (F2 blocker DEFERRED) richiede questo aggregatore come canonical read-side contract per il working build host verifier. |
| **Confine** | Pure `docs/` artifact (no SDK API surface; no source code modifications; no `include/chronon3d/` edits per Cat-3 anti-duplication). Aggregatore read-only: no live runtime verification on this VPS; per-test runtime eseguibile solo on working build host con `cmake --build` + golden tests + `bash` one-liner per row. **PRODUCT VALIDATION (Tests 8, 9, 15, 17, 18) NON appartiene a questo ticket**: vive in PipelineGen (`../refactored/docs/product-validation/PRODUCT-VALIDATION-AGGREGATOR.md`). Il core non deve sapere cosa sia una founder dashboard. |
| **Soluzione accettabile** | Index table 6 righe con colonne: `#` / `Test scope` / `Deliverable (file / gate)` / `PASS criterion (osservabile)` / `FAIL criterion (osservabile)` / `Stato corrente`. Ogni PASS/FAIL criterion è una `bash` one-liner che termina in `exit 0` o `exit 1` su working build host (no stime percentuali per AGENTS.md §honesty "non segnare verde una suite che restituisce failure"). |
| **Criteri di accettazione** | 1) 6 righe presenti (Tests 10, 11, 12, 13, 14, 16); 2) ogni riga ha un bash one-liner eseguibile; 3) ogni riga ha uno `Stato corrente` ∈ {PASS, FAIL, PARTIAL, NOT RUN, BLOCKED, PLANNED, DONE, OPEN, HARNESS-COMPLETE, GATE-WIRED, REGISTRY-COMPLETE, TABLE-COMPLETE, EVIDENCE-GAP, NOT-YET-OPENED}; 4) nessuna stima percentile; 5) §Forward-pointed tickets section per i ticket aperti (Test 13); 6) §Product validation pointer section per Tests 8/9/15/17/18 (PipelineGen); 7) §Cat-5 alignment section per i 4 doc (CHANGELOG/FOLLOWUP/CURRENT_STATUS/TICKET-125); 8) §honesty annotation §honesty PARTIAL cert in tutte le righe macchina-verifiable-only-on-build-host. |
| **Push iterativo cadence** | **Desired**: commit + push iterativo on `main` per ogni test refinement cycle. **Reality**: push ancora bloccato da F2 (`TICKET-INFRA-F2-DIVERGENCE (P0)` §Open Blocker; LOCAL_AHEAD=4 / REMOTE_AHEAD=10). **Expected outcome**: PARTIAL cert on local main finché F2 non risolto. Cadenza: per-test refinement cycles deferrati a next session per AGENTS.md §"Fare PR piccole e mirate" (ogni test = proprio atomic commit). |

# Engine certification — Index 6 deliverable (Tests 10-16 core subset)

| # | Test scope | Deliverable (file / gate) | PASS criterion (osservabile) | FAIL criterion (osservabile) | Stato corrente |
|---|---|---|---|---|---|
| 10 | Determinismo brutale | `tools/check_determinism.sh` + `tests/tools/selftest_check_determinism.sh` | `bash tools/check_determinism.sh` exits 0 + emits `GATE_PASS: determinism` + all 140 RGBA sha256 hashes resolve to ONE canonical hash across 7 configs × 20 renders (1T/2T/8T Debug Cold + 1T Debug Warm + 1T Release Cold + 2 AXIS-DUP) | exit 1 OR >1 distinct hash | GATE-WIRED + selftest PASS + PARTIAL on env-blocked VPS (selftest fully exercisable; full 140-render sweep deferred to working host per §honesty) |
| 11 | Cronometro del fix | `tools/check_fix_cronograph.sh` + `docs/fix_cronograph_log.jsonl` (append-only fix log) | `bash tools/check_fix_cronograph.sh` exits 0 + emits `GATE_PASS: fix_cronograph`; rolling-mean of last 5 entries: `repro_m<30 AND rca_m<120 AND (test_m+fix_m)<1440` AND NOT (latest entry has `new_tickets>=10 AND adapters>=4 AND verified!="yes"`) | exit 1 OR any rolling-mean target breached OR catastrophic envelope conjunction TRUE | GATE-WIRED + PARTIAL §honesty (2 demonstration JSONL entries with `verified="PARTIAL"` recorded: glow-clipped text_glow.cpp use_geo_transform branch 12m/85m/18m/22m/10m + text-shifted-400px overlay_diagnostic_panels.cpp metrics panel 15m/40m/20m/15m/5m) |
| 12 | Audit single source of truth | `tools/check_single_source_of_truth.sh` wired as gate [24/24] in `tools/check_architecture_boundaries.sh` | `bash tools/check_single_source_of_truth.sh` exits 0 + emits `GATE_PASS: 8/8 concepts + 4/4 specific patterns`; 0 entries in VIOLATIONS bash array; Concept 2 (TextPlacement) count ≤ 200 known-rot cap; Concept 5 (Composition) count ≤ 2 known-rot cap | exit 1 OR VIOLATIONS non-empty OR cap exceeded | GATE-WIRED + EXERCISABLE on this VPS (static analysis tool — no `chronon3d_cli` runtime required); 12/12 audits clean at HEAD per 2026-07-12 verification |
| 13 | Reconciliatione indexing (2-interpretation forward-point) | either (a) orchestrator alias-for-Test-11 (Test #13 in `tools/first_principles_product_check.sh` enum = same as Test 11) OR (b) separate-framework-slot-distinct-from-Test-11 | Resolution per Test #13 forward-point: regrep `AGENTS.md` broader regex (`Test \#13`, `git log --all --grep='Test 13'`) + confirm with maintainer; IF (a): refactor TICKET-125 row 13 to `ALIAS-FOR-TEST-11` + close as DUPLICATE; IF (b): open separate TICKET-TEST-13-CORE | None yet (resolution open per L1 P0 follow-up) | EVIDENCE-GAP (forward-pointed a TICKET-TEST-13-INDEXING / TICKET-127; canonical AGENTS.md grep returns NO matches for "Test 13" — interpretation pending maintainer review) |
| 14 | Scalabilità lineare | `tools/check_linear_scaling.sh` + `tests/tools/selftest_check_linear_scaling.sh` | `bash tools/check_linear_scaling.sh` exits 0 + emits `GATE_PASS: linear_scaling`; 4 N-dims (1/10/50/100) × 5 invariants = 20 measurements all within soft tolerance bands: `time_superlinear@N≤1.5×(N=10)/2.5×(N=100)`, `ram_superlinear@N≤2×baseline`, `cache_bounded@N≤5×baseline`, `error_rate≤1%`, `throughput_non_collapse@100≥25%×throughput@50+≥4××throughput@1` | exit 1 OR any invariant breached | GATE-WIRED + selftest PASS + PARTIAL on env-blocked VPS (selftest fully exercisable; full 161-render sweep deferred to working host per §honesty) |
| 16 | Registro sunset feature | `docs/FEATURE_SUNSET.md` (regola "Tre Non" + scadenza 30gg) | `rg -c '^## ' docs/FEATURE_SUNSET.md` ≥ 3 sections (header + 3-non rule + scadenza) + ≥ 5 entries with `DEFERRED-VERIFY` status | Section count < 3 OR DEFERRED entries < 5 | REGISTRY-COMPLETE (cycle 1: 5 DEFERRED-VERIFY candidati content/common/ + content/ae_parity/ + content/text_placement/ + content/backgrounds/ + content/examples/; zero eliminazioni concrete — forward-pointed a TICKET-SUNSET-VERIFY P0) |

# Product validation pointer (Tests 8, 9, 15, 17, 18 → PipelineGen)

I test di PRODUCT VALIDATION non appartengono al core engine. Sono migrati a PipelineGen (`../refactored/docs/product-validation/PRODUCT-VALIDATION-AGGREGATOR.md`):

| # | Test scope | Nuova casa |
|---|---|---|
| 8 | manual_touches_per_video counter (touchpoint manuali) | PipelineGen `docs/product-validation/PRODUCT-VALIDATION-AGGREGATOR.md` row 8 |
| 9 | Pilota cliente reale (7gg) | PipelineGen `docs/product-validation/TICKET-TEST-9-PILOT-7GG.md` (ex TICKET-126, deleted) |
| 15 | Test del prodotto (non del motore) | PipelineGen `docs/product-validation/PRODUCT-VALIDATION-AGGREGATOR.md` row 15 |
| 17 | Confronto diretto (Chronon3D / pipeline precedente / Remotion v4) | PipelineGen `docs/product-validation/PRODUCT-VALIDATION-AGGREGATOR.md` row 17 |
| 18 | Dashboard settimanale del fondatore (Weekly founder dashboard — 8 metriche) | PipelineGen `docs/product-validation/TICKET-TEST-18-WEEKLY-DASHBOARD.md` (ex TICKET-128, deleted) + `scripts/run_weekly_scorecard.sh` (migrato da `tools/`) |

Il core non deve sapere cosa sia una founder dashboard: telemetry SQLite è prodotto dal motore, l'aggregazione/lettura è responsabilità di PipelineGen.

# Forward-pointed tickets (P0/P1/P2)

Per Cat-2 canonical namespace requirement, ogni forward-pointed ticket referenziato sopra ha un file canonico `docs/tickets/TICKET-NNN-*.md` corrispondente:

| Forward-point slug | Canonical file | Stato | Cross-link | Priorità |
|---|---|---|---|---|
| TICKET-TEST-13-INDEXING | `docs/tickets/TICKET-127-test-13-indexing.md` | OPEN (L1 P0 follow-up) | this TICKET-125 row 13; 2-interpretation reconciliatione per code-reviewer final verdict | P0 (resolutione pre-requisito per next aggregator refresh) |

Forward-pointed product-validation tickets (TICKET-TEST-9-PILOT-7GG, TICKET-TEST-18-WEEKLY-DASHBOARD) vivono ora in PipelineGen `docs/product-validation/`.

# Cat-5 4-doc same-commit alignment

| Doc | Section | Edit content |
|---|---|---|
| `docs/CHANGELOG.md` | prepended at top | `docs(aggregator): TICKET-125 LOCAL-ONLY cert + push blocked F2 ticket` entry (references to TICKET-125 inline) |
| `docs/FOLLOWUP_TICKETS.md` | §Open Blockers | TICKET-125-TEST-AGGREGATOR (P0) row + TICKET-TEST-13-INDEXING (P0) row (references to TICKET-125 inline) |
| `docs/CURRENT_STATUS.md` | §Hygiene | 1-line cite-only row "TICKET-125 — Engine certification aggregator (Tests 10-16 core subset) | PARTIAL catalog landed" with markdown link to this file (cite-only per L3 Cat-3 anti-duplication) |
| `docs/tickets/TICKET-125-test-aggregator.md` | this entire artifact | INDEX + Product validation pointer + Forward-pointed section + Cat-5 alignment section + §honesty cert |

# §honesty cert discipline

- **§honesty PARTIAL cert su questo commit**: push iterativo Rule #5 SHA-triple NON verificato per F2 infra blocker (`TICKET-INFRA-F2-DIVERGENCE P0`); LOCAL_AHEAD=4 / REMOTE_AHEAD=10 al 2026-07-12.
- **§honesty gap markers (non-fabricated)**: Test 13 EVIDENCE-GAP è un marker onesto, no fabrication per AGENTS.md §honesty "non inventare".
- **§honesty PARTIAL per build/ctest verification**: tutti i test che richiedono `cmake --build` + `ctest` runtime (Test 10 / Test 14) sono PARTIAL su questo VPS per §honesty "non segnare verde una suite che restituisce failure".
- **§honesty product validation split**: i Tests 8/9/15/17/18 sono stati migrati a PipelineGen — nessuna riga di prodotto è rimasta nel core con stato fabbricato.

# Cross-link (anchor to canonical references)

- AGENTS.md §Test 10 / §Test 11 / §Test 12 spec sections + §Cat-3 + §Cat-5 + §honesty + §"Fare PR piccole e mirate" + §"INFO-level diagnostic style" Rule #2 + §"Regole di lint documentale"
- Roadmap / V0.2 milestone (`docs/ROADMAP.md`)
- Canonical baselines: `docs/baselines/main-7eb5c2ba-baseline.md` (11/11 PASS) + `docs/baselines/index.md` (15-baseline TOC)
- Documentation governance: `docs/DOCUMENTATION_GOVERNANCE.md` (canonical vs support doc roles)
- TICKET-INFRA-F2-DIVERGENCE (`docs/FOLLOWUP_TICKETS.md` §Open Blockers P0) — pre-requisito per push iterativo Rule #5
- Product validation aggregator: `../refactored/docs/product-validation/PRODUCT-VALIDATION-AGGREGATOR.md` (PipelineGen)