// ---------------------------------------------------------------------------
// blend2d_bridge.cpp
//
// Core mask helper for blend2d bridge. Sampling and compositing moved to
// blend2d_bridge/ sub-directory to keep translation-unit sizes reasonable.
// ---------------------------------------------------------------------------

#include "blend2d_bridge.hpp"
#include "blend2d_bridge/bl2d_sampling.hpp"
#include "blend2d_bridge/bl2d_compositing.hpp"
#include "blend2d_bridge/bl2d_transform.hpp"
#include <chronon3d/scene/mask/mask_utils.hpp>

namespace chronon3d::blend2d_bridge {

bool pixel_passes_mask(const RenderState& state, i32 x, i32 y) {
    if (!state.mask || !state.mask->enabled()) return true;
    if (state.mask_alpha_cache && y >= 0 && y < state.mask_alpha_cache->height() &&
        x >= 0 && x < state.mask_alpha_cache->width()) {
        return state.mask_alpha_cache->get_pixel(x, y).a > 0.0f;
    }
    Vec4 local = state.layer_inv_matrix * Vec4(static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, 0.0f, 1.0f);
    return mask_contains_local_point(*state.mask, Vec2{local.x, local.y});
}

} // namespace chronon3d::blend2d_bridge
