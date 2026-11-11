# Wave 7-9 compiler/runtime boundary debt

Status: `ACTIVE (narrowed)` — the plan bridge was demolished on 2026-11-10
(`eed9d4049`); the remaining scope is the test-only legacy plan compiler
(`render_plan::compile_render_plan` / `PreparedRenderPlan`) plus the D28/D30
header moves transferred from `TICKET-COMPILER-DEMOLITION-WAVES`.

Owner: Chronon compiler/runtime maintainers

The plan bridge was the intentional migration debt while the legacy plan
compiler remained available for output parity. It was demolished on
2026-11-10; the legacy plan compiler is now test-only.

Exit condition: production callers use `Compiler` and the staged
frontend/lowering pipeline; the retained legacy plan compiler is either retired
with its test corpus or has a documented test-only ownership decision.
Legacy-vs-typed equivalence is **NOT RUN** because the legacy execution lane
was deleted; no PASS is claimed.

Removal scope: retire the legacy plan-compiler authority (`PreparedRenderPlan`,
`compile_render_plan`, compose/validate TUs) and its test corpus, or record the
test-only decision; the bridge is already gone. No new production caller may
use the legacy authority.

## TODO — cosa manca (agg. 2026-10-28)

- [x] **Plan bridge demolition** — CHIUSO (2026-11-10, `eed9d4049`):
  `CompiledProgramPlanBridge`, `compiled_program_plan_bridge.hpp`, the
  friendship and `prepared_plan_` are gone; `CompiledProgram` carries only
  typed data. Zero production references remain and
  `tools/architecture_rules_wave2.toml` forbids reintroduction. The
  legacy-vs-typed equivalence gate is **NOT RUN** because the legacy execution
  lane was deleted; no PASS is claimed.
- [ ] **Legacy plan-compiler authority** — `render_plan::compile_render_plan` /
  `PreparedRenderPlan` are still built and exercised by the `tests/c_abi`
  corpus, but have no production caller. Decide whether to retire the authority
  and its test corpus or keep it explicitly test-only; do not call it from
  production. Gate: removal or a documented test-only ownership decision.

## Assorbimenti 2026-11-05

- [ ] **Progettare `PixelProgram → DeviceProgram` senza una nuova authority** — assorbito da `archive/TICKET-LOCAL-ACCEPTANCE-CLOSEOUT-2026-09-16.md`; va deciso dentro il confine compiler/runtime di questa scheda, senza introdurre cache/registry/scheduler paralleli.

## Assorbimenti 2026-11-10 (da TICKET-COMPILER-DEMOLITION-WAVES)

- **D28/D30 — physical graph-builder header moves and reverse include roots:**
  `graph_builder_coordinates.hpp` and `evaluated_layer_placement.hpp` still
  live under `src/render_graph/builder/` only for the private compat include
  roots. Move them to `src/compiler/graph_builder/` and update includes; gate:
  `configured_compiler_no_legacy_graph_sources` + codemodel.

## Wave 2 closeout — public plan accessors removed (2026-09-07) (historical)

> The bridge described in this section was demolished on 2026-11-10; the text
> below records the 2026-09-07 checkpoint.

`CompiledProgram` no longer exposes `prepared_plan()` / `compiled_composition()`
member accessors; `render_budget` was already promoted into `ProgramMetadata`.
The retained legacy `PreparedRenderPlan` (owned for the program's lifetime) is
now reachable only through the internal plan bridge declared in
`include/chronon3d/compiler/detail/compiled_program_plan_bridge.hpp` and
implemented out-of-line in `src/compiler/compiled_program.cpp` (single TU).

Consumers migrated to the bridge (one line each):

1. `src/compiler/program_sampler.cpp` — legacy evaluation seam.
2. `src/runtime/renderer_warmup.cpp` — program warmup overload.
3. `src/runtime/sdk_render_engine.cpp` (`sdk_render_plan_methods_detail.hpp`) —
   `compile_plan_json` + `render_plan_file`.
4. `src/c_api/chronon3d_c_api.cpp` — `chronon_plan` compile handler.
5. CLI render-plan adapter (`render_plan_preparation.{hpp,cpp}`, inspection,
   `command_render_plan_internal.inc`) — through `context.prepared()`.
6. `apps/chronon3d_cli/utils/job/render_job.cpp` — executor compatibility view.
7. Tests: `test_compiler_boundary.cpp`, `test_compiled_program_execution_equivalence.cpp`.

Bridge Demolition Debt card (owner/reason/exit/equivalence/removal/status):

- Owner: compiler/runtime maintainers.
- Reason: at the time, CompiledProgram was the transported render-plan artifact
  while the legacy execution path still evaluated CompiledComposition and read
  plan fields the numeric metadata did not carry.
- Exit condition: all seams execute from the numeric program; the compiler
  boundary no longer produces `PreparedRenderPlan`/`CompiledComposition`
  (observable: `prepared_plan_` member + bridge header deleted and
  `render_plan::compile_render_plan` no longer called by `Compiler::compile`).
- Equivalence gate: compiler boundary, still-frame execution equivalence
  (`test_compiled_program_execution_equivalence`), render_job_contract,
  linux-fast-dev build.
- Removal scope: bridge header, friendship in `compiled_program.hpp`,
  `prepared_plan_` member, out-of-line bridge implementations, and every
  consumer include added above.
- Status: CLOSED for the bridge (2026-11-10). The legacy plan-compiler
  authority remains test-only and is tracked by the open TODO above.

Enforcement: `tools/architecture_rules_wave2.toml` (registered in the root
manifest) forbids member-style accessor call sites tree-wide, any
redeclaration in `include/chronon3d/compiler/compiled_program.hpp`, and any
return of `CompiledProgramPlanBridge` or its header.

Housekeeping (same closeout): the stale
`docs/architecture/RENDER-SESSION-MIGRATION-MATRIX.md` rows now name the real
owners (`JobSession`, `RuntimeContext`, `CompiledProgram`) and carry the
reconciliation log; `TextLayoutCache` is marked DONE (`RuntimeContext` owns
`m_text_layout_cache`).
