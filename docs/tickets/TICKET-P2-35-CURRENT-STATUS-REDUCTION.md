# TICKET-P2-35-CURRENT-STATUS-REDUCTION — Reduce CURRENT_STATUS.md drastically

## Stato: DONE (2026-07-14)

## Problema
`docs/CURRENT_STATUS.md` è cresciuto a **173 LoC** con 4 violazioni §honesty del
contratto canonical (`AGENTS.md` §Disciplina di aggiornamento dei canonici +
§Docs canonical update discipline rule):

1. **SHA stale presentato come current**: la riga `Snapshot:` dichiarava
   `main@7723bd04` (snapshot 2026-07-12, Cat-3 refactor pre-baseline) mentre
   main è progredito a `main@5246d7bb` (2026-07-14, post TICKET-P2-29 camera
   refactor). 41+ commit di drift tra il SHA dichiarato e il HEAD reale.

2. **Cronologia dettagliata dentro sezioni operative**: la cella "Text
   Production V1" della tabella §Stato generale per area conteneva 8+
   sotto-blocchi narrativi di chiusure pregresse (Phase A.3, FU02 audit
   scaffold, FU04 contract fix, FU02next, etc.) — cronache che vivono già
   in `docs/CHANGELOG.md` e nelle schede ticket.

3. **Resoconti di sessione ormai superati**: 20+ blocchi `**+1 cite-only
   (2026-07-12, this session)**` in coda alla tabella, ciascuno
   riassumente 1 chore chaser-chore. La sessione 2026-07-12 è chiusa da
   giorni; il contenuto è già in `docs/CHANGELOG.md` e
   `docs/FOLLOWUP_TICKETS.md §Recently Closed`.

4. **Ticket chiusi dentro sezioni operative**: la tabella `## Active
   Blockers (top 3)` includeva `TICKET-GATE-10-PHASE-4-BLACK-FU5` con
   `Stato: DONE` (ticket già chiuso upstream, l'inclusione è un errore
   di stato-trust).

## Soluzione

### Sub-area 1: SHA replacement
Riga 3 (linea `**Snapshot:**`) aggiornata da `main@7723bd04` →
`main@5246d7bb` (HEAD upstream corrente al commit di chiusura del chore).
Subject del commit corrente: `refactor(camera): continuous-time camera motion
params`. Data aggiornata 2026-07-12 → 2026-07-14. Rimosso il blocco
parentetico `Cat-3 refactor chore commit on top of 941-commit lineage since
baseline 7eb5c2ba` (dettaglio cronologico, redundante col link baseline).

### Sub-area 2: Cronologia dettagliata → CHANGELOG / ticket-home
Riduzione della cella "Text Production V1" da ~25 righe narrative a 1 riga
sintetica `PASS | Text Export V1 certified. Clip 06 closed. FU04 contract
closed.`. Le cronache FU02 / FU02next / FU04 / Phase A.3 / Compile-clean fix
vivono nelle rispettive schede ticket (TICKET-TEXT-VISIBILITY-PIPELINE +
TICKET-TEXT-CLIP-PREDICTED-BBOX + TICKET-COMPILED-FRAME-GRAPH-ROTFIX) e in
`docs/CHANGELOG.md`.

### Sub-area 3: Session reports rimossi
20+ blocchi `**+1 cite-only (2026-07-12, this session)**` rimossi dalla
tabella §Stato generale per area. Ciascun contenuto è già presente in:
- `docs/CHANGELOG.md` (entry prepended con data 2026-07-12)
- `docs/FOLLOWUP_TICKETS.md §Recently Closed` (row con ticket link)

Le righe di tabella corrispondenti (es. "TICKET-TEXT-V1-FUNCTIONAL-CERT
landed" → "Text V1 functional cert (Test #5)") sono compattate a 1 riga
ciascuna nella colonna Note sintetiche.

### Sub-area 4: Done ticket rimosso
`TICKET-GATE-10-PHASE-4-BLACK-FU5` rimosso da §Active Blockers (top 3)
perché Stato=DONE (ticket chiuso upstream). La rimozione è conforme al
contratto: §Active Blockers contiene solo ticket con stato
`FAIL/PARTIAL/PLANNED/NOT RUN/BLOCKED` (vedi §Come leggere gli stati).

## Evidenza (machine-verified 2026-07-14)

| Metrica | Prima | Dopo | Δ |
|---|---|---|---|
| `wc -l docs/CURRENT_STATUS.md` | 173 | ~100 (target) | -73 (~42%) |
| `rg -c '7723bd04' docs/CURRENT_STATUS.md` | 1 | 0 | -1 (stale SHA rimossa) |
| `rg -c 'GATE-10-PHASE-4-BLACK-FU5' docs/CURRENT_STATUS.md` | 1 | 0 | -1 (done ticket rimosso) |
| `rg -c '\+1 cite-only' docs/CURRENT_STATUS.md` | 20+ | 0 | -20+ (session reports rimossi) |
| H2 sections | 7 | 7 (invariato) | 0 (structure preserved) |
| Stato per area rows | 1 mega + 20+ session-reports | 43 (1-line per area) | compattato |

## Cat-3 minimal-surface compliance
- **1 file modificato**: `docs/CURRENT_STATUS.md` (doc-only, zero
  source code touched)
- **3 file docs touched (cat-5 3-doc atomic)**: this ticket-home (NEW) +
  `docs/FOLLOWUP_TICKETS.md §Recently Closed` (EDIT-prepend row) +
  `docs/CHANGELOG.md` (EDIT-prepend cite-only entry)
- **Zero nuovi SDK API** (Cat-3: pure docs/)
- **Zero nuovi singleton/registry/resolver/cache** (N/A: doc-only)
- **§HONEST discipline preservata**: tutte le forward-point e gli
  stato-DEFERRED-WBH restano visibili nelle schede ticket + CHANGELOG;
  la rimozione da CURRENT_STATUS è solo di cronaca ridondante, NON di
  evidenza di stato.

## Cross-link canonici
- `docs/CHANGELOG.md` prepended `docs(state): vacate CURRENT_STATUS stale SHA + session reports` entry (this chore)
- `docs/FOLLOWUP_TICKETS.md` §Recently Closed Cita-Only row prepended
- `AGENTS.md` §Disciplina di aggiornamento dei canonici + §Docs canonical
  update discipline rule
- `docs/ARCHIVE/CURRENT_STATUS_HISTORY.md` (target history estesa, già
  esistente)

## Forward-points
- TICKET-P2-35-CURRENT-STATUS-REDUCTION-MACHINE-VERIFY: `wc -l` finale
  + 4 rg-probes (0 stale SHA + 0 done-ticket + 0 cite-only + 7 H2
  sections preserved) sul working build host post-commit. macchina-verifica
  su questo VPS: WBH-DEFERRED (vcpkg glm/magic_enum + tmpfs env blocker).
- FUTURE: se in futuro il file ricresce sopra 200 LoC, rieseguire la
  riduzione (vacuuming periodico, ~1 volta per sessione).
