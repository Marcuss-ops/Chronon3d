# TICKET-TEST-13-CORE — Camera brutal certification

| Campo | Valore |
|---|---|
| Stato | OPEN (slot Test 13 risolto come test distinto) |
| Priorità | P1 |
| Owner | Chronon3D engine certification |
| Scope | Camera projection/resolution correctness, determinism e failure envelope |

## Verdict dell’audit

Test 13 **non è un alias di Test 11**.

- Test 11 = fix speed / cronometro / render cost (`tools/check_fix_cronograph.sh`, `tools/measure_render_cost.sh`).
- Test 13 = camera brutal, presente come slot distinto nel runner
  `tools/first_principles_product_check.sh`.
- Il runner aveva una label errata (`Camera brutal` commentata `Test #9`); questa
  viene corretta concettualmente da questo ticket senza creare una seconda
  camera authority.

## Contratto eseguibile

Il gate Test 13 deve essere eseguibile sul working build host con:

```bash
bash tools/check_camera_architecture.sh
bash tools/verify_camera_functional_linux.sh
```

Il contratto minimo richiede:

1. camera statica con projection valida;
2. camera animata su position/rotation/zoom o FOV;
3. near/far plane valide e rifiuto fail-loud dei parametri non validi;
4. render golden statico + animato;
5. stesso SHA di sorgente, binary e asset per i run comparati;
6. ASAN/UBSAN sul corpus camera;
7. nessuna regressione di `FrameDelta`/dirty state;
8. output deterministico tra run ripetuti.

PASS richiede exit 0 da entrambi i gate e output camera/golden disponibili.
FAIL è qualunque exit non-zero, mismatch golden, sanitizer finding o output
non deterministico. Se binary, asset o corpus non sono disponibili, il risultato
è BLOCKED/NOT RUN, mai PASS.

## Non-scope

- Non crea un nuovo scheduler, registry, resolver o camera authority.
- Non modifica Test 11 e non duplica il suo cronometro.
- Non certifica camera advanced completa: quella resta PARTIAL finché baseline
  same-SHA, sanitizer e consumer SDK non sono tutti verdi.

## Acceptance

- [ ] Label Test 13 distinta da Test 11 nel catalogo.
- [ ] `TICKET-125` punta a questo contratto.
- [ ] `TICKET-127` chiuso come resolved/distinct-test.
- [ ] `CURRENT_STATUS` riporta la decisione.
- [ ] Esecuzione su WBH documenta SHA, toolchain, exit code e artifact.
