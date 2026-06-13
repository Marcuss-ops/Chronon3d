#include <chronon3d/scene/model/core/mask_utils.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace chronon3d {

namespace detail {

bool point_in_rect_mask(Vec2 local, const Mask& mask) {
    const f32 hw = mask.size.x * 0.5f;
    const f32 hh = mask.size.y * 0.5f;
    return local.x >= -hw && local.x <= hw && local.y >= -hh && local.y <= hh;
}

bool point_in_circle_mask(Vec2 local, const Mask& mask) {
    const f32 r = mask.radius;
    return local.x * local.x + local.y * local.y <= r * r;
}

bool point_in_rounded_rect_mask(Vec2 local, const Mask& mask) {
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

bool mask_contains_local_point(const Mask& mask, Vec2 local) {
    if (!mask.enabled()) return true;

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

std::uint64_t hash_mask_cache_key(const Mask& mask, const Mat4& layer_inv_matrix, i32 width, i32 height) {
    auto mix = [](std::uint64_t seed, std::uint64_t value) {
        seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        return seed;
    };
    auto hv = [](auto value) -> std::uint64_t {
        return static_cast<std::uint64_t>(std::hash<std::decay_t<decltype(value)>>{}(value));
    };

    std::uint64_t seed = 0;
    seed = mix(seed, static_cast<std::uint64_t>(mask.type));
    seed = mix(seed, hv(mask.pos.x));
    seed = mix(seed, hv(mask.pos.y));
    seed = mix(seed, hv(mask.pos.z));
    seed = mix(seed, hv(mask.size.x));
    seed = mix(seed, hv(mask.size.y));
    seed = mix(seed, hv(mask.radius));
    seed = mix(seed, static_cast<std::uint64_t>(mask.inverted));
    seed = mix(seed, static_cast<std::uint64_t>(width));
    seed = mix(seed, static_cast<std::uint64_t>(height));
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            seed = mix(seed, hv(layer_inv_matrix[i][j]));
        }
    }
    return seed;
}

std::shared_ptr<Framebuffer> rasterize_mask_alpha(const Mask& mask, const Mat4& layer_inv_matrix, i32 width, i32 height) {
    auto fb = std::make_shared<Framebuffer>(width, height);
    fb->clear(Color::transparent());
    if (!mask.enabled() || width <= 0 || height <= 0) {
        return fb;
    }

    const Mat4 screen_from_local = glm::inverse(layer_inv_matrix);
    const f32 hw = (mask.type == MaskType::Circle) ? mask.radius : mask.size.x * 0.5f;
    const f32 hh = (mask.type == MaskType::Circle) ? mask.radius : mask.size.y * 0.5f;

    const Vec4 corners[4] = {
        screen_from_local * Vec4{-hw + mask.pos.x, -hh + mask.pos.y, 0.0f, 1.0f},
        screen_from_local * Vec4{ hw + mask.pos.x, -hh + mask.pos.y, 0.0f, 1.0f},
        screen_from_local * Vec4{ hw + mask.pos.x,  hh + mask.pos.y, 0.0f, 1.0f},
        screen_from_local * Vec4{-hw + mask.pos.x,  hh + mask.pos.y, 0.0f, 1.0f},
    };

    f32 min_x = std::numeric_limits<f32>::max();
    f32 min_y = std::numeric_limits<f32>::max();
    f32 max_x = std::numeric_limits<f32>::lowest();
    f32 max_y = std::numeric_limits<f32>::lowest();
    for (const auto& c : corners) {
        if (std::abs(c.w) < 1e-7f) continue;
        const f32 x = c.x / c.w;
        const f32 y = c.y / c.w;
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
    }

    raster::BBox bbox{
        static_cast<i32>(std::floor(min_x)),
        static_cast<i32>(std::floor(min_y)),
        static_cast<i32>(std::ceil(max_x)),
        static_cast<i32>(std::ceil(max_y))
    };
    bbox.clip_to(width, height);
    if (bbox.is_empty()) {
        return fb;
    }

    for (i32 y = bbox.y0; y < bbox.y1; ++y) {
        Color* row = fb->pixels_row(y);
        for (i32 x = bbox.x0; x < bbox.x1; ++x) {
            Vec4 local = layer_inv_matrix * Vec4(static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, 0.0f, 1.0f);
            if (mask_contains_local_point(mask, Vec2{local.x, local.y})) {
                row[x] = {1.0f, 1.0f, 1.0f, 1.0f};
            }
        }
    }

    return fb;
}

} // namespace chronon3d
