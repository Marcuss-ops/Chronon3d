#include <chronon3d/backends/assets/image_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/model/mask_utils.hpp>
#include <chronon3d/media/media_placement.hpp>
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
    
    BLImage render_img;
    int img_w = 0;
    int img_h = 0;
    bool using_placeholder = false;

    if (!cached || cached->bl_img.empty()) {
        int pw = static_cast<int>(std::round(image.size.x));
        int ph = static_cast<int>(std::round(image.size.y));
        pw = std::max(1, pw);
        ph = std::max(1, ph);

        render_img = BLImage(pw, ph, BL_FORMAT_PRGB32);
        BLContext ctx(render_img);
        ctx.setFillStyle(BLRgba32(0x3A, 0x0C, 0x0C, 0xFF)); // Deep red bg
        ctx.fillRect(0, 0, pw, ph);
        ctx.setStrokeStyle(BLRgba32(0xFF, 0x44, 0x44, 0xFF)); // Crimson border and cross
        ctx.setStrokeWidth(2.0f);
        ctx.strokeRect(0, 0, pw, ph);
        ctx.strokeLine(0, 0, pw, ph);
        ctx.strokeLine(0, ph, pw, 0);
        ctx.end();

        img_w = pw;
        img_h = ph;
        using_placeholder = true;
    } else {
        render_img = cached->bl_img;
        img_w = cached->width;
        img_h = cached->height;
    }

    const f32 final_opacity = image.opacity * state.opacity;
    if (final_opacity <= 0.001f) return true;

    // Handle ImageCrop (if enabled, crop origin and size are normalized relative to cached image)
    Vec2 original_source_size = Vec2(static_cast<float>(img_w), static_cast<float>(img_h));
    Vec2 effective_source_size = original_source_size;
    Vec2 effective_source_origin = Vec2(0.0f, 0.0f);
    
    // Skip crop on placeholders to avoid clipping fallback logic
    if (image.crop.enabled && !using_placeholder) {
        effective_source_size = image.crop.size * original_source_size;
        effective_source_origin = image.crop.origin * original_source_size;
    }

    // Call compute_media_placement
    // Force placeholders to use Stretch fit to match requested size exactly
    FitMode resolved_fit = using_placeholder ? FitMode::Stretch : image.fit;
    MediaPlacementResult placement = compute_media_placement(
        effective_source_size,
        image.size,
        resolved_fit,
        image.focal_point
    );

    Vec2 final_src_origin = effective_source_origin + placement.src_rect.origin;
    Vec2 final_src_size = placement.src_rect.size;

    int src_x = static_cast<int>(std::round(final_src_origin.x));
    int src_y = static_cast<int>(std::round(final_src_origin.y));
    int src_w = static_cast<int>(std::round(final_src_size.x));
    int src_h = static_cast<int>(std::round(final_src_size.y));

    // Clamp values to stay within bounds
    src_x = std::clamp(src_x, 0, img_w);
    src_y = std::clamp(src_y, 0, img_h);
    src_w = std::clamp(src_w, 0, img_w - src_x);
    src_h = std::clamp(src_h, 0, img_h - src_y);

    if (src_w <= 0 || src_h <= 0) return true;

    // Create a sub-image and blit the cropped region into it using Blend2D context
    BLImage sub_img(src_w, src_h, BL_FORMAT_PRGB32);
    BLContext ctx(sub_img);
    ctx.blitImage(BLPointI(0, 0), render_img, BLRectI(src_x, src_y, src_w, src_h));
    ctx.end();

    // Scale mapping from sub_img space [0,0 -> src_w,src_h] to destination dst_rect
    Vec2 scale = placement.dst_rect.size / Vec2(static_cast<float>(src_w), static_cast<float>(src_h));
    Mat4 scaled_model = state.matrix
                      * glm::translate(Mat4(1.0f), Vec3(placement.dst_rect.origin, 0.0f))
                      * glm::scale(Mat4(1.0f), Vec3(scale, 1.0f));

    // Scale the radius from destination space to sub-image source space.
    float scaled_radius = 0.0f;
    if (image.radius > 0.0f && placement.dst_rect.size.x > 0.0f) {
        scaled_radius = image.radius * (static_cast<float>(src_w) / placement.dst_rect.size.x);
    }

    const bool mask_enabled = state.mask && state.mask->enabled();
    const int clip_x0 = state.clip_rect ? state.clip_rect->x0 : -1;
    const int clip_y0 = state.clip_rect ? state.clip_rect->y0 : -1;
    const int clip_x1 = state.clip_rect ? state.clip_rect->x1 : -1;
    const int clip_y1 = state.clip_rect ? state.clip_rect->y1 : -1;
    spdlog::debug(
        "[image-render] layer='{}' path='{}' cached={}x{} sub={}x{} requested={}x{} scale=({:.4f},{:.4f}) opacity={:.3f} mask={} clip={} clip_rect=[{},{} -> {},{}] tx={:.2f} ty={:.2f}",
        state.layer_id,
        image.path,
        img_w,
        img_h,
        src_w,
        src_h,
        image.size.x,
        image.size.y,
        scale.x,
        scale.y,
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

    bool use_fast_fb = (image.radius <= 0.0f && !image.crop.enabled && image.fit == FitMode::Stretch && cached && cached->fb_img);
    if (use_fast_fb) {
        blend2d_bridge::composite_framebuffer_transformed(fb, *cached->fb_img, scaled_model, final_opacity, BlendMode::Normal, &state);
    } else {
        blend2d_bridge::composite_bl_image_transformed(fb, sub_img, scaled_model, final_opacity, BlendMode::Normal, &state, scaled_radius);
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
    spdlog::debug(
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
