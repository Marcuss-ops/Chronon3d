# TICKET-051 — A4.3 per-preset visual diagnostic

## Stato
OPEN

## Priorità
P1

## Problema
Manca un test diagnostico per-preset che verifichi la correttezza visuale di ogni preset registrato.

## Evidenza
`tests/text/test_text_preset_visual.cpp` esiste ma non ha copertura per-preset completa.  <!-- drift-allow: stale-ref -->

## Impatto
Nessuna garanzia che tutti i preset producano output corretti. Blocca Text V1.

## Confine
Solo test visuali, non modifica ai preset stessi.

## Soluzione accettabile
Test che itera su tutti i preset registrati e verifica output non vuoto + hash deterministico.

## Criteri di accettazione
- Test per-preset eseguibile
- Output PNG non nero per ogni preset
- Determinismo tra run seriali e paralleli

## Collegamenti
- Gate: A4.3 visual
- File: tests/text/test_text_preset_visual.cpp  <!-- drift-allow: stale-ref -->
- Milestone: M1
