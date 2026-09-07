#pragma once

#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>

namespace chronon3d::compiler {

/// Samples an immutable program at a frame boundary.
/// Runtime code depends on this interface instead of reaching through the
/// compiler result to an authoring-oriented compatibility object.
class ProgramSampler final {
public:
    [[nodiscard]] Result<EvaluatedCompositionFrame, CompositionEvaluateError>
    sample(const CompiledProgram& program,
           const CompositionEvaluateContext& context,
           Frame frame) const;
};

}  // namespace chronon3d::compiler
