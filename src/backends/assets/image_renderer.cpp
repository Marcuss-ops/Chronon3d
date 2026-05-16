#include <chronon3d/backends/assets/image_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/mask/mask_utils.hpp>
#include "../software/utils/blend2d_bridge.hpp"
#include <algorithm>
#include <cmath>

namespace chronon3d {

bool ImageRenderer::draw_image(const ImageShape& image, const RenderState& state, Framebuffer& fb) {
    if (image.path.empty() || image.size.x <= 0 || image.size.y <= 0) {
        return false;
    }
    const CachedImage* cached = m_cache.get_or_load(image.path);
    if (!cached || cached->bl_img.empty()) return false;

    const f32 final_opacity = image.opacity * state.opacity;
    if (final_opacity <= 0.001f) return true;

    // The 'model' matrix maps from node-local space (0,0..size) to screen.
    // Our bridge expects a matrix mapping (0,0..img_pixels) to screen.
    const float sx = image.size.x / static_cast<float>(cached->width);
    const float sy = image.size.y / static_cast<float>(cached->height);
    Mat4 scaled_model = state.matrix * glm::scale(Mat4(1.0f), Vec3(sx, sy, 1.0f));

    blend2d_bridge::composite_bl_image_transformed(fb, cached->bl_img, scaled_model, final_opacity, BlendMode::Normal, &state);

    return true;
}

} // namespace chronon3d
