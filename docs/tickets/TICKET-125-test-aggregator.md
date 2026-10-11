| **TICKET-125 — Engine certification aggregator (Tests 10-16 core subset)** | |
| --- | --- |
| **Stato** | TABLE-COMPLETE — catalog and artifact pointers reconciled with this checkout on 2026-10-11; the underlying certification rows retain their observed states |
| **Priorità** | P0 (catalog coordination; completion tracked independently from certification) |
| **Problema** | I deliverable di ENGINE CERTIFICATION (Tests 10-16 core subset: determinism, fix cronograph, SSoT audit, indexing, scaling, sunset registry) erano distribuiti su file e ticket senza un indice che distinguesse gate presenti, prove raccolte e gap aperti. L'aggregatore rende osservabili deliverable, criteri di verifica e stato attuale senza attribuire PASS a verifiche non eseguite. |
| **Evidenza** | Sei righe di catalogo per Tests 10, 11, 12, 13, 14 e 16; artifact audit sul checkout `e8e06f5b3`; regole canoniche documentali in `AGENTS.md` e blocker index in `docs/FOLLOWUP_TICKETS.md`. |
| **Impatto** | Senza un aggregatore canonico, il push iterativo Rule #5 fallisce per mancanza di SHA-triple lookup atomico; i deliverable di certification rimangono sparsi su commit messages + CHANGELOG + tickets senza una single read-side entry point. Il push cadence PARTIAL già documentato (F2 blocker DEFERRED) richiede questo aggregatore come canonical read-side contract per il working build host verifier. |
| **Confine** | Pure `docs/` artifact (no SDK API surface; no source code modifications; no `include/chronon3d/` edits per Cat-3 anti-duplication). Aggregatore read-only: no live runtime verification on this VPS; per-test runtime eseguibile solo on working build host con `cmake --build` + golden tests + `bash` one-liner per row. **PRODUCT VALIDATION (Tests 8, 9, 15, 17, 18) NON appartiene a questo ticket**: vive in PipelineGen (`../refactored/docs/product-validation/PRODUCT-VALIDATION-AGGREGATOR.md`). Il core non deve sapere cosa sia una founder dashboard. |
| **Soluzione accettabile** | Index table 6 righe con colonne: `#` / `Test scope` / `Deliverable (file / gate)` / `PASS criterion (osservabile)` / `FAIL criterion (osservabile)` / `Stato corrente`. Ogni PASS/FAIL criterion è una `bash` one-liner che termina in `exit 0` o `exit 1` su working build host (no stime percentuali per AGENTS.md §honesty "non segnare verde una suite che restituisce failure"). |
| **Criteri di accettazione** | 1) 6 righe presenti (Tests 10, 11, 12, 13, 14, 16); 2) ogni riga ha un comando verificabile sullo stato degli artefatti/gate presenti; 3) ogni riga riporta lo stato realmente osservato; 4) nessuna stima percentile; 5) i link puntano a file esistenti oppure dichiarano esplicitamente l'assenza; 6) §Product validation pointer per Tests 8/9/15/17/18; 7) annotazione §honesty per le verifiche che richiedono build host; 8) aggiornamento dei soli canonici previsti da AGENTS.md. |
| **Push iterativo cadence** | Il checkout locale `main` è allineato a `origin/main` su `e8e06f5b3`; le note di stato locali sono modifiche non committate. La CI GitHub sullo SHA remoto non alloca runner (vedi [MAIN-TRUTH](TICKET-MAIN-TRUTH-STAGE-1-2.md), run 38118149793). Nessun risultato locale sostituisce quella certificazione; nessun push è stato eseguito. |

## Current artifact audit — 2026-10-11

HEAD `e8e06f5b315f4ffe39e6819e926dcfc1156b8408` was audited against every row.
The table below now distinguishes present gates from absent historical
artifacts. `docs/CHANGELOG.md` and `docs/FEATURE_SUNSET.md` are not canonical
documents in this checkout; their absence is recorded rather than treated as a
catalog failure. No runtime certification was inferred from artifact presence.

**Verdict:** TABLE-COMPLETE. This ticket owns the accuracy and traceability of
the catalog, not completion of each certification suite. Tests whose gate or
evidence is absent remain BLOCKED/NOT RUN in the table and require their own
work before certification.

# Dual-executor verdict

**Verdetto:** `GraphExecutor` è la sola autorità produttiva di esecuzione.
`executor.cpp` decide il percorso (`fully_recorded` compiled program oppure
fallback DAG via `execute_levels`), possiede il lifecycle del frame, pubblica
gli errori e restituisce l'output finale.

`node_runner.cpp`, `executor_levels.cpp` e `node_executor.cpp` sono helper con
ownership non sovrapposta: rispettivamente orchestrazione per-node, scheduling
dei livelli e primitive `RenderGraphNode::execute`/ownership framebuffer.

Aggiornamento Wave 1: il vecchio adapter, il test helper e il simbolo ABI
`execute_command_plan` sono stati rimossi; il gate `check_graph_executor_authority.sh`
verifica zero residue in `src/`, `include/` e `apps/`. `CommandPlan` resta il
solo contratto dati backend-neutral.

# Engine certification — Index 6 deliverable (Tests 10-16 core subset)

| # | Test scope | Deliverable (file / gate) | PASS criterion (osservabile) | FAIL criterion (osservabile) | Stato corrente |
|---|---|---|---|---|---|
| 10 | Determinismo brutale | `tools/check_determinism.sh` (present); self-test reference absent | `bash tools/check_determinism.sh` exits 0 and emits `GATE_PASS: determinism` on the certified build host | nonzero exit or hash disagreement | PARTIAL — gate presente; sweep 140 render non verificata qui |
| 11 | Cronometro del fix | historical gate and JSONL log absent | `test -x tools/check_fix_cronograph.sh && test -f docs/fix_cronograph_log.jsonl` | missing gate/log or gate failure | BLOCKED — referenced gate and log absent |
| 12 | Architecture invariants | `python3 tools/check_architecture.py` (present) | command exits 0 and emits `GATE_PASS` | nonzero exit or any failed rule | PARTIAL — 206/206 static rules passed 2026-10-11; historical dedicated SSoT gate absent |
| 13 | Camera certification | camera gate scripts and `TICKET-TEST-13-CORE.md` absent | `test -x tools/check_camera_architecture.sh && test -x tools/verify_camera_functional_linux.sh` | missing gate, failed gate, golden mismatch, sanitizer finding or nondeterminism | BLOCKED — gates and referenced ticket absent; no runtime certification |
| 14 | Scalabilità lineare | historical gate and self-test absent | `test -x tools/check_linear_scaling.sh && test -x tests/tools/selftest_check_linear_scaling.sh` | missing gate or failed invariant | BLOCKED — referenced gates absent |
| 16 | Registro sunset feature | `docs/FEATURE_SUNSET.md` absent | `test -f docs/FEATURE_SUNSET.md` | missing registry or fewer than five verified entries | BLOCKED — registry absent |

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

Non esiste ancora una scheda canonica per il gap Test 13; la tabella lo segnala
senza presentarlo come ticket già aperto. I ticket di prodotto restano in
PipelineGen:

| Forward-point slug | Canonical file | Stato | Cross-link | Priorità |
|---|---|---|---|---|
| Test 13 camera certification | Nessun file ticket canonico presente | BLOCKED | row 13 in this catalog; create a dedicated ticket when that work is scheduled | P1 |

Forward-pointed product-validation tickets (TICKET-TEST-9-PILOT-7GG, TICKET-TEST-18-WEEKLY-DASHBOARD) vivono ora in PipelineGen `docs/product-validation/`.

# Canonical documentation alignment

| Doc | Section | Edit content |
|---|---|---|
| `docs/FOLLOWUP_TICKETS.md` | Open blockers | remove TICKET-125 after catalog completion; retain only genuinely active blockers |
| `docs/CURRENT_STATUS.md` | Active blockers | remove the TICKET-125 row; the catalog's underlying blocked tests remain stated here |
| `docs/tickets/TICKET-125-test-aggregator.md` | this artifact | catalog and artifact pointers reflect the current checkout |

# §honesty cert discipline

- **§honesty PARTIAL cert su questo commit**: push iterativo Rule #5 SHA-triple NON verificato per F2 infra blocker (`TICKET-INFRA-F2-DIVERGENCE P0`); LOCAL_AHEAD=4 / REMOTE_AHEAD=10 al 2026-07-12.
- **§honesty Test 13**: l'ambiguità è risolta come test distinto (Camera brutal); il runtime resta OPEN/PARTIAL finché i gate camera e sanitizer non sono eseguiti sul WBH.
- **§honesty runtime verification**: Test 10 è PARTIAL perché la sweep completa non è stata eseguita. Test 11, 13, 14 e 16 sono BLOCKED perché i rispettivi gate/registry non sono presenti; nessuno stato deriva da una build o da una suite non eseguita.
- **§honesty product validation split**: i Tests 8/9/15/17/18 sono stati migrati a PipelineGen — nessuna riga di prodotto è rimasta nel core con stato fabbricato.

# Cross-link (anchor to canonical references)

- AGENTS.md §Test 10 / §Test 11 / §Test 12 spec sections + §Cat-3 + §Cat-5 + §honesty + §"Fare PR piccole e mirate" + §"INFO-level diagnostic style" Rule #2 + §"Regole di lint documentale"
- Roadmap / V0.2 milestone (`docs/ROADMAP.md`)
- Canonical baselines: `docs/baselines/main-7eb5c2ba-baseline.md` (11/11 PASS) + `docs/baselines/index.md` (15-baseline TOC)
- Documentation governance: `docs/DOCUMENTATION_GOVERNANCE.md` (canonical vs support doc roles)
- TICKET-INFRA-F2-DIVERGENCE (`docs/FOLLOWUP_TICKETS.md` §Open Blockers P0) — pre-requisito per push iterativo Rule #5
- Product validation aggregator: `../refactored/docs/product-validation/PRODUCT-VALIDATION-AGGREGATOR.md` (PipelineGen)
- Executor ownership and profiling verdict: `docs/EXECUTOR_OWNERSHIP.md`

## Checkout re-audit — 2026-10-11

- `tools/check_single_source_of_truth.sh` and
  `tools/check_architecture_boundaries.sh` do not exist in the current
  checkout. The canonical gate manifest instead registers
  `check_architecture.py`.
- `python3 tools/check_architecture.py` passed **206/206** architecture rules
  on this checkout. This is CPU/static evidence and does not certify the
  historical dedicated 8-concept/4-pattern gate or same-SHA GitHub CI.
- Re-run on 2026-10-11 confirmed 206/206; the text-header fan-out gate also
  passed (19/22 consumers, zero direct Unicode utility consumers). Recorded
  with dirty-worktree scope in [`CPU-ONLY-EVIDENCE.md`](../evidence/CPU-ONLY-EVIDENCE.md).
- The historical baseline remains unverified on the current integration SHA;
  no GPU is required for this local static gate.
