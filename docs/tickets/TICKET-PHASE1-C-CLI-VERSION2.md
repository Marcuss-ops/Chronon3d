# TICKET-PHASE1-C-CLI-VERSION2 — CompositionInput CLI support (Phase 1c V2)

## Stato

OPEN (P2) — future-chore stub

## Problema

Il supporto via `--props <file>` / `--props-json <inline>` per il
`CompositionInput` Phase 1c è stato droppato per conflitti di rebase
strutturale dal WIP stash (origin `19da5238`, 377 commit dietro
`origin/main@253a178d`). L'architettura CLI upstream attuale non supporta
l'injection di Composition Props da linea di comando con parse-time
validation.

## Soluzione accettabile

Re-introdurre i CLI flags adattandoli alla post-deprecation registry form
(non il baseline `19da5238` su cui lo stash fu authored), al post-Phase-3
`SequenceSpec` layout, e all'esistente `CompositionInputArgs` union upstream.
Refactoring deve essere file-localized + incremental, non porting bulk.

## Criteri di accettazione

- Build verde sui compilatori supportati (no `-Werror` failures).
- Test coverage per il parse-time validation path (`try_load_composition_input`
  helper canonico + `RenderDiagnostic` emission per malformed JSON).
- Nessuna regressione sui tools (`chronon3d_cli_tests` PASS).

## Forward-points

- Origine drop: [TICKET-COMPOSITION-INPUT-PHASE-1C-1D-STASH-DROP](TICKET-COMPOSITION-INPUT-PHASE-1C-1D-STASH-DROP.md).
- Companion: [TICKET-PHASE1D-V2-REGISTRY-INTROSPECTION](TICKET-PHASE1D-V2-REGISTRY-INTROSPECTION.md) (introspection CLI surface).
- Parent tracker: [TICKET-COMPOSITIONDESCRIPTOR-MIGRATION](TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md) (canonical-registry form).
