# TICKET-011 — pre-existing mainline build rot

## Stato
OPEN

## Priorità
P0

## Problema
Build rot pre-esistente su `main` che impedisce la compilazione pulita di alcuni target.

## Evidenza
`cmake --build` su preset Linux fallisce per target specifici.

## Impatto
Blocca arch-boundary gate 1–8 → blocca baseline verde. Impedisce l'esecuzione dei test.

## Confine
Solo fix di build (include mancanti, link, dipendenze CMake). Nessuna modifica funzionale.

## Soluzione accettabile
Ripristinare la compilazione pulita di tutti i target richiesti dai gate.

## Criteri di accettazione
- Build core PASS
- Build lean PASS
- Test core PASS
- Nessuna modifica a `include/chronon3d/`

## Collegamenti
- Gate: arch-boundary (gate 1–8)
- Milestone: M0
