# TICKET-PHASE3-SEQUENCE-PAIRS-EDGE-FIELDS — SequenceSpec edge-fields extension (Phase 3 V2)

## Stato

DONE-VIA-EXISTING (2026-07-15) — SUPERSEDED-BY-EXISTING nella
canonical architettura upstream (`origin/main@e416d231`). Stub chiuso
durante re-implementatione `feat/composition-input-phase-1c-1d-v2`.

## Problema

Stub forward-point di 5 campi `SequenceSpec` (`trim_after` /
`freeze_at` / `loop_duration` / `premount` / `postmount`) + active gate
5-field-aware, droppati dal WIP stash Phase 1c/1d per conflitti di
rebase strutturale.

## Soluzione accettabile

**SUPERSEDED-BY-EXISTING**: i 5 campi sono GIÀ strutturalmente presenti
e wired in upstream `origin/main@e416d231` — vedi `SequenceSpec` struct
declaration in `include/chronon3d/scene/builders/scene_builder.hpp:96`
(5 campi + `{0}`-defaults + `std::optional<Frame>` per i nullable); la
5-field-aware active gate (`active_start = spec.from - spec.premount`,
`active_end = spec.from + spec.duration + spec.postmount + spec.trim_after`,
clamp-on-premount/postmount, loop wrapping, freeze-at precedence)
risiede in
`include/chronon3d/scene/builders/detail/scene_builder_sequences.inl:35-67`;
zero regressione ABI sui 200+ chiamanti esistenti via brace-init con
`{0}`-default perché i 5 campi sono tutti `Frame{}`-default
(`std::optional<Frame>{}` per `freeze_at` e `loop_duration`). Non usare
il retired `SceneBuilder::series()` sugar né reintrodurre `Composition
ResolveFn`.

## Criteri di accettazione (verified at origin/main@e416d231)

- [x] `SequenceSpec` dichiara i 5 campi verbatim (rg-probe
      `trim_after|freeze_at|loop_duration|premount|postmount` in
      `include/chronon3d/scene/builders/scene_builder.hpp`).
- [x] `compile_sequence()` logica 5-field-aware wired in
      `include/chronon3d/scene/builders/detail/scene_builder_sequences.inl`:
      `active_start = spec.from - spec.premount`,
      `active_end = spec.from + spec.duration + spec.postmount + spec.trim_after`,
      clamp premount/postmount, loop duration wrapping, freeze-at precedence.
- [x] I 200+ chiamanti brace-init `{0}`-default continuano a compilare
      senza source modification (tutti i 5 campi hanno `{0}`-default o
      `std::optional<Frame>{}` vuoto → conditional checks graceful
      bypass).
- [x] NO `SceneBuilder::series()` reintrodotto (canonical sugar ritirato
      preservato per `TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3` lineage).

## Forward-points

Branch context: re-implementatione attiva su `feat/composition-input-phase-1c-1d-v2`.

- Origine drop: [TICKET-COMPOSITION-INPUT-PHASE-1C-1D-STASH-DROP](TICKET-COMPOSITION-INPUT-PHASE-1C-1D-STASH-DROP.md).
- Canonical SequenceSpec extension pattern: [TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3](TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3.md) §Forward-points.
- Companion: [TICKET-PHASE1-C-CLI-VERSION2](TICKET-PHASE1-C-CLI-VERSION2.md).
- Companion: [TICKET-PHASE1D-V2-REGISTRY-INTROSPECTION](TICKET-PHASE1D-V2-REGISTRY-INTROSPECTION.md).
