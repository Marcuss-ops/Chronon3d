#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/scene/model/shape/fill.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/layer/mask.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <xxhash.h>

#include <bit>
#include <type_traits>
#include <string_view>
// <variant> is transitively included via effect_stack.hpp → effect_params.hpp

namespace chronon3d::graph {

[[nodiscard]] inline u64 hash_combine(u64 seed, u64 value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

[[nodiscard]] inline u64 hash_bytes(const void* data, usize size) {
    return XXH64(data, size, 0);
}

[[nodiscard]] inline u64 hash_string(std::string_view value) {
    return XXH64(value.data(), value.size(), 0);
}

template <typename T>
[[nodiscard]] inline u64 hash_value(const T& value) {
    return hash_bytes(&value, sizeof(value));
}

[[nodiscard]] inline u64 hash_vec2(const Vec2& value) {
    return hash_bytes(&value, sizeof(value));
}

[[nodiscard]] inline u64 hash_vec3(const Vec3& value) {
    return hash_bytes(&value, sizeof(value));
}

[[nodiscard]] inline u64 hash_color(const Color& color) {
    return hash_bytes(&color, sizeof(color));
}

[[nodiscard]] inline u64 hash_transform(const Transform& transform) {
    u64 h = hash_vec3(transform.position);
    h = hash_combine(h, hash_vec3(transform.anchor));
    h = hash_combine(h, hash_vec3(transform.scale));
    h = hash_combine(h, hash_bytes(&transform.rotation, sizeof(transform.rotation)));
    h = hash_combine(h, hash_bytes(&transform.opacity, sizeof(transform.opacity)));
    return h;
}

[[nodiscard]] inline u64 hash_surface_policy(SurfacePolicy policy) {
    return hash_bytes(&policy, sizeof(policy));
}

[[nodiscard]] inline u64 hash_transform_policy(TransformPolicy policy) {
    return hash_bytes(&policy, sizeof(policy));
}

[[nodiscard]] inline u64 hash_video_source(const video::VideoSource& source) {
    u64 seed = hash_string(source.path);
    seed = hash_combine(seed, hash_bytes(&source.source_start, sizeof(source.source_start)));
    seed = hash_combine(seed, hash_bytes(&source.duration, sizeof(source.duration)));
    seed = hash_combine(seed, hash_bytes(&source.source_fps, sizeof(source.source_fps)));
    seed = hash_combine(seed, hash_bytes(&source.speed, sizeof(source.speed)));
    const u64 loop = static_cast<u64>(source.loop_mode);
    seed = hash_combine(seed, hash_bytes(&loop, sizeof(loop)));
    seed = hash_combine(seed, hash_vec2(source.size));
    return seed;
}

[[nodiscard]] inline u64 hash_fill(const Fill& f) {
    u64 seed = hash_bytes(&f.type, sizeof(f.type));
    seed = hash_combine(seed, hash_color(f.solid));
    for (const auto& stop : f.gradient.stops) {
        seed = hash_combine(seed, hash_bytes(&stop.offset, sizeof(f32)));
        seed = hash_combine(seed, hash_color(stop.color));
    }
    seed = hash_combine(seed, hash_vec2(f.gradient.from));
    seed = hash_combine(seed, hash_vec2(f.gradient.to));
    return seed;
}

[[nodiscard]] inline u64 hash_blur_params(const BlurParams& p) {
    return hash_value(p.radius);
}

[[nodiscard]] inline u64 hash_tint_params(const TintParams& p) {
    u64 seed = hash_color(p.color);
    return hash_combine(seed, hash_value(p.amount));
}

[[nodiscard]] inline u64 hash_brightness_params(const BrightnessParams& p) {
    return hash_value(p.value);
}

[[nodiscard]] inline u64 hash_contrast_params(const ContrastParams& p) {
    return hash_value(p.value);
}

[[nodiscard]] inline u64 hash_drop_shadow_params(const DropShadowParams& p) {
    u64 seed = hash_vec2(p.offset);
    seed = hash_combine(seed, hash_color(p.color));
    return hash_combine(seed, hash_value(p.radius));
}

[[nodiscard]] inline u64 hash_glow_params(const GlowParams& p) {
    u64 seed = hash_value(p.radius);
    seed = hash_combine(seed, hash_value(p.intensity));
    seed = hash_combine(seed, hash_color(p.color));
    seed = hash_combine(seed, hash_value(p.threshold));
    seed = hash_combine(seed, hash_value(p.spread));
    seed = hash_combine(seed, hash_value(p.softness));
    seed = hash_combine(seed, hash_value(p.falloff));
    seed = hash_combine(seed, hash_value(p.core_strength));
    seed = hash_combine(seed, hash_value(p.aura_strength));
    seed = hash_combine(seed, hash_value(p.bloom_strength));
    seed = hash_combine(seed, hash_value(p.outer_downscale));
    seed = hash_combine(seed, hash_value(p.preserve_source));
    return hash_combine(seed, hash_value(p.additive));
}

[[nodiscard]] inline u64 hash_focus_in_ladder_params(const FocusInLadderParams& p) {
    u64 seed = 0;
    for (f32 level : p.levels)
        seed = hash_combine(seed, hash_value(level));
    seed = hash_combine(seed, hash_value(static_cast<int>(p.duration)));
    seed = hash_combine(seed, hash_value(static_cast<int>(p.easing.preset)));
    if (p.easing.cubic.has_value()) {
        seed = hash_combine(seed, hash_value(p.easing.cubic->x1));
        seed = hash_combine(seed, hash_value(p.easing.cubic->y1));
        seed = hash_combine(seed, hash_value(p.easing.cubic->x2));
        seed = hash_combine(seed, hash_value(p.easing.cubic->y2));
    }
    seed = hash_combine(seed, hash_value(p.interpolate_between_levels));
    seed = hash_combine(seed, hash_value(p.scale_start));
    return hash_combine(seed, hash_value(p.scale_end));
}

[[nodiscard]] inline u64 hash_bloom_params(const BloomParams& p) {
    u64 seed = hash_value(p.threshold);
    seed = hash_combine(seed, hash_value(p.radius));
    return hash_combine(seed, hash_value(p.intensity));
}

[[nodiscard]] inline u64 hash_effect_stack(const EffectStack& effects) {
    static thread_local int depth = 0;
    if (depth > 50) return 0; // Prevent stack overflow
    depth++;
    struct DepthGuard { ~DepthGuard() { depth--; } } guard;
    u64 seed = 0;
    for (const auto& e : effects) {
        if (!e.enabled) {
            continue;
        }
        seed = hash_combine(seed, hash_string(e.descriptor.id));
        using enum effects::EffectType;
        switch (e.effect_type) {
        case Blur: {
            if (auto* p = std::get_if<BlurParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case Tint: {
            if (auto* p = std::get_if<TintParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case Brightness: {
            if (auto* p = std::get_if<BrightnessParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case Contrast: {
            if (auto* p = std::get_if<ContrastParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case DropShadow: {
            if (auto* p = std::get_if<DropShadowParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case Glow: {
            if (auto* p = std::get_if<GlowParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case Bloom: {
            if (auto* p = std::get_if<BloomParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case Fake3DWave: {
            if (auto* p = std::get_if<Fake3DWaveParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case Saturation: {
            if (auto* p = std::get_if<SaturationParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case HueRotate: {
            if (auto* p = std::get_if<HueRotateParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case Invert: {
            if (auto* p = std::get_if<InvertParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case Vignette: {
            if (auto* p = std::get_if<VignetteParams>(&e.params))
                seed = hash_combine(seed, hash_bytes(p, sizeof(*p)));
            break;
        }
        case FocusInLadder: {
            if (auto* p = std::get_if<FocusInLadderParams>(&e.params))
                seed = hash_combine(seed, hash_focus_in_ladder_params(*p));
            break;
        }
        case Unknown:
            break;
        }
    }
    return seed;
}

[[nodiscard]] inline u64 hash_mask(const Mask& m) {
    if (!m.enabled()) {
        return 0;
    }
    u64 seed = hash_value(static_cast<int>(m.type));
    seed = hash_combine(seed, hash_vec3(m.pos));
    seed = hash_combine(seed, hash_vec2(m.size));
    seed = hash_combine(seed, hash_value(m.radius));
    seed = hash_combine(seed, hash_value(m.inverted));
    return seed;
}

[[nodiscard]] inline u64 hash_shape(const Shape& s) {
    u64 seed = hash_value(static_cast<int>(s.type));
    switch (s.type) {
        case ShapeType::Rect:
            seed = hash_combine(seed, hash_vec2(s.rect.size));
            seed = hash_combine(seed, hash_value(s.rect.stroke.enabled));
            seed = hash_combine(seed, hash_color(s.rect.stroke.color));
            seed = hash_combine(seed, hash_value(s.rect.stroke.width));
            return hash_combine(seed, hash_bytes(&s.rect.stroke.alignment, sizeof(StrokeAlignment)));
        case ShapeType::RoundedRect:
            seed = hash_combine(seed, hash_vec2(s.rounded_rect.size));
            seed = hash_combine(seed, hash_bytes(&s.rounded_rect.radius, sizeof(f32)));
            seed = hash_combine(seed, hash_value(s.rounded_rect.stroke.enabled));
            seed = hash_combine(seed, hash_color(s.rounded_rect.stroke.color));
            seed = hash_combine(seed, hash_value(s.rounded_rect.stroke.width));
            return hash_combine(seed, hash_bytes(&s.rounded_rect.stroke.alignment, sizeof(StrokeAlignment)));
        case ShapeType::Circle:
            seed = hash_combine(seed, hash_bytes(&s.circle.radius, sizeof(f32)));
            seed = hash_combine(seed, hash_value(s.circle.stroke.enabled));
            seed = hash_combine(seed, hash_color(s.circle.stroke.color));
            seed = hash_combine(seed, hash_value(s.circle.stroke.width));
            return hash_combine(seed, hash_bytes(&s.circle.stroke.alignment, sizeof(StrokeAlignment)));
        case ShapeType::Line:
            seed = hash_combine(seed, hash_vec3(s.line.to));
            seed = hash_combine(seed, hash_bytes(&s.line.thickness, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.line.stroke.trim_start, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.line.stroke.trim_end, sizeof(f32)));
            seed = hash_combine(seed, hash_value(s.line.stroke.enabled));
            seed = hash_combine(seed, hash_color(s.line.stroke.color));
            seed = hash_combine(seed, hash_value(s.line.stroke.width));
            return hash_combine(seed, hash_bytes(&s.line.stroke.alignment, sizeof(StrokeAlignment)));
        case ShapeType::Path:
            seed = hash_combine(seed, hash_value(s.path.commands.size()));
            for (const auto& cmd : s.path.commands) {
                seed = hash_combine(seed, hash_value(static_cast<int>(cmd.type)));
                seed = hash_combine(seed, hash_vec2(cmd.p0));
                seed = hash_combine(seed, hash_vec2(cmd.p1));
                seed = hash_combine(seed, hash_vec2(cmd.p2));
            }
            seed = hash_combine(seed, hash_bytes(&s.path.stroke.width, sizeof(f32)));
            seed = hash_combine(seed, hash_value(static_cast<int>(s.path.stroke.cap)));
            seed = hash_combine(seed, hash_value(static_cast<int>(s.path.stroke.join)));
            seed = hash_combine(seed, hash_bytes(&s.path.stroke.trim_start, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.path.stroke.trim_end, sizeof(f32)));
            seed = hash_combine(seed, hash_value(s.path.closed));
            seed = hash_combine(seed, hash_value(static_cast<int>(s.path.fill.type)));
            seed = hash_combine(seed, hash_color(s.path.fill.solid));
            seed = hash_combine(seed, hash_vec2(s.path.fill.gradient.from));
            seed = hash_combine(seed, hash_vec2(s.path.fill.gradient.to));
            seed = hash_combine(seed, hash_value(s.path.fill.gradient.stops.size()));
            for (const auto& stop : s.path.fill.gradient.stops) {
                seed = hash_combine(seed, hash_bytes(&stop.offset, sizeof(f32)));
                seed = hash_combine(seed, hash_color(stop.color));
            }
            return seed;
        case ShapeType::Image:
            seed = hash_combine(seed, hash_bytes(s.image.path.data(), s.image.path.size()));
            seed = hash_combine(seed, hash_vec2(s.image.size));
            return hash_combine(seed, hash_bytes(&s.image.opacity, sizeof(f32)));
        case ShapeType::GridBackground:
            seed = hash_combine(seed, hash_vec2(s.grid_background.size));
            seed = hash_combine(seed, hash_vec2(s.grid_background.offset));
            seed = hash_combine(seed, hash_color(s.grid_background.bg_color));
            seed = hash_combine(seed, hash_color(s.grid_background.grid_color));
            seed = hash_combine(seed, hash_bytes(&s.grid_background.spacing, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.grid_background.minor_thickness, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.grid_background.major_thickness, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.grid_background.major_every, sizeof(i32)));
            return hash_combine(seed, hash_bytes(&s.grid_background.centered, sizeof(bool)));
        case ShapeType::Text:
            seed = hash_combine(seed, hash_bytes(s.text.text.data(), s.text.text.size()));
            seed = hash_combine(seed, hash_bytes(s.text.style.font_path.data(), s.text.style.font_path.size()));
            seed = hash_combine(seed, hash_bytes(s.text.style.font_family.data(), s.text.style.font_family.size()));
            seed = hash_combine(seed, hash_bytes(s.text.style.font_style.data(), s.text.style.font_style.size()));
            seed = hash_combine(seed, hash_vec2(s.text.box.size));
            seed = hash_combine(seed, hash_value(s.text.box.enabled));
            seed = hash_combine(seed, hash_bytes(&s.text.style.font_weight, sizeof(int)));
            seed = hash_combine(seed, hash_bytes(&s.text.style.size, sizeof(f32)));
            seed = hash_combine(seed, hash_color(s.text.style.color));
            seed = hash_combine(seed, hash_bytes(&s.text.style.align, sizeof(TextAlign)));
            seed = hash_combine(seed, hash_bytes(&s.text.style.line_height, sizeof(f32)));
            return hash_combine(seed, hash_bytes(&s.text.style.tracking, sizeof(f32)));
        case ShapeType::FakeBox3D:
            seed = hash_combine(seed, hash_vec3(s.fake_box3d.world_pos));
            seed = hash_combine(seed, hash_vec2(s.fake_box3d.size));
            seed = hash_combine(seed, hash_bytes(&s.fake_box3d.depth, sizeof(f32)));
            seed = hash_combine(seed, hash_color(s.fake_box3d.color));
            seed = hash_combine(seed, hash_bytes(&s.fake_box3d.top_tint, sizeof(f32)));
            return hash_combine(seed, hash_bytes(&s.fake_box3d.side_tint, sizeof(f32)));
        case ShapeType::GridPlane:
            seed = hash_combine(seed, hash_vec3(s.grid_plane.world_pos));
            seed = hash_combine(seed, hash_value(static_cast<int>(s.grid_plane.axis)));
            seed = hash_combine(seed, hash_bytes(&s.grid_plane.extent, sizeof(f32)));
            seed = hash_combine(seed, hash_bytes(&s.grid_plane.spacing, sizeof(f32)));
            seed = hash_combine(seed, hash_color(s.grid_plane.color));
            seed = hash_combine(seed, hash_bytes(&s.grid_plane.fade_distance, sizeof(f32)));
            return hash_combine(seed, hash_bytes(&s.grid_plane.fade_min_alpha, sizeof(f32)));
        default:
            return seed;
    }
}

// ── Text hashing (canonical, used by all text caches) ─────────────

[[nodiscard]] inline u64 hash_text_style_full(
    const TextShape& t,
    float effective_size,
    int padding,
    const Mat4* transform = nullptr
) {
    u64 seed = 0;
    seed = hash_combine(seed, hash_string(t.text));
    seed = hash_combine(seed, hash_string(t.style.font_path));
    seed = hash_combine(seed, hash_string(t.style.font_family));
    seed = hash_combine(seed, hash_value(t.style.font_weight));
    seed = hash_combine(seed, hash_string(t.style.font_style));
    seed = hash_combine(seed, hash_value(effective_size));
    seed = hash_combine(seed, hash_color(t.style.color));
    seed = hash_combine(seed, hash_bytes(&t.style.align, sizeof(TextAlign)));
    seed = hash_combine(seed, hash_value(t.style.line_height));
    seed = hash_combine(seed, hash_value(t.style.tracking));
    seed = hash_combine(seed, hash_value(t.style.max_lines));
    seed = hash_combine(seed, hash_value(t.style.auto_scale));
    seed = hash_combine(seed, hash_value(t.style.min_size));
    seed = hash_combine(seed, hash_value(t.style.max_size));
    seed = hash_combine(seed, hash_value(t.style.auto_fit));
    seed = hash_combine(seed, hash_value(t.style.ellipsis));
    seed = hash_combine(seed, hash_bytes(&t.style.overflow, sizeof(TextOverflow)));
    seed = hash_combine(seed, hash_bytes(&t.style.wrap, sizeof(TextWrap)));
    seed = hash_combine(seed, hash_vec2(t.box.size));
    seed = hash_combine(seed, hash_value(t.box.enabled));
    seed = hash_combine(seed, hash_value(padding));

    // Paint
    seed = hash_combine(seed, hash_color(t.style.paint.fill));
    seed = hash_combine(seed, hash_value(t.style.paint.stroke_enabled));
    seed = hash_combine(seed, hash_color(t.style.paint.stroke_color));
    seed = hash_combine(seed, hash_value(t.style.paint.stroke_width));

    // Box style
    seed = hash_combine(seed, hash_value(t.style.box_style.enabled));
    seed = hash_combine(seed, hash_vec2(t.style.box_style.padding));
    seed = hash_combine(seed, hash_value(t.style.box_style.radius));
    seed = hash_combine(seed, hash_color(t.style.box_style.background));
    seed = hash_combine(seed, hash_value(t.style.box_style.border_enabled));
    seed = hash_combine(seed, hash_color(t.style.box_style.border_color));
    seed = hash_combine(seed, hash_value(t.style.box_style.border_width));

    // Vertical align
    seed = hash_combine(seed, hash_bytes(&t.style.vertical_align, sizeof(VerticalAlign)));

    // Shadows
    seed = hash_combine(seed, hash_value(t.style.shadows.size()));
    for (const auto& shadow : t.style.shadows) {
        seed = hash_combine(seed, hash_value(shadow.enabled));
        seed = hash_combine(seed, hash_vec2(shadow.offset));
        seed = hash_combine(seed, hash_value(shadow.blur));
        seed = hash_combine(seed, hash_value(shadow.opacity));
        seed = hash_combine(seed, hash_color(shadow.color));
    }

    // Transform
    if (transform) {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                seed = hash_combine(seed, hash_value((*transform)[i][j]));
            }
        }
    }

    return seed;
}

[[nodiscard]] inline u64 hash_render_node_content_only(const RenderNode& n) {
    u64 seed = hash_bytes(n.name.data(), n.name.size());
    // Ignore world_transform for content-only hashing
    seed = hash_combine(seed, hash_shape(n.shape));
    seed = hash_combine(seed, hash_color(n.color));
    seed = hash_combine(seed, hash_fill(n.fill));
    seed = hash_combine(seed, hash_value(n.visible));
    seed = hash_combine(seed, hash_surface_policy(n.surface_policy));
    seed = hash_combine(seed, hash_transform_policy(n.transform_policy));
    if (n.shadow.enabled) {
        seed = hash_combine(seed, hash_vec2(n.shadow.offset));
        seed = hash_combine(seed, hash_color(n.shadow.color));
        seed = hash_combine(seed, hash_bytes(&n.shadow.radius, sizeof(f32)));
    }
    if (n.glow.enabled) {
        seed = hash_combine(seed, hash_bytes(&n.glow.radius, sizeof(f32)));
        seed = hash_combine(seed, hash_bytes(&n.glow.intensity, sizeof(f32)));
        seed = hash_combine(seed, hash_color(n.glow.color));
    }
    return seed;
}

[[nodiscard]] inline u64 hash_render_node_placement_only(const RenderNode& n) {
    return hash_transform(n.world_transform);
}

[[nodiscard]] inline u64 hash_render_node(const RenderNode& n) {
    static thread_local int depth = 0;
    if (depth > 50) return 0; // Prevent stack overflow
    depth++;
    struct DepthGuard { ~DepthGuard() { depth--; } } guard;

    u64 seed = hash_render_node_content_only(n);
    return hash_combine(seed, hash_render_node_placement_only(n));
}

} // namespace chronon3d::graph
