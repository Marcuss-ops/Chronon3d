#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/effect_processor.hpp>
#include "../effects/render_effects_processor.hpp"
#include "effects/color/exposure_levels.hpp"
#include "effects/generate/fill_noise_offset.hpp"
#include "effects/blur/directional_blur.hpp"
#include "effects/blur/radial_blur.hpp"
#include "effects/stroke/stroke.hpp"
#include <chronon3d/effects/curves.hpp>
#include <chronon3d/effects/color_pipeline.hpp>
#include <stdexcept>

namespace chronon3d::renderer {

// ── Blur ─────────────────────────────────────────────────────────────────────

class SoftwareBlurEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& /*context*/) override {
        if (auto* p = std::get_if<BlurParams>(&params)) {
            renderer::apply_blur(fb, p->radius);
        }
    }
};

// ── Tint ─────────────────────────────────────────────────────────────────────

class SoftwareTintEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& /*context*/) override {
        if (auto* p = std::get_if<TintParams>(&params)) {
            LayerEffect e;
            e.tint = Color{p->color.r, p->color.g, p->color.b, p->color.a * p->amount};
            apply_color_effects(fb, e);
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
        if (p->intensity > 0.0f)
            apply_glow_effect(fb, *p, context.clip, context.debug_cfg);
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
        apply_bloom_effect(fb, *p, context.clip, context.diagnostics_enabled);
    }
};

// ── Fake3DWave ───────────────────────────────────────────────────────────────

class SoftwareFake3DWaveEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<Fake3DWaveParams>(&params)) {
            apply_fake_3d_wave(fb, *p, context.time_seconds);
        }
    }
};

// ── Exposure ─────────────────────────────────────────────────────────────────

class SoftwareExposureEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& /*context*/) override {
        if (auto* p = std::get_if<ExposureParams>(&params)) {
            apply_exposure(fb, p->stops);
        }
    }
};

// ── Levels ───────────────────────────────────────────────────────────────────

class SoftwareLevelsEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& /*context*/) override {
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
                         p->blue.output_black, p->blue.output_white);
        }
    }
};

// ── Fill ─────────────────────────────────────────────────────────────────────

class SoftwareFillEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& /*context*/) override {
        if (auto* p = std::get_if<FillParams>(&params)) {
            apply_fill(fb, p->color, p->amount,
                       p->mode == FillMode::Replace);
        }
    }
};

// ── Noise ────────────────────────────────────────────────────────────────────

class SoftwareNoiseEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& /*context*/) override {
        if (auto* p = std::get_if<NoiseParams>(&params)) {
            apply_noise(fb, p->amount, p->seed,
                        p->animated, p->color_mode == NoiseColorMode::RGB,
                        static_cast<uint32_t>(0));
        }
    }
};

// ── Curves ────────────────────────────────────────────────────────────────

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
            pipeline.apply(fb);
        }
    }
};

// ── Stroke ─────────────────────────────────────────────────────────────────

class SoftwareStrokeEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<StrokeParams>(&params)) {
            apply_stroke(fb, p->color, p->width, p->softness, p->mode,
                         std::nullopt, context.effect_scratch);
        }
    }
};

// ── Radial Blur ────────────────────────────────────────────────────────────

class SoftwareRadialBlurEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& /*context*/) override {
        if (auto* p = std::get_if<RadialBlurParams>(&params)) {
            apply_radial_blur(fb, p->center.x, p->center.y,
                              p->amount, p->render_samples);
        }
    }
};

// ── Directional Blur ─────────────────────────────────────────────────────────

class SoftwareDirectionalBlurEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<DirectionalBlurParams>(&params)) {
            apply_directional_blur(fb, p->angle, p->length, p->samples,
                                   std::nullopt, context.effect_scratch);
        }
    }
};

// ── Offset ───────────────────────────────────────────────────────────────────

class SoftwareOffsetEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& /*context*/) override {
        if (auto* p = std::get_if<OffsetParams>(&params)) {
            apply_offset(fb, p->offset.x, p->offset.y,
                         p->edge_mode, p->filter);
        }
    }
};

// ── Factory functions ────────────────────────────────────────────────────────

std::unique_ptr<EffectProcessor> create_blur_effect_processor() {
    return std::make_unique<SoftwareBlurEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_tint_effect_processor() {
    return std::make_unique<SoftwareTintEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_brightness_effect_processor() {
    return std::make_unique<SoftwareBrightnessEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_contrast_effect_processor() {
    return std::make_unique<SoftwareContrastEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_glow_effect_processor() {
    return std::make_unique<SoftwareGlowEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_drop_shadow_effect_processor() {
    return std::make_unique<SoftwareDropShadowEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_bloom_effect_processor() {
    return std::make_unique<SoftwareBloomEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_saturation_effect_processor() {
    return std::make_unique<SoftwareSaturationEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_hue_rotate_effect_processor() {
    return std::make_unique<SoftwareHueRotateEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_invert_effect_processor() {
    return std::make_unique<SoftwareInvertEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_vignette_effect_processor() {
    return std::make_unique<SoftwareVignetteEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_fake_3d_wave_effect_processor() {
    return std::make_unique<SoftwareFake3DWaveEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_exposure_effect_processor() {
    return std::make_unique<SoftwareExposureEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_levels_effect_processor() {
    return std::make_unique<SoftwareLevelsEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_fill_effect_processor() {
    return std::make_unique<SoftwareFillEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_noise_effect_processor() {
    return std::make_unique<SoftwareNoiseEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_offset_effect_processor() {
    return std::make_unique<SoftwareOffsetEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_curves_effect_processor() {
    return std::make_unique<SoftwareCurvesEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_stroke_effect_processor() {
    return std::make_unique<SoftwareStrokeEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_radial_blur_effect_processor() {
    return std::make_unique<SoftwareRadialBlurEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_directional_blur_effect_processor() {
    return std::make_unique<SoftwareDirectionalBlurEffectProcessor>();
}

} // namespace chronon3d::renderer
