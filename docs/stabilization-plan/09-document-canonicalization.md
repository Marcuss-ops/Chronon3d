# Canonicalizzazione dei documenti

## Problema

Alcune aree hanno due documenti che descrivono lo stesso lavoro, per esempio:

- `docs/02-determinism.md` e `docs/stabilization-plan/02-determinism.md`;
- `docs/03-execution-scope-and-precomp.md` e `docs/stabilization-plan/03-execution-scope-and-precomp.md`;
- roadmap, status e ticket che ripetono lo stesso stato con parole diverse.

Questo crea drift e checkbox contraddittori.

## Regola canonica proposta

- `docs/STATUS.md`: stato corrente sintetico e verificato.
- `docs/NEXT_STEPS.md`: prossime attività ordinate.
- `docs/ROADMAP.md`: direzione e milestone.
- `docs/FOLLOWUP_TICKETS.md`: difetti e follow-up numerati.
- `docs/adr/`: decisioni architetturali.
- `docs/migrations/`: cronologia dettagliata delle migrazioni.
- `docs/stabilization-plan/`: indice e checklist operative brevi.
- Documenti tecnici estesi già esistenti fuori da `stabilization-plan`: fonte canonica del dettaglio.

## TODO

- [ ] Inventariare i documenti che descrivono la stessa area.
- [ ] Assegnare una fonte canonica a ogni area.
- [ ] Trasformare i duplicati in redirect Markdown brevi.
- [ ] Non mantenere due checklist indipendenti per lo stesso work package.
- [ ] Aggiornare tutti i link interni verso la fonte canonica.
- [ ] Aggiungere una tabella di ownership documentale.
- [ ] Definire il vocabolario: Implementato, Compilato, Testato, Validato in CI.
- [ ] Vietare il checkbox verde quando il comando associato non restituisce zero.
- [ ] Aggiungere un controllo automatico di doc sync.

## Prima pulizia

- [ ] Rendere `docs/02-determinism.md` fonte canonica del dettaglio determinismo.
- [ ] Ridurre `docs/stabilization-plan/02-determinism.md` a stato sintetico e link.
- [ ] Rendere `docs/03-execution-scope-and-precomp.md` fonte canonica del dettaglio scope.
- [ ] Ridurre `docs/stabilization-plan/03-execution-scope-and-precomp.md` a stato sintetico e link.
- [ ] Allineare `STATUS`, `ROADMAP` e `NEXT_STEPS` dopo la baseline verde.

## Completato quando

Ogni informazione ha una sola fonte canonica, gli altri documenti la referenziano e nessuna area mantiene checklist divergenti.