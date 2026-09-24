#include <chronon3d/scene/model/core/mask_utils.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/path_utils.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>

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

f32 mask_alpha_at_local_point(const Mask& mask, Vec2 local) {
    if (!mask.enabled()) return 1.0f;
    local.x -= mask.pos.x;
    local.y -= mask.pos.y;

    bool inside = false;
    f32 distance = std::numeric_limits<f32>::max();
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
        case MaskType::Path: {
            PathShape path;
            path.commands = mask.path;
            path.closed = true;
            for (const auto& polyline : math::flatten_path(path, 24)) {
                if (polyline.size() < 2) continue;
                for (std::size_t i = 0; i + 1 < polyline.size(); ++i) {
                    const Vec2 a = polyline[i], b = polyline[i + 1];
                    const Vec2 ab = b - a;
                    const f32 len2 = glm::dot(ab, ab);
                    const f32 t = len2 > 1e-12f
                        ? std::clamp(glm::dot(local - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
                    distance = std::min(distance, glm::length(local - (a + t * ab)));
                    if ((a.y > local.y) != (b.y > local.y)) {
                        const f32 crossing_x = a.x + (local.y - a.y) * (b.x - a.x) / (b.y - a.y);
                        if (local.x < crossing_x) inside = !inside;
                    }
                }
            }
            if (!std::isfinite(distance) || distance == std::numeric_limits<f32>::max())
                inside = false;
            else {
                const f32 signed_distance = (inside ? distance : -distance) + mask.expansion;
                if (mask.feather <= 1e-6f) inside = signed_distance >= 0.0f;
                else {
                    const f32 t = std::clamp((signed_distance + mask.feather * 0.5f) / mask.feather,
                                             0.0f, 1.0f);
                    const f32 alpha = t * t * (3.0f - 2.0f * t);
                    return mask.inverted ? 1.0f - alpha : alpha;
                }
            }
            break;
        }
        default:
            inside = true;
            break;
    }
    const f32 alpha = inside ? 1.0f : 0.0f;
    return mask.inverted ? 1.0f - alpha : alpha;
}

bool mask_contains_local_point(const Mask& mask, Vec2 local) {
    return mask_alpha_at_local_point(mask, local) >= 0.5f;
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
    seed = mix(seed, hv(mask.feather));
    seed = mix(seed, hv(mask.expansion));
    seed = mix(seed, static_cast<std::uint64_t>(mask.inverted));
    for (const auto& command : mask.path) {
        seed = mix(seed, static_cast<std::uint64_t>(command.type));
        seed = mix(seed, hv(command.p0.x)); seed = mix(seed, hv(command.p0.y));
        seed = mix(seed, hv(command.p1.x)); seed = mix(seed, hv(command.p1.y));
        seed = mix(seed, hv(command.p2.x)); seed = mix(seed, hv(command.p2.y));
    }
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
    const f32 path_pad = mask.type == MaskType::Path
        ? std::max(0.0f, mask.expansion) + std::max(0.0f, mask.feather) * 0.5f : 0.0f;
    const f32 hw = (mask.type == MaskType::Circle) ? mask.radius : mask.size.x * 0.5f + path_pad;
    const f32 hh = (mask.type == MaskType::Circle) ? mask.radius : mask.size.y * 0.5f + path_pad;

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
            const f32 alpha = mask_alpha_at_local_point(mask, Vec2{local.x, local.y});
            if (alpha > 0.0f) row[x] = {1.0f, 1.0f, 1.0f, alpha};
        }
    }

    return fb;
}

} // namespace chronon3d
