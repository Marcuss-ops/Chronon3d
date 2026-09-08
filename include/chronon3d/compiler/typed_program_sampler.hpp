#pragma once

#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/compiler/frame_parameters.hpp>
#include <chronon3d/core/types/frame.hpp>

namespace chronon3d::compiler {

/// Samples only the immutable typed compiler IR.  It has no dependency on
/// CompiledComposition, PreparedRenderPlan, or runtime/backend state.
class TypedProgramSampler final {
public:
    [[nodiscard]] FrameParameterBlock sample(const CompiledProgram& program,
                                              Frame frame) const;
};

}  // namespace chronon3d::compiler
