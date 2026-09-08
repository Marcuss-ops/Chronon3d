#pragma once

#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/compiler/frame_parameters.hpp>
#include <chronon3d/core/types/frame.hpp>

#include <memory_resource>

namespace chronon3d {
class Scene;
struct FrameContext;
}

namespace chronon3d::compiler {

/// Samples an immutable program at a frame boundary.
class ProgramSampler final {
public:
    /// Materializes the immutable graph topology once. Frame-varying activity
    /// and transforms belong to FrameParameterBlock and are never sampled here.
    [[nodiscard]] chronon3d::Scene materialize_scene(
        const CompiledProgram& program,
        std::pmr::memory_resource* resource = std::pmr::get_default_resource()) const;

};

}  // namespace chronon3d::compiler
