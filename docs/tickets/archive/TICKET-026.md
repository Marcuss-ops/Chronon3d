# TICKET-026 — camera MotionBlurSettings Mode duality

## Stato
PARTIAL

## Priorità
P1

## Problema
`MotionBlurSettings` ha una dualità di Mode che crea ambiguità nel contratto.

## Evidenza
Code search in `include/camera/` per MotionBlurSettings.

## Impatto
Blocca arch-boundary gate 5. Rischio di motion blur inconsistente.

## Confine
Solo MotionBlurSettings. Non tocca altri componenti camera.

## Soluzione accettabile
Unificare il Mode in un solo contratto esplicito o documentare chiaramente la dualità.

## Criteri di accettazione
- Contratto Mode unificato e documentato
- Test motion blur invariati
- Nessuna regressione

## Collegamenti
- Gate: arch-boundary (gate 5)
- Area: include/camera/
- Milestone: M0, M2
