// Software effect processors: core colour / grade operations.
// Spatial, halo and composite stages live in the *_composite unit.
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/software_session_resources.hpp>
#include <chronon3d/backends/software/effect_processor.hpp>
#include "../effects/render_effects_processor.hpp"
#include "effects/color/exposure_levels.hpp"
#include "effects/generate/fill_noise_offset.hpp"
#include "effects/generate/fractal_noise.hpp"
#include "effects/generate/checkerboard.hpp"
#include "effects/generate/four_color_gradient.hpp"
#include "effects/generate/light_rays.hpp"
#include "effects/blur/directional_blur.hpp"
#include "effects/blur/radial_blur.hpp"
#include "effects/stroke/stroke.hpp"
#include "effects/distort/warp.hpp"
#include <chronon3d/effects/curves.hpp>
#include <chronon3d/effects/color_pipeline.hpp>
#include <chronon3d/effects/color_contract.hpp>
#include <chronon3d/effects/glow_pipeline.hpp>
#include <chronon3d/backends/software/sampling/sampler2d.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>


namespace chronon3d::renderer {

class SoftwareBlurEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<BlurParams>(&params)) {
            if (p->box)
                renderer::apply_box_blur(fb, p->radius, context.clip,
                                         p->passes == 0 ? 3 : p->passes);
            else
                renderer::apply_blur(fb, p->radius, context.clip);
        }
    }
};

class SoftwareTintEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<TintParams>(&params)) {
            LayerEffect e;
            e.tint = Color{p->color.r, p->color.g, p->color.b, p->color.a * p->amount};
            apply_color_effects(fb, e, context.clip);
        }
    }
};

class SoftwareLottieTintEffectProcessor final : public EffectProcessor {
    static float to_srgb(float value) {
        value = std::max(value, 0.0f);
        return value <= 0.0031308f ? 12.92f * value
            : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    }
    static float to_linear(float value) {
        value = std::clamp(value, 0.0f, 1.0f);
        return value <= 0.04045f ? value / 12.92f
            : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<LottieTintParams>(&params);
        if (!p) throw std::runtime_error("Lottie Tint processor received invalid parameters");
        const std::array values{p->black_r, p->black_g, p->black_b,
                                p->white_r, p->white_g, p->white_b, p->amount};
        if (std::any_of(values.begin(), values.end(), [](float v) {
                return !std::isfinite(v) || v < 0.0f || v > 1.0f;
            }))
            throw std::runtime_error("Lottie Tint parameters must be finite and in [0,1]");
        const auto clip = context.clip;
        const int x0 = clip ? std::clamp(clip->x0, 0, fb.width()) : 0;
        const int x1 = clip ? std::clamp(clip->x1, 0, fb.width()) : fb.width();
        const int y0 = clip ? std::clamp(clip->y0, 0, fb.height()) : 0;
        const int y1 = clip ? std::clamp(clip->y1, 0, fb.height()) : fb.height();
        const std::array black{p->black_r, p->black_g, p->black_b};
        const std::array white{p->white_r, p->white_g, p->white_b};
        for (int y = y0; y < y1; ++y) {
            Color* row = fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                if (row[x].a <= 1.0e-8f) continue;
                auto rgb = color::unpremultiply_rgb(row[x]);
                const float gray = to_srgb((rgb.r + rgb.g + rgb.b) / 3.0f);
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    const float source = channel == 0 ? rgb.r : channel == 1 ? rgb.g : rgb.b;
                    const float mapped = to_linear(black[channel] +
                        gray * (white[channel] - black[channel]));
                    const float output = std::lerp(source, mapped, p->amount);
                    if (channel == 0) rgb.r = output;
                    else if (channel == 1) rgb.g = output;
                    else rgb.b = output;
                }
                row[x] = color::premultiply_rgb(rgb, row[x].a);
            }
        }
    }
};

class SoftwareLottieTritoneEffectProcessor final : public EffectProcessor {
    static float to_srgb(float value) {
        value = std::max(value, 0.0f);
        return value <= 0.0031308f ? 12.92f * value
            : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    }
    static float to_linear(float value) {
        value = std::clamp(value, 0.0f, 1.0f);
        return value <= 0.04045f ? value / 12.92f
            : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<LottieTritoneParams>(&params);
        if (!p) throw std::runtime_error("Lottie Tritone processor received invalid parameters");
        const std::array values{p->dark_r, p->dark_g, p->dark_b,
            p->mid_r, p->mid_g, p->mid_b, p->bright_r, p->bright_g,
            p->bright_b, p->amount};
        if (std::any_of(values.begin(), values.end(), [](float v) {
                return !std::isfinite(v) || v < 0.0f || v > 1.0f;
            }))
            throw std::runtime_error("Lottie Tritone parameters must be finite and in [0,1]");
        const std::array dark{p->dark_r, p->dark_g, p->dark_b};
        const std::array mid{p->mid_r, p->mid_g, p->mid_b};
        const std::array bright{p->bright_r, p->bright_g, p->bright_b};
        const auto clip = context.clip;
        const int x0 = clip ? std::clamp(clip->x0, 0, fb.width()) : 0;
        const int x1 = clip ? std::clamp(clip->x1, 0, fb.width()) : fb.width();
        const int y0 = clip ? std::clamp(clip->y0, 0, fb.height()) : 0;
        const int y1 = clip ? std::clamp(clip->y1, 0, fb.height()) : fb.height();
        for (int y = y0; y < y1; ++y) {
            Color* row = fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                if (row[x].a <= 1.0e-8f) continue;
                auto rgb = color::unpremultiply_rgb(row[x]);
                const float gray = to_srgb((rgb.r + rgb.g + rgb.b) / 3.0f);
                const float lower = std::min(gray * 2.0f, 1.0f);
                const float upper = std::max((gray - 0.5f) * 2.0f, 0.0f);
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    const float mapped_srgb = gray <= 0.5f
                        ? std::lerp(dark[channel], mid[channel], lower)
                        : std::lerp(mid[channel], bright[channel], upper);
                    const float source = channel == 0 ? rgb.r : channel == 1 ? rgb.g : rgb.b;
                    const float output = std::lerp(source, to_linear(mapped_srgb), p->amount);
                    if (channel == 0) rgb.r = output;
                    else if (channel == 1) rgb.g = output;
                    else rgb.b = output;
                }
                row[x] = color::premultiply_rgb(rgb, row[x].a);
            }
        }
    }
};

class SoftwareBrightnessEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<BrightnessParams>(&params);
        if (!p) throw std::runtime_error("Brightness processor received invalid parameters");
        LayerEffect e; e.brightness = p->value;
        apply_color_effects(fb, e, context.clip);
    }
};

class SoftwareContrastEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<ContrastParams>(&params);
        if (!p) throw std::runtime_error("Contrast processor received invalid parameters");
        LayerEffect e; e.contrast = p->value;
        apply_color_effects(fb, e, context.clip);
    }
};

class SoftwareGlowEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<GlowParams>(&params);
        if (!p) throw std::runtime_error("Glow processor received invalid parameters");
        if (p->intensity > 0.0f) {
            if (!context.effect_scratch)
                throw std::runtime_error("Glow processor requires EffectScratchResources");
            run_glow_pipeline(fb, GlowPipeline::from(*p), context.clip,
                              /*source_is_alpha_mask=*/false, context.debug_cfg,
                              &context.effect_scratch->glow_kernel_scratch());
        }
    }
};

class SoftwareInnerGlowEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<InnerGlowParams>(&params);
        if (!p) throw std::runtime_error("InnerGlow processor received invalid parameters");
        const float size = std::clamp(p->size, 0.0f, 256.0f);
        const float choke = std::clamp(p->choke, 0.0f, 1.0f);
        const float intensity = std::clamp(p->intensity, 0.0f, 4.0f);
        if (size <= 0.0f || intensity <= 0.0f) return;
        const int w = fb.width();
        const int h = fb.height();
        if (w <= 0 || h <= 0) return;
        (void)context;

        const int band = std::max(1, static_cast<int>(std::lround(size)));
        const int inset = std::clamp(static_cast<int>(std::lround(choke * size)), 0, 256);
        const auto erode_alpha = [w, h](const Framebuffer& source, int radius) {
            Framebuffer result(w, h);
            result.clear(Color::transparent());
            if (radius <= 0) {
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                        result.set_pixel(x, y, Color{0.0f, 0.0f, 0.0f, source.get_pixel(x, y).a});
                return result;
            }
            Framebuffer horizontal(w, h);
            horizontal.clear(Color::transparent());
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    float value = 1.0f;
                    for (int dx = -radius; dx <= radius; ++dx) {
                        const int sx = std::clamp(x + dx, 0, w - 1);
                        value = std::min(value, source.get_pixel(sx, y).a);
                    }
                    horizontal.set_pixel(x, y, Color{0.0f, 0.0f, 0.0f, value});
                }
            }
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    float value = 1.0f;
                    for (int dy = -radius; dy <= radius; ++dy) {
                        const int sy = std::clamp(y + dy, 0, h - 1);
                        value = std::min(value, horizontal.get_pixel(x, sy).a);
                    }
                    result.set_pixel(x, y, Color{0.0f, 0.0f, 0.0f, value});
                }
            }
            return result;
        };

        const Framebuffer outer = erode_alpha(fb, inset);
        const Framebuffer inner = erode_alpha(fb, inset + band);
        Framebuffer rim(w, h);
        rim.clear(Color::transparent());
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float value = std::max(0.0f, outer.get_pixel(x, y).a - inner.get_pixel(x, y).a);
                rim.set_pixel(x, y, Color{0.0f, 0.0f, 0.0f, value});
            }
        }
        apply_blur(rim, std::max(0.5f, size * 0.25f), std::nullopt, 1);

        const Color tint{std::clamp(p->color_r, 0.0f, 1.0f),
                         std::clamp(p->color_g, 0.0f, 1.0f),
                         std::clamp(p->color_b, 0.0f, 1.0f),
                         std::clamp(p->color_a, 0.0f, 1.0f)};
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                Color& pixel = fb.pixels_row(y)[x];
                if (pixel.a <= 0.0f) continue;
                const float coverage = std::clamp(rim.get_pixel(x, y).a * intensity, 0.0f, 1.0f) *
                                       std::clamp(pixel.a, 0.0f, 1.0f);
                if (coverage <= 0.0f) continue;
                Color straight = pixel.unpremultiplied();
                straight.r = 1.0f - (1.0f - straight.r) * (1.0f - tint.r * coverage);
                straight.g = 1.0f - (1.0f - straight.g) * (1.0f - tint.g * coverage);
                straight.b = 1.0f - (1.0f - straight.b) * (1.0f - tint.b * coverage);
                pixel = straight.premultiplied();
            }
        }
    }
};

class SoftwareGlowAdvancedEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<GlowAdvancedParams>(&params);
        if (!p) throw std::runtime_error("GlowAdvanced processor received invalid parameters");
        if (p->intensity <= 0.0f) return;
        if (!context.effect_scratch)
            throw std::runtime_error("GlowAdvanced processor requires EffectScratchResources");
        run_glow_pipeline(fb, GlowPipeline::from(*p), context.clip,
                          /*source_is_alpha_mask=*/false, context.debug_cfg,
                          &context.effect_scratch->glow_kernel_scratch());
    }
};

class SoftwareSaturationEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<SaturationParams>(&params);
        if (!p) throw std::runtime_error("Saturation processor received invalid parameters");
        apply_saturation(fb, p->value, context.clip);
    }
};

class SoftwareHueRotateEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<HueRotateParams>(&params);
        if (!p) throw std::runtime_error("HueRotate processor received invalid parameters");
        apply_hue_rotate(fb, p->degrees, context.clip);
    }
};

class SoftwareInvertEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<InvertParams>(&params);
        if (!p) throw std::runtime_error("Invert processor received invalid parameters");
        apply_invert(fb, p->amount, context.clip);
    }
};


std::unique_ptr<EffectProcessor> create_blur_effect_processor() {
    return std::make_unique<SoftwareBlurEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_tint_effect_processor() {
    return std::make_unique<SoftwareTintEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_lottie_tint_effect_processor() {
    return std::make_unique<SoftwareLottieTintEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_lottie_tritone_effect_processor() {
    return std::make_unique<SoftwareLottieTritoneEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_brightness_effect_processor() {
    return std::make_unique<SoftwareBrightnessEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_contrast_effect_processor() {
    return std::make_unique<SoftwareContrastEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_glow_effect_processor() {
    return std::make_unique<SoftwareGlowEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_saturation_effect_processor() {
    return std::make_unique<SoftwareSaturationEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_hue_rotate_effect_processor() {
    return std::make_unique<SoftwareHueRotateEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_invert_effect_processor() {
    return std::make_unique<SoftwareInvertEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_inner_glow_effect_processor() {
    return std::make_unique<SoftwareInnerGlowEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_glow_advanced_effect_processor() {
    return std::make_unique<SoftwareGlowAdvancedEffectProcessor>();
}


} // namespace chronon3d::renderer
