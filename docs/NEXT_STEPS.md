# Chronon3D — Next Steps Reali

Piano operativo per chiudere lo stato pre-stabile del repository.

## Ordine obbligatorio

1. Correggere `tools/check_architecture_boundaries.sh`: tutti i controlli devono essere eseguiti prima del PASS/FAIL finale e ogni violazione deve produrre exit code non-zero.
2. Rifare i test scheduler usando solo `FrameGraphCompiler`, `CompiledFrameGraph` e scheduler esplicito. Eliminare ogni riferimento a `ExecutionPlanCache` e agli overload raw graph.
3. Riallineare `PrecompNode`: stessa API tra header e implementazione, chiave basata su grafo padre + nodo corrente, lease bloccata, executor preso dalla sessione, nessun executor locale.
4. Eliminare la race su `current_identity`: clonare il contesto prima del dispatch e scrivere l’identità soltanto sul clone per nodo.
5. Introdurre `ExecutionScope` reali per root, tile e precomp. Uno scope figlio non deve mai resettare arena o stato del padre.
6. Chiudere il confine SDK: `Chronon3D::SDK` unico target pubblico, consumer installato obbligatorio in CI, niente include da `src/` o da `experimental/`.
7. Registrare una validazione completa e ripetibile: architecture gate, build core, determinismo, precomp, no-content, install consumer e CI.

## Dopo il P0

- TICKET-002: riparare diagnostics/content a piccoli blocchi, registrando il numero di errori residui dopo ogni PR.
- TICKET-006: correggere il linkage del text backend usando il target canonico, senza duplicare sorgenti.
- TICKET-005: decidere esplicitamente se ripristinare o rimuovere `keyframes()`, poi aggiornare test e documentazione.
- TICKET-008: usare `graph_structure_unchanged` soltanto come hint verificato da hash/topologia, con fallback corretto e benchmark.
- Expressions V2: completare Gate 3 Path A → Path B e poi gli altri gate. Non rimuovere la quarantena prima di tutti gli otto gate.

## Cosa non rifare

- Non reintrodurre `ExecutionPlanCache`.
- Non reintrodurre executor su `RenderGraph` grezzo.
- Non creare nuovi registry, resolver, sampler o cache paralleli.
- Non costruire `GraphExecutor` dentro i nodi.
- Non iniziare V3 prima della chiusura P0.
- Non promuovere Expressions V2 solo perché compila in modalità opt-in.

## Criteri di chiusura

| Area | Prova richiesta |
|---|---|
| Gate architettura | repository pulito verde; fixture con violazione rossa |
| Scheduler | target compilato e test seriale/parallelo/tile ripetuti verdi |
| Precomp | test cache, lease, nested execution e concorrenza verdi |
| Scope | child execution non invalida arena o stato del parent |
| SDK | install, configure, build e run di un consumer esterno |
| CI | esito richiesto registrato sul commit di chiusura |
| Documenti | `STATUS`, `ROADMAP`, ticket e questo file aggiornati insieme |
