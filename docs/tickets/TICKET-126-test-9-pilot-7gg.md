<table>
<tr><th colspan="2">TICKET-126 — Test 9 Pilot protocol 7gg (forward-point stub)</th></tr>
<tr><th>Stato</th><td>OPEN (stub — forward-pointed from TICKET-125 row 9)</td></tr>
<tr><th>Priorità</th><td>P1 (parallel precedent to Test 15 HARNESS-COMPLETE row; pilot-runtime requires human execution, non-VPS-deferrable)</td></tr>
<tr><th>Problema</th><td>Test 9 (Pilota cliente reale 7gg) richiede un harness canonico (<code>docs/product-tests/TEST-9-pilot-protocol.md</code> + <code>docs/product-tests/TEST-9-feedback-form.md</code>) per catturare evidenza non-developer che il prodotto Chronon3D è utile senza spiegare l'architettura (parallel precedent a Test 15). L'harness attualmente è MISSING — TICKET-125 row 9 carries HARNESS-MISSING marker onesto (no fabrication).</td></tr>
<tr><th>Evidenza</th><td>TICKET-125 row 9 (this catalog) + Test 15 row (HARNESS-COMPLETE row 15) come parallel precedent: 4 docs <code>docs/product-tests/TEST-15-{one-pager,feedback-form,pilot-protocol,transcript-template}.md</code> landed 2026-07-12 commit <code>16855f33</code>. Test 9 mirrors the same harness shape but with a 7-day timeline (vs Test 15's 10-min per-subject).</td></tr>
<tr><th>Impatto</th><td>Senza un harness canonico Test 9, il push iterativo Rule #5 NON ha un deliverable canonico per il push-side (<code>chronon3d_cli render</code> evidence) per il pilota 7gg. Forward-point cat-4 ancillary: <code>docs/product-tests/TEST-9-pilot-protocol.md</code> + <code>docs/product-tests/TEST-9-feedback-form.md</code> + <code>docs/product-tests/TEST-9-transcript-7gg.md</code>.</td></tr>
<tr><th>Confine</th><td>Pure <code>docs/product-tests/</code> artifact (no SDK API surface; no <code>include/chronon3d/</code> edits per Cat-3 anti-duplication). Harness + protocol = HARNESS-COMPLETE; pilot-runtime requires a real user + 7 real clients → DEFERRED per AGENTS.md §honesty "non inventare": non può essere macchina-verified su questo VPS.</td></tr>
<tr><th>Soluzione accettabile</th><td>3 NEW docs under <code>docs/product-tests/</code>: (1) <code>TEST-9-pilot-protocol.md</code> — 7-day timeline SOP with role-balanced cohort (founder 2 + PM 2 + designer 2 + operator 2); (2) <code>TEST-9-feedback-form.md</code> — Q1 better-than-previous + Q2 would-you-use-it + Q3 time-saved-per-video-7gg + verbatim quote + NPS 0-10 + meta data; (3) <code>TEST-9-transcript-7gg.md</code> — 7-day daily diary template that preserves verbatim client responses (roughness is signal). Sharded from Test 15 (10-min per-subject) on the timeline axis ONLY; the verbatim-question shape is identical (CAT-9 mirror = median Q1 ≥ +1 across ≥5 clients).</td></tr>
<tr><th>Criteri di accettazione</th><td>(1) 3 NEW files present in <code>docs/product-tests/</code>; (2) CAT-9 PASS criterion sharpened per Test 15 CAT-15 pattern (median Q1 ≥ +1 across ≥5 clients + median Q2 ≥ +1 + median Q3 ≥ 30 min/video); (3) §honest PARTIAL cert on pilot-runtime phase (deferred to user); (4) Cat-5 3-doc aligned (CHANGELOG prepended + FOLLOWUP_TICKETS row updated + CURRENT_STATUS row added) on close.</td></tr>
</table>

# Cross-link

- TICKET-125 row 9 (HARNESS-MISSING marker onesto)
- Test 15 row in TICKET-125 (HARNESS-COMPLETE parallel precedent — 4 docs landed commit `16855f33`)
- Canonical Test 15 forward-point: 7-day pilot protocol (this ticket is the Test 9 workstream, the suggested-followup from Test 8 CHANGELOG entry)
- AGENTS.md §honesty "non inventare" (harness + protocol = HARNESS-COMPLETE on disk; pilot-runtime = user-execution-required)
- AGENTS.md §Cat-3 + §Cat-5 + §"Fare PR piccole e mirate"

# §honesty stub-cert

- File scritto come Cat-2 canonical stub per evitare phantom reference (this ticket filename viene referenziato in TICKET-125 + (futuro) FOLLOWUP_TICKETS row update).
- Tutta la substance (3 NEW docs + 7-day diary + cohort SOP) deferred a next cycle post-F2-RESOLVED.
- No fabrication: la riga "Stato: OPEN (stub — forward-pointed from TICKET-125 row 9)" è onesta.
