# ExecutionScope e Precomp

## Stato reale

La fondazione `ExecutionScope` è già presente e parte del plumbing produttivo è stata implementata. Il lavoro resta parziale perché non tutti i call site usano ancora il nuovo contratto e alcuni fallback tile conservano sessioni locali.

## Implementato

- [x] Tipo `ExecutionScope` con kind Root, Tile e Precomp.
- [x] Parent chain e depth.
- [x] Arena esplicita per scope.
- [x] Owner key per recursion detection.
- [x] Limite `kMaxScopeDepth` con clamp e `would_overflow()`.
- [x] Test root, child, sibling, grandchild e recursion.
- [x] Overload `GraphExecutor::execute_with_scope`.
- [x] Routing dell'arena tramite `scope.arena()`.
- [x] Reset limitato all'arena passata all'executor.
- [x] Plumbing Root → Tile nei principali percorsi tile.
- [x] Costruzione di scope Precomp figlio nel percorso dedicato.

## Parziale

- [ ] Migrare tutti i call site produttivi dal vecchio overload a `execute_with_scope`.
- [ ] Migrare i call site dei test al nuovo overload.
- [ ] Eliminare i `RenderSession local_session` rimasti nei fallback tile.
- [ ] Riutilizzare sessione e cache parent in tutti i percorsi tile.
- [ ] Garantire che il `ProgramLease` viva per tutta la durata dello scope Precomp.
- [ ] Verificare che teardown figlio non invalidi stato parent.
- [ ] Eliminare ogni fallback identità Precomp che possa aliasare sibling node.
- [ ] Rendere obbligatoria una `NodeIdentity` valida nei percorsi produttivi.

## TODO — migrazione Precomp

- [ ] Inventariare ogni chiamata a `GraphExecutor::execute` nel codice produttivo.
- [ ] Migrare `PrecompNode` al solo contratto scope-aware.
- [ ] Derivare la chiave cache da graph identity e node identity valide.
- [ ] Vietare fallback silenziosi basati su identità invalida.
- [ ] Verificare nested Precomp con composizioni uguali ma nodi differenti.
- [ ] Verificare sibling Precomp con stessa composizione e bucket distinti.
- [ ] Verificare clear concorrente mentre una lease è attiva.
- [ ] Verificare direct e indirect recursion con risultato deterministico.

## TODO — test mancanti

- [ ] Test memory lifetime root → tile → precomp.
- [ ] Test parent arena non resettata dal child.
- [ ] Test session/cache reuse nei fallback tile.
- [ ] Test race su sibling Precomp.
- [ ] Test call-site guard che impedisca il ritorno al vecchio overload.
- [ ] Test boundary che vieti executor locali nei nodi.

## Completato quando

- tutti i call site produttivi usano `ExecutionScope`;
- root, tile e precomp condividono solo servizi intenzionalmente condivisi;
- ogni child possiede memoria temporanea isolata;
- nessun child invalida il parent;
- due Precomp distinti non condividono identità o cache per errore;
- i test memory, race, recursion e lease sono verdi.