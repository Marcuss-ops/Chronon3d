#pragma once

#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/compiler/frame_parameters.hpp>
#include <chronon3d/compiler/typed_program_sampler.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>

namespace chronon3d::compiler {

/// Samples an immutable program at a frame boundary.
class ProgramSampler final {
public:
    [[nodiscard]] Result<EvaluatedCompositionFrame, CompositionEvaluateError>
    sample(const CompiledProgram& program,
           const CompositionEvaluateContext& context,
           Frame frame) const;

    [[nodiscard]] FrameParameterBlock
    sample(const CompiledProgram& program, Frame frame) const;
};

}  // namespace chronon3d::compiler
