#pragma once

#include <chronon3d/math/math_base.hpp>
#include <chronon3d/scene/mask/mask.hpp>
#include <chronon3d/math/math_base.hpp>
#include <cstdint>
#include <memory>

namespace chronon3d {

class Framebuffer;

// Checks whether a point in layer-local coordinates
// falls inside (or outside, if inverted) the given mask.
// Returns true if the pixel should be drawn.
bool mask_contains_local_point(const Mask& mask, Vec2 local);

// Computes a hash key for caching mask rasterization results.
std::uint64_t hash_mask_cache_key(const Mask& mask, const Mat4& layer_inv_matrix, i32 width, i32 height);

// Rasterizes the mask into an alpha-only framebuffer (values 0.0 or 1.0).
std::shared_ptr<Framebuffer> rasterize_mask_alpha(const Mask& mask, const Mat4& layer_inv_matrix, i32 width, i32 height);

} // namespace chronon3d
