# TICKET-DOCS-DISCIPLINE-FORMALIZATION — Docs-discipline chain aggregation

## Stato

DONE (2026-07-12, embedded as final aggregation milestone for P1-P9g docs-discipline chain)

## Priorità

P2

## Problema

Una catena cross-commit (P1-P9g, ~13 chore commits) è stata completata senza un punto di aggregazione coerente nel changelog, portando al rischio di violare la nuova policy [`docs/DOCUMENTATION_GOVERNANCE.md §CHANGELOG`](docs/DOCUMENTATION_GOVERNANCE.md) ("≤6 punti per ticket, voci brevi") a causa dell'inclusione di blocchi convention eccessivi (>5000 chars per alcune entry storiche pre-P8).

## Evidenza

Catena cronologica (post P8 docs-discipline rule formalization — SHAs inline-citati per AGENTS.md §SHA-cite inline-only rule):

- **`cf673ecc` P9b** — `docs(agents): add 21ece2b3 recovery lineage` (post-race-window recovery + AGENTS.md Origine extension cite `21ece2b3` + `4028f6cc` + `b589fdba` lineage)
- **`0b2ba74b` P9c** — `docs(agents): refine unique-edit recovery documentation` (Cat-3 anti-dup trim per 5° bullet INDEX entry + Obs.2 inline qualifier anchor `git reset --hard '@{u}' (literal documentation...)`)
- **`0299a042` P9e** — `fix(docs): FOLLOWUP rot-fix + open WORKSPACE-RESCUE ticket` (cat-1 sed-strip rot-fix `<<<<<<< HEAD` marker + cat-3 new row-add per TICKET-LOST-COMMIT-WORKSPACE-RESCUE)
- **`5dedc34d` P9g** — `feat(tools): add recover_workspace_rescue.sh` (cat-4 ancillary gate codifying 2 recovery patterns from AGENTS.md §Post-push SHA-selfcheck invariant 5° bullet per b589fdba + 21ece2b3/cf673ecc/0299a042 precedents)

Sub-chain antecedente (pre-P8 docs-discipline rule — SHAs inline-cited in original CHANGELOG entries per file policy):

- **P1** — TextSpec placement (orphan-drop lineage)
- **P2** — umbrella include (orphan-drop lineage)
- **P3** — rotate_z migration a `rotate(Vec3)` (Canonical API rename)
- **P4** — static gates (8/8 machine-verified)
- **P5** — Camera V1 macchina-verify (ENV-BLOCKED-WBH-DEFERRED + TICKET-CAMERA-V1-MACHINE-VERIFY forward-point)
- **P6** — gate profile split (developer/wbh)
- **P7** — tracked .githooks (canonical hook auto-install precedent)
- **P8** — docs-discipline rule formalization (AGENTS.md §Regole di lint documentale append: `### Docs canonical update discipline rule` come regola #6)

## Impatto

Frammentazione della leggibilità documentale; potenziale violazione della policy ≤6 punti a causa della somma di entry verbose pre-P8 disperse nel changelog.

## Confine

Solo riallineamento dei documenti canonici:
1. aggregazione milestone in `docs/CHANGELOG.md` (~3 punti breve, ≤300 chars).
2. creazione di questa scheda per cronaca estesa + 4 SHA inline-citati post-P8 (P1-P8 SHAs delegati al CHANGELOG originale come fonte canonica precedente Cat-3 anti-dup compliant).
3. NO entry in `docs/FOLLOWUP_TICKETS.md` (ticket è DONE pre-apertura, no forward-point applicable per docs metadata storico).
4. NO edit to `docs/CURRENT_STATUS.md` (per essa Cat-5 3-doc forward-point deferred è fuori scope di questo chore aggregato).

Zero sorgenti codice modificati in questo chore. Pattern Cat-5 2-doc (CHANGELOG + ticket spec file).

## Soluzione accettabile

Estrarre il dettaglio della cronologia SHA-citata in questa scheda di supporto (ruolo "Scheda ticket specifico" per `DOCUMENTATION_GOVERNANCE.md` template); inserire esclusivamente il brief aggregato nel CHANGELOG rispettando ≤6 punti.

## Criteri di accettazione

- CHANGELOG entry contiene ≤6 punti, body ≤300 chars, privo di dettagli operativi o listati source-file per entry;
- questa scheda assolve il ruolo storicizzante-aminissibile definito da `DOCUMENTATION_GOVERNANCE.md` (canonical template per `docs/tickets/TICKET-NNN.md`);
- 4 SHAs post-P8 (`cf673ecc` + `0b2ba74b` + `0299a042` + `5dedc34d`) inline-citati esattamente per AGENTS.md §SHA-cite inline-only rule;
- nessuna entry di `docs/FOLLOWUP_TICKETS.md` toccata (ticket DONE pre-apertura = no forward-point applicable);
- Cat-3 + Cat-5 + Discipline rule all compliant.

## Forward-point (cat-5 deferred, NOT in questo commit)

- `TICKET-CHANGELOG-HISTORICAL-REFS-UPDATE` — pre-existing forward-point (P2, OPEN) per eventuale cleanup dei 6 historical refs in `docs/CHANGELOG.md` lines 1003, 3360, 3362, 3377, 5226, 5235 al vecchio path `tests/visual/ae_parity/glow_final_compositions.hpp` (post Step 7 refactor(glow) §4 file move `(933cf6748 → bf02ac0)`). NOT in questo commit per AGENTS.md `Fare PR piccole e mirate` + SHA-cite inline-only rule (NON aggiornare commit-message SHAs inline-citati backward — preserves audit trail integrity).

## Cross-link lineage

- REGOLE: [`docs/DOCUMENTATION_GOVERNANCE.md`](docs/DOCUMENTATION_GOVERNANCE.md) (canonical template per ticket spec files + §CHANGELOG ≤6 punti policy + "Scheda ticket specifico" role for `docs/tickets/TICKET-NNN.md`)
- MILESTONE: [`docs/CHANGELOG.md`](docs/CHANGELOG.md) (la entry che questo ticket supporta; pre-existing verbose entries reference P1-P9g SHAs come canonical source per sub-chain SHA discovery)
- AGENTS.md v0.1 §Insieme canonico della documentazione + §SHA-cite inline-only rule + §Disciplina di aggiornamento dei canonici + §Regole di lint documentale (la P8 docs-discipline rule vive al bottom di questa section come regola #6)
- Pre-session mid-lineage (P1-P7): file convention pre-P8 era reference; AGENTS.md §Fare PR piccole e mirate policy non formalmente applied retroattivamente
