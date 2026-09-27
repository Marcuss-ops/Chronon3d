// LayerBuilder masks-domain methods.
// Contains mask, track matte, and transition method implementations.
// Extracted from commands/layer_property_commands.cpp as part of the
// domain split (core, transform, layout, text, shapes, effects, media, masks).

#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/model/layer/track_matte.hpp>

#include <memory_resource>
#include <algorithm>
#include <limits>
#include <utility>

namespace chronon3d {

// ── Mask ──────────────────────────────────────────────────────────────

LayerBuilder& LayerBuilder::mask_rect(RectMaskParams p) {
    m_layer.mask.operations.clear();
    m_layer.mask.path.clear();
    m_layer.mask.type = MaskType::Rect;
    m_layer.mask.size = p.size;
    m_layer.mask.pos = p.pos;
    m_layer.mask.feather = p.feather;
    m_layer.mask.expansion = p.expansion;
    m_layer.mask.inverted = p.inverted;
    return *this;
}

LayerBuilder& LayerBuilder::mask_rounded_rect(RoundedRectMaskParams p) {
    m_layer.mask.operations.clear();
    m_layer.mask.path.clear();
    m_layer.mask.type = MaskType::RoundedRect;
    m_layer.mask.size = p.size;
    m_layer.mask.radius = p.radius;
    m_layer.mask.pos = p.pos;
    m_layer.mask.feather = p.feather;
    m_layer.mask.expansion = p.expansion;
    m_layer.mask.inverted = p.inverted;
    return *this;
}

LayerBuilder& LayerBuilder::mask_circle(CircleMaskParams p) {
    m_layer.mask.operations.clear();
    m_layer.mask.path.clear();
    m_layer.mask.type = MaskType::Circle;
    m_layer.mask.size = {p.radius * 2.0f, p.radius * 2.0f};
    m_layer.mask.radius = p.radius;
    m_layer.mask.pos = p.pos;
    m_layer.mask.feather = p.feather;
    m_layer.mask.expansion = p.expansion;
    m_layer.mask.inverted = p.inverted;
    return *this;
}

LayerBuilder& LayerBuilder::mask_ellipse(EllipseMaskParams p) {
    m_layer.mask.operations.clear();
    m_layer.mask.path.clear();
    m_layer.mask.type = MaskType::Ellipse;
    m_layer.mask.size = p.size;
    m_layer.mask.pos = p.pos;
    m_layer.mask.feather = p.feather;
    m_layer.mask.expansion = p.expansion;
    m_layer.mask.inverted = p.inverted;
    return *this;
}

LayerBuilder& LayerBuilder::mask_path(PathMaskParams p) {
    m_layer.mask.operations.clear();
    m_layer.mask.type = MaskType::Path;
    m_layer.mask.path = std::move(p.commands);
    m_layer.mask.pos = p.pos;
    m_layer.mask.feather = p.feather;
    m_layer.mask.expansion = p.expansion;
    m_layer.mask.inverted = p.inverted;
    if (m_layer.mask.path.empty()) {
        m_layer.mask.type = MaskType::None;
        return *this;
    }
    f32 min_x = std::numeric_limits<f32>::max();
    f32 min_y = std::numeric_limits<f32>::max();
    f32 max_x = std::numeric_limits<f32>::lowest();
    f32 max_y = std::numeric_limits<f32>::lowest();
    for (const auto& command : m_layer.mask.path) {
        if (command.type == PathCommandType::Close) continue;
        min_x = std::min(min_x, command.p0.x); min_y = std::min(min_y, command.p0.y);
        max_x = std::max(max_x, command.p0.x); max_y = std::max(max_y, command.p0.y);
        if (command.type == PathCommandType::QuadraticTo || command.type == PathCommandType::CubicTo) {
            min_x = std::min(min_x, command.p1.x); min_y = std::min(min_y, command.p1.y);
            max_x = std::max(max_x, command.p1.x); max_y = std::max(max_y, command.p1.y);
        }
        if (command.type == PathCommandType::CubicTo) {
            min_x = std::min(min_x, command.p2.x); min_y = std::min(min_y, command.p2.y);
            max_x = std::max(max_x, command.p2.x); max_y = std::max(max_y, command.p2.y);
        }
    }
    const Vec2 center{(min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f};
    for (auto& command : m_layer.mask.path) {
        if (command.type == PathCommandType::Close) continue;
        command.p0 -= center;
        if (command.type == PathCommandType::QuadraticTo || command.type == PathCommandType::CubicTo)
            command.p1 -= center;
        if (command.type == PathCommandType::CubicTo) command.p2 -= center;
    }
    m_layer.mask.pos += Vec3{center, 0.0f};
    m_layer.mask.size = {max_x - min_x, max_y - min_y};
    return *this;
}

LayerBuilder& LayerBuilder::mask_composite(Mask mask) {
    m_layer.mask = std::move(mask);
    return *this;
}

// ── Track matte ───────────────────────────────────────────────────────

LayerBuilder& LayerBuilder::track_matte_alpha(std::string src) {
    m_layer.track_matte.type = TrackMatteType::Alpha;
    m_layer.track_matte.source_layer = std::pmr::string{src, m_layer.track_matte.source_layer.get_allocator().resource()};
    return *this;
}

LayerBuilder& LayerBuilder::track_matte_alpha_inverted(std::string src) {
    m_layer.track_matte.type = TrackMatteType::AlphaInverted;
    m_layer.track_matte.source_layer = std::pmr::string{src, m_layer.track_matte.source_layer.get_allocator().resource()};
    return *this;
}

LayerBuilder& LayerBuilder::track_matte_luma(std::string src) {
    m_layer.track_matte.type = TrackMatteType::Luma;
    m_layer.track_matte.source_layer = std::pmr::string{src, m_layer.track_matte.source_layer.get_allocator().resource()};
    return *this;
}

LayerBuilder& LayerBuilder::track_matte_luma_inverted(std::string src) {
    m_layer.track_matte.type = TrackMatteType::LumaInverted;
    m_layer.track_matte.source_layer = std::pmr::string{src, m_layer.track_matte.source_layer.get_allocator().resource()};
    return *this;
}

// ── Transitions ───────────────────────────────────────────────────────

LayerBuilder& LayerBuilder::transition_in(LayerTransitionSpec spec) {
    // A layer transition changes the rendered pixels over time.  This must
    // override the text builder's conservative static-content classification;
    // otherwise a text layer is cached at frame 0 (transparent during its
    // fade-in) and never becomes visible on later frames.
    if (!spec.transition_id.empty() && spec.transition_id != "none") {
        m_layer.cache_static = false;
    }
    m_layer.transition_in = std::move(spec);
    return *this;
}

LayerBuilder& LayerBuilder::transition_out(LayerTransitionSpec spec) {
    if (!spec.transition_id.empty() && spec.transition_id != "none") {
        m_layer.cache_static = false;
    }
    m_layer.transition_out = std::move(spec);
    return *this;
}

} // namespace chronon3d
