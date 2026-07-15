# TICKET-PHASE3-SEQUENCE-PAIRS-EDGE-FIELDS — SequenceSpec edge-fields extension (Phase 3 V2)

## Stato

OPEN (P2) — future-chore stub

## Problema

5 campi per `SequenceSpec` (`trim_after` / `freeze_at` / `loop_duration` /
`premount` / `postmount`) + active gate 5-field-aware non sono approdati
a main perché la implementazione droppata era tightly coupled al retired
`SceneBuilder::series()` authoring sugar (che il progetto ha
intenzionalmente ritirato, vedi TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3
§Forward-points).

## Soluzione accettabile

Reintrodurre i 5 campi tramite l'estensione canonica di pattern
documentata per V3 CLI Unification (vedi
[TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3](TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3.md)
§Forward-points). NON usare il retired `SceneBuilder::series()` sugar.
Implementation deve preservare la pre-Phase-3 math per i 200+ chiamanti
esistenti che usano braced-init con `{0}`-default per i 5 nuovi campi.

## Criteri di accettazione

- Build verde su cronon3d_scene target; `chronon3d_timeline_tests` PASS.
- I 200+ chiamanti esistenti con `{0}`-default continuano a compilare e
  comportarsi identicamente al pre-Phase-3 (zero behavioral regression).
- Test coverage per `compile_sequence()` con ciascuno dei 5 campi
  (boundary cases: trim_after, freeze_at, loop_duration, premount,
  postmount, e tutte le combinazioni 0/1/2/3 attivi).

## Forward-points

- Origine drop: [TICKET-COMPOSITION-INPUT-PHASE-1C-1D-STASH-DROP](TICKET-COMPOSITION-INPUT-PHASE-1C-1D-STASH-DROP.md).
- Canonical SequenceSpec extension pattern: [TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3](TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3.md) §Forward-points.
- Companion: [TICKET-PHASE1-C-CLI-VERSION2](TICKET-PHASE1-C-CLI-VERSION2.md) (CLI flags input).
- Companion: [TICKET-PHASE1D-V2-REGISTRY-INTROSPECTION](TICKET-PHASE1D-V2-REGISTRY-INTROSPECTION.md) (introspection CLI surface).
