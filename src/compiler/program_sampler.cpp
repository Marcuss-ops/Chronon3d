#include <chronon3d/compiler/program_sampler.hpp>

namespace chronon3d::compiler {

Result<EvaluatedCompositionFrame, CompositionEvaluateError>
ProgramSampler::sample(const CompiledProgram& program,
                       const CompositionEvaluateContext& context,
                       Frame frame) const {
    return chronon3d::evaluate(program.compiled_composition(), context, frame);
}

}  // namespace chronon3d::compiler
