// ---------------------------------------------------------------------------
// effect_stack.cpp — Effect stack dispatcher + shadow/glow node-level primitives
// ---------------------------------------------------------------------------

#include "../render_effects_processor.hpp"
#include "../../primitive_renderer.hpp"
#include "effects_internal.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>

namespace chronon3d {
namespace renderer {

namespace {

struct GlowLayerPass {
    float radius_scale;
    float intensity_scale;
    float buffer_scale;
};

[[nodiscard]] i32 blur_padding_for_radius(float radius) {
    return std::max(1, static_cast<i32>(std::ceil(std::max(0.0f, radius))) + 2);
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

BlendMode glow_blend_mode(const GlowParams& params) {
    if (params.quality == GlowQuality::SkiaLike || !params.layers.empty()) {
        return params.blend;
    }
    return params.additive ? BlendMode::Add : BlendMode::Screen;
}

float glow_trigger_from_pixel(const Color& c, const GlowParams& params) {
    if (c.a <= 0.0f) return 0.0f;

    float trigger = c.a;
    if (params.threshold > 0.0f) {
        const float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
        const float denom = std::max(1e-4f, 1.0f - params.threshold);
        trigger = (lum - params.threshold) / denom;
    }

    trigger = std::clamp(trigger, 0.0f, 1.0f);
    if (params.softness != 1.0f) {
        trigger = std::pow(trigger, std::max(0.01f, params.softness));
    }
    return trigger;
}

Color glow_color_from_trigger(float trigger, const GlowParams& params, float intensity_scale) {
    const float amount = trigger * params.intensity * intensity_scale;
    return {
        params.color.r * amount,
        params.color.g * amount,
        params.color.b * amount,
        params.color.a * amount
    };
}

void accumulate_glow_pass(Framebuffer& dst, const Framebuffer& src, const GlowParams& params) {
    const float falloff = std::max(0.01f, params.falloff);
    for (i32 y = 0; y < dst.height(); ++y) {
        Color* dst_row = dst.pixels_row(y);
        const Color* src_row = src.pixels_row(y);
        for (i32 x = 0; x < dst.width(); ++x) {
            Color g = src_row[x];
            if (g.a <= 0.0f) continue;

            const float shaped = std::pow(std::clamp(g.a, 0.0f, 1.0f), falloff);
            if (g.a > 0.0f) {
                const float ratio = shaped / g.a;
                g.r *= ratio;
                g.g *= ratio;
                g.b *= ratio;
            }
            g.a = shaped;

            Color& acc = dst_row[x];
            acc.r = std::min(1.0f, acc.r + g.r);
            acc.g = std::min(1.0f, acc.g + g.g);
            acc.b = std::min(1.0f, acc.b + g.b);
            acc.a = std::max(acc.a, g.a);
        }
    }
}

void accumulate_scaled_glow_pass(Framebuffer& dst, const Framebuffer& src, const GlowParams& params, float scale) {
    const float falloff = std::max(0.01f, params.falloff);
    for (i32 y = 0; y < dst.height(); ++y) {
        Color* dst_row = dst.pixels_row(y);
        const float sy = (static_cast<float>(y) + 0.5f) * scale;
        for (i32 x = 0; x < dst.width(); ++x) {
            const float sx = (static_cast<float>(x) + 0.5f) * scale;
            Color g = src.sample(sx, sy, SamplingMode::Bilinear);
            if (g.a <= 0.0f) continue;

            const float shaped = std::pow(std::clamp(g.a, 0.0f, 1.0f), falloff);
            if (g.a > 0.0f) {
                const float ratio = shaped / g.a;
                g.r *= ratio;
                g.g *= ratio;
                g.b *= ratio;
            }
            g.a = shaped;

            Color& acc = dst_row[x];
            acc.r = std::min(1.0f, acc.r + g.r);
            acc.g = std::min(1.0f, acc.g + g.g);
            acc.b = std::min(1.0f, acc.b + g.b);
            acc.a = std::max(acc.a, g.a);
        }
    }
}

} // namespace

void apply_effect_stack(Framebuffer& fb, const EffectStack& stack,
                        float time_seconds, const std::optional<raster::BBox>& clip,
                        bool diagnostics_enabled) {
    using enum effects::EffectType;
    const auto stack_start = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    double blur_ms = 0.0;
    double tint_ms = 0.0;
    double brightness_ms = 0.0;
    double contrast_ms = 0.0;
    double glow_ms = 0.0;
    double glow_extract_ms = 0.0;
    double glow_blur_ms = 0.0;
    double glow_accumulate_ms = 0.0;
    double glow_composite_ms = 0.0;
    double shadow_ms = 0.0;
    double bloom_ms = 0.0;
    double fake3d_ms = 0.0;

    for (const auto& inst : stack) {
        if (!inst.enabled) continue;

        switch (inst.effect_type) {

        case Blur: {
            auto* p = std::any_cast<BlurParams>(&inst.params);
            if (p && p->radius > 0.0f) {
                const auto t0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                auto effect_clip = expand_effect_clip(clip, fb.width(), fb.height(), p->radius);
                apply_blur(fb, p->radius, effect_clip);
                if (diagnostics_enabled) {
                    blur_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                }
            }
            break;
        }

        case Tint: {
            auto* p = std::any_cast<TintParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                LayerEffect e;
                e.tint = Color{p->color.r, p->color.g, p->color.b, p->color.a * p->amount};
                apply_color_effects(fb, e, clip);
                if (diagnostics_enabled) {
                    tint_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                }
            }
            break;
        }

        case Brightness: {
            auto* p = std::any_cast<BrightnessParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                LayerEffect e; e.brightness = p->value;
                apply_color_effects(fb, e, clip);
                if (diagnostics_enabled) {
                    brightness_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                }
            }
            break;
        }

        case Contrast: {
            auto* p = std::any_cast<ContrastParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                LayerEffect e; e.contrast = p->value;
                apply_color_effects(fb, e, clip);
                if (diagnostics_enabled) {
                    contrast_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                }
            }
            break;
        }

        case Glow: {
            auto* p = std::any_cast<GlowParams>(&inst.params);
            if (p && p->intensity > 0.0f) {
                const auto t0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
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
                const float base_radius = std::max(0.0f, p->radius) * std::max(0.0f, p->spread);
                const i32 blur_pad = blur_padding_for_radius(base_radius);
                const i32 x_min_pad = std::max(0, x_min - blur_pad);
                const i32 x_max_pad = std::min(w, x_max + blur_pad);
                const i32 y_min_pad = std::max(0, y_min - blur_pad);
                const i32 y_max_pad = std::min(h, y_max + blur_pad);
                const i32 roi_w = x_max_pad - x_min_pad;
                const i32 roi_h = y_max_pad - y_min_pad;
                if (roi_w > 0 && roi_h > 0) {
                    auto source_fb = acquire_temp_framebuffer(roi_w, roi_h);
                    source_fb->clear({0,0,0,0});
                    for (i32 y = 0; y < roi_h; ++y) {
                        const i32 sy = y + y_min_pad;
                        const Color* src_row = fb.pixels_row(sy);
                        Color* source_row = source_fb->pixels_row(y);
                        std::copy(src_row + x_min_pad, src_row + x_min_pad + roi_w, source_row);
                    }

                    auto glow_acc_fb = acquire_temp_framebuffer(roi_w, roi_h);
                    glow_acc_fb->clear({0,0,0,0});
                    auto pass_fb = acquire_temp_framebuffer(roi_w, roi_h);
                    pass_fb->clear({0,0,0,0});

                    std::vector<GlowLayerPass> passes;
                    if (!p->layers.empty()) {
                        for (const auto& layer : p->layers) {
                            passes.push_back({layer.radius, layer.opacity, std::clamp(layer.scale, 0.05f, 1.0f)});
                        }
                    } else {
                        const float outer_downscale = std::clamp(p->outer_downscale, 0.05f, 1.0f);
                        passes.push_back({0.10f * base_radius, std::max(0.0f, p->core_strength), 1.0f});
                        passes.push_back({0.35f * base_radius, std::max(0.0f, p->aura_strength), 1.0f});
                        passes.push_back({1.00f * base_radius, std::max(0.0f, p->bloom_strength), outer_downscale});
                    }

                    for (const auto& pass : passes) {
                        const float r = (!p->layers.empty()) ? pass.radius_scale * std::max(0.0f, p->spread) : pass.radius_scale;
                        if (r < 0.5f || pass.intensity_scale <= 0.0f) continue;

                        const auto t_build0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

                        if (pass.buffer_scale >= 0.999f) {
                            pass_fb->clear({0,0,0,0});
                            for (i32 y = 0; y < roi_h; ++y) {
                                Color* pass_row = pass_fb->pixels_row(y);
                                const Color* source_row = source_fb->pixels_row(y);
                                for (i32 x = 0; x < roi_w; ++x) {
                                    const float trigger = glow_trigger_from_pixel(source_row[x], *p);
                                    if (trigger > 0.0f) {
                                        pass_row[x] = glow_color_from_trigger(trigger, *p, pass.intensity_scale);
                                    }
                                }
                            }

                            if (diagnostics_enabled) {
                                glow_extract_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_build0).count();
                            }

                            const auto t_blur0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                            apply_blur(*pass_fb, r, std::nullopt, 1);
                            if (diagnostics_enabled) {
                                glow_blur_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_blur0).count();
                            }

                            const auto t_acc0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                            accumulate_glow_pass(*glow_acc_fb, *pass_fb, *p);
                            if (diagnostics_enabled) {
                                glow_accumulate_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_acc0).count();
                            }
                        } else {
                            const i32 small_w = std::max(1, static_cast<i32>(std::ceil(static_cast<float>(roi_w) * pass.buffer_scale)));
                            const i32 small_h = std::max(1, static_cast<i32>(std::ceil(static_cast<float>(roi_h) * pass.buffer_scale)));
                            auto small_fb = acquire_temp_framebuffer(small_w, small_h);
                            small_fb->clear({0,0,0,0});

                            for (i32 y = 0; y < small_h; ++y) {
                                Color* small_row = small_fb->pixels_row(y);
                                const float sy = (static_cast<float>(y) + 0.5f) / pass.buffer_scale;
                                for (i32 x = 0; x < small_w; ++x) {
                                    const float sx = (static_cast<float>(x) + 0.5f) / pass.buffer_scale;
                                    const Color c = source_fb->sample(sx, sy, SamplingMode::Bilinear);
                                    const float trigger = glow_trigger_from_pixel(c, *p);
                                    if (trigger > 0.0f) {
                                        small_row[x] = glow_color_from_trigger(trigger, *p, pass.intensity_scale);
                                    }
                                }
                            }

                            if (diagnostics_enabled) {
                                glow_extract_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_build0).count();
                            }

                            const auto t_blur0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                            apply_blur(*small_fb, std::max(0.5f, r * pass.buffer_scale), std::nullopt, 1);
                            if (diagnostics_enabled) {
                                glow_blur_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_blur0).count();
                            }

                            const auto t_acc0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                            accumulate_scaled_glow_pass(*glow_acc_fb, *small_fb, *p, pass.buffer_scale);
                            if (diagnostics_enabled) {
                                glow_accumulate_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_acc0).count();
                            }
                        }
                    }

                    const auto t_comp0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                    for (i32 y = 0; y < roi_h; ++y) {
                        const i32 dy = y + y_min_pad;
                        if (dy < 0 || dy >= h) continue;
                        Color* dst_row = fb.pixels_row(dy);
                        const Color* glow_row = glow_acc_fb->pixels_row(y);
                        const Color* source_row = source_fb->pixels_row(y);
                        for (i32 x = 0; x < roi_w; ++x) {
                            const i32 dx = x + x_min_pad;
                            if (dx < 0 || dx >= w) continue;

                            if (p->preserve_source) {
                                dst_row[dx] = compositor::blend(source_row[x], glow_row[x], BlendMode::Normal);
                            } else {
                                dst_row[dx] = compositor::blend(glow_row[x], source_row[x], glow_blend_mode(*p));
                            }
                        }
                    }
                    if (diagnostics_enabled) {
                        glow_composite_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_comp0).count();
                    }
                }
                if (diagnostics_enabled) {
                    glow_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                }
            }
            break;
        }

        case DropShadow: {
            auto* p = std::any_cast<DropShadowParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                const i32 w = fb.width(), h = fb.height();
                const float contact_blur = std::max(1.0f, p->radius * 0.45f);
                const float ambient_blur  = std::max(contact_blur + 4.0f, p->radius * 2.0f);
                const float spread =
                    std::max(std::abs(p->offset.x), std::abs(p->offset.y)) + ambient_blur;
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
                const i32 blur_pad = blur_padding_for_radius(std::max(contact_blur, ambient_blur));
                const i32 x_min_pad = std::max(0, x_min_dst - blur_pad);
                const i32 x_max_pad = std::min(w, x_max_dst + blur_pad);
                const i32 y_min_pad = std::max(0, y_min_dst - blur_pad);
                const i32 y_max_pad = std::min(h, y_max_dst + blur_pad);
                const i32 roi_w = x_max_pad - x_min_pad;
                const i32 roi_h = y_max_pad - y_min_pad;
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
                                    const float alpha = std::min(1.0f, c.a * p->color.a * opacity_scale);
                                    shadow_row[dx - x_min_pad] = {p->color.r, p->color.g, p->color.b, alpha};
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
                if (diagnostics_enabled) {
                    shadow_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                }
            }
            break;
        }

        case Bloom: {
            auto* p = std::any_cast<BloomParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
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
                const i32 blur_pad = blur_padding_for_radius(p->radius);
                const i32 x_min_pad = std::max(0, x_min - blur_pad);
                const i32 x_max_pad = std::min(w, x_max + blur_pad);
                const i32 y_min_pad = std::max(0, y_min - blur_pad);
                const i32 y_max_pad = std::min(h, y_max + blur_pad);
                const i32 roi_w = x_max_pad - x_min_pad;
                const i32 roi_h = y_max_pad - y_min_pad;
                auto bright_fb = acquire_temp_framebuffer(roi_w, roi_h);
                bright_fb->clear(Color{0.0f, 0.0f, 0.0f, 0.0f});
                auto& bright = *bright_fb;
                if (roi_w > 0 && roi_h > 0) {
                    for (i32 y = 0; y < roi_h; ++y) {
                        const i32 sy = y + y_min_pad;
                        if (sy < y_min || sy >= y_max) continue;
                        const Color* src_row   = fb.pixels_row(sy);
                        Color*       bright_row = bright.pixels_row(y);
                        for (i32 x = 0; x < roi_w; ++x) {
                            const i32 sx = x + x_min_pad;
                            if (sx < x_min || sx >= x_max) continue;
                            const Color c = src_row[sx];
                            const f32 lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
                            if (lum > p->threshold && c.a > 0.0f) {
                                const f32 denom = std::max(1.0f - p->threshold, 0.001f);
                                const f32 excess = std::clamp((lum - p->threshold) / denom, 0.0f, 1.0f);
                                bright_row[x] = {c.r * excess, c.g * excess, c.b * excess, c.a};
                            }
                        }
                    }
                    if (p->radius > 0.0f) apply_blur(bright, p->radius, std::nullopt, 1);
                    for (i32 y = 0; y < roi_h; ++y) {
                        const i32 dy = y + y_min_pad;
                        if (dy < 0 || dy >= h) continue;
                        Color*       dst_row    = fb.pixels_row(dy);
                        const Color* bright_row = bright.pixels_row(y);
                        for (i32 x = 0; x < roi_w; ++x) {
                            const i32 dx = x + x_min_pad;
                            if (dx < 0 || dx >= w) continue;
                            const Color b = bright_row[x];
                            if (b.a <= 0.0f) continue;
                            // NaN/Inf guard: skip contaminated bloom values to prevent
                    // framebuffer corruption.  compositor::blend() is NOT used
                    // because its Add mode early-exits when src.a <= 0 (and we
                    // want pure additive RGB with alpha unchanged).
                    if (std::isnan(b.r) || std::isnan(b.g) || std::isnan(b.b) ||
                        std::isinf(b.r) || std::isinf(b.g) || std::isinf(b.b)) {
                        continue;
                    }
                    const float bloom_r = b.r * p->intensity;
                    const float bloom_g = b.g * p->intensity;
                    const float bloom_b = b.b * p->intensity;
                    dst_row[dx].r += bloom_r;
                    dst_row[dx].g += bloom_g;
                    dst_row[dx].b += bloom_b;
                    // Alpha unchanged: additive RGB contribution does not
                    // affect coverage (consistent with compositing convention).
                        }
                    }
                }
                if (diagnostics_enabled) {
                    bloom_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                }
            }
            break;
        }

        case Fake3DWave: {
            auto* p = std::any_cast<Fake3DWaveParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                apply_fake_3d_wave(fb, *p, time_seconds);
                if (diagnostics_enabled) {
                    fake3d_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                }
            }
            break;
        }

        case Unknown:
            break;
        }
    }

    if (diagnostics_enabled) {
        const double total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stack_start).count();
        spdlog::info(
            "[EffectStack] total={:.2f}ms blur={:.2f} tint={:.2f} brightness={:.2f} contrast={:.2f} glow={:.2f} glow_extract={:.2f} glow_blur={:.2f} glow_acc={:.2f} glow_comp={:.2f} shadow={:.2f} bloom={:.2f} fake3d={:.2f}",
            total_ms, blur_ms, tint_ms, brightness_ms, contrast_ms, glow_ms, glow_extract_ms, glow_blur_ms, glow_accumulate_ms, glow_composite_ms, shadow_ms, bloom_ms, fake3d_ms
        );
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
            renderer::draw_transformed_shape(fb, node.shape, shadow_model, {base.r, base.g, base.b, alpha}, spread, &state, nullptr, node.corner_radius);
    }
    renderer::draw_transformed_shape(fb, node.shape, shadow_model,
                           {base.r, base.g, base.b, base.a * 0.7f * state.opacity}, 0.0f, &state, nullptr, node.corner_radius);
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
            renderer::draw_transformed_shape(fb, node.shape, model, {base.r, base.g, base.b, alpha}, expand, &state, nullptr, node.corner_radius);
    }
}

} // namespace renderer
} // namespace chronon3d
