# TICKET-044 — arch_boundaries_selftest hardcoded paths

## Stato
OPEN

## Priorità
P1

## Problema
`tools/check_architecture_boundaries_selftest.sh` ha path hardcoded che impediscono l'esecuzione corretta in ambienti non standard.

## Evidenza
Eseguire il selftest → fallimenti su path non trovati.

## Impatto
Blocca arch-boundary gate 5 → blocca baseline verde.

## Confine
Solo il selftest, non il gate principale.

## Soluzione accettabile
Rendere i path relativi o configurabili, o sostituire con rilevamento automatico.

## Criteri di accettazione
- Selftest PASS su checkout pulito
- Nessuna modifica al comportamento del gate principale

## Collegamenti
- Gate: arch-boundary (gate 5)
- Tool: tools/check_architecture_boundaries_selftest.sh
- Milestone: M0
