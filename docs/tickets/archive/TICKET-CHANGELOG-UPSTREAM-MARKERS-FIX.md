# TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX

## Stato

DONE — 2026-08-02 (`bf413d58`)

## Problema

Estendere il gate di rilevamento marker di conflitto upstream anche al testo `.md` in modo da prevenire merge di prose con conflict markers.

## Criteri di accettazione

- Il gate rileva marker di conflitto in file `.md` oltre che in sorgenti.
- Nessun falso positivo sui file canonici.

Evidenza: `tools/check_no_changelog_conflict_markers.sh` scansiona tutti i
file Markdown sotto `docs/`, inclusi file non ancora tracciati, e passa sullo
stesso checkout senza marker residui.
