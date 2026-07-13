# TICKET-MAIN-TEST-EQUALS-INVESTIGATION — Hypothetical silent-failure request deferred (no code)

## Stato: DEFERRED (2026-07-13, hypothetical disposition)

## Origine richiesta
L'utente ha richiesto in sessione:
> "Fixare il silent failure su `main_test::equals`. Trova la causa radice
> (probabilmente un early-return che maschera un fallimento di assertion),
> correggi, commit + push via `wrap_push.sh` + SHA selfcheck."

## Verifica codebase (eseguita in questa sessione)
Due sweep esaustivi in `tests/`, `src/`, `include/`, `apps/`, `tools/`, e `git history`:

1. `grep -rnE 'main_test::equals'` → **0 hit**
2. `grep -rnE 'namespace main_test|class main_test|main_test::'` → **0 hit**
3. `grep -rnEi 'main_test|main-test|MainTest'` (case-insensitive) → **0 hit**
4. `find tests -name '*.hpp' -exec grep -lE 'main_test|MainTest|equals' {} \;` → **0 hit**
5. `grep -rnE 'bool equals|::equals\(|equals =' tests/` → 0 hit
   (Chronon3D usa `operator==` canonico, non pattern `equals()` method)
6. `git log --all --grep='main_test\|equals'` → 0 commit referenziano questi pattern
7. Substr `equals` presente solo in TEST_CASE descriptions
   (es.: `"shade_ndotl on back-facing normal equals ambient"` — nomi descrittivi,
   non simboli funzione)

**Conclusione oggettiva**: il simbolo `main_test::equals` non esiste nel
codebase Chronon3D.

## Disposizione (decisione utente: "Richiesta ipotetica / no code")
Per rispettare AGENTS.md §honesty (`Non segnare verde una suite che
restituisce failure`, `Non inventare`, `Don't surprise the user`):

- **NON** applicare alcuna fix cieca a un simbolo inesistente
  (rot-pattern §honesty-violation, inventerebbe codice senza base).
- **NON** scrivere codice.
- **NON** aprire un PR contro `main` (preserverebbe rot §honesty).
- **NON** aggiornare canonical (`CURRENT_STATUS.md`, `FOLLOWUP_TICKETS.md`,
  `CHANGELOG.md`, `ROADMAP.md`) — la richiesta è ipotetica, NON modifica stato
  di area, NON apre/chiude blocker, NON è milestone
  (vedi AGENTS.md §`Disciplina di aggiornamento dei canonici`).
- **SÌ** creare questo ticket Cat-5 come audit trail della richiesta.

## Decisioni D1–D3 (thinker verdict, già prodotte in sessione)

| ID    | Verdict | Rationale                                                            |
| ----- | ------- | -------------------------------------------------------------------- |
| **D1** | (a) Report absence + use `ask_user` per chiarimento                     | unica opzione AGENTS.md §honesty-compliant                           |
| **D2** | Candidate files: `tests/text/`, `tests/scene/`, `tests/sabotage/`, `tests/pipeline/`, `tests/install_consumer/main.cpp` | per indagine futura se utente fornisce simbolo reale |
| **D3** | (ipotetica) → NO code, solo questo ticket di tracking                    | preserva rot §honesty + GATE-MNT-01 baseline verde                  |

## Forward-point (se utente torna con simbolo corretto)
Se in futuro l'utente fornisce il nome del simbolo reale (es.: un vero test
case in `tests/` che mostra silent failure), questo ticket è il riferimento
canonico audit-trail. Le tre ipotesi di lavoro plausibili:

1. **Cat-5 ticket per rot simbolo-specifico**: investigare e fixare il
   silent-failure pattern nel file `:line:col` reale
   (pattern probabile = `if (assert_passed) { ... } else { /* silent skip */ }`).
2. **Cat-3 minimal-surface fix**: sostituire early-return mascherante con
   doctest `FAIL(...)` esplicito, oppure assertion macro fail-loud
   (es.: `#define CHRONON_DOCTEST_FAIL_ON_EARLY_RETURN` sezione header).
3. **Cat-5 forward-point (lint-tool)**: aggiungere
   `tools/check_silent_assertion_skip.sh` che rileva pattern
   early-return-after-`REQUIRE`/`CHECK`
   (anti-duplication con i cat-5 futuri silent-failure tickets;
   convenzione `[INFO] <gate-name>: ...` su PASS addizionale al
   canonico `GATE_PASS`, vedi AGENTS.md §`Regole di lint documentale`).

## Criteri di chiusura (futuri)
- [ ] ticket in attesa di chiarimento dal utente.
- [ ] HEAD baseline verde al commit di creazione di questo ticket
      (snapshot pre-implementazione).
- [ ] nessun codice committato (nessun fix applicato).
- [ ] nessuna rot §honesty introdotta.
- [ ] nessun canonical doc aggiornato
      (richiesta ipotetica = out of scope AGENTS.md §`Disciplina di
      aggiornamento dei canonici`).

## Riferimenti
- AGENTS.md §`Disciplina di aggiornamento dei canonici`
  (richiesta ipotetica → no canonical churn).
- AGENTS.md §`Test binary staleness check (honesty, pre-ctest invariant)`
  (anche se non applicato qui, principio §honesty condiviso).
- AGENTS.md §GATE-MNT-01 (Mantenere baseline verde: proibito push RED).
- Thinker verdict in-sessione (decisioni D1/D2/D3).
