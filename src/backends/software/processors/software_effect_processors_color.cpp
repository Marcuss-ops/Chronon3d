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

class SoftwareVignetteEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<VignetteParams>(&params);
        if (!p) throw std::runtime_error("Vignette processor received invalid parameters");
        apply_vignette(fb, p->radius, p->softness, p->amount, p->color, context.clip);
    }
};

class SoftwareDropShadowEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<DropShadowParams>(&params);
        if (!p) throw std::runtime_error("DropShadow processor received invalid parameters");
        apply_shadow_effect(fb, *p, context.clip, context.diagnostics_enabled);
    }
};

class SoftwareBloomEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<BloomParams>(&params);
        if (!p) throw std::runtime_error("Bloom processor received invalid parameters");
        run_bloom_effect(fb, *p, context.clip);
    }
};

class SoftwareExposureEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<ExposureParams>(&params)) {
            apply_exposure(fb, p->stops, context.clip);
        }
    }
};

class SoftwareLevelsEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<LevelsParams>(&params)) {
            apply_levels(fb,
                         p->master.input_black, p->master.input_white,
                         p->master.gamma,
                         p->master.output_black, p->master.output_white,
                         p->red.input_black, p->red.input_white,
                         p->red.gamma,
                         p->red.output_black, p->red.output_white,
                         p->green.input_black, p->green.input_white,
                         p->green.gamma,
                         p->green.output_black, p->green.output_white,
                         p->blue.input_black, p->blue.input_white,
                         p->blue.gamma,
                         p->blue.output_black, p->blue.output_white,
                         context.clip);
        }
    }
};

class SoftwareTemperatureTintEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<TemperatureTintParams>(&params);
        if (!p) throw std::runtime_error("TemperatureTint processor received invalid parameters");
        const float temperature = std::clamp(p->temperature, -1.0f, 1.0f);
        const float tint = std::clamp(p->tint, -1.0f, 1.0f);
        const float red_gain = std::exp2(0.35f * temperature + 0.15f * tint);
        const float green_gain = std::exp2(0.05f * temperature - 0.30f * tint);
        const float blue_gain = std::exp2(-0.35f * temperature + 0.15f * tint);
        int x0 = 0, x1 = fb.width(), y0 = 0, y1 = fb.height();
        if (context.clip) {
            x0 = std::clamp(context.clip->x0, 0, fb.width());
            x1 = std::clamp(context.clip->x1, 0, fb.width());
            y0 = std::clamp(context.clip->y0, 0, fb.height());
            y1 = std::clamp(context.clip->y1, 0, fb.height());
        }
        for (int y = y0; y < y1; ++y) {
            Color* row = fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                if (row[x].a <= 0.0f) continue;
                Color straight = row[x].unpremultiplied();
                straight.r *= red_gain;
                straight.g *= green_gain;
                straight.b *= blue_gain;
                row[x] = straight.premultiplied();
            }
        }
    }
};

class SoftwareLiftGammaGainEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<LiftGammaGainParams>(&params);
        if (!p) throw std::runtime_error("LiftGammaGain processor received invalid parameters");
        const float lift = std::clamp(p->lift, -1.0f, 1.0f);
        const float gamma = std::clamp(p->gamma, 0.1f, 4.0f);
        const float gain = std::clamp(p->gain, 0.0f, 4.0f);
        const float exponent = 1.0f / gamma;
        const auto shape = [=](float value) {
            const float lifted = value + lift;
            return std::copysign(std::pow(std::abs(lifted), exponent), lifted) * gain;
        };
        int x0 = 0, x1 = fb.width(), y0 = 0, y1 = fb.height();
        if (context.clip) {
            x0 = std::clamp(context.clip->x0, 0, fb.width());
            x1 = std::clamp(context.clip->x1, 0, fb.width());
            y0 = std::clamp(context.clip->y0, 0, fb.height());
            y1 = std::clamp(context.clip->y1, 0, fb.height());
        }
        for (int y = y0; y < y1; ++y) {
            Color* row = fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                if (row[x].a <= 0.0f) continue;
                Color straight = row[x].unpremultiplied();
                straight.r = shape(straight.r);
                straight.g = shape(straight.g);
                straight.b = shape(straight.b);
                row[x] = straight.premultiplied();
            }
        }
    }
};

class SoftwareColorMatrixEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<ColorMatrixParams>(&params);
        if (!p) throw std::runtime_error("ColorMatrix processor received invalid parameters");
        if (std::any_of(p->values.begin(), p->values.end(),
                        [](float value) { return !std::isfinite(value) ||
                                                 value < -16.0f || value > 16.0f; }))
            throw std::runtime_error("ColorMatrix coefficients must be finite and in [-16, 16]");
        ColorPipeline pipeline;
        pipeline.add_stage(ColorMatrixStage{*p});
        pipeline.apply(fb, context.clip);
    }
};

class SoftwarePosterizeEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<PosterizeParams>(&params);
        if (!p) throw std::runtime_error("Posterize processor received invalid parameters");
        p->validate();
        const float scale = static_cast<float>(p->levels - 1);
        const int x0 = context.clip ? std::clamp(context.clip->x0, 0, fb.width()) : 0;
        const int x1 = context.clip ? std::clamp(context.clip->x1, 0, fb.width()) : fb.width();
        const int y0 = context.clip ? std::clamp(context.clip->y0, 0, fb.height()) : 0;
        const int y1 = context.clip ? std::clamp(context.clip->y1, 0, fb.height()) : fb.height();
        for (int y = y0; y < y1; ++y) {
            Color* row = fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                const float alpha = row[x].a;
                if (alpha <= 0.0f) continue;
                auto straight = color::unpremultiply_rgb(row[x]);
                const auto quantize = [scale](float value) {
                    return std::floor(std::clamp(value, 0.0f, 1.0f) * scale + 0.5f) / scale;
                };
                straight.r = quantize(straight.r);
                straight.g = quantize(straight.g);
                straight.b = quantize(straight.b);
                row[x] = color::premultiply_rgb(straight, alpha);
            }
        }
    }
};

class SoftwareColorMapEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<ColorMapParams>(&params);
        if (!p) throw std::runtime_error("ColorMap processor received invalid parameters");
        p->validate();
        const int x0 = context.clip ? std::clamp(context.clip->x0, 0, fb.width()) : 0;
        const int x1 = context.clip ? std::clamp(context.clip->x1, 0, fb.width()) : fb.width();
        const int y0 = context.clip ? std::clamp(context.clip->y0, 0, fb.height()) : 0;
        const int y1 = context.clip ? std::clamp(context.clip->y1, 0, fb.height()) : fb.height();
        for (int y = y0; y < y1; ++y) {
            Color* row = fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                const float alpha = row[x].a;
                if (alpha <= 0.0f) continue;
                auto straight = color::unpremultiply_rgb(row[x]);
                const float luma =
                    0.2126f * straight.r + 0.7152f * straight.g + 0.0722f * straight.b;
                const float scaled = std::clamp(luma, 0.0f, 1.0f) * 4.0f;
                const int lower = std::min(3, static_cast<int>(scaled));
                const float t = scaled - static_cast<float>(lower);
                const std::size_t lo = static_cast<std::size_t>(lower) * 3;
                const std::size_t hi = static_cast<std::size_t>(lower + 1) * 3;
                const float mapped_r = p->stops[lo] +
                                       (p->stops[hi] - p->stops[lo]) * t;
                const float mapped_g = p->stops[lo + 1] +
                                       (p->stops[hi + 1] - p->stops[lo + 1]) * t;
                const float mapped_b = p->stops[lo + 2] +
                                       (p->stops[hi + 2] - p->stops[lo + 2]) * t;
                straight.r += (mapped_r - straight.r) * p->amount;
                straight.g += (mapped_g - straight.g) * p->amount;
                straight.b += (mapped_b - straight.b) * p->amount;
                row[x] = color::premultiply_rgb(straight, alpha);
            }
        }
    }
};

class SoftwareLinearWipeEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<LinearWipeParams>(&params);
        if (!p) throw std::runtime_error("LinearWipe processor received invalid parameters");
        p->validate();
        if (p->progress >= 1.0f) return;

        const float x_direction = std::cos(p->angle_degrees * std::numbers::pi_v<float> / 180.0f);
        const float y_direction = std::sin(p->angle_degrees * std::numbers::pi_v<float> / 180.0f);
        const float projection_min = std::min(0.0f, x_direction) + std::min(0.0f, y_direction);
        const float projection_max = std::max(0.0f, x_direction) + std::max(0.0f, y_direction);
        const float projection_span = std::max(projection_max - projection_min, 1.0e-6f);

        const int x0 = context.clip ? std::clamp(context.clip->x0, 0, fb.width()) : 0;
        const int x1 = context.clip ? std::clamp(context.clip->x1, 0, fb.width()) : fb.width();
        const int y0 = context.clip ? std::clamp(context.clip->y0, 0, fb.height()) : 0;
        const int y1 = context.clip ? std::clamp(context.clip->y1, 0, fb.height()) : fb.height();

        for (int y = y0; y < y1; ++y) {
            Color* row = fb.pixels_row(y);
            const float ny = (static_cast<float>(y) + 0.5f) / static_cast<float>(fb.height());
            for (int x = x0; x < x1; ++x) {
                float reveal = 0.0f;
                if (p->progress >= 1.0f) {
                    reveal = 1.0f;
                } else if (p->progress > 0.0f) {
                    const float nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(fb.width());
                    const float coordinate = ((nx * x_direction + ny * y_direction) - projection_min) /
                                             projection_span;
                    if (p->feather <= 0.0f) {
                        reveal = coordinate < p->progress ? 1.0f : 0.0f;
                    } else {
                        const float t = std::clamp(
                            (coordinate - (p->progress - p->feather * 0.5f)) / p->feather,
                            0.0f, 1.0f);
                        const float eased = t * t * (3.0f - 2.0f * t);
                        reveal = 1.0f - eased;
                    }
                }
                if (reveal <= 0.0f) {
                    row[x] = Color::transparent();
                } else if (reveal < 1.0f) {
                    row[x].r *= reveal;
                    row[x].g *= reveal;
                    row[x].b *= reveal;
                    row[x].a *= reveal;
                }
            }
        }
    }
};

class SoftwareMinMaxEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<MinMaxParams>(&params);
        if (!p) throw std::invalid_argument("MinMax processor received invalid parameters");
        p->validate();
        if (p->radius == 0 || fb.width() <= 0 || fb.height() <= 0) return;
        if (!context.effect_scratch) throw std::runtime_error("MinMax requires effect scratch");
        auto& scratch = *context.effect_scratch;
        scratch.ensure_size(fb.width(), fb.height());
        auto& horizontal = *scratch.framebuffer;
        auto& output = *scratch.framebuffer_b;
        const int radius = static_cast<int>(p->radius);
        const auto combine = [maximum = p->maximum](Color a, Color b) {
            const auto op = [maximum](float x, float y) { return maximum ? std::max(x, y) : std::min(x, y); };
            return Color{op(a.r,b.r), op(a.g,b.g), op(a.b,b.b), op(a.a,b.a)};
        };
        // Separable square morphology; always read the full source halo even
        // when only a clipped destination region is being refreshed.
        for (int y = 0; y < fb.height(); ++y) for (int x = 0; x < fb.width(); ++x) {
            Color value = fb.get_pixel(x, y);
            for (int dx = -radius; dx <= radius; ++dx)
                value = combine(value, x + dx < 0 || x + dx >= fb.width()
                    ? Color::transparent() : fb.get_pixel(x + dx, y));
            horizontal.set_pixel(x, y, value);
        }
        for (int y = 0; y < fb.height(); ++y) for (int x = 0; x < fb.width(); ++x) {
            Color value = horizontal.get_pixel(x, y);
            for (int dy = -radius; dy <= radius; ++dy)
                value = combine(value, y + dy < 0 || y + dy >= fb.height()
                    ? Color::transparent() : horizontal.get_pixel(x, y + dy));
            output.set_pixel(x, y, value);
        }
        const int x0 = context.clip ? std::clamp(context.clip->x0, 0, fb.width()) : 0;
        const int x1 = context.clip ? std::clamp(context.clip->x1, 0, fb.width()) : fb.width();
        const int y0 = context.clip ? std::clamp(context.clip->y0, 0, fb.height()) : 0;
        const int y1 = context.clip ? std::clamp(context.clip->y1, 0, fb.height()) : fb.height();
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x)
            fb.set_pixel(x, y, output.get_pixel(x, y));
    }
};

class SoftwareStretchWarpEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<StretchWarpParams>(&params);
        if (!p) throw std::invalid_argument("StretchWarp processor received invalid parameters");
        p->validate();
        if (p->stretch == 0.0f || fb.width() <= 0 || fb.height() <= 0) return;
        if (!context.effect_scratch) throw std::runtime_error("StretchWarp requires effect scratch");
        auto& scratch = *context.effect_scratch;
        scratch.ensure_size(fb.width(), fb.height());
        scratch.framebuffer->blit(fb, 0, 0);
        const sampling::Sampler2D sampler(*scratch.framebuffer, sampling::EdgeMode::Clamp);
        const float radians = p->angle_degrees * std::numbers::pi_v<float> / 180.0f;
        const float dx = std::cos(radians), dy = std::sin(radians);
        const float lo = std::min(0.0f, dx * fb.width()) + std::min(0.0f, dy * fb.height());
        const float hi = std::max(0.0f, dx * fb.width()) + std::max(0.0f, dy * fb.height());
        const float boundary = std::lerp(lo, hi, p->position);
        const int x0 = context.clip ? std::clamp(context.clip->x0, 0, fb.width()) : 0;
        const int x1 = context.clip ? std::clamp(context.clip->x1, 0, fb.width()) : fb.width();
        const int y0 = context.clip ? std::clamp(context.clip->y0, 0, fb.height()) : 0;
        const int y1 = context.clip ? std::clamp(context.clip->y1, 0, fb.height()) : fb.height();
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float distance = std::max(0.0f, px * dx + py * dy - boundary) * p->stretch;
            if (distance > 0.0f)
                fb.set_pixel(x, y, sampler.bilinear(px - dx * distance, py - dy * distance));
        }
    }
};

class SoftwareLensFlareEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<LensFlareParams>(&params);
        if (!p) throw std::invalid_argument("LensFlare processor received invalid parameters");
        p->validate();
        if (p->intensity == 0.0f || fb.width() <= 0 || fb.height() <= 0) return;
        if (!context.effect_scratch) throw std::runtime_error("LensFlare requires effect scratch");
        auto& scratch = *context.effect_scratch;
        scratch.ensure_size(fb.width(), fb.height());
        auto& mask = *scratch.framebuffer;
        for (int y = 0; y < fb.height(); ++y) for (int x = 0; x < fb.width(); ++x) {
            const Color c = fb.get_pixel(x, y);
            const float luma = c.a > 0.0f ? (0.2126f*c.r + 0.7152f*c.g + 0.0722f*c.b) / c.a : 0.0f;
            const float gain = luma > p->threshold
                ? std::clamp((luma - p->threshold) / std::max(1.0f - p->threshold, 1.0e-6f), 0.0f, 1.0f) : 0.0f;
            mask.set_pixel(x, y, Color{c.r*gain, c.g*gain, c.b*gain, c.a*gain});
        }
        const sampling::Sampler2D sampler(mask, sampling::EdgeMode::Transparent);
        const float cx = p->center.x * fb.width(), cy = p->center.y * fb.height();
        const float radius = p->radius * std::min(fb.width(), fb.height());
        const int x0 = context.clip ? std::clamp(context.clip->x0, 0, fb.width()) : 0;
        const int x1 = context.clip ? std::clamp(context.clip->x1, 0, fb.width()) : fb.width();
        const int y0 = context.clip ? std::clamp(context.clip->y0, 0, fb.height()) : 0;
        const int y1 = context.clip ? std::clamp(context.clip->y1, 0, fb.height()) : fb.height();
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            Color light = sampler.bilinear(2.0f*cx - px, 2.0f*cy - py) * p->ghost;
            for (int tap = 0; tap < 16; ++tap) {
                const float theta = 2.0f * std::numbers::pi_v<float> * tap / 16.0f;
                light = light + sampler.bilinear(px + radius*std::cos(theta), py + radius*std::sin(theta)) * (p->halo/16.0f);
                const float offset = radius * (2.0f*tap/15.0f - 1.0f);
                light = light + sampler.bilinear(px + offset, py) * (p->streak/16.0f);
            }
            light = light * p->intensity;
            if (light.a <= 0.0f) continue;
            // A bounded source-over light layer preserves premultiplication,
            // including artifacts generated over formerly transparent pixels.
            const float alpha = std::clamp(light.a, 0.0f, 1.0f);
            const float scale = alpha / light.a;
            const Color source = fb.get_pixel(x, y);
            fb.set_pixel(x, y, Color{light.r*scale + source.r*(1.0f-alpha),
                light.g*scale + source.g*(1.0f-alpha), light.b*scale + source.b*(1.0f-alpha),
                alpha + source.a*(1.0f-alpha)});
        }
    }
};

class SoftwareFillEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<FillParams>(&params)) {
            apply_fill(fb, p->color, p->amount,
                       p->mode, context.clip);
        }
    }
};

class SoftwareLut3DEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<Lut3DParams>(&params);
        if (!p) throw std::runtime_error("Lut3D processor received invalid parameters");
        const auto expected = static_cast<std::size_t>(p->dimension) * p->dimension * p->dimension;
        if (p->dimension < 2 || p->dimension > 64 || !p->values ||
            p->values->size() != expected || !std::isfinite(p->amount) ||
            p->amount < 0.0f || p->amount > 1.0f)
            throw std::runtime_error("Lut3D processor received invalid table dimensions or amount");
        for (std::size_t channel = 0; channel < 3; ++channel)
            if (!std::isfinite(p->domain_min[channel]) ||
                !std::isfinite(p->domain_max[channel]) ||
                p->domain_max[channel] <= p->domain_min[channel])
                throw std::runtime_error("Lut3D processor received an invalid domain range");
        ColorPipeline pipeline;
        pipeline.add_stage(*p);
        pipeline.apply(fb, context.clip);
    }
};

class SoftwareCurvesEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<CurvesParams>(&params)) {
            if (!context.curve_cache) {
                throw std::runtime_error("Curves processor requires a runtime-owned CurveCache");
            }
            ColorPipeline pipeline;
            CurvesStage stage;
            if (!p->master.empty())
                stage.master = context.curve_cache->get_or_compile(p->master);
            if (!p->red.empty())
                stage.red = context.curve_cache->get_or_compile(p->red);
            if (!p->green.empty())
                stage.green = context.curve_cache->get_or_compile(p->green);
            if (!p->blue.empty())
                stage.blue = context.curve_cache->get_or_compile(p->blue);
            pipeline.add_stage(stage);
            pipeline.apply(fb, context.clip);
        }
    }
};

class SoftwareStrokeEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<StrokeParams>(&params)) {
            apply_stroke(fb, p->color, p->width, p->softness, p->mode,
                         *context.effect_scratch, context.clip);
        }
    }
};

class SoftwareSharpenEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& ctx) override {
        const auto* p = std::get_if<SharpenParams>(&params);
        if (!p) throw std::runtime_error("Sharpen processor received invalid parameters");
        if (!std::isfinite(p->amount) || p->amount < -4.0f || p->amount > 4.0f ||
            !std::isfinite(p->radius) || p->radius <= 0.0f || p->radius > 64.0f ||
            !std::isfinite(p->threshold) || p->threshold < 0.0f || p->threshold > 1.0f)
            throw std::invalid_argument("sharpen parameters are outside their valid ranges");
        if (p->amount == 0.0f || fb.width() < 1 || fb.height() < 1) return;
        Framebuffer source(fb.width(), fb.height());
        source.blit(fb, 0, 0);
        renderer::apply_blur(source, p->radius, ctx.clip, 2);
        const int x0 = ctx.clip ? std::clamp(ctx.clip->x0, 0, fb.width()) : 0;
        const int y0 = ctx.clip ? std::clamp(ctx.clip->y0, 0, fb.height()) : 0;
        const int x1 = ctx.clip ? std::clamp(ctx.clip->x1, 0, fb.width()) : fb.width();
        const int y1 = ctx.clip ? std::clamp(ctx.clip->y1, 0, fb.height()) : fb.height();
        for (int y = y0; y < y1; ++y) {
            Color* dst = fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                const Color center = fb.pixels_row(y)[x];
                const Color blurred = source.pixels_row(y)[x];
                float dr = center.r-blurred.r, dg = center.g-blurred.g, db = center.b-blurred.b;
                if (std::sqrt(dr*dr + dg*dg + db*db) < p->threshold) dr = dg = db = 0.0f;
                dst[x] = Color{center.r+p->amount*dr, center.g+p->amount*dg,
                               center.b+p->amount*db, center.a};
            }
        }
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

std::unique_ptr<EffectProcessor> create_drop_shadow_effect_processor() {
    return std::make_unique<SoftwareDropShadowEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_bloom_effect_processor() {
    return std::make_unique<SoftwareBloomEffectProcessor>();
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

std::unique_ptr<EffectProcessor> create_vignette_effect_processor() {
    return std::make_unique<SoftwareVignetteEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_exposure_effect_processor() {
    return std::make_unique<SoftwareExposureEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_levels_effect_processor() {
    return std::make_unique<SoftwareLevelsEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_temperature_tint_effect_processor() {
    return std::make_unique<SoftwareTemperatureTintEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_lift_gamma_gain_effect_processor() {
    return std::make_unique<SoftwareLiftGammaGainEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_color_matrix_effect_processor() {
    return std::make_unique<SoftwareColorMatrixEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_posterize_effect_processor() {
    return std::make_unique<SoftwarePosterizeEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_color_map_effect_processor() {
    return std::make_unique<SoftwareColorMapEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_linear_wipe_effect_processor() {
    return std::make_unique<SoftwareLinearWipeEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_min_max_effect_processor() {
    return std::make_unique<SoftwareMinMaxEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_stretch_warp_effect_processor() {
    return std::make_unique<SoftwareStretchWarpEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_lens_flare_effect_processor() {
    return std::make_unique<SoftwareLensFlareEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_fill_effect_processor() {
    return std::make_unique<SoftwareFillEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_lut_3d_effect_processor() {
    return std::make_unique<SoftwareLut3DEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_sharpen_effect_processor() {
    return std::make_unique<SoftwareSharpenEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_curves_effect_processor() {
    return std::make_unique<SoftwareCurvesEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_stroke_effect_processor() {
    return std::make_unique<SoftwareStrokeEffectProcessor>();
}

} // namespace chronon3d::renderer
