# Wave 7-9 compiler/runtime boundary debt

Status: ACTIVE

Owner: Chronon compiler/runtime maintainers

The new compiler IR and runtime lifetime types are introduced while the legacy
plan compiler and prepared execution artifact remain available for output
parity. This bridge is intentional migration debt, not a second public IR.

Exit condition: compiler golden snapshots and legacy execution output are
identical for text, transition, precomposition, and video plans; all callers
use `Compiler` and the staged frontend/lowering pipeline.

Removal scope: delete the legacy compiler authorities and the private prepared
plan bridge only after the equivalence gate is green. No Wave 7-9 change may
delete those authorities prematurely.

## Wave 2 closeout — public plan accessors removed (2026-09-07)

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
- Reason: CompiledProgram is the transported render-plan artifact while the
  legacy execution path still evaluates CompiledComposition and reads plan
  fields the numeric metadata does not carry.
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
- Status: ACTIVE.

Enforcement: `tools/architecture_rules_wave2.toml` (registered in the root
manifest) forbids member-style accessor call sites tree-wide and any
redeclaration in `include/chronon3d/compiler/compiled_program.hpp`, and pins
the bridge header + single-TU implementations via contract rules.

Housekeeping (same closeout): the stale
`docs/architecture/RENDER-SESSION-MIGRATION-MATRIX.md` rows now name the real
owners (`JobSession`, `RuntimeContext`, `CompiledProgram`) and carry the
reconciliation log; `TextLayoutCache` is marked DONE (`RuntimeContext` owns
`m_text_layout_cache`).
