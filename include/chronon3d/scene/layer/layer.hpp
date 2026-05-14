#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/core/frame.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/scene/mask/mask.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/scene/layer/depth_role.hpp>
#include <chronon3d/layout/layout_rules.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/video/video_source.hpp>
#include <string>
#include <vector>
#include <memory_resource>

namespace chronon3d {

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
    std::pmr::vector<RenderNode> nodes;
    std::pmr::string precomp_composition_name; // for LayerKind::Precomp
    video::VideoSource video_source{};         // for LayerKind::Video

    explicit Layer(std::pmr::memory_resource* res = std::pmr::get_default_resource())
        : name(res), nodes(res), precomp_composition_name(res) {}

    [[nodiscard]] bool active_at(Frame frame) const {
        if (!visible) return false;
        if (frame < from) return false;
        if (duration < 0) return true;
        return frame < from + duration;
    }
};

} // namespace chronon3d
