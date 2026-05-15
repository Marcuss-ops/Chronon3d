#include "render_effects_processor.hpp"
#include "../primitive_renderer.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {
namespace renderer {

void apply_blur(Framebuffer& fb, f32 radius) {
    const i32 r = std::max(1, static_cast<i32>(std::round(radius)));
    const i32 w = fb.width(), h = fb.height();
    Framebuffer tmp(w, h);
    tmp.clear(Color::transparent());

    for (int pass = 0; pass < 3; ++pass) {
        for (i32 y = 0; y < h; ++y) {
            Color sum{0, 0, 0, 0};
            for (i32 x = -r; x <= r; ++x) {
                Color p = fb.get_pixel(std::clamp(x, 0, w - 1), y);
                sum.r += p.r; sum.g += p.g; sum.b += p.b; sum.a += p.a;
            }
            const f32 inv = 1.0f / static_cast<f32>(2 * r + 1);
            for (i32 x = 0; x < w; ++x) {
                tmp.set_pixel(x, y, {sum.r * inv, sum.g * inv, sum.b * inv, sum.a * inv});
                Color add = fb.get_pixel(std::min(x + r + 1, w - 1), y);
                Color rem = fb.get_pixel(std::max(x - r,     0),     y);
                sum.r += add.r - rem.r; sum.g += add.g - rem.g;
                sum.b += add.b - rem.b; sum.a += add.a - rem.a;
            }
        }
        for (i32 x = 0; x < w; ++x) {
            Color sum{0, 0, 0, 0};
            for (i32 y = -r; y <= r; ++y) {
                Color p = tmp.get_pixel(x, std::clamp(y, 0, h - 1));
                sum.r += p.r; sum.g += p.g; sum.b += p.b; sum.a += p.a;
            }
            const f32 inv = 1.0f / static_cast<f32>(2 * r + 1);
            for (i32 y = 0; y < h; ++y) {
                fb.set_pixel(x, y, {sum.r * inv, sum.g * inv, sum.b * inv, sum.a * inv});
                Color add = tmp.get_pixel(x, std::min(y + r + 1, h - 1));
                Color rem = tmp.get_pixel(x, std::max(y - r,     0));
                sum.r += add.r - rem.r; sum.g += add.g - rem.g;
                sum.b += add.b - rem.b; sum.a += add.a - rem.a;
            }
        }
    }
}

void apply_color_effects(Framebuffer& fb, const LayerEffect& effect) {
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

void apply_effect_stack(Framebuffer& fb, const EffectStack& stack) {
    for (const auto& inst : stack) {
        if (!inst.enabled) continue;
        std::visit([&fb](const auto& p) {
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
            } else if constexpr (std::is_same_v<T, BloomParams>) {
                const i32 w = fb.width(), h = fb.height();
                Framebuffer bright(w, h);
                bright.clear(Color::transparent());
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
            }
        }, inst.params);
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
