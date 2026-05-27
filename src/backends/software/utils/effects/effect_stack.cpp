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

namespace {

struct GlowLayerPass {
    float radius_scale;
    float intensity_scale;
};

[[nodiscard]] Color add_glow_color(const Color& dst, const Color& glow) {
    const float r = std::clamp(glow.r, 0.0f, 1.0f);
    const float g = std::clamp(glow.g, 0.0f, 1.0f);
    const float b = std::clamp(glow.b, 0.0f, 1.0f);
    return {
        std::min(1.0f, dst.r + r),
        std::min(1.0f, dst.g + g),
        std::min(1.0f, dst.b + b),
        std::max(dst.a, glow.a)
    };
}

[[nodiscard]] Color screen_glow_color(const Color& dst, const Color& glow) {
    const float r = std::clamp(glow.r, 0.0f, 1.0f);
    const float g = std::clamp(glow.g, 0.0f, 1.0f);
    const float b = std::clamp(glow.b, 0.0f, 1.0f);
    return {
        1.0f - (1.0f - dst.r) * (1.0f - r),
        1.0f - (1.0f - dst.g) * (1.0f - g),
        1.0f - (1.0f - dst.b) * (1.0f - b),
        std::max(dst.a, glow.a)
    };
}

std::optional<raster::BBox> expand_effect_clip(
    const std::optional<raster::BBox>& clip,
    int width,
    int height,
    float spread
) {
    if (!clip) {
        return std::nullopt;
    }

    const int margin = static_cast<int>(std::ceil(std::max(0.0f, spread))) + 2;

    raster::BBox out = *clip;
    out.x0 -= margin;
    out.y0 -= margin;
    out.x1 += margin;
    out.y1 += margin;
    out.clip_to(width, height);
    return out;
}

} // namespace

void apply_effect_stack(Framebuffer& fb, const EffectStack& stack,
                        float time_seconds, const std::optional<raster::BBox>& clip) {
    for (const auto& inst : stack) {
        if (!inst.enabled) continue;

        if (auto* p = std::any_cast<BlurParams>(&inst.params)) {
        if (p->radius > 0.0f) {
            auto effect_clip = expand_effect_clip(clip, fb.width(), fb.height(), p->radius);
            apply_blur(fb, p->radius, effect_clip);
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
            if (p->intensity > 0.0f) {
                const i32 w = fb.width(), h = fb.height();
                const float extent = glow_effect_extent(*p);
                auto effect_clip = expand_effect_clip(clip, w, h, extent);
                
                i32 x_min = 0, x_max = w;
                i32 y_min = 0, y_max = h;
                if (effect_clip) {
                    x_min = std::max(0, effect_clip->x0);
                    x_max = std::min(w, effect_clip->x1);
                    y_min = std::max(0, effect_clip->y0);
                    y_max = std::min(h, effect_clip->y1);
                }
                const i32 roi_w = x_max - x_min;
                const i32 roi_h = y_max - y_min;
                if (roi_w > 0 && roi_h > 0) {
                    auto original_fb = acquire_temp_framebuffer(roi_w, roi_h);
                    auto glow_acc_fb = acquire_temp_framebuffer(roi_w, roi_h);
                    original_fb->clear({0,0,0,0});
                    glow_acc_fb->clear({0,0,0,0});

                    // 1. Extract source into a local ROI so blur sampling never sees
                    // stale pixels outside the effect bounds.
                    for (i32 y = 0; y < roi_h; ++y) {
                        const Color* src_row = fb.pixels_row(y + y_min);
                        Color* orig_row = original_fb->pixels_row(y);
                        for (i32 x = 0; x < roi_w; ++x) {
                            orig_row[x] = src_row[x + x_min];
                        }
                    }

                    const float base_radius = std::max(0.0f, p->radius) * std::max(0.0f, p->spread);
                    const GlowLayerPass passes[] = {
                        {0.35f, std::max(0.0f, p->core_strength)},
                        {0.75f, std::max(0.0f, p->aura_strength)},
                        {1.50f, std::max(0.0f, p->bloom_strength)},
                    };

                    for (const auto& pass : passes) {
                        auto pass_fb = acquire_temp_framebuffer(roi_w, roi_h);
                        pass_fb->clear({0,0,0,0});
                        const float r = base_radius * pass.radius_scale;
                        if (r < 0.5f) continue;

                        // Build a glow mask from source alpha/luma, then tint it.
                        for (i32 y = 0; y < roi_h; ++y) {
                            const Color* orig_row = original_fb->pixels_row(y);
                            Color* pass_row = pass_fb->pixels_row(y);
                            for (i32 x = 0; x < roi_w; ++x) {
                                const Color c = orig_row[x];
                                if (c.a <= 0.0f) continue;

                                float trigger = c.a;
                                if (p->threshold > 0.0f) {
                                    const float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
                                    trigger = std::max(0.0f, (lum - p->threshold) / (1.0f - p->threshold + 1e-4f));
                                }

                                if (trigger > 0.0f) {
                                    trigger = std::clamp(trigger, 0.0f, 1.0f);
                                    if (p->softness != 1.0f) {
                                        trigger = std::pow(trigger, std::max(0.01f, p->softness));
                                    }

                                    const float amount = trigger * p->intensity * pass.intensity_scale;
                                    pass_row[x] = {
                                        p->color.r * amount,
                                        p->color.g * amount,
                                        p->color.b * amount,
                                        p->color.a * amount
                                    };
                                }
                            }
                        }

                        apply_blur(*pass_fb, r, std::nullopt);

                        // Accumulate into glow_acc_fb
                        for (i32 y = 0; y < roi_h; ++y) {
                            Color* acc_row = glow_acc_fb->pixels_row(y);
                            const Color* pass_row = pass_fb->pixels_row(y);
                            for (i32 x = 0; x < roi_w; ++x) {
                                Color g = pass_row[x];
                                if (g.a <= 0.0f) continue;

                                const float falloff = std::max(0.01f, p->falloff);
                                const float shaped = std::pow(std::clamp(g.a, 0.0f, 1.0f), falloff);
                                if (g.a > 0.0f) {
                                    const float ratio = shaped / g.a;
                                    g.r *= ratio;
                                    g.g *= ratio;
                                    g.b *= ratio;
                                }
                                g.a = shaped;

                                Color& acc = acc_row[x];
                                acc.r = std::min(1.0f, acc.r + g.r);
                                acc.g = std::min(1.0f, acc.g + g.g);
                                acc.b = std::min(1.0f, acc.b + g.b);
                                acc.a = std::max(acc.a, g.a);
                            }
                        }
                    }

                    // 3. Composite the glow back into the original buffer (glow behind).
                    for (i32 y = 0; y < roi_h; ++y) {
                        Color* dst_row = fb.pixels_row(y + y_min);
                        const Color* glow_row = glow_acc_fb->pixels_row(y);
                        for (i32 x = 0; x < roi_w; ++x) {
                            if (glow_row[x].a <= 0.0f) continue;

                            Color& dst = dst_row[x + x_min];
                            dst = compositor::blend(dst, glow_row[x], BlendMode::Normal);
                        }
                    }
                }
            }

        } else if (auto* p = std::any_cast<DropShadowParams>(&inst.params)) {
            // Offset + blur alpha mask, composite behind content
            const i32 w = fb.width(), h = fb.height();
            const float spread =
                std::max(std::abs(p->offset.x), std::abs(p->offset.y)) + p->radius;
            auto effect_clip = expand_effect_clip(clip, w, h, spread);
            i32 x_min_src = 0, x_max_src = w;
            i32 y_min_src = 0, y_max_src = h;
            i32 x_min_dst = 0, x_max_dst = w;
            i32 y_min_dst = 0, y_max_dst = h;
            const i32 ox = static_cast<i32>(std::round(p->offset.x));
            const i32 oy = static_cast<i32>(std::round(p->offset.y));
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
                if (p->radius > 0.0f) apply_blur(shadow_map, p->radius, effect_clip);
                
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
            auto effect_clip = expand_effect_clip(clip, w, h, p->radius);
            i32 x_min = 0, x_max = w;
            i32 y_min = 0, y_max = h;
            if (effect_clip) {
                x_min = std::max(0, effect_clip->x0);
                x_max = std::min(w, effect_clip->x1);
                y_min = std::max(0, effect_clip->y0);
                y_max = std::min(h, effect_clip->y1);
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
                if (p->radius > 0.0f) apply_blur(bright, p->radius, effect_clip);
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
    Mat4 shadow_model = glm::translate(Mat4(1.0f), Vec3(node.shadow.offset.x, node.shadow.offset.y, 0)) * base_model;

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
