# Architecture Decision Records — Chronon3D

> Decisioni architetturali *impegnative*. Ogni ADR è numerato,
> datato, marcato con uno status esplicito, e si chiude solo quando
> il comportamento che descrive è *validato in CI*.

## Numerazione

`ADR-NNN-slug-kebab.md` in questa directory. La numerazione è
monotonica; gli slot sono riservati dai *Proposed* e mai riutilizzati
per contenuti diversi anche dopo `Deprecated`.

## Status

- **Proposed** — dibattito aperto, nessuna implementazione vincolante.
- **Accepted** — decisione operativa, già implementata (anche solo in PR
  mergeata) e con almeno una marcia CI verde su `linux-ci`/`linux-full-validation`
  che ne copre il comportamento.
- **Deprecated** — la decisione è stata *sostituita* (vedi ADR che rende
  obsoleta) o *invalidata* da refactor successivi. Still-listed per
  contesto storico.
- **Superseded by ADR-MMM** — la decisione è stata rimpiazzata. ADR di
  destinazione citato in cima.

## Struttura

Ogni ADR ha, nell'ordine:

1. **Status** (riga singola, immediatamente sotto il titolo).
2. **Date** + **Deciders** + facoltativi **Tags**.
3. **Context** — la situazione, il blocker, le alternative possibili.
4. **Decision** — cosa si è deciso, codice o invariante se possibile.
5. **Consequences** — positive, negative, neutrali.
6. **Alternatives considered** — perché non si è scelto altro.
7. **References** — commit, PR, ticket, ADR collegati.

## Regola di chiusura

Un ADR diventa **Accepted** solo quando il comportamento che
protegge è *validato in CI* (vedi piano 07 sezione D4) sul commit
che aggiorna `STATUS.md`. Un `Accepted` non viene retrocesso.

## Regola di aggiornamento

`docs/STATUS.md` cita gli ADR rilevanti per l'area aperta.
[`../migrations/`](../migrations/) conserva i changelog dettagliati
ma lo *status* vive qui. `refactor-roadmap/*.md` referenzia gli
ADR via slug anziché riscrivere la decisione.

## Indice

Vedi [`INDEX.md`](INDEX.md) per l'elenco corrente.
