#pragma once

#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/compiler/frame_parameters.hpp>
#include <chronon3d/core/types/frame.hpp>

namespace chronon3d::compiler {

/// Samples only the immutable typed compiler IR. It has no dependency on
/// authoring bridges or execution-layer state.
class TypedProgramSampler final {
public:
    /// Refill a frame-slot-owned block. The destination keeps its vector
    /// capacity across leased frames, so steady-state sampling does not
    /// allocate a new parameter vector.
    void sample_into(const CompiledProgram& program,
                     Frame frame,
                     FrameParameterBlock& destination) const;

    /// Sample continuously for temporal shutter accumulation.
    void sample_into(const CompiledProgram& program,
                     double frame,
                     FrameParameterBlock& destination) const;

    [[nodiscard]] FrameParameterBlock sample(const CompiledProgram& program,
                                              Frame frame) const;
};

}  // namespace chronon3d::compiler
