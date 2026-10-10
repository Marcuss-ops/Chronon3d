#include <chronon3d/scene/model/core/mask_utils.hpp>
#include <chronon3d/core/hash/hash_primitives.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/path_utils.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace chronon3d {

namespace {

using FlattenedMaskPath = std::vector<std::vector<Vec2>>;

struct MaskPrimitiveView {
    MaskType type{MaskType::None};
    Vec3 pos{0.0f};
    Vec2 size{0.0f};
    f32 radius{0.0f};
    const std::vector<PathCommand>* path{nullptr};
    const graphics::Field2D* field{nullptr};
    std::string_view text_source{};
    f32 feather{0.0f};
    f32 expansion{0.0f};
    f32 opacity{1.0f};
    bool inverted{false};
};

MaskPrimitiveView mask_view(const Mask& mask) {
    return {mask.type, mask.pos, mask.size, mask.radius, &mask.path,
            mask.field ? &*mask.field : nullptr, mask.text_source,
            mask.feather, mask.expansion, mask.opacity, mask.inverted};
}

MaskPrimitiveView mask_view(const MaskOperation& operation) {
    return {operation.type, operation.pos, operation.size, operation.radius,
            &operation.path, operation.field ? &*operation.field : nullptr,
            operation.text_source,
            operation.feather, operation.expansion, operation.opacity,
            operation.inverted};
}

FlattenedMaskPath flatten_mask_path(const MaskPrimitiveView& mask) {
    PathShape path;
    if (mask.path) path.commands = *mask.path;
    path.closed = true;
    return math::flatten_path(path, 24);
}

f32 mask_primitive_alpha(MaskPrimitiveView mask, Vec2 local,
                         const FlattenedMaskPath* flattened = nullptr,
                         Vec2 canvas_pixel = {},
                         const TextMaskSampler* text_sampler = nullptr) {
    if (mask.type == MaskType::None) return 0.0f;
    local.x -= mask.pos.x;
    local.y -= mask.pos.y;

    f32 signed_distance = -std::numeric_limits<f32>::max();
    switch (mask.type) {
        case MaskType::Rect: {
            const Vec2 q{std::abs(local.x) - mask.size.x * 0.5f,
                         std::abs(local.y) - mask.size.y * 0.5f};
            const Vec2 outside{std::max(q.x, 0.0f), std::max(q.y, 0.0f)};
            const f32 outside_distance = glm::length(outside);
            const f32 inside_distance = std::min(std::max(q.x, q.y), 0.0f);
            signed_distance = -(outside_distance + inside_distance);
            break;
        }
        case MaskType::RoundedRect: {
            const f32 radius = std::clamp(mask.radius, 0.0f,
                std::min(mask.size.x, mask.size.y) * 0.5f);
            const Vec2 q{std::abs(local.x) - (mask.size.x * 0.5f - radius),
                         std::abs(local.y) - (mask.size.y * 0.5f - radius)};
            const Vec2 outside{std::max(q.x, 0.0f), std::max(q.y, 0.0f)};
            const f32 outside_distance = glm::length(outside);
            const f32 inside_distance = std::min(std::max(q.x, q.y), 0.0f);
            signed_distance = radius - (outside_distance + inside_distance);
            break;
        }
        case MaskType::Circle:
            signed_distance = mask.radius - glm::length(local);
            break;
        case MaskType::Ellipse: {
            const f32 rx = mask.size.x * 0.5f;
            const f32 ry = mask.size.y * 0.5f;
            if (rx <= 0.0f || ry <= 0.0f) return mask.inverted ? 1.0f : 0.0f;
            const Vec2 p{std::abs(local.x), std::abs(local.y)};
            const f32 k0 = glm::length(Vec2{p.x / rx, p.y / ry});
            const f32 k1 = glm::length(Vec2{p.x / (rx * rx), p.y / (ry * ry)});
            signed_distance = k1 > 1e-8f ? -(k0 * (k0 - 1.0f) / k1) : std::min(rx, ry);
            break;
        }
        case MaskType::Field2D: {
            if (!mask.field || mask.size.x <= 0.0f || mask.size.y <= 0.0f)
                return mask.inverted ? 1.0f : 0.0f;
            const Vec2 normalized{local.x / mask.size.x + 0.5f,
                                  local.y / mask.size.y + 0.5f};
            signed_distance = (mask.field->sample(normalized) - 0.5f) *
                std::min(mask.size.x, mask.size.y);
            break;
        }
        case MaskType::Text:
            {
                const f32 source = text_sampler && *text_sampler
                    ? std::clamp((*text_sampler)(mask.text_source, canvas_pixel), 0.0f, 1.0f)
                    : 0.0f;
                return (mask.inverted ? 1.0f - source : source) *
                    std::clamp(mask.opacity, 0.0f, 1.0f);
            }
        case MaskType::Path: {
            const auto owned_flattened = flattened ? FlattenedMaskPath{} : flatten_mask_path(mask);
            const auto& polylines = flattened ? *flattened : owned_flattened;
            bool inside = false;
            f32 distance = std::numeric_limits<f32>::max();
            for (const auto& polyline : polylines) {
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
                signed_distance = -std::numeric_limits<f32>::max();
            else
                signed_distance = inside ? distance : -distance;
            break;
        }
        default:
            signed_distance = std::numeric_limits<f32>::max();
            break;
    }
    signed_distance += mask.expansion;
    f32 alpha = 0.0f;
    if (mask.feather <= 1e-6f) alpha = signed_distance >= 0.0f ? 1.0f : 0.0f;
    else {
        const f32 t = std::clamp((signed_distance + mask.feather * 0.5f) / mask.feather,
                                 0.0f, 1.0f);
        alpha = t * t * (3.0f - 2.0f * t);
    }
    alpha = mask.inverted ? 1.0f - alpha : alpha;
    return alpha * std::clamp(mask.opacity, 0.0f, 1.0f);
}

f32 mask_alpha_with_flattened(const Mask& mask, Vec2 local,
                              const std::vector<FlattenedMaskPath>* flattened,
                              Vec2 canvas_pixel = {},
                              const TextMaskSampler* text_sampler = nullptr) {
    if (!mask.enabled()) return 1.0f;
    const auto get_flattened = [&](std::size_t index, MaskPrimitiveView view)
        -> const FlattenedMaskPath* {
        if (view.type != MaskType::Path) return nullptr;
        if (flattened && index < flattened->size()) return &(*flattened)[index];
        return nullptr;
    };
    const f32 primary = mask_primitive_alpha(mask_view(mask), local,
        get_flattened(0, mask_view(mask)), canvas_pixel, text_sampler);
    // A leading subtract mask subtracts its coverage from the implicit full
    // layer mask. Add, intersect and difference use the primary shape as the
    // first authored coverage, matching the no-prior-mask identity behavior.
    f32 accumulated = mask.initial_mode == MaskCombineMode::Subtract
        ? 1.0f - primary : primary;
    for (std::size_t index = 0; index < mask.operations.size(); ++index) {
        const auto& operation = mask.operations[index];
        const auto view = mask_view(operation);
        const f32 source = mask_primitive_alpha(view, local,
            get_flattened(index + 1, view), canvas_pixel, text_sampler);
        switch (operation.mode) {
            case MaskCombineMode::Add:
                accumulated = accumulated + source * (1.0f - accumulated);
                break;
            case MaskCombineMode::Subtract:
                accumulated *= 1.0f - source;
                break;
            case MaskCombineMode::Intersect:
                accumulated *= source;
                break;
            case MaskCombineMode::Difference:
                // Alpha symmetric difference: keep either mask's contribution
                // where the other mask has no coverage.
                accumulated = accumulated * (1.0f - source) + source * (1.0f - accumulated);
                break;
        }
    }
    return std::clamp(accumulated, 0.0f, 1.0f);
}

std::vector<FlattenedMaskPath> flatten_mask_stack(const Mask& mask) {
    std::vector<FlattenedMaskPath> flattened;
    flattened.reserve(mask.operations.size() + 1);
    const auto flatten_if_path = [&](MaskPrimitiveView view) {
        flattened.push_back(view.type == MaskType::Path
            ? flatten_mask_path(view) : FlattenedMaskPath{});
    };
    flatten_if_path(mask_view(mask));
    for (const auto& operation : mask.operations) flatten_if_path(mask_view(operation));
    return flattened;
}

} // namespace

f32 mask_alpha_at_local_point(const Mask& mask, Vec2 local) {
    return mask_alpha_with_flattened(mask, local, nullptr);
}

f32 mask_alpha_at_local_point(const Mask& mask, Vec2 local,
                              Vec2 canvas_pixel, const TextMaskSampler& text_sampler) {
    return mask_alpha_with_flattened(mask, local, nullptr, canvas_pixel, &text_sampler);
}

bool mask_contains_local_point(const Mask& mask, Vec2 local) {
    return mask_alpha_at_local_point(mask, local) >= 0.5f;
}

std::uint64_t hash_mask_cache_key(const Mask& mask, const Mat4& layer_inv_matrix, i32 width, i32 height) {
    auto hv = [](auto value) -> std::uint64_t {
        return static_cast<std::uint64_t>(std::hash<std::decay_t<decltype(value)>>{}(value));
    };

    std::uint64_t seed = 0;
    seed = core::hash::hash_combine(seed, static_cast<std::uint64_t>(mask.type));
    seed = core::hash::hash_combine(seed, static_cast<std::uint64_t>(mask.initial_mode));
    seed = core::hash::hash_combine(seed, hv(mask.pos.x));
    seed = core::hash::hash_combine(seed, hv(mask.pos.y));
    seed = core::hash::hash_combine(seed, hv(mask.pos.z));
    seed = core::hash::hash_combine(seed, hv(mask.size.x));
    seed = core::hash::hash_combine(seed, hv(mask.size.y));
    seed = core::hash::hash_combine(seed, hv(mask.radius));
    seed = core::hash::hash_combine(seed, hv(mask.feather));
    seed = core::hash::hash_combine(seed, hv(mask.expansion));
    seed = core::hash::hash_combine(seed, hv(mask.opacity));
    seed = core::hash::hash_combine(seed, static_cast<std::uint64_t>(mask.inverted));
    seed = core::hash::hash_combine(seed, std::hash<std::string>{}(mask.text_source));
    seed = core::hash::hash_combine(seed, mask.field.has_value());
    if (mask.field) seed = core::hash::hash_combine(seed, graphics::field2d_fingerprint(*mask.field));
    seed = core::hash::hash_combine(seed, static_cast<std::uint64_t>(mask.path.size()));
    for (const auto& command : mask.path) {
        seed = core::hash::hash_combine(seed, static_cast<std::uint64_t>(command.type));
        seed = core::hash::hash_combine(seed, hv(command.p0.x)); seed = core::hash::hash_combine(seed, hv(command.p0.y));
        seed = core::hash::hash_combine(seed, hv(command.p1.x)); seed = core::hash::hash_combine(seed, hv(command.p1.y));
        seed = core::hash::hash_combine(seed, hv(command.p2.x)); seed = core::hash::hash_combine(seed, hv(command.p2.y));
    }
    seed = core::hash::hash_combine(seed, static_cast<std::uint64_t>(mask.operations.size()));
    for (const auto& operation : mask.operations) {
        seed = core::hash::hash_combine(seed, static_cast<std::uint64_t>(operation.mode));
        seed = core::hash::hash_combine(seed, static_cast<std::uint64_t>(operation.type));
        seed = core::hash::hash_combine(seed, hv(operation.pos.x)); seed = core::hash::hash_combine(seed, hv(operation.pos.y));
        seed = core::hash::hash_combine(seed, hv(operation.pos.z));
        seed = core::hash::hash_combine(seed, hv(operation.size.x)); seed = core::hash::hash_combine(seed, hv(operation.size.y));
        seed = core::hash::hash_combine(seed, hv(operation.radius)); seed = core::hash::hash_combine(seed, hv(operation.feather));
        seed = core::hash::hash_combine(seed, hv(operation.expansion)); seed = core::hash::hash_combine(seed, hv(operation.opacity));
        seed = core::hash::hash_combine(seed, operation.field.has_value());
        if (operation.field) seed = core::hash::hash_combine(seed, graphics::field2d_fingerprint(*operation.field));
        seed = core::hash::hash_combine(seed, static_cast<std::uint64_t>(operation.inverted));
        seed = core::hash::hash_combine(seed, std::hash<std::string>{}(operation.text_source));
        seed = core::hash::hash_combine(seed, static_cast<std::uint64_t>(operation.path.size()));
        for (const auto& command : operation.path) {
            seed = core::hash::hash_combine(seed, static_cast<std::uint64_t>(command.type));
            seed = core::hash::hash_combine(seed, hv(command.p0.x)); seed = core::hash::hash_combine(seed, hv(command.p0.y));
            seed = core::hash::hash_combine(seed, hv(command.p1.x)); seed = core::hash::hash_combine(seed, hv(command.p1.y));
            seed = core::hash::hash_combine(seed, hv(command.p2.x)); seed = core::hash::hash_combine(seed, hv(command.p2.y));
        }
    }
    seed = core::hash::hash_combine(seed, static_cast<std::uint64_t>(width));
    seed = core::hash::hash_combine(seed, static_cast<std::uint64_t>(height));
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            seed = core::hash::hash_combine(seed, hv(layer_inv_matrix[i][j]));
        }
    }
    return seed;
}

std::shared_ptr<Framebuffer> rasterize_mask_alpha(const Mask& mask, const Mat4& layer_inv_matrix, i32 width, i32 height) {
    return rasterize_mask_alpha(mask, layer_inv_matrix, width, height, 0, 0, {});
}

std::shared_ptr<Framebuffer> rasterize_mask_alpha(
    const Mask& mask, const Mat4& layer_inv_matrix, i32 width, i32 height,
    i32 origin_x, i32 origin_y, const TextMaskSampler& text_sampler) {
    auto fb = std::make_shared<Framebuffer>(width, height);
    fb->clear(Color::transparent());
    if (!mask.enabled() || width <= 0 || height <= 0) {
        return fb;
    }

    const Mat4 screen_from_local = glm::inverse(layer_inv_matrix);
    const f32 padding = std::max(0.0f, mask.expansion) + std::max(0.0f, mask.feather) * 0.5f;
    const f32 hw = (mask.type == MaskType::Circle) ? mask.radius + padding : mask.size.x * 0.5f + padding;
    const f32 hh = (mask.type == MaskType::Circle) ? mask.radius + padding : mask.size.y * 0.5f + padding;

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
    // Inverted coverage is non-zero outside the source geometry, while path
    // geometry may extend beyond its declared size. Rasterize the full target
    // for those cases so the bounding-box optimization cannot erase valid
    // outside coverage. Composite operations can extend beyond the primary
    // mask, so their aggregate is evaluated across the full target as well.
    const bool has_text_source = mask.type == MaskType::Text ||
        std::any_of(mask.operations.begin(), mask.operations.end(),
            [](const MaskOperation& operation) { return operation.type == MaskType::Text; });
    if (!mask.operations.empty() || mask.inverted ||
        mask.initial_mode == MaskCombineMode::Subtract ||
        mask.type == MaskType::Path || has_text_source)
        bbox = raster::BBox{0, 0, width, height};
    bbox.clip_to(width, height);
    if (bbox.is_empty()) {
        return fb;
    }

    const auto flattened_paths = flatten_mask_stack(mask);
    for (i32 y = bbox.y0; y < bbox.y1; ++y) {
        Color* row = fb->pixels_row(y);
        for (i32 x = bbox.x0; x < bbox.x1; ++x) {
            Vec4 local = layer_inv_matrix * Vec4(static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, 0.0f, 1.0f);
            const f32 alpha = mask_alpha_with_flattened(
                mask, Vec2{local.x, local.y}, &flattened_paths,
                Vec2{static_cast<f32>(origin_x + x) + 0.5f,
                     static_cast<f32>(origin_y + y) + 0.5f}, &text_sampler);
            if (alpha > 0.0f) row[x] = {1.0f, 1.0f, 1.0f, alpha};
        }
    }

    return fb;
}

} // namespace chronon3d
