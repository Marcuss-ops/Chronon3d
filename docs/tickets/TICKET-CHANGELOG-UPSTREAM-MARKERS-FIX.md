# TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX

## Stato

OPEN — P0 docs

## Problema

Estendere il gate di rilevamento marker di conflitto upstream anche al testo `.md` in modo da prevenire merge di prose con conflict markers.

## Criteri di accettazione

- Il gate rileva marker di conflitto in file `.md` oltre che in sorgenti.
- Nessun falso positivo sui file canonici.
