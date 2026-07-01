# TICKET-046 — filename drift stale references

## Stato
OPEN

## Priorità
P1

## Problema
`tools/check_filename_drift.sh` rileva riferimenti stale a file rinominati o spostati.

## Evidenza
Eseguire `bash tools/check_filename_drift.sh` — output mostra riferimenti non aggiornati.

## Impatto
Blocca arch-boundary gate 5 → blocca baseline verde.

## Confine
Solo riferimenti nei file di documentazione e script. Non tocca codice C++.

## Soluzione accettabile
Aggiornare tutti i riferimenti ai percorsi correnti.

## Criteri di accettazione
- check_filename_drift.sh → PASS
- Nessun riferimento a file inesistenti

## Collegamenti
- Gate: arch-boundary (gate 5)
- Tool: tools/check_filename_drift.sh
- Milestone: M0
