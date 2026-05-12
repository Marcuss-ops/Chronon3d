#pragma once

#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/depth_role.hpp>
#include <chronon3d/scene/layer_effect.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/description/visual_desc.hpp>
#include <string>
#include <vector>

namespace chronon3d {

// Result of evaluating one LayerDesc at a specific frame.
// All animated values are resolved; no AnimatedValue<T> here.
struct EvaluatedLayer {
    std::string name;

    bool      visible{true};
    Transform world_transform;     // position, rotation (Quat), scale
    f32       opacity{1.0f};

    bool      is_3d{false};
    DepthRole depth_role{DepthRole::None};
    f32       resolved_z{0.0f};    // effective world-space Z after depth role

    BlendMode blend_mode{BlendMode::Normal};

    // Visuals are static in v1 -- copied directly from LayerDesc.
    std::vector<VisualDesc> visuals;

    // Effects resolved from EffectDesc list into the existing flat struct.
    // DropShadow and Glow are per-RenderNode; handled by LegacySceneAdapter.
    LayerEffect resolved_effect;
};

} // namespace chronon3d
