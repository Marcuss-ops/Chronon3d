#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/core/frame.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/scene/mask/mask.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/scene/layer/depth_role.hpp>
#include <chronon3d/scene/layer/track_matte.hpp>
#include <chronon3d/scene/material_2_5d.hpp>
#include <chronon3d/layout/layout_rules.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <string>
#include <vector>
#include <memory>
#include <memory_resource>

namespace chronon3d {

namespace video { struct VideoSource; }

enum class LayerKind {
    Normal,       // standard layer: draws its own content, then effects applied
    Adjustment,   // no own content: effects applied to everything rendered below it
    Null,         // no rendering at all; useful as a parent for transform hierarchy
    Precomp,      // references a nested composition by name
    Video,        // plays a video file via VideoNode
    Glass         // frosted glass: blurs background within shapes, then draws content
};

struct Layer {
    std::pmr::string name;
    std::pmr::string parent_name;
    LayerKind kind{LayerKind::Normal};
    Transform transform{};
    Frame from{0};
    Frame duration{-1};
    bool      visible{true};
    bool      is_3d{false};
    bool      hierarchy_resolved{false};
    Mask      mask{};
    EffectStack effects;     // ordered effect stack
    BlendMode blend_mode{BlendMode::Normal};
    DepthRole   depth_role{DepthRole::None};
    f32         depth_offset{0.0f};
    LayoutRules layout{};
    TrackMatte  track_matte{};
    Material2_5D material{};
    std::pmr::vector<RenderNode> nodes;
    std::pmr::string precomp_composition_name; // for LayerKind::Precomp
    
    // Video source parameters (isolated from FFmpeg/Backend headers)
    std::unique_ptr<video::VideoSource> video_source;

    explicit Layer(std::pmr::memory_resource* res = std::pmr::get_default_resource());
    ~Layer();
    
    // Custom copy/move for unique_ptr
    Layer(const Layer& other);
    Layer& operator=(const Layer& other);
    Layer(Layer&& other) noexcept;
    Layer& operator=(Layer&& other) noexcept;

    [[nodiscard]] bool active_at(Frame frame) const {
        if (!visible) return false;
        if (frame < from) return false;
        if (duration < 0) return true;
        return frame < from + duration;
    }
};

} // namespace chronon3d
