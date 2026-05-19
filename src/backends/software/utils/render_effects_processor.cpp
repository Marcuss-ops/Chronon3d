#include "render_effects_processor.hpp"
#include "../primitive_renderer.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/core/trace.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <algorithm>
#include <cmath>
#include <immintrin.h>

namespace chronon3d {
namespace renderer {

namespace {

constexpr f32 kTau = 6.28318530718f;

inline f32 clamp01(f32 v) {
    return std::clamp(v, 0.0f, 1.0f);
}

inline f32 safe_scale(f32 depth, f32 perspective) {
    return std::max(0.1f, 1.0f + depth * perspective / 100.0f);
}

void deform_horizontal(const Framebuffer& src, Framebuffer& dst, const Fake3DWaveParams& p,
                       float time_seconds, bool shadow_pass) {
    const i32 w = src.width();
    const i32 h = src.height();
    const i32 slices = std::clamp(p.slices, 1, 256);
    const f32 cx = static_cast<f32>(w) * 0.5f;

    for (i32 s = 0; s < slices; ++s) {
        const i32 y0 = (s * h) / slices;
        const i32 y1 = ((s + 1) * h) / slices;
        const i32 yy1 = std::max(y0 + 1, y1);
        const f32 norm = (static_cast<f32>(s) + 0.5f) / static_cast<f32>(slices);
        const f32 angle = time_seconds * p.speed + norm * p.frequency * kTau + p.phase;
        const f32 wave = std::sin(angle);
        const f32 depth = std::cos(angle);
        const f32 dx = wave * p.amplitude_px;
        const f32 scale_x = safe_scale(depth * p.depth_px, p.perspective);
        f32 shade = 1.0f + std::max(0.0f, depth) * p.highlight;
        shade *= 1.0f - std::max(0.0f, -depth) * p.side_darkening;
        shade = clamp01(shade);

        for (i32 y = y0; y < yy1; ++y) {
            for (i32 x = 0; x < w; ++x) {
                const f32 src_x = ((static_cast<f32>(x) - cx) / scale_x) + cx - dx;
                const Color c = src.sample_bilinear(src_x + 0.5f, static_cast<f32>(y) + 0.5f);
                if (c.a <= 0.0f) continue;
                if (shadow_pass) {
                    const f32 a = c.a * p.shadow_color.a;
                    dst.set_pixel(x, y, {
                        p.shadow_color.r,
                        p.shadow_color.g,
                        p.shadow_color.b,
                        a
                    });
                } else {
                    dst.set_pixel(x, y, {
                        c.r * shade,
                        c.g * shade,
                        c.b * shade,
                        c.a
                    });
                }
            }
        }
    }
}

void deform_vertical(const Framebuffer& src, Framebuffer& dst, const Fake3DWaveParams& p,
                     float time_seconds, bool shadow_pass) {
    const i32 w = src.width();
    const i32 h = src.height();
    const i32 slices = std::clamp(p.slices, 1, 256);
    const f32 cy = static_cast<f32>(h) * 0.5f;

    for (i32 s = 0; s < slices; ++s) {
        const i32 x0 = (s * w) / slices;
        const i32 x1 = ((s + 1) * w) / slices;
        const i32 xx1 = std::max(x0 + 1, x1);
        const f32 norm = (static_cast<f32>(s) + 0.5f) / static_cast<f32>(slices);
        const f32 angle = time_seconds * p.speed + norm * p.frequency * kTau + p.phase;
        const f32 wave = std::sin(angle);
        const f32 depth = std::cos(angle);
        const f32 dy = wave * p.amplitude_px;
        const f32 scale_y = safe_scale(depth * p.depth_px, p.perspective);
        f32 shade = 1.0f + std::max(0.0f, depth) * p.highlight;
        shade *= 1.0f - std::max(0.0f, -depth) * p.side_darkening;
        shade = clamp01(shade);

        for (i32 x = x0; x < xx1; ++x) {
            for (i32 y = 0; y < h; ++y) {
                const f32 src_y = ((static_cast<f32>(y) - cy) / scale_y) + cy - dy;
                const Color c = src.sample_bilinear(static_cast<f32>(x) + 0.5f, src_y + 0.5f);
                if (c.a <= 0.0f) continue;
                if (shadow_pass) {
                    const f32 a = c.a * p.shadow_color.a;
                    dst.set_pixel(x, y, {
                        p.shadow_color.r,
                        p.shadow_color.g,
                        p.shadow_color.b,
                        a
                    });
                } else {
                    dst.set_pixel(x, y, {
                        c.r * shade,
                        c.g * shade,
                        c.b * shade,
                        c.a
                    });
                }
            }
        }
    }
}

void deform_wave(const Framebuffer& src, Framebuffer& dst, const Fake3DWaveParams& p,
                 float time_seconds, bool shadow_pass) {
    switch (p.axis) {
    case WaveAxis::Horizontal:
        deform_horizontal(src, dst, p, time_seconds, shadow_pass);
        break;
    case WaveAxis::Vertical:
        deform_vertical(src, dst, p, time_seconds, shadow_pass);
        break;
    }
}

void apply_shadow_buffer(Framebuffer& content, const Framebuffer& shadow) {
    const i32 w = content.width();
    const i32 h = content.height();
    for (i32 y = 0; y < h; ++y) {
        for (i32 x = 0; x < w; ++x) {
            const Color shadow_px = shadow.get_pixel(x, y);
            if (shadow_px.a <= 0.0f) continue;
            const Color content_px = content.get_pixel(x, y);
            content.set_pixel(x, y, compositor::blend(content_px, shadow_px, BlendMode::Normal));
        }
    }
}

inline bool apply_color_effects_avx2(Framebuffer& fb, const LayerEffect& effect) {
#if defined(__AVX2__) || defined(_MSC_VER)
    const bool needs_brightness_contrast = effect.brightness != 0.0f || effect.contrast != 1.0f;
    const bool needs_tint = effect.tint.a > 0.0f;
    if (!needs_brightness_contrast && !needs_tint) {
        return true;
    }

    const i32 w = fb.width();
    const i32 h = fb.height();
    const __m256 half = _mm256_set1_ps(0.5f);
    const __m256 brightness = _mm256_set1_ps(effect.brightness);
    const __m256 contrast = _mm256_set1_ps(effect.contrast);
    const __m256 tint_mix = _mm256_set1_ps(effect.tint.a);
    const __m256 inv_tint_mix = _mm256_set1_ps(1.0f - effect.tint.a);
    const __m256 tint_rgb = _mm256_set_ps(effect.tint.a, effect.tint.b, effect.tint.g, effect.tint.r,
                                          effect.tint.a, effect.tint.b, effect.tint.g, effect.tint.r);

    for (i32 y = 0; y < h; ++y) {
        Color* row = fb.pixels_row(y);
        i32 x = 0;
        for (; x + 1 < w; x += 2) {
            const __m256 src = _mm256_loadu_ps(reinterpret_cast<const float*>(row + x));
            __m256 work = src;
            if (needs_brightness_contrast) {
                work = _mm256_add_ps(work, brightness);
                work = _mm256_sub_ps(work, half);
                work = _mm256_mul_ps(work, contrast);
                work = _mm256_add_ps(work, half);
                work = _mm256_blend_ps(work, src, 0x88);
            }
            if (needs_tint) {
                work = _mm256_mul_ps(work, inv_tint_mix);
                work = _mm256_add_ps(work, _mm256_mul_ps(tint_rgb, tint_mix));
                work = _mm256_blend_ps(work, src, 0x88);
            }
            _mm256_storeu_ps(reinterpret_cast<float*>(row + x), work);
        }
        for (; x < w; ++x) {
            Color c = row[x];
            if (c.a <= 0.0f) {
                continue;
            }

            if (needs_brightness_contrast) {
                auto adj = [&](f32 v) {
                    return std::clamp((v + effect.brightness - 0.5f) * effect.contrast + 0.5f, 0.0f, 1.0f);
                };
                c.r = adj(c.r); c.g = adj(c.g); c.b = adj(c.b);
            }
            if (needs_tint) {
                const f32 t = effect.tint.a;
                c.r = c.r * (1.0f - t) + effect.tint.r * t;
                c.g = c.g * (1.0f - t) + effect.tint.g * t;
                c.b = c.b * (1.0f - t) + effect.tint.b * t;
            }
            row[x] = c;
        }
    }
    return true;
#else
    (void)fb;
    (void)effect;
    return false;
#endif
}

std::shared_ptr<Framebuffer> acquire_temp_framebuffer(int w, int h) {
    if (profiling::g_current_framebuffer_pool) {
        return profiling::g_current_framebuffer_pool->acquire_pooled(
            w, h,
            std::shared_ptr<cache::FramebufferPool>(profiling::g_current_framebuffer_pool, [](auto*){})
        );
    }
    auto fb = std::make_shared<Framebuffer>(w, h);
    fb->clear(Color::transparent());
    return fb;
}

} // namespace

void apply_blur(Framebuffer& fb, f32 radius) {
    const i32 r = std::max(1, static_cast<i32>(std::round(radius)));
    const i32 w = fb.width(), h = fb.height();
    auto tmp_fb = acquire_temp_framebuffer(w, h);
    Framebuffer& tmp = *tmp_fb;

    for (int pass = 0; pass < 3; ++pass) {
        for (i32 y = 0; y < h; ++y) {
            const Color* src_row = fb.pixels_row(y);
            Color* tmp_row = tmp.pixels_row(y);
            Color sum{0, 0, 0, 0};
            for (i32 x = -r; x <= r; ++x) {
                const Color p = src_row[std::clamp(x, 0, w - 1)];
                sum.r += p.r; sum.g += p.g; sum.b += p.b; sum.a += p.a;
            }
            const f32 inv = 1.0f / static_cast<f32>(2 * r + 1);
            for (i32 x = 0; x < w; ++x) {
                tmp_row[x] = {sum.r * inv, sum.g * inv, sum.b * inv, sum.a * inv};
                const Color add = src_row[std::min(x + r + 1, w - 1)];
                const Color rem = src_row[std::max(x - r,     0)];
                sum.r += add.r - rem.r; sum.g += add.g - rem.g;
                sum.b += add.b - rem.b; sum.a += add.a - rem.a;
            }
        }
        for (i32 x = 0; x < w; ++x) {
            Color* dst_row = fb.pixels_row(0);
            Color sum{0, 0, 0, 0};
            for (i32 y = -r; y <= r; ++y) {
                const Color p = tmp.pixels_row(std::clamp(y, 0, h - 1))[x];
                sum.r += p.r; sum.g += p.g; sum.b += p.b; sum.a += p.a;
            }
            const f32 inv = 1.0f / static_cast<f32>(2 * r + 1);
            for (i32 y = 0; y < h; ++y) {
                dst_row[y * w + x] = {sum.r * inv, sum.g * inv, sum.b * inv, sum.a * inv};
                const Color add = tmp.pixels_row(std::min(y + r + 1, h - 1))[x];
                const Color rem = tmp.pixels_row(std::max(y - r,     0))[x];
                sum.r += add.r - rem.r; sum.g += add.g - rem.g;
                sum.b += add.b - rem.b; sum.a += add.a - rem.a;
            }
        }
    }
}

void apply_color_effects(Framebuffer& fb, const LayerEffect& effect) {
    if (apply_color_effects_avx2(fb, effect)) {
        return;
    }

    const i32 w = fb.width(), h = fb.height();
    for (i32 y = 0; y < h; ++y) {
        for (i32 x = 0; x < w; ++x) {
            Color c = fb.get_pixel(x, y);
            if (c.a <= 0.0f) continue;

            if (effect.brightness != 0.0f || effect.contrast != 1.0f) {
                auto adj = [&](f32 v) {
                    return std::clamp((v + effect.brightness - 0.5f) * effect.contrast + 0.5f, 0.0f, 1.0f);
                };
                c.r = adj(c.r); c.g = adj(c.g); c.b = adj(c.b);
            }
            if (effect.tint.a > 0.0f) {
                const f32 t = effect.tint.a;
                c.r = c.r * (1.0f - t) + effect.tint.r * t;
                c.g = c.g * (1.0f - t) + effect.tint.g * t;
                c.b = c.b * (1.0f - t) + effect.tint.b * t;
            }
            fb.set_pixel(x, y, c);
        }
    }
}

void apply_fake_3d_wave(Framebuffer& fb, const Fake3DWaveParams& params, float time_seconds) {
    if (!params.shadow_enabled && params.amplitude_px <= 0.0f && params.depth_px <= 0.0f) {
        return;
    }

    const Framebuffer src = fb;
    auto body_fb = acquire_temp_framebuffer(src.width(), src.height());
    deform_wave(src, *body_fb, params, time_seconds, false);

    if (params.shadow_enabled && params.shadow_color.a > 0.0f) {
        auto shadow_fb = acquire_temp_framebuffer(src.width(), src.height());
        deform_wave(src, *shadow_fb, params, time_seconds, true);
        if (params.shadow_offset.x != 0.0f || params.shadow_offset.y != 0.0f) {
            auto shifted_fb = acquire_temp_framebuffer(src.width(), src.height());
            const i32 ox = static_cast<i32>(std::round(params.shadow_offset.x));
            const i32 oy = static_cast<i32>(std::round(params.shadow_offset.y));
            for (i32 y = 0; y < shadow_fb->height(); ++y) {
                for (i32 x = 0; x < shadow_fb->width(); ++x) {
                    const Color c = shadow_fb->get_pixel(x, y);
                    if (c.a <= 0.0f) continue;
                    shifted_fb->set_pixel(x + ox, y + oy, c);
                }
            }
            shadow_fb = std::move(shifted_fb);
        }
        if (params.shadow_blur > 0.0f) {
            apply_blur(*shadow_fb, params.shadow_blur);
        }
        apply_shadow_buffer(*body_fb, *shadow_fb);
    }

    fb = std::move(*body_fb);
}

static void apply_one_param(Framebuffer& fb, const EffectParams& params, float time_seconds) {
    std::visit([&fb, time_seconds](const auto& p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, BlurParams>) {
            if (p.radius > 0.0f) apply_blur(fb, p.radius);
        } else if constexpr (std::is_same_v<T, TintParams>) {
            LayerEffect e;
            e.tint = Color{p.color.r, p.color.g, p.color.b, p.color.a * p.amount};
            apply_color_effects(fb, e);
        } else if constexpr (std::is_same_v<T, BrightnessParams>) {
            LayerEffect e; e.brightness = p.value;
            apply_color_effects(fb, e);
        } else if constexpr (std::is_same_v<T, ContrastParams>) {
            LayerEffect e; e.contrast = p.value;
            apply_color_effects(fb, e);
        } else if constexpr (std::is_same_v<T, GlowParams>) {
            // Full-frame glow: extract non-transparent pixels, blur, tint, composite
            const i32 w = fb.width(), h = fb.height();
            auto alpha_map_fb = acquire_temp_framebuffer(w, h);
            auto& alpha_map = *alpha_map_fb;
            for (i32 y = 0; y < h; ++y) {
                for (i32 x = 0; x < w; ++x) {
                    const Color c = fb.get_pixel(x, y);
                    if (c.a > 0.0f) {
                        alpha_map.set_pixel(x, y, {p.color.r, p.color.g, p.color.b, c.a * p.intensity});
                    }
                }
            }
            if (p.radius > 0.0f) apply_blur(alpha_map, p.radius);
            for (i32 y = 0; y < h; ++y) {
                for (i32 x = 0; x < w; ++x) {
                    const Color glow_c = alpha_map.get_pixel(x, y);
                    if (glow_c.a <= 0.0f) continue;
                    const Color src_c = fb.get_pixel(x, y);
                    fb.set_pixel(x, y, compositor::blend(glow_c, src_c, BlendMode::Normal));
                }
            }
        } else if constexpr (std::is_same_v<T, DropShadowParams>) {
            // Full-frame shadow: extract alpha, offset, blur, tint, composite behind
            const i32 w = fb.width(), h = fb.height();
            auto shadow_map_fb = acquire_temp_framebuffer(w, h);
            auto& shadow_map = *shadow_map_fb;
            const i32 ox = static_cast<i32>(std::round(p.offset.x));
            const i32 oy = static_cast<i32>(std::round(p.offset.y));
            for (i32 y = 0; y < h; ++y) {
                for (i32 x = 0; x < w; ++x) {
                    const Color c = fb.get_pixel(x, y);
                    if (c.a > 0.0f) {
                        const i32 dx = x + ox;
                        const i32 dy = y + oy;
                        if (dx >= 0 && dx < w && dy >= 0 && dy < h) {
                            shadow_map.set_pixel(dx, dy, {p.color.r, p.color.g, p.color.b, c.a * p.color.a});
                        }
                    }
                }
            }
            if (p.radius > 0.0f) apply_blur(shadow_map, p.radius);
            
            // Compose shadow BEHIND: we need a temp buffer for this
            auto result_fb = acquire_temp_framebuffer(w, h);
            auto& result = *result_fb;
            for (i32 y = 0; y < h; ++y) {
                for (i32 x = 0; x < w; ++x) {
                    const Color sc = shadow_map.get_pixel(x, y);
                    const Color fc = fb.get_pixel(x, y);
                    result.set_pixel(x, y, compositor::blend(fc, sc, BlendMode::Normal));
                }
            }
            fb = std::move(result);
        } else if constexpr (std::is_same_v<T, BloomParams>) {
            const i32 w = fb.width(), h = fb.height();
            auto bright_fb = acquire_temp_framebuffer(w, h);
            auto& bright = *bright_fb;
            for (i32 y = 0; y < h; ++y) {
                for (i32 x = 0; x < w; ++x) {
                    const Color c = fb.get_pixel(x, y);
                    const f32 lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
                    if (lum > p.threshold && c.a > 0.0f) {
                        const f32 excess = (lum - p.threshold) / (1.0f - p.threshold + 1e-4f);
                        bright.set_pixel(x, y, {c.r * excess, c.g * excess, c.b * excess, c.a});
                    }
                }
            }
            if (p.radius > 0.0f) apply_blur(bright, p.radius);
            for (i32 y = 0; y < h; ++y) {
                for (i32 x = 0; x < w; ++x) {
                    const Color b = bright.get_pixel(x, y);
                    if (b.a <= 0.0f) continue;
                    const Color src = fb.get_pixel(x, y);
                    fb.set_pixel(x, y, {
                        std::min(1.0f, src.r + b.r * p.intensity),
                        std::min(1.0f, src.g + b.g * p.intensity),
                        std::min(1.0f, src.b + b.b * p.intensity),
                        src.a
                    });
                }
            }
        } else if constexpr (std::is_same_v<T, Fake3DWaveParams>) {
            apply_fake_3d_wave(fb, p, time_seconds);
        }
    }, params);
}

void apply_effect_stack(Framebuffer& fb, const EffectStack& stack, float time_seconds) {
    for (const auto& inst : stack) {
        if (!inst.enabled) continue;
        
        // Handle both variant (legacy) and direct types (modular)
        if (auto* v = std::any_cast<EffectParams>(&inst.params)) {
            apply_one_param(fb, *v, time_seconds);
        } else if (auto* p = std::any_cast<BlurParams>(&inst.params)) {
            apply_one_param(fb, EffectParams{*p}, time_seconds);
        } else if (auto* p = std::any_cast<TintParams>(&inst.params)) {
            apply_one_param(fb, EffectParams{*p}, time_seconds);
        } else if (auto* p = std::any_cast<BrightnessParams>(&inst.params)) {
            apply_one_param(fb, EffectParams{*p}, time_seconds);
        } else if (auto* p = std::any_cast<ContrastParams>(&inst.params)) {
            apply_one_param(fb, EffectParams{*p}, time_seconds);
        } else if (auto* p = std::any_cast<GlowParams>(&inst.params)) {
            apply_one_param(fb, EffectParams{*p}, time_seconds);
        } else if (auto* p = std::any_cast<DropShadowParams>(&inst.params)) {
            apply_one_param(fb, EffectParams{*p}, time_seconds);
        } else if (auto* p = std::any_cast<BloomParams>(&inst.params)) {
            apply_one_param(fb, EffectParams{*p}, time_seconds);
        } else if (auto* p = std::any_cast<Fake3DWaveParams>(&inst.params)) {
            apply_one_param(fb, EffectParams{*p}, time_seconds);
        }
    }
}

void draw_shadow(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    if (node.shadow.color.a <= 0.0f) return;

    const Color base = node.shadow.color.to_linear();
    const Mat4& base_model = state.matrix;
    Mat4 shadow_model = math::translate(Vec3(node.shadow.offset.x, node.shadow.offset.y, 0)) * base_model;

    constexpr int LAYERS = 6;
    for (int i = LAYERS; i >= 1; --i) {
        const f32 t      = static_cast<f32>(i) / LAYERS;
        const f32 spread = node.shadow.radius * t;
        const f32 alpha  = base.a * (1.0f - t * t) * state.opacity;
        if (alpha > 0.0f)
            draw_transformed_shape(fb, node.shape, shadow_model, {base.r, base.g, base.b, alpha}, spread, &state);
    }
    draw_transformed_shape(fb, node.shape, shadow_model, {base.r, base.g, base.b, base.a * 0.7f * state.opacity}, 0.0f, &state);
}

void draw_glow(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    if (node.glow.intensity <= 0.0f || node.glow.color.a <= 0.0f) return;

    const Color base = node.glow.color.to_linear();
    const Mat4& model = state.matrix;

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
