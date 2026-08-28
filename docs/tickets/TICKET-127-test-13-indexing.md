<table>
<tr><th colspan="2">TICKET-127 — Test 13 Indexing</th></tr>
<tr><th>Stato</th><td>RESOLVED — Test 13 è un test distinto da Test 11</td></tr>
<tr><th>Priorità</th><td>P0 follow-up chiuso tramite audit documentale</td></tr>
<tr><th>Verdetto</th><td>Test 13 = <strong>Camera brutal</strong>; non è alias di Test 11. Test 11 resta Fix speed / cronometro / render cost.</td></tr>
<tr><th>Evidenza</th><td>L'audit completo trova lo slot <code>== Camera brutal ==</code> in <code>tools/first_principles_product_check.sh</code>. Lo stesso file identifica Test 11 tramite <code>== Costo ==</code> e <code>check_fix_cronograph.sh</code>. L'annotazione legacy della riga Camera brutal diceva erroneamente Test #9; il contratto corretto è ora canonizzato in <code>docs/tickets/TICKET-TEST-13-CORE.md</code>.</td></tr>
<tr><th>Conseguenza</th><td>TICKET-125 row 13 diventa <code>OPEN (slot-filled by TICKET-TEST-13-CORE)</code>; non viene eliminato come duplicato e non viene accorpato a Test 11.</td></tr>
<tr><th>Acceptance</th><td>(1) audit AGENTS/docs/tools eseguito; (2) Test 11 e Test 13 hanno scope distinti; (3) ticket canonico Test 13 creato; (4) TICKET-125 e CURRENT_STATUS aggiornati; (5) contratto eseguibile definito senza nuova authority architetturale.</td></tr>
</table>

# Audit record — 2026-08-28

```text
Test 11 → tools/check_fix_cronograph.sh + tools/measure_render_cost.sh
Test 13 → tools/first_principles_product_check.sh: Camera brutal slot
```

Canonical Test 13 contract: [`TICKET-TEST-13-CORE.md`](TICKET-TEST-13-CORE.md).
