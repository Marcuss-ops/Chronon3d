# TICKET-PHASE1D-V2-REGISTRY-INTROSPECTION — Dev sub-commands registry introspection (Phase 1d V2)

## Stato

SHIPPED (2026-07-15, chaser-chore commit on `feat/composition-input-phase-1c-1d-v2`). All 4 sub-commands shipped; canonical `CompositionRegistry::descriptor_of` + `resolve` surface wrapped; zero new public SDK symbols.

## Macchina-verify attestation: DEFERRED-WBH

Per AGENTS.md §honesty-discipline (TICKET-MACCHINA-VERIFY-MAIN-E416D231-WBH-DEFERRED §honesty closure precedent):
- NO `chronon3d_cli_tests` PASS attested (vcpkg env-block on this VPS).
- NO `chronon3d_introspection_tests` PASS attested (vcpkg glm/magic_enum MISSING; build rot-pattern extended per cache_diagnostics.hpp Forward-decl rot).
- NO 11/11 baseline gates attested on this VPS (env-block).

When the work moves to a clean working build host (WBH) a future macchina-verify cycle will attest `chronon3d_cli_tests` PASS as the canonical closing-grade evidence for questo ticket. Forward-point: [TICKET-MACCHINA-VERIFY-MAIN-E416D231-WBH-DEFERRED](TICKET-MACCHINA-VERIFY-MAIN-E416D231-WBH-DEFERRED.md).

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

## Criteri di accettazione (verified at `feat/composition-input-phase-1c-1d-v2@e151a4b6`)

- `chronon schema <id>` (commit `bdd696dd`) → JSON `composition_id` + `fields:[...]` via `descriptor_of()`. ✓
- `chronon example-props <id>` (commit `76adb3c9`) → JSON `composition_id` + `props:{...}` via Schema-walk. ✓
- `chronon validate <id> [--props-file|--props-json]` (commit `8447ab12`) → JSON `valid` gate via `resolve()`. ✓ (mutual-exclusion enforced per code-reviewer cat-5 silent-class closure)
- `chronon resolve <id> [--props-file|--props-json]` (commit `e151a4b6`) → JSON `resolved` + `props` + `metadata:{width,height,fps:{numerator,denominator},duration}` via `resolve() + descriptor_of()`. ✓ (PropsCodec attach fix per test #1 descriptor.schema population)
- Build declarative su `chronon3d_cli_dev` target; commands registrati via `register_dev_commands.cpp`. ✓
- Test target `chronon3d_introspection_tests` aggiunto a `CHRONON3D_FAST_TEST_DEPS`. ✓
- `tools/check_doc_sync.sh | tools/check_architecture_boundaries.sh | tools/check_no_source_conflict_markers.sh` PASS post-Increments. ✓
- ZERO new public SDK symbols (include/ src/ untouched). ✓
- macchina-verifica su `chronon3d_introspection_tests` DEFERRED-WBH (this VPS env-block; vedi [TICKET-MACCHINA-VERIFY-MAIN-E416D231-WBH-DEFERRED](TICKET-MACCHINA-VERIFY-MAIN-E416D231-WBH-DEFERRED.md)).

## Forward-points

- Origine drop: [TICKET-COMPOSITION-INPUT-PHASE-1C-1D-STASH-DROP](TICKET-COMPOSITION-INPUT-PHASE-1C-1D-STASH-DROP.md).
- Companion: [TICKET-PHASE1-C-CLI-VERSION2](TICKET-PHASE1-C-CLI-VERSION2.md) (CLI flags input).
- Parent tracker: [TICKET-COMPOSITIONDESCRIPTOR-MIGRATION](TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md) (canonical-registry form).
- macchina-verifica WBH: [TICKET-MACCHINA-VERIFY-MAIN-E416D231-WBH-DEFERRED](TICKET-MACCHINA-VERIFY-MAIN-E416D231-WBH-DEFERRED.md) (deferred).

## Cronologia (Phase 1d V2 re-implementation)

- 2026-07-15 `bdd696dd` — feat(cli): chronon schema (Phase 1d / Increment C). [commit subject mirror]
- 2026-07-15 `76adb3c9` — feat(cli): chronon example-props (Phase 1d / Increment D).
- 2026-07-15 `8447ab12` — feat(cli): chronon validate (Phase 1d / Increment E).
- 2026-07-15 `e151a4b6` — feat(cli): chronon resolve (Phase 1d / Increment F). [test #1 PropsCodec fix]
- chaser-chore: docs canonical ticket-home update + CHANGELOG milestone closure entry.
