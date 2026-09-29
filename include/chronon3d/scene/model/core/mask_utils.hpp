#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/layer/mask.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace chronon3d {

class Framebuffer;

// Checks whether a point in layer-local coordinates
// falls inside (or outside, if inverted) the given mask.
// Returns true if the pixel should be drawn.
bool mask_contains_local_point(const Mask& mask, Vec2 local);
f32 mask_alpha_at_local_point(const Mask& mask, Vec2 local);
using TextMaskSampler = std::function<f32(std::string_view, Vec2)>;
f32 mask_alpha_at_local_point(const Mask& mask, Vec2 local,
                              Vec2 canvas_pixel, const TextMaskSampler& text_sampler);

// Computes a hash key for caching mask rasterization results.
std::uint64_t hash_mask_cache_key(const Mask& mask, const Mat4& layer_inv_matrix, i32 width, i32 height);

// Rasterizes the mask into an alpha-only framebuffer (values 0.0 or 1.0).
std::shared_ptr<Framebuffer> rasterize_mask_alpha(const Mask& mask, const Mat4& layer_inv_matrix, i32 width, i32 height);
std::shared_ptr<Framebuffer> rasterize_mask_alpha(
    const Mask& mask, const Mat4& layer_inv_matrix, i32 width, i32 height,
    i32 origin_x, i32 origin_y, const TextMaskSampler& text_sampler);

} // namespace chronon3d
