# TICKET-PHASE1D-V2-REGISTRY-INTROSPECTION — Dev sub-commands registry introspection (Phase 1d V2)

## Stato

OPEN (P2, future-chore stub)

## Problema

I subcommand dev aggiuntivi Phase 1d (`chronon schema`, `chronon example-props`,
`chronon validate`, `chronon resolve`) sono stati droppati per conflitti
di rebase strutturale dal WIP stash (origin `19da5238`, 377 commit dietro
`origin/main@253a178d`). Le scoperte analitiche di upstream (TICKET-LEGACY-RENDER-CLI-GATE-FAILURE
lineage) mostrano che la canonical registry surface è ora
`CompositionRegistry::descriptor_of(name)` + `descriptors()` (già shipped),
NON la abandoned `CompositionResolveFn` lambda abstraction (non-canonical,
confliggente con i canonical registry accessors adottati upstream).

## Soluzione accettabile

Aggiungere subcommand di type introspection fondati esclusivamente e
canonicamente su `CompositionRegistry::descriptor_of(name)` +
`descriptors()` surface, NON sull'abandoned `CompositionResolveFn`
lambda abstraction. Implementation deve essere chronologico-canonical
compatibile con architettura upstream corrente.

## Criteri di accettazione

- `chronon schema <id>` emette JSON skeleton compatibile con
  `PropsSchema` aggregate (in `props_schema.hpp`).
- `chronon example-props <id>` emette default `ValueMap` JSON per typed
  Props via `encode` lambda (Phase 1b).
- `chronon validate <id> --props p.json` esegue decode + validate pipeline.
- `chronon resolve <id> --props p.json` emette `ResolvedCompositionSpec`
  via canonical registry accessors (no `CompositionResolveFn`).
- Build verde su cronon3d_cli target; `chronon3d_cli_tests` PASS.

## Forward-points

- Origine drop: [TICKET-COMPOSITION-INPUT-PHASE-1C-1D-STASH-DROP](TICKET-COMPOSITION-INPUT-PHASE-1C-1D-STASH-DROP.md).
- Companion: [TICKET-PHASE1-C-CLI-VERSION2](TICKET-PHASE1-C-CLI-VERSION2.md) (CLI flags input).
- Parent tracker: [TICKET-COMPOSITIONDESCRIPTOR-MIGRATION](TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md) (canonical-registry form).
