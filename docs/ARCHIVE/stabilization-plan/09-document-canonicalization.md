# Canonicalizzazione dei documenti

## Problema

Alcune aree avevano due documenti che descrivevano lo stesso lavoro, creando drift e checkbox contraddittori.

## Regola canonica

- `docs/01-baseline-green.md`: fonte canonica del baseline verde.
- `docs/02-determinism.md`: fonte canonica del contratto determinismo.
- `docs/03-execution-scope-and-precomp.md`: fonte canonica del contratto scope.
- `docs/STATUS.md`: stato corrente sintetico e verificato.
- `docs/NEXT_STEPS.md`: prossime attività ordinate.
- `docs/ROADMAP.md`: direzione e milestone.
- `docs/FOLLOWUP_TICKETS.md`: difetti e follow-up numerati.
- `docs/adr/`: decisioni architetturali.
- `docs/migrations/`: cronologia dettagliata delle migrazioni.
- `docs/stabilization-plan/`: checklist operative brevi con redirect alle fonti canoniche.

## Prima pulizia — eseguita

- [x] Inventariare i documenti che descrivono la stessa area.
- [x] Coppie duplicate trovate: `01-baseline-green`, `02-determinism`, `03-execution-scope-and-precomp`.
- [x] Rendere `docs/02-determinism.md` fonte canonica del dettaglio determinismo.
- [x] Ridurre `docs/stabilization-plan/02-determinism.md` a stato sintetico e link.
- [x] Rendere `docs/03-execution-scope-and-precomp.md` fonte canonica del dettaglio scope.
- [x] Ridurre `docs/stabilization-plan/03-execution-scope-and-precomp.md` a stato sintetico e link.
- [x] Ridurre `docs/stabilization-plan/01-baseline-green.md` a stato sintetico e link.
- [x] Aggiornare `docs/stabilization-plan/README.md` con link alle fonti canoniche.

## Ancora aperto

- [ ] Assegnare una fonte canonica a ogni altra area non ancora coperta.
- [ ] Trasformare eventuali altri duplicati in redirect Markdown brevi.
- [ ] Aggiornare tutti i link interni verso la fonte canonica.
- [ ] Aggiungere una tabella di ownership documentale.
- [ ] Definire il vocabolario: Implementato, Compilato, Testato, Validato in CI.
- [ ] Vietare il checkbox verde quando il comando associato non restituisce zero.
- [ ] Aggiungere un controllo automatico di doc sync.
- [ ] Allineare `STATUS`, `ROADMAP` e `NEXT_STEPS` dopo la baseline verde.

## Completato quando

Ogni informazione ha una sola fonte canonica, gli altri documenti la referenziano e nessuna area mantiene checklist divergenti.
