// ---------------------------------------------------------------------------
// effect_stack.cpp — Effect stack dispatcher + shadow/glow node-level primitives
// ---------------------------------------------------------------------------

#include "../render_effects_processor.hpp"
#include "../../primitive_renderer.hpp"
#include "effects_internal.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <algorithm>

namespace chronon3d {
namespace renderer {

void apply_effect_stack(Framebuffer& fb, const EffectStack& stack,
                        float time_seconds, const std::optional<raster::BBox>& clip) {
    for (const auto& inst : stack) {
        if (!inst.enabled) continue;

        if (auto* p = std::any_cast<BlurParams>(&inst.params)) {
        if (p->radius > 0.0f) {
            apply_blur(fb, p->radius, clip);
        }

        } else if (auto* p = std::any_cast<TintParams>(&inst.params)) {
            LayerEffect e;
            e.tint = Color{p->color.r, p->color.g, p->color.b, p->color.a * p->amount};
            apply_color_effects(fb, e, clip);

        } else if (auto* p = std::any_cast<BrightnessParams>(&inst.params)) {
            LayerEffect e; e.brightness = p->value;
            apply_color_effects(fb, e, clip);

        } else if (auto* p = std::any_cast<ContrastParams>(&inst.params)) {
            LayerEffect e; e.contrast = p->value;
            apply_color_effects(fb, e, clip);

        } else if (auto* p = std::any_cast<GlowParams>(&inst.params)) {
            // Extract alpha-masked version, blur it with glow tint, composite in front
            const i32 w = fb.width(), h = fb.height();
            i32 x_min = 0, x_max = w;
            i32 y_min = 0, y_max = h;
            if (clip) {
                x_min = std::max(0, clip->x0);
                x_max = std::min(w, clip->x1);
                y_min = std::max(0, clip->y0);
                y_max = std::min(h, clip->y1);
            }
            auto alpha_map_fb = acquire_temp_framebuffer(w, h);
            alpha_map_fb->clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
            auto& alpha_map = *alpha_map_fb;
            if (x_min < x_max && y_min < y_max) {
                for (i32 y = y_min; y < y_max; ++y) {
                    const Color* src_row = fb.pixels_row(y);
                    Color* alpha_row = alpha_map.pixels_row(y);
                    for (i32 x = x_min; x < x_max; ++x) {
                        const Color c = src_row[x];
                        if (c.a > 0.0f) {
                            alpha_row[x] = {p->color.r, p->color.g, p->color.b, c.a * p->intensity};
                        }
                    }
                }
                if (p->radius > 0.0f) apply_blur(alpha_map, p->radius, clip);
                for (i32 y = y_min; y < y_max; ++y) {
                    Color* dst_row = fb.pixels_row(y);
                    const Color* glow_row = alpha_map.pixels_row(y);
                    for (i32 x = x_min; x < x_max; ++x) {
                        const Color glow_c = glow_row[x];
                        if (glow_c.a <= 0.0f) continue;
                        dst_row[x] = compositor::blend(glow_c, dst_row[x], BlendMode::Normal);
                    }
                }
            }

        } else if (auto* p = std::any_cast<DropShadowParams>(&inst.params)) {
            // Offset + blur alpha mask, composite behind content
            const i32 w = fb.width(), h = fb.height();
            i32 x_min_src = 0, x_max_src = w;
            i32 y_min_src = 0, y_max_src = h;
            i32 x_min_dst = 0, x_max_dst = w;
            i32 y_min_dst = 0, y_max_dst = h;
            const i32 ox = static_cast<i32>(std::round(p->offset.x));
            const i32 oy = static_cast<i32>(std::round(p->offset.y));
            if (clip) {
                x_min_dst = std::max(0, clip->x0);
                x_max_dst = std::min(w, clip->x1);
                y_min_dst = std::max(0, clip->y0);
                y_max_dst = std::min(h, clip->y1);

                x_min_src = std::max(0, clip->x0 - ox);
                x_max_src = std::min(w, clip->x1 - ox);
                y_min_src = std::max(0, clip->y0 - oy);
                y_max_src = std::min(h, clip->y1 - oy);
            }
            auto shadow_map_fb = acquire_temp_framebuffer(w, h);
            shadow_map_fb->clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
            auto& shadow_map = *shadow_map_fb;
            if (x_min_dst < x_max_dst && y_min_dst < y_max_dst) {
                for (i32 y = y_min_src; y < y_max_src; ++y) {
                    const Color* src_row = fb.pixels_row(y);
                    for (i32 x = x_min_src; x < x_max_src; ++x) {
                        const Color c = src_row[x];
                        if (c.a > 0.0f) {
                            const i32 dx = x + ox;
                            const i32 dy = y + oy;
                            if (dx >= 0 && dx < w && dy >= 0 && dy < h) {
                                Color* shadow_row = shadow_map.pixels_row(dy);
                                shadow_row[dx] = {p->color.r, p->color.g, p->color.b, c.a * p->color.a};
                            }
                        }
                    }
                }
                if (p->radius > 0.0f) apply_blur(shadow_map, p->radius, clip);
                
                // Composite shadow BEHIND content in-place
                for (i32 y = y_min_dst; y < y_max_dst; ++y) {
                    Color*       fb_row = fb.pixels_row(y);
                    const Color* shadow_row = shadow_map.pixels_row(y);
                    for (i32 x = x_min_dst; x < x_max_dst; ++x) {
                        fb_row[x] = compositor::blend(fb_row[x], shadow_row[x], BlendMode::Normal);
                    }
                }
            }

        } else if (auto* p = std::any_cast<BloomParams>(&inst.params)) {
            const i32 w = fb.width(), h = fb.height();
            i32 x_min = 0, x_max = w;
            i32 y_min = 0, y_max = h;
            if (clip) {
                x_min = std::max(0, clip->x0);
                x_max = std::min(w, clip->x1);
                y_min = std::max(0, clip->y0);
                y_max = std::min(h, clip->y1);
            }
            auto bright_fb = acquire_temp_framebuffer(w, h);
            bright_fb->clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
            auto& bright = *bright_fb;
            if (x_min < x_max && y_min < y_max) {
                for (i32 y = y_min; y < y_max; ++y) {
                    const Color* src_row   = fb.pixels_row(y);
                    Color*       bright_row = bright.pixels_row(y);
                    for (i32 x = x_min; x < x_max; ++x) {
                        const Color c = src_row[x];
                        const f32 lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
                        if (lum > p->threshold && c.a > 0.0f) {
                            const f32 excess = (lum - p->threshold) / (1.0f - p->threshold + 1e-4f);
                            bright_row[x] = {c.r * excess, c.g * excess, c.b * excess, c.a};
                        }
                    }
                }
                if (p->radius > 0.0f) apply_blur(bright, p->radius, clip);
                for (i32 y = y_min; y < y_max; ++y) {
                    Color*       dst_row    = fb.pixels_row(y);
                    const Color* bright_row = bright.pixels_row(y);
                    for (i32 x = x_min; x < x_max; ++x) {
                        const Color b = bright_row[x];
                        if (b.a <= 0.0f) continue;
                        const Color src = dst_row[x];
                        dst_row[x] = {
                            std::min(1.0f, src.r + b.r * p->intensity),
                            std::min(1.0f, src.g + b.g * p->intensity),
                            std::min(1.0f, src.b + b.b * p->intensity),
                            src.a
                        };
                    }
                }
            }

        } else if (auto* p = std::any_cast<Fake3DWaveParams>(&inst.params)) {
            apply_fake_3d_wave(fb, *p, time_seconds);
        }
    }
}

// ── Node-level shadow and glow primitives ────────────────────────────────────
//
// These are the shape-level effects (per-RenderNode), distinct from the
// layer-level EffectStack above.

void draw_shadow(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    if (node.shadow.color.a <= 0.0f) return;

    const Color base        = node.shadow.color.to_linear();
    const Mat4& base_model  = state.matrix;
    Mat4 shadow_model = math::translate(Vec3(node.shadow.offset.x, node.shadow.offset.y, 0)) * base_model;

    constexpr int LAYERS = 6;
    for (int i = LAYERS; i >= 1; --i) {
        const f32 t      = static_cast<f32>(i) / LAYERS;
        const f32 spread = node.shadow.radius * t;
        const f32 alpha  = base.a * (1.0f - t * t) * state.opacity;
        if (alpha > 0.0f)
            draw_transformed_shape(fb, node.shape, shadow_model, {base.r, base.g, base.b, alpha}, spread, &state);
    }
    draw_transformed_shape(fb, node.shape, shadow_model,
                           {base.r, base.g, base.b, base.a * 0.7f * state.opacity}, 0.0f, &state);
}

void draw_glow(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    if (node.glow.intensity <= 0.0f || node.glow.color.a <= 0.0f) return;

    const Color base    = node.glow.color.to_linear();
    const Mat4& model   = state.matrix;

    constexpr int LAYERS = 5;
    for (int i = LAYERS; i >= 1; --i) {
        const f32 t      = static_cast<f32>(i) / LAYERS;
        const f32 expand = node.glow.radius * t;
        const f32 alpha  = base.a * node.glow.intensity * (1.0f - t) * state.opacity;
        if (alpha > 0.0f)
            draw_transformed_shape(fb, node.shape, model, {base.r, base.g, base.b, alpha}, expand, &state);
    }
}

} // namespace renderer
} // namespace chronon3d
