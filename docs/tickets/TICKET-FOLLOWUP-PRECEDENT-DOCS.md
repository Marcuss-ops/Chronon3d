# TICKET-FOLLOWUP-PRECEDENT-DOCS — Precedenti documentali e lint rules

## Stato

OPEN (a chiusura: bucket `## Regole di lint documentale` in AGENTS.md aggrega >=2 rule)

## Priorità

P2

## Problema

Le regole dedotte dalla pratica e dai cleanup documentali pregressi (es. commit `3febd8cd`, commit `4cded60e`) necessitano di promozione a regole stabili in `AGENTS.md` per impedire il drift; altrimenti gli autori successivi riscrivono gli stessi cleanup ogni volta che il pattern ricorre e la disciplina documentale decade in code review ripetuti.

## Evidenza

- Commit `3febd8cd` (doc-correctness, 2026-07-10) — ha corretto SHA cite errati in ADR-020 §References ma ne ha inseriti due per lo stesso SHA (inline + standalone), creando duplicazione downstream.
- Commit `4cded60e` (code-review polish, 2026-07-10) — ha dedupato la cite duplicata di `d4737889` lasciando solo la versione inline-on-file; body footnote cita esplicitamente la necessità di promuovere la regola in AGENTS.md (oggetto di questo ticket).
- Ulteriori rots documentali noti che richiedono aggregazione nella sezione `## Regole di lint documentale` una volta formalizzati:
  - Linguaggio di soft-retraction inappropriato nelle ADR-References (lineage TICKET-FOLLOWUP-RETIREMENT-SHA-CORRECTION-OTHER-COMMITS).
  - Drift del formato di referral per SHA già ritirati (lineage TICKET-FOLLOWUP-REFERRAL-OTHER-OLD-SHA-CITES).
  - Filename-drift aliases residui non coperti dal blacklist `STATUS*.md` esistente in AGENTS.md.

## Impatto

Impatto diretto su:

- scopribilità (`rg` audit +1 punto di frizione per ogni pattern non canonizzato);
- onere di deduplicazione a carico del downstream linting (commit-body review + code review diventano l'unico enforcement);
- leggibilità narrativa di §References (le voci standalone rompono il flusso di lettura).

## Confine

Ambito esclusivo: governance documentale (`AGENTS.md`, `docs/DOCUMENTATION_GOVERNANCE.md`).
Nessun impatto sul codice compilato. Zero nuovi root-symbol. Nessun `#include <msdfgen>|<libtess2>|<unicode>`.

## Soluzione accettabile

1. Aggregare le attuali lint rules e gli anti-pattern noti in una nuova sezione `## Regole di lint documentale` in AGENTS.md, collocata tra `## Regole di lavoro` e `## Workflow Git obbligatorio`.
2. La prima rule (`inline-only SHA cite`) è promossa come entry iniziale del bucket, oggetto del commit che apre questo ticket.
3. Regole future migrate in commit atomici successivi, ciascuno referenziato nel campo Lineage del presente ticket.

## Criteri di accettazione

- `AGENTS.md` contiene la sezione `## Regole di lint documentale` popolata con la rule inline-only SHA cite;
- Regole future migrate dal tracking di questo ticket in commit atomici separati;
- Nessuna alterazione al codebase (`include/chronon3d/` intatto, zero nuovi root-symbol);
- TICKET chiuso quando almeno 2 rule sono aggregate nel bucket (regola corrente + 1 futuro).

## Lineage

- Commit `3febd8cd` (doc-correctness, parent commit che ha evidenziato il pattern).
- Commit `4cded60e` (code-review polish, parent commit che ha dedupato e richiesto la promozione).
- Questo ticket nasce dentro il commit di apertura della rule bucket (SHA corrente da assegnare al push).

## Collegamenti

- ADR: nessuno (regola operativa, non decisione architetturale).
- baseline: nessuna (pure docs patch, zero compilation footprint).
- ticket correlati: TICKET-FOLLOWUP-COMMITTED-CONFLICT-MARKERS (P0 rot scoperto durante l'esecuzione di questo ticket), TICKET-FOLLOWUP-RETIREMENT-SHA-CORRECTION-OTHER-COMMITS, TICKET-FOLLOWUP-REFERRAL-OTHER-OLD-SHA-CITES.
