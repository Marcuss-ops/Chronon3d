<table>
<tr><th colspan="2">TICKET-128 — Test 18 Long-form content (forward-point stub, P2 post-F2)</td></td></tr>
<tr><th>Stato</th><td>NOT-YET-OPENED (stub — forward-pointed from TICKET-125 row 18; scope TBD post-F2-RESOLVED)</td></tr>
<tr><th>Priorità</th><td>P2 (post-F2-RESOLVED cycle; not on the current push-boundary chain)</td></tr>
<tr><th>Problema</th><td>Test 18 (Long-form content post-M1.8 surface) non ha ancora uno scope canonico definito. TICKET-125 row 18 carries NOT-YET-OPENED marker onesto + this stub ticket. Differenzia da Test 17 (Confronto diretto) per la timeline-axis: Test 17 = one-shot benchmark 8 metriche × 3 prodotti; Test 18 = long-form content focus (probabilmente ≥10min video rendering surface + kinetic typography + multi-scene composition + audio STAGED per §honesty); scope TBD.</td></tr>
<tr><th>Evidenza</th><td>TICKET-125 row 18 (NOT-YET-OPENED marker onesto). The M1.8 Text Simplicity plan (`docs/TEXT_SIMPLICITY_ACTION_PLAN.md`) + Test 15 (Test del prodotto — non-developer evidence) + Test 17 (Confronto diretto) collectively surface the long-form content axis but no specific Test 18 spec exists in AGENTS.md at HEAD. Forward-point post-F2-RESOLVED.</td></tr>
<tr><th>Impatto</th><td>Senza uno scope canonico Test 18, l'aggregator TICKET-125 11-row index è structurally incomplete (1 row carries NOT-YET-OPENED marker). Dopo F2-RESOLVED + post-M1.8 close, Test 18 scope becomes plannable: long-form video rendering surface + multi-scene composition + audio STAGED per §honesty (no `scene.audio()` API surface exists).</td></tr>
<tr><th>Confine</th><td>Pure <code>docs/</code> artifact (no SDK API surface; no <code>include/chronon3d/</code> edits per Cat-3). Scope + deliverable definition deferred to post-F2-RESOLVED cycle per AGENTS.md §"Fare PR piccole e mirate".</td></tr>
<tr><th>Soluzione accettabile</th><td>POST-F2-RESOLVED + POST-M1.8-CLOSE: open Test 18 spec in a follow-up session. Candidate scope (subject to maintainer review): (1) extended Chronon3D vs Remotion comparison on long-form (≥10min) video axis; (2) audio staging per §honesty (no `scene.audio()` API surface — placeholder asset per Cat-3); (3) multi-scene composition benchmark (≥3 scene transitions per video); (4) acceptance test survey (similar to Test 15 HARNESS-COMPLETE pattern but for long-form). Each candidate requires an ADR + a spec verbatim.</td></tr>
<tr><th>Criteri di accettazione</th><td>Post-F2: (1) maintainer spec verbatim per Test 18 surface + deliverable definition; (2) one of the 4 candidate scopes opened as Test 18 cat-4 ancillary ticket + 3 NEW <code>docs/product-tests/</code> harness docs; (3) TICKET-125 row 18 refactored from `NOT-YET-OPENED` to `OPEN (Test-18-<SCOPE>)`; (4) this stub closed as RESOLVED; (5) Cat-5 3-doc aligned on close.</td></tr>
</table>

# Cross-link

- TICKET-125 row 18 (NOT-YET-OPENED marker onesto)
- `docs/TEXT_SIMPLICITY_ACTION_PLAN.md` (M1.8 — Text Simplicity plan)
- Test 15 HARNESS-COMPLETE parallel (4 docs landed commit `16855f33`)
- Test 17 TABLE-COMPLETE row (the 24-cell Confronto diretto; Test 18 = long-form-axis extension)
- AGENTS.md §honesty + §"Fare PR piccole e mirate" + §Cat-3 audio STAGED placeholder pattern

# §honesty stub-cert

- File scritto come Cat-2 canonical stub per evitare phantom reference (this ticket filename viene referenziato in TICKET-125).
- Test 18 scope deferred onestamente; no fabrication: la riga "NOT-YET-OPENED (stub — forward-pointed post-F2-RESOLVED cycle)" è onesta.
- §honesty "non inventare" honored: this stub declares no scope candidate resolution preference.
