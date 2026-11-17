#pragma once

#include <chronon3d/compiler/compiled_program.hpp>

#include <memory_resource>

namespace chronon3d {
class Scene;
class FontEngine;
}

namespace chronon3d::compiler {

/// True when a shape layer carries SVG/path content whose paint, geometry or
/// operators are sampled at materialization time instead of being fixed by the
/// compiled program. Such a layer must be re-materialized per frame and must
/// stay out of frame-zero node caches.
[[nodiscard]] bool layer_has_time_varying_path(const StaticLayerProgram& layer);

/// Program-level form of `layer_has_time_varying_path`. The render pipeline
/// uses it to bypass the per-program topology cache for animated SVG paths.
[[nodiscard]] bool program_has_time_varying_paths(const CompiledProgram& program);

/// Builds the immutable Scene topology for a compiled program.
///
/// Frame-varying activity and transforms belong to FrameParameterBlock and
/// are sampled by TypedProgramSampler. SVG path content has no frame payload:
/// its paint, geometry and operators are sampled while materializing, so a
/// program that contains such content is materialized per frame instead of
/// being cached by program fingerprint alone.
class SceneMaterializer final {
public:
    /// `font_engine` shapes text layers.  It must be the runtime engine so the
    /// authored (logical) font paths resolve against the mounted asset root;
    /// a null engine leaves text runs unshaped (empty glyphs) and logs.
    /// `frame_number` is the composition frame used to sample animated SVG
    /// path paint/geometry/operators at topology-build time.
    [[nodiscard]] chronon3d::Scene materialize_scene(
        const CompiledProgram& program,
        std::pmr::memory_resource* resource = std::pmr::get_default_resource(),
        FontEngine* font_engine = nullptr,
        double frame_number = 0.0) const;
};

}  // namespace chronon3d::compiler
