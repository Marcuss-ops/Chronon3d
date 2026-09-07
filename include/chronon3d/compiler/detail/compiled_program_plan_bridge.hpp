// SPDX-License-Identifier: MIT
#pragma once
//
// ═════════════════════════════════════════════════════════════════════════════
// compiler/detail/compiled_program_plan_bridge.hpp — INTERNAL plan bridge
// ═════════════════════════════════════════════════════════════════════════════
//
// Wave 2 (Pure CompiledProgram) demolition seam.
//
// `compiler::CompiledProgram` is the frozen, data-oriented compiler output.
// Its public surface deliberately exposes NO accessor to the legacy prepared
// plan (`render_plan::PreparedRenderPlan` / `timeline::CompiledComposition`):
// those artifacts belong to the legacy execution path that Waves 7-9
// (`TICKET-COMPILER-BOUNDARY-WAVE7-9.md`) are evacuating.
//
// Until that equivalence gate is green, the handful of execution seams that
// still run on `CompiledComposition` (ProgramSampler evaluation, renderer
// warmup, the prepared-composition executor compatibility view, and the
// plan-lifetime consumers that read canvas/output/fingerprint/asset fields
// the numeric metadata does not yet carry) reach the retained legacy plan
// ONLY through this internal header.
//
// This header is NOT part of the SDK public surface:
//   * it lives under compiler/detail/ and is absent from the
//     Chronon3DPublicHeaders.cmake manifest, so it is never installed or
//     advertised to V0.1 consumers;
//   * its functions are declared in namespace `chronon3d::compiler::detail`
//     and are implemented out-of-line in src/compiler/compiled_program.cpp,
//     so an external consumer cannot link them;
//   * the `CompiledProgram` class declaration only ever names the bridge as
//     a friend — no public accessor exists to reintroduce.
//
// ─────────────────────────────────────────────────────────────────────────────
// DEMOLITION DEBT CARD — compiled-program plan bridge
// ─────────────────────────────────────────────────────────────────────────────
// 1. Owner:      compiler/runtime maintainers (same owners as the Wave 7-9
//                compiler-boundary debt in
//                TICKET-COMPILER-BOUNDARY-WAVE7-9.md).
// 2. Reason:     CompiledProgram became the single artifact transported by
//                the render-plan route while the legacy execution path still
//                evaluates CompiledComposition and reads plan fields
//                (canvas/output/fingerprints/asset manifest) that the
//                numeric ProgramMetadata does not yet carry.  The plan is
//                retained privately on CompiledProgram (not destroyed)
//                because its lifetime must match the program's.
// 3. Exit condition: every seam below executes from the numeric program
//                (metadata + nodes/passes/resources/parameters) and the
//                PreparedRenderPlan/CompiledComposition compatibility view is
//                no longer produced by the compiler boundary.  Observable:
//                `prepared_plan_` removed from CompiledProgram, this bridge
//                deleted, and the compiler no longer calls
//                `render_plan::compile_render_plan`.
// 4. Equivalence test/gate: compiler_boundary,
//                test_compiled_program_execution_equivalence (still-frame
//                bit-exact parity between the render_program seam and the
//                legacy compiled-composition seam), render_job_contract, and
//                the linux-fast-dev build.
// 5. Removal scope: this header, the `detail::CompiledProgramPlanBridge`
//                friendship in compiled_program.hpp, the
//                `prepared_plan_` member, the out-of-line bridge
//                implementations in compiled_program.cpp, and every consumer
//                include added for the bridge (program_sampler.cpp,
//                renderer_warmup.cpp, sdk_render_engine.cpp,
//                chronon3d_c_api.cpp, the CLI render-plan adapter + job
//                resolver, and the two equivalence tests).
// 6. Status:     ACTIVE.
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/compiler/compiled_program.hpp>

namespace chronon3d {
struct CompiledComposition;
}
namespace chronon3d::render_plan {
struct PreparedRenderPlan;
}

namespace chronon3d::compiler::detail {

/// Friendship gate to the legacy plan retained on `CompiledProgram`.
/// Declared here; the two static accessors are defined out-of-line in
/// `src/compiler/compiled_program.cpp` so this header stays free of the
/// heavyweight `PreparedRenderPlan`/`CompiledComposition` definitions.
class CompiledProgramPlanBridge {
public:
    [[nodiscard]] static const render_plan::PreparedRenderPlan&
    prepared_plan(const CompiledProgram& program) noexcept;

    [[nodiscard]] static const CompiledComposition&
    compiled_composition(const CompiledProgram& program) noexcept;
};

/// Convenience wrappers for the in-tree seams listed in the demolition card
/// above.  Do NOT use these from new code: new execution paths must consume
/// the numeric `CompiledProgram` (metadata + nodes/passes/...).
[[nodiscard]] inline const render_plan::PreparedRenderPlan&
prepared_plan(const CompiledProgram& program) noexcept {
    return CompiledProgramPlanBridge::prepared_plan(program);
}

[[nodiscard]] inline const CompiledComposition&
compiled_composition(const CompiledProgram& program) noexcept {
    return CompiledProgramPlanBridge::compiled_composition(program);
}

}  // namespace chronon3d::compiler::detail
