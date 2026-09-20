#pragma once

#include <chronon3d/compiler/compiled_program.hpp>

#include <memory_resource>

namespace chronon3d {
class Scene;
}

namespace chronon3d::compiler {

/// Builds the immutable Scene topology for a compiled program.
///
/// Frame-varying activity and transforms belong to FrameParameterBlock and
/// are sampled by TypedProgramSampler; this authority only materializes the
/// topology that can be cached by program fingerprint.
class SceneMaterializer final {
public:
    [[nodiscard]] chronon3d::Scene materialize_scene(
        const CompiledProgram& program,
        std::pmr::memory_resource* resource = std::pmr::get_default_resource()) const;
};

}  // namespace chronon3d::compiler
