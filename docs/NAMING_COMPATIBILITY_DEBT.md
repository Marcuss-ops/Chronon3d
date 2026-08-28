# Naming and compatibility debt register

Classification is evidence-based. A path is removed only when there are no
production callers, no supported consumer includes, no ABI symbol, and no
required test contract.

| Item | Classification | Evidence / action |
|---|---|---|
| `chronon3d::Composition` | ABI required / canonical | Public model used by authoring, registry and runtime; retain. |
| `CompositionDefinition` | deprecated compatibility alias | Existing ABI baseline and tests reference the name; alias to `Composition`, no second storage model. |
| `CompiledComposition::definition` | ABI/source compatibility adapter | Existing runtime/CLI/C ABI consumers use it; retain as a view to `composition`, migrate callers progressively. |
| `CompiledComposition::composition` | canonical runtime storage | New code must use it; owns the canonical `Composition` snapshot. |
| `compile_composition(CompositionDefinition, ...)` | deprecated adapter | Boundary-only forwarding overload; no independent compiler logic. |
| `TileExecutionPolicy` | deprecated naming alias | It is an alias of `ExecutionResolver`; retain until all source callers migrate. |
| `command_plan_executor.cpp` | ABI required compatibility shim | `execute_command_plan` is present in the ABI baseline; retain symbol, no production callers. |
| `command_plan_executor.hpp` under `include/` | dead public path | Removed from public include tree; test declaration lives under `tests/helpers/`. |
| `executor_levels.cpp`, `node_runner.cpp`, `node_executor.cpp` | active helpers | Profiling/call graph showed distinct ownership; do not remove or merge. |
| `docs/CHANGELOG.md` and old documentation paths | historical/deferred | Not code/API paths; classify per-line in filename drift audit, do not recreate files. |

## Removal rule

Only the dead public `command_plan_executor.hpp` path was removed. The ABI
symbol and implementation remain intentionally. No other candidate met the
removal proof: unused by production is insufficient when ABI or test/source
compatibility still requires the name.
