// ---------------------------------------------------------------------------
// effect_shadow_impl.cpp — DropShadow effect + node-level draw_shadow
//
// Extracted from effect_stack.cpp to keep the dispatcher focused on
// orchestration.
// ---------------------------------------------------------------------------

#include "../render_effects_processor.hpp"
#include "../../primitive_renderer.hpp"
#include "effects_internal.hpp"
#include "effect_helpers.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {
namespace renderer {

void apply_shadow_effect(
    Framebuffer& fb,
    const DropShadowParams& p,
    const std::optional<raster::BBox>& clip,
    bool /*diagnostics_enabled*/
) {
    const i32 w = fb.width(), h = fb.height();
    const float contact_blur = std::max(1.0f, p.radius * 0.45f);
    const float ambient_blur  = std::max(contact_blur + 4.0f, p.radius * 2.0f);
    const float spread =
        std::max(std::abs(p.offset.x), std::abs(p.offset.y)) + ambient_blur;
    // The EffectStackNode already allocates a spatially expanded ROI for the
    // shadow.  The effect operates in that framebuffer's local coordinates;
    // using the executor clip here can incorrectly describe the parent
    // canvas and eliminate the source range after ROI translation.  The ROI
    // itself is the canonical safe clip for this spatial effect.
    const std::optional<raster::BBox> effect_clip = std::nullopt;
    i32 x_min_src = 0, x_max_src = w;
    i32 y_min_src = 0, y_max_src = h;
    i32 x_min_dst = 0, x_max_dst = w;
    i32 y_min_dst = 0, y_max_dst = h;
    const i32 ox = static_cast<i32>(std::round(p.offset.x));
    const i32 oy = static_cast<i32>(std::round(p.offset.y));
    if (effect_clip) {
        x_min_dst = std::max(0, effect_clip->x0);
        x_max_dst = std::min(w, effect_clip->x1);
        y_min_dst = std::max(0, effect_clip->y0);
        y_max_dst = std::min(h, effect_clip->y1);

        x_min_src = std::max(0, effect_clip->x0 - ox);
        x_max_src = std::min(w, effect_clip->x1 - ox);
        y_min_src = std::max(0, effect_clip->y0 - oy);
        y_max_src = std::min(h, effect_clip->y1 - oy);
    }
    const i32 blur_pad = blur_padding_for_radius(std::max(contact_blur, ambient_blur));
    const i32 x_min_pad = std::max(0, x_min_dst - blur_pad);
    const i32 x_max_pad = std::min(w, x_max_dst + blur_pad);
    const i32 y_min_pad = std::max(0, y_min_dst - blur_pad);
    const i32 y_max_pad = std::min(h, y_max_dst + blur_pad);
    const i32 roi_w = x_max_pad - x_min_pad;
    const i32 roi_h = y_max_pad - y_min_pad;

    // Guard: when the clip region is inverted or empty (e.g. an empty
    // layer bbox or a sub-pixel shape), roi dimensions become
    // non-positive.  acquire_temp_framebuffer would throw
    // "Framebuffer dimensions must be positive" — early-out instead.
    if (roi_w <= 0 || roi_h <= 0) {
        return;
    }

    auto build_shadow = [&](f32 opacity_scale, f32 blur_radius, f32 offset_scale) {
        auto shadow_map_fb = acquire_temp_framebuffer(roi_w, roi_h);
        shadow_map_fb->clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
        auto& shadow_map = *shadow_map_fb;
        if (roi_w > 0 && roi_h > 0) {
            const int lox = static_cast<i32>(std::round(static_cast<float>(ox) * offset_scale));
            const int loy = static_cast<i32>(std::round(static_cast<float>(oy) * offset_scale));
            for (i32 y = y_min_src; y < y_max_src; ++y) {
                const Color* src_row = fb.pixels_row(y);
                for (i32 x = x_min_src; x < x_max_src; ++x) {
                    const Color c = src_row[x];
                    if (c.a <= 0.0f) {
                        continue;
                    }
                    const i32 dx = x + lox;
                    const i32 dy = y + loy;
                    if (dx >= x_min_pad && dx < x_max_pad && dy >= y_min_pad && dy < y_max_pad) {
                        Color* shadow_row = shadow_map.pixels_row(dy - y_min_pad);
                        const float alpha = std::min(1.0f, c.a * p.color.a * opacity_scale);
                        shadow_row[dx - x_min_pad] = {p.color.r, p.color.g, p.color.b, alpha};
                    }
                }
            }
            if (blur_radius > 0.0f) {
                apply_blur(shadow_map, blur_radius, std::nullopt, 1);
            }
        }
        return shadow_map_fb;
    };

    auto contact_map = build_shadow(0.85f, contact_blur, 1.0f);
    auto ambient_map = build_shadow(0.30f, ambient_blur, 1.65f);


    // Composite shadow BEHIND content in-place
    for (i32 y = 0; y < roi_h; ++y) {
        const i32 dy = y + y_min_pad;
        if (dy < 0 || dy >= h) continue;
        Color*       fb_row = fb.pixels_row(dy);
        const Color* contact_row = contact_map->pixels_row(y);
        const Color* ambient_row = ambient_map->pixels_row(y);
        for (i32 x = 0; x < roi_w; ++x) {
            const i32 dx = x + x_min_pad;
            if (dx < 0 || dx >= w) continue;
            Color shadow_px{
                std::max(contact_row[x].r, ambient_row[x].r),
                std::max(contact_row[x].g, ambient_row[x].g),
                std::max(contact_row[x].b, ambient_row[x].b),
                std::max(contact_row[x].a, ambient_row[x].a),
            };
            if (shadow_px.a <= 0.0f) continue;
            fb_row[dx] = compositor::blend(fb_row[dx], shadow_px, BlendMode::Normal);
        }
    }
}

// ── Node-level shadow primitive ──────────────────────────────────────────────

void draw_shadow(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    if (node.shadow.color.a <= 0.0f) return;

    const Color base        = node.shadow.color.to_linear();
    const Mat4& base_model  = state.matrix;
    Mat4 shadow_model = glm::translate(Mat4(1.0f), Vec3(node.shadow.offset.x, node.shadow.offset.y, 0)) * base_model;

    constexpr int LAYERS = 6;
    for (int i = LAYERS; i >= 1; --i) {
        const f32 t      = static_cast<f32>(i) / LAYERS;
        const f32 spread = node.shadow.radius * t;
        const f32 alpha  = base.a * (1.0f - t * t) * state.opacity;
        if (alpha > 0.0f)
            renderer::draw_transformed_shape(fb, node.shape, shadow_model, {base.r, base.g, base.b, alpha}, spread, &state, nullptr, node.corner_radius);
    }
    renderer::draw_transformed_shape(fb, node.shape, shadow_model,
                           {base.r, base.g, base.b, base.a * 0.7f * state.opacity}, 0.0f, &state, nullptr, node.corner_radius);
}

} // namespace renderer
} // namespace chronon3d
