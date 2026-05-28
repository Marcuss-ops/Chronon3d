#include <chronon3d/backends/assets/image_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/mask/mask_utils.hpp>
#include "../software/utils/blend2d_bridge.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace chronon3d {

bool ImageRenderer::draw_image(const ImageShape& image, const RenderState& state, Framebuffer& fb) {
    if (image.path.empty() || image.size.x <= 0 || image.size.y <= 0) {
        return false;
    }
    const CachedImage* cached = ImageCache::instance().get_or_load(image.path);
    if (!cached || cached->bl_img.empty()) return false;

    const f32 final_opacity = image.opacity * state.opacity;
    if (final_opacity <= 0.001f) return true;

    // The 'model' matrix maps from node-local space (0,0..size) to screen.
    // Our bridge expects a matrix mapping (0,0..img_pixels) to screen.
    const float sx = image.size.x / static_cast<float>(cached->width);
    const float sy = image.size.y / static_cast<float>(cached->height);
    Mat4 scaled_model = state.matrix * glm::scale(Mat4(1.0f), Vec3(sx, sy, 1.0f));

    const bool mask_enabled = state.mask && state.mask->enabled();
    const int clip_x0 = state.clip_rect ? state.clip_rect->x0 : -1;
    const int clip_y0 = state.clip_rect ? state.clip_rect->y0 : -1;
    const int clip_x1 = state.clip_rect ? state.clip_rect->x1 : -1;
    const int clip_y1 = state.clip_rect ? state.clip_rect->y1 : -1;
    spdlog::info(
        "[image-render] layer='{}' path='{}' cached={}x{} requested={}x{} scale=({:.4f},{:.4f}) opacity={:.3f} mask={} clip={} clip_rect=[{},{} -> {},{}] tx={:.2f} ty={:.2f}",
        state.layer_id,
        image.path,
        cached->width,
        cached->height,
        image.size.x,
        image.size.y,
        sx,
        sy,
        final_opacity,
        mask_enabled ? 1 : 0,
        state.clip_rect ? 1 : 0,
        clip_x0,
        clip_y0,
        clip_x1,
        clip_y1,
        scaled_model[3][0],
        scaled_model[3][1]
    );

    if (image.radius <= 0.0f && cached->fb_img) {
        blend2d_bridge::composite_framebuffer_transformed(fb, *cached->fb_img, scaled_model, final_opacity, BlendMode::Normal, &state);
    } else {
        blend2d_bridge::composite_bl_image_transformed(fb, cached->bl_img, scaled_model, final_opacity, BlendMode::Normal, &state, image.radius);
    }

    return true;
}

bool ImageRenderer::draw_image_tiled(const ImageShape& image, const RenderState& state, Framebuffer& fb) {
    if (image.path.empty() || image.size.x <= 0 || image.size.y <= 0) {
        return false;
    }
    const CachedImage* cached = ImageCache::instance().get_or_load(image.path);
    if (!cached || cached->bl_img.empty()) return false;

    const f32 final_opacity = image.opacity * state.opacity;
    if (final_opacity <= 0.001f) return true;

    const float sx = image.size.x / static_cast<float>(cached->width);
    const float sy = image.size.y / static_cast<float>(cached->height);
    Mat4 scaled_model = state.matrix * glm::scale(Mat4(1.0f), Vec3(sx, sy, 1.0f));

    const bool mask_enabled = state.mask && state.mask->enabled();
    const int clip_x0 = state.clip_rect ? state.clip_rect->x0 : -1;
    const int clip_y0 = state.clip_rect ? state.clip_rect->y0 : -1;
    const int clip_x1 = state.clip_rect ? state.clip_rect->x1 : -1;
    const int clip_y1 = state.clip_rect ? state.clip_rect->y1 : -1;
    spdlog::info(
        "[image-render-tiled] layer='{}' path='{}' cached={}x{} requested={}x{} scale=({:.4f},{:.4f}) opacity={:.3f} mask={} clip={} clip_rect=[{},{} -> {},{}] tx={:.2f} ty={:.2f}",
        state.layer_id,
        image.path,
        cached->width,
        cached->height,
        image.size.x,
        image.size.y,
        sx,
        sy,
        final_opacity,
        mask_enabled ? 1 : 0,
        state.clip_rect ? 1 : 0,
        clip_x0,
        clip_y0,
        clip_x1,
        clip_y1,
        scaled_model[3][0],
        scaled_model[3][1]
    );

    blend2d_bridge::composite_bl_image_tiled(fb, cached->bl_img, scaled_model, final_opacity, BlendMode::Normal, &state);

    return true;
}

} // namespace chronon3d
