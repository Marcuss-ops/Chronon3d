# TICKET — Wave closeout demolition debt

This ticket records the compatibility boundaries retained while the typed
compiler/runtime and final video-plan contracts replace older entry points.

## Legacy composition adapter

- **Owner:** runtime/compiler migration.
- **Reason:** `RenderEngine::prepare(Composition)` accepts an arbitrary
  per-frame `Composition::SceneFunction`, while the typed compiler currently
  lowers `RenderPlan` into `CompiledProgram`; no one-frame projection is
  semantically equivalent.
- **Exit condition:** prepared single-frame and batch execution consume only
  `CompiledProgram` plus `FrameParameterBlock`; no production
  `compatibility_composition` or arbitrary callback route remains.
- **Equivalence test/gate:** compiled-program corpus equivalence covering
  frame-varying callbacks, camera evaluation, assets, and batch ordering.
- **Removal scope:** `compatibility_composition`, the Composition preparation
  overload/adapter, compatibility render methods, and their timeline includes.
- **Status:** `ACTIVE`

## Legacy video resolver helper

- **Owner:** media/video execution migration.
- **Reason:** the resolver migration retained an internal helper while callers
  moved from path flags to `ExecutionRequirements`, `OutputSpec`, and
  `VideoCapabilities`.
- **Exit condition:** all resolver decisions are produced directly from the
  semantic input contract and no compatibility-named helper or duplicate path
  view remains.
- **Equivalence test/gate:** `tests/cli/test_packet_assembler_contract.cpp`
  resolver matrix, including direct-YUV, full-graph, packet-copy, and invalid
  capability outcomes.
- **Removal scope:** the old helper and its legacy flag translation; keep the
  single public `resolve_video_execution*` authority and `VideoExecutionPlan`.
- **Status:** `REMOVED` — helper renamed to `resolve_plan_from_capabilities`;
  no `resolve_legacy_internal` symbol remains; the final GOP equivalence matrix
  passes in `tests/cli/test_packet_assembler_contract.cpp`.
