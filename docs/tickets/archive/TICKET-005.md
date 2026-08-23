# TICKET-005 — post-cascade cleanup

## Stato
PARTIAL

## Priorità
P1

## Problema
Cleanup post-cascata di refactoring non completato. Rimangono riferimenti a codice legacy, import non necessari e artifact obsoleti in `src/` e `include/`.

## Evidenza
Code search per pattern legacy in `src/` e `include/`.

## Impatto
Blocca arch-completeness gate 5. Aumenta il debito tecnico e la superficie di manutenzione.

## Confine
Solo cleanup (rimozione dead code, import non usati, artifact obsoleti). Nessuna nuova feature.

## Soluzione accettabile
Rimozione completa di riferimenti legacy, dead code, e import non necessari.

## Criteri di accettazione
- Nessun riferimento a codice deprecato
- Build e test invariati
- Nessuna regressione nei gate

## Collegamenti
- Gate: arch-completeness (gate 5)
- Milestone: M0
- Ticket correlati: TICKET-011
