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
#include <chronon3d/effects/glow_pipeline.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
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

class SoftwareFillEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<FillParams>(&params)) {
            apply_fill(fb, p->color, p->amount,
                       p->mode == FillMode::Replace, context.clip);
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
