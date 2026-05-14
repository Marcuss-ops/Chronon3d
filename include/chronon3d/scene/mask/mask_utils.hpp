#pragma once

#include <chronon3d/scene/mask/mask.hpp>
#include <chronon3d/math/vec2.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {

namespace detail {

inline bool point_in_rect_mask(Vec2 local, const Mask& mask) {
    const f32 hw = mask.size.x * 0.5f;
    const f32 hh = mask.size.y * 0.5f;
    return local.x >= -hw && local.x <= hw && local.y >= -hh && local.y <= hh;
}

inline bool point_in_circle_mask(Vec2 local, const Mask& mask) {
    const f32 r = mask.radius;
    return local.x * local.x + local.y * local.y <= r * r;
}

inline bool point_in_rounded_rect_mask(Vec2 local, const Mask& mask) {
    const f32 hw = mask.size.x * 0.5f;
    const f32 hh = mask.size.y * 0.5f;
    const f32 r  = std::min(mask.radius, std::min(hw, hh));
    const f32 ax = std::abs(local.x);
    const f32 ay = std::abs(local.y);
    if (ax > hw || ay > hh) return false;
    const f32 inner_w = hw - r;
    const f32 inner_h = hh - r;
    if (ax <= inner_w || ay <= inner_h) return true;
    const f32 dx = ax - inner_w;
    const f32 dy = ay - inner_h;
    return dx * dx + dy * dy <= r * r;
}

} // namespace detail

// Main entry point: checks whether a point in layer-local coordinates
// falls inside (or outside, if inverted) the given mask.
// Returns true if the pixel should be drawn.
inline bool mask_contains_local_point(const Mask& mask, Vec2 local) {
    if (!mask.enabled()) return true;

    // Shift by mask offset so mask.pos acts as its center.
    local.x -= mask.pos.x;
    local.y -= mask.pos.y;

    bool inside = false;
    switch (mask.type) {
        case MaskType::Rect:
            inside = detail::point_in_rect_mask(local, mask);
            break;
        case MaskType::RoundedRect:
            inside = detail::point_in_rounded_rect_mask(local, mask);
            break;
        case MaskType::Circle:
            inside = detail::point_in_circle_mask(local, mask);
            break;
        default:
            inside = true;
            break;
    }
    return mask.inverted ? !inside : inside;
}

} // namespace chronon3d
