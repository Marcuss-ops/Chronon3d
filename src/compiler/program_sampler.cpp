#include <chronon3d/compiler/program_sampler.hpp>

#include <chronon3d/compiler/detail/compiled_program_plan_bridge.hpp>

namespace chronon3d::compiler {

Result<EvaluatedCompositionFrame, CompositionEvaluateError>
ProgramSampler::sample(const CompiledProgram& program,
                       const CompositionEvaluateContext& context,
                       Frame frame) const {
    // Wave 2-9 demolition seam: ProgramSampler is the canonical adapter that
    // keeps the legacy CompiledComposition evaluation reachable until the
    // numeric program path executes directly (see the bridge debt card).
    return chronon3d::evaluate(
        detail::compiled_composition(program), context, frame);
}

}  // namespace chronon3d::compiler
