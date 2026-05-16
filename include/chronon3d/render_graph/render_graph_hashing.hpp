#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/vec3.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/scene/mask/mask.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <xxhash.h>

#include <bit>
#include <type_traits>
#include <string_view>
#include <variant>

namespace chronon3d::graph {

[[nodiscard]] inline u64 hash_combine(u64 seed, u64 value) {
    seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
}

[[nodiscard]] inline u64 hash_bytes(const void* data, usize size) {
    return XXH64(data, size, 0);
}

[[nodiscard]] inline u64 hash_string(std::string_view value) {
    return XXH64(value.data(), value.size(), 0);
}

template <typename T>
[[nodiscard]] inline u64 hash_value(T value) {
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

[[nodiscard]] inline u64 hash_video_source(const video::VideoSource& source) {
    u64 seed = hash_string(source.path);
    seed = hash_combine(seed, hash_bytes(&source.source_start, sizeof(source.source_start)));
    seed = hash_combine(seed, hash_bytes(&source.duration, sizeof(source.duration)));
    seed = hash_combine(seed, hash_bytes(&source.source_fps, sizeof(source.source_fps)));
    seed = hash_combine(seed, hash_bytes(&source.speed, sizeof(source.speed)));
    const u64 loop = static_cast<u64>(source.loop_mode);
    seed = hash_combine(seed, hash_bytes(&loop, sizeof(loop)));
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
    return hash_combine(seed, hash_color(p.color));
}

[[nodiscard]] inline u64 hash_effect_params(const EffectParams& p) {
    return std::visit([](const auto& params) -> u64 {
        using T = std::decay_t<decltype(params)>;
        if constexpr (std::is_same_v<T, BlurParams>) return hash_combine(1, hash_blur_params(params));
        if constexpr (std::is_same_v<T, TintParams>) return hash_combine(2, hash_tint_params(params));
        if constexpr (std::is_same_v<T, BrightnessParams>) return hash_combine(3, hash_brightness_params(params));
        if constexpr (std::is_same_v<T, ContrastParams>) return hash_combine(4, hash_contrast_params(params));
        if constexpr (std::is_same_v<T, DropShadowParams>) return hash_combine(5, hash_drop_shadow_params(params));
        if constexpr (std::is_same_v<T, GlowParams>) return hash_combine(6, hash_glow_params(params));
        return 0;
    }, p);
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
        if (auto* params = std::any_cast<EffectParams>(&e.params)) {
            seed = hash_combine(seed, hash_effect_params(*params));
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
            return hash_combine(seed, hash_vec2(s.rect.size));
        case ShapeType::RoundedRect:
            seed = hash_combine(seed, hash_vec2(s.rounded_rect.size));
            return hash_combine(seed, hash_value(s.rounded_rect.radius));
        case ShapeType::Circle:
            return hash_combine(seed, hash_value(s.circle.radius));
        case ShapeType::Line:
            seed = hash_combine(seed, hash_vec3(s.line.to));
            return hash_combine(seed, hash_value(s.line.thickness));
        case ShapeType::Text:
            seed = hash_combine(seed, hash_bytes(s.text.text.data(), s.text.text.size()));
            seed = hash_combine(seed, hash_bytes(s.text.style.font_path.data(), s.text.style.font_path.size()));
            return hash_combine(seed, hash_value(s.text.style.size));
        case ShapeType::Image:
            seed = hash_combine(seed, hash_bytes(s.image.path.data(), s.image.path.size()));
            return hash_combine(seed, hash_vec2(s.image.size));
        default:
            return seed;
    }
}

[[nodiscard]] inline u64 hash_render_node(const RenderNode& n) {
    static thread_local int depth = 0;
    if (depth > 50) return 0; // Prevent stack overflow
    depth++;
    struct DepthGuard { ~DepthGuard() { depth--; } } guard;

    u64 seed = hash_bytes(n.name.data(), n.name.size());
    seed = hash_combine(seed, hash_transform(n.world_transform));
    seed = hash_combine(seed, hash_shape(n.shape));
    seed = hash_combine(seed, hash_color(n.color));
    seed = hash_combine(seed, hash_value(n.visible));
    if (n.shadow.enabled) {
        seed = hash_combine(seed, hash_vec2(n.shadow.offset));
        seed = hash_combine(seed, hash_color(n.shadow.color));
        seed = hash_combine(seed, hash_value(n.shadow.radius));
    }
    if (n.glow.enabled) {
        seed = hash_combine(seed, hash_value(n.glow.radius));
        seed = hash_combine(seed, hash_value(n.glow.intensity));
        seed = hash_combine(seed, hash_color(n.glow.color));
    }
    return seed;
}

} // namespace chronon3d::graph
