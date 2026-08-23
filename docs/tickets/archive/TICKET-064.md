# TICKET-064 — ExecutionScope — ScopeError/ScopeErrorCode structured error model

## Stato
PARTIAL

## Priorità
P1

## Problema
`ExecutionScope` (§9) necessita di un modello di errore strutturato con `ScopeError`/`ScopeErrorCode` (preparazione PR 6.8).

## Evidenza
`include/chronon3d/core/execution/scope_error.hpp` esiste ma non ancora integrato completamente.

## Impatto
Blocca arch-boundary gate 5. Error handling non uniforme nell'execution scope.

## Confine
Solo il modello di errore di ExecutionScope. Non tocca altri componenti.

## Soluzione accettabile
Integrare `ScopeError`/`ScopeErrorCode` in tutti i punti di errore dell'ExecutionScope.

## Criteri di accettazione
- Modello di errore uniforme in tutto ExecutionScope
- Test error handling invariati
- Nessuna regressione

## Collegamenti
- Gate: arch-boundary (gate 5)
- File: include/chronon3d/core/execution/scope_error.hpp
- Milestone: M0
