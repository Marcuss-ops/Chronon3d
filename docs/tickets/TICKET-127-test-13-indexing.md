<table>
<tr><th colspan="2">TICKET-127 — Test 13 Indexing (forward-point stub, L1 P0 follow-up)</th></tr>
<tr><th>Stato</th><td>OPEN (stub — L1 P0 follow-up per code-reviewer final verdict on TICKET-125)</td></tr>
<tr><th>Priorità</th><td>P0 (resolutione pre-requisito per next aggregator refresh; the 2-interpretation ambiguity MUST be resolved before next TICKET-125 cycle)</td></tr>
<tr><th>Problema</th><td>Test 13 ha 2 interpretazioni canoniche mutualmente esclusive: (a) Test 13 = orchestrator alias for Test 11 (Test #13 in <code>tools/first_principles_product_check.sh</code> enum = same as Test 11 §Cronometro del fix); (b) Test 13 = separate-framework-slot-distinct-from-Test-11 (unfulfilled slot nel First-Principles Product Check framework). TICKET-125 row 13 carries EVIDENCE-GAP marker onesto + this stub ticket.</td></tr>
<tr><th>Evidenza</th><td><strong>Canonical <code>AGENTS.md</code> grep returns 0 hits</strong> per <code>Test 13</code> / <code>TEST-13</code> / <code>Test #13</code> within the canonical documentation set (verified via <code>grep -rn 'Test 13\|TEST-13' AGENTS.md docs/</code> at HEAD <code>e2baead5</code>). The only Test #N references in the orchestrator are Test #1 (Product demo) + Test #4 (Brutal elimination) + Test #5 (SSOT) + Test #6 (Determinism) + Test #7 (Multilingual) + Test #8 (Fail-loud) + Test #10 (Legacy grep) + Test #11 (Real cost / reclassified to Fix speed) + Test #12 (Scale 100 batch) + Test #13 (Camera brutal — orchestrator enum entry). The orchestrator's Test #13 = "Camera brutal" which is a 7-entry stub-only section header (NOT yet a deliverable); resolution via maintainer review.</td></tr>
<tr><th>Impatto</th><td>Without resolution, TICKET-125 11-row index carries a 1-row LANDMINE: depending on interpretation (a) vs (b), the aggregator either drops a row (alias → DUPLICATE) or surfaces an unfulfilled slot (separate framework → NEW TICKET-TEST-13-CORE deliverable). Resolutione pre-requisito before next aggregator refresh.</td></tr>
<tr><th>Confine</th><td>Pure <code>docs/</code> artifact (no SDK API surface; no <code>include/chronon3d/</code> edits per Cat-3). Resolutione scope: regrep <code>AGENTS.md</code> broader regex (incl. <code>Test \#[0-9]+</code>, <code>git log --all --grep='Test 13'</code>) + confirm con maintainer (open question).</td></tr>
<tr><th>Soluzione accettabile</th><td><strong>Branch (a)</strong> — IF Test 13 = alias-for-Test-11: refactor TICKET-125 row 13 from `EVIDENCE-GAP` to `ALIAS-FOR-TEST-11 (DUPLICATE)` + close this stub as DUPLICATE; <strong>Branch (b)</strong> — IF Test 13 = separate-framework-slot-distinct-from-Test-11: open separate <code>TICKET-TEST-13-CORE</code> deliverable per fill the framework slot (Camera brutal = orchestrator stub; required deliverable: brutal-camera-benchmark gate per TICKET-PROJECTION-V1 §catastrophic envelope). Either branch closes this stub ticket.</td></tr>
<tr><th>Criteri di accettazione</th><td>(1) Maintainer confirm (a) vs (b) per open question; (2) IF (a): TICKET-125 row 13 refactored + this stub closed as DUPLICATE + the Test 11 row carries a `+ Test #13 alias` footnote; (3) IF (b): NEW <code>TICKET-TEST-13-CORE</code> ticket opened per Camera brutal deliverable + this stub closed as RESOLVED + TICKET-125 row 13 refactored to `OPEN (slot-filled by TICKET-TEST-13-CORE)`; (4) Cat-5 3-doc aligned on resolution.</td></tr>
</table>

# Resolution path (decision deadline: pre next aggregator refresh)

1. **regrep `AGENTS.md` broader regex**: `grep -rnE 'Test \#[0-9]+|TEST-NN|Test [0-9]+' AGENTS.md docs/`
2. **cross-check orchestrator enum entries**: `grep -nE '^#|== |Test #' tools/first_principles_product_check.sh`
3. **maintainer review** (open question): which interpretation (a) or (b) is canonical?
4. **decision** → branch (a) refactor OR branch (b) new TICKET

# Cross-link

- TICKET-125 row 13 (EVIDENCE-GAP marker onesto)
- `tools/first_principles_product_check.sh` (orchestrator enum = Test #13 = "Camera brutal" stub-only header)
- `tools/check_camera_architecture.sh` (Camera V1 architecture boundary gate — the natural substrate per branch (b) resolution)
- Test 11 entry (the alias hypothesis per branch (a))
- AGENTS.md §honesty + §"Fare PR piccole e mirate"

# §honesty stub-cert

- File scritto come Cat-2 canonical stub per evitare phantom reference (this ticket filename viene referenziato in TICKET-125).
- La substance del resolution path è documentata onestamente senza pre-giudicare maintainer review.
- No fabrication: la riga "OPEN (stub — L1 P0 follow-up)" è onesta.
