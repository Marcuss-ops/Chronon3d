#pragma once

#include <chronon3d/runtime/render_preparation.hpp>

namespace chronon3d {
class Scene;
class SoftwareRenderer;
struct CompositionSpec;
} // namespace chronon3d

namespace chronon3d::runtime::detail {

RenderPreparationResult prepare_render_scene(
    SoftwareRenderer* renderer,
    const Scene& scene,
    const CompositionSpec& spec,
    const RenderPreparationOptions& options);

} // namespace chronon3d::runtime::detail
