#pragma once

#include <chronon3d/compiler/compiled_program.hpp>

#include <memory_resource>

namespace chronon3d {
class Scene;
class FontEngine;
}

namespace chronon3d::compiler {

/// Builds the immutable Scene topology for a compiled program.
///
/// Frame-varying activity and transforms belong to FrameParameterBlock and
/// are sampled by TypedProgramSampler; this authority only materializes the
/// topology that can be cached by program fingerprint.
class SceneMaterializer final {
public:
    /// `font_engine` shapes text layers.  It must be the runtime engine so the
    /// authored (logical) font paths resolve against the mounted asset root;
    /// a null engine leaves text runs unshaped (empty glyphs) and logs.
    [[nodiscard]] chronon3d::Scene materialize_scene(
        const CompiledProgram& program,
        std::pmr::memory_resource* resource = std::pmr::get_default_resource(),
        FontEngine* font_engine = nullptr) const;
};

}  // namespace chronon3d::compiler
