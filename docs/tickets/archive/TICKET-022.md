# TICKET-022 — camera double look-at compiled path

## Stato
PARTIAL

## Priorità
P1

## Problema
Codice camera con doppio percorso di look-at compilato che crea duplicazione e potenziale inconsistenza.

## Evidenza
Code search in `include/camera/` per pattern look-at duplicati.

## Impatto
Blocca arch-boundary gate 5/6. Rischio di comportamento divergente tra i due percorsi.

## Confine
Solo il percorso di look-at nella camera. Non tocca altri componenti camera.

## Soluzione accettabile
Unificare in un solo percorso canonico di look-at.

## Criteri di accettazione
- Un solo percorso di look-at
- Test camera invariati
- Parity test tra vecchio e nuovo percorso

## Collegamenti
- Gate: arch-boundary (gate 5/6)
- Area: include/camera/
- Milestone: M0, M2
