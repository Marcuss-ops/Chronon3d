# TICKET-024 — camera OrbitMotion world-Z vs camera basis

## Stato
PARTIAL

## Priorità
P1

## Problema
`OrbitMotion` ha un conflitto tra world-Z e camera basis nella rotazione orbitale.

## Evidenza
Code search in `include/camera/` per OrbitMotion.

## Impatto
Comportamento imprevedibile della camera orbitale. Blocca camera path.

## Confine
Solo OrbitMotion. Non tocca altri movimenti camera.

## Soluzione accettabile
Risolvere l'ambiguità world-Z vs camera basis con un contratto esplicito.

## Criteri di accettazione
- Contratto documentato e testato
- Test camera invariati per altri movimenti
- Nessuna regressione

## Collegamenti
- Area: include/camera/
- Ticket correlati: TICKET-022
- Milestone: M0, M2
