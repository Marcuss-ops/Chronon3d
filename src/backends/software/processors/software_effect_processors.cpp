#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/effect_processor.hpp>
#include "../utils/render_effects_processor.hpp"
#include "effects/color/exposure_levels.hpp"
#include "effects/generate/fill_noise_offset.hpp"
#include "effects/blur/directional_blur.hpp"
#include "effects/blur/radial_blur.hpp"
#include "effects/stroke/stroke.hpp"
#include <chronon3d/effects/curves.hpp>
#include <chronon3d/effects/color_pipeline.hpp>

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
               const effects::EffectExecutionContext& /*context*/) override {
        if (auto* p = std::get_if<CurvesParams>(&params)) {
            // Use the global curve cache to share with dispatch and other processors
            ColorPipeline pipeline;
            CurvesStage stage;
            if (!p->master.empty())
                stage.master = global_curve_cache().get_or_compile(p->master);
            if (!p->red.empty())
                stage.red = global_curve_cache().get_or_compile(p->red);
            if (!p->green.empty())
                stage.green = global_curve_cache().get_or_compile(p->green);
            if (!p->blue.empty())
                stage.blue = global_curve_cache().get_or_compile(p->blue);
            pipeline.add_stage(stage);
            pipeline.apply(fb);
        }
    }
};

// ── Stroke ─────────────────────────────────────────────────────────────────

class SoftwareStrokeEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& /*context*/) override {
        if (auto* p = std::get_if<StrokeParams>(&params)) {
            apply_stroke(fb, p->color, p->width, p->softness, p->mode);
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
               const effects::EffectExecutionContext& /*context*/) override {
        if (auto* p = std::get_if<DirectionalBlurParams>(&params)) {
            apply_directional_blur(fb, p->angle, p->length, p->samples);
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
