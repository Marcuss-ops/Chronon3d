# Chronon3D — Indice unico della documentazione

Questo è l'**unico punto di ingresso** alla documentazione di Chronon3D.
Un documento che non è raggiungibile da qui non è autoritativo: se ne trovi uno
così, o lo colleghi qui, o è candidato alla cancellazione.

Regola: **una sola fonte per ogni fatto**.

| Fatto | Fonte unica |
|---|---|
| Identità di release (scope, SHA, criterio same-SHA) | [`RELEASE_V0_1_CONTRACT.md`](RELEASE_V0_1_CONTRACT.md) |
| Stato osservato adesso | [`CURRENT_STATUS.md`](CURRENT_STATUS.md) |
| Requisiti permanenti di release | [`RELEASE_GATE.md`](RELEASE_GATE.md) |
| Blocker aperti (max 10) | [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) |
| Piano operativo e milestone | [`CHRONON_PLAN.md`](CHRONON_PLAN.md) · [`ROADMAP.md`](ROADMAP.md) |
| Regole operative per agenti e persone | [`../AGENTS.md`](../AGENTS.md) |
| API pubblica | `include/chronon3d/` (gli header sono la spec) |

Nessun altro file deve ridefinire questi contenuti. Tutto il resto *punta* qui.

## Ordine di lettura

1. [`../README.md`](../README.md) — cos'è il progetto, quick start.
2. [`CURRENT_STATUS.md`](CURRENT_STATUS.md) — dove siamo, cosa è certificato, cosa no.
3. [`RELEASE_V0_1_CONTRACT.md`](RELEASE_V0_1_CONTRACT.md) + [`RELEASE_GATE.md`](RELEASE_GATE.md) — quando una revisione è rilasciabile.
4. [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) — cosa manca, con quale priorità.
5. [`../AGENTS.md`](../AGENTS.md) — come si tocca il codice.
6. Solo se serve il dettaglio di un'area: architecture / adr / design / evidence.
7. Per scavare nel passato: [`tickets/archive/`](tickets/archive/) e `git log --follow`.

## Mappa delle sezioni

| Sezione | Cosa contiene | Quando aprirla |
|---|---|---|
| radice `docs/` | stato, gate, piano, roadmap, governance, protocolli di certificazione | sempre, come punto di partenza |
| [`adr/`](adr/) | 36 decisioni architetturali registrate (ADR) | prima di cambiare un confine tra moduli |
| [`architecture/`](architecture/) | descrizione strutturale del sistema | per capire un sottosistema |
| [`design/`](design/) | proposte di design non ancora landed | prima di implementare una feature nuova |
| [`evidence/`](evidence/) | 31 registri di prova (run, host, numeri) | quando devi *dimostrare* qualcosa |
| [`baselines/`](baselines/) | baseline di performance e golden | per confrontare una misura |
| [`benchmarks/`](benchmarks/) | corpus e metodologia di misura | per toccare le performance |
| [`gpu/`](gpu/) | vincoli del backend GPU | per il percorso Vulkan/CUDA |
| [`reference/`](reference/) | regole di lint, riferimenti normativi | quando un gate ti boccia |
| [`schemas/`](schemas/) | contratto degli schema versionati | per RenderPlan e IPC |
| [`tickets/`](tickets/) | **solo lavoro aperto** | per prendere in carico un blocker |
| [`tickets/archive/`](tickets/archive/) | ticket chiusi, conservati come prova storica | per ricostruire perché una scelta è stata fatta |

## Ticket: aperto vs archiviato

Un ticket vive in `docs/tickets/` **finché è aperto**. Quando è chiuso:

1. il suo stato viene aggiornato nell'ultima riga del ticket stesso;
2. `git mv docs/tickets/TICKET-X.md docs/tickets/archive/`;
3. i link in [`CURRENT_STATUS.md`](CURRENT_STATUS.md),
   [`FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) e nel ticket che lo cita
   vengono ripuntati su `archive/`;
4. se il ticket chiude un fatto che va ricordato in futuro, quel fatto migra in
   [`CURRENT_STATUS.md`](CURRENT_STATUS.md) o in un ADR — non resta nel ticket.

Nessun ticket archiviato torna in `docs/tickets/`: se il problema riappare, si
apre un ticket nuovo che linka l'archivio.

## Dove scrivere cosa (routing)

| Se stai per scrivere… | Non farlo qui: scrivilo in |
|---|---|
| un aggiornamento cronologico del lavoro fatto | un commit message; il chronicle non va in nessun documento |
| un elenco di cose da fare | `FOLLOWUP_TICKETS.md` (indice) + un ticket per il dettaglio |
| lo stato corrente | `CURRENT_STATUS.md` (una riga per area, non un diario) |
| una misura | `evidence/` con host, comando e output |
| una decisione irreversibile | un ADR in `adr/`, numerato |
| una regola che un gate può verificare | il gate stesso; il documento si limita a linkarlo |

## Igiene

- un grep manuale: ogni link
  relativo in `docs/` deve risolvere a un file esistente.
- Un documento duplicato è peggio di un documento assente: se due file dicono
  la stessa cosa, uno dei due va ridotto a un puntatore.
