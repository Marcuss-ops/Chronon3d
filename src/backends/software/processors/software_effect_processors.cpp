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
#include <chronon3d/effects/curves.hpp>
#include <chronon3d/effects/color_pipeline.hpp>
#include <chronon3d/effects/glow_pipeline.hpp>
#include <algorithm>
#include <cstdint>
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
        if (p->intensity > 0.0f) {
            run_glow_pipeline(fb, GlowPipeline::from(*p), context.clip,
                              /*source_is_alpha_mask=*/false, context.debug_cfg);
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
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<NoiseParams>(&params)) {
            apply_noise(fb, p->amount, p->seed,
                        p->animated, p->color_mode == NoiseColorMode::RGB,
                        static_cast<uint32_t>(std::max<std::int64_t>(
                            context.frame.integral(), 0)), context.clip, p->size,
                        p->shadow_response, p->midtone_response,
                        p->highlight_response);
        }
    }
};

class SoftwareFractalNoiseEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<FractalNoiseParams>(&params);
        if (!p) throw std::runtime_error("FractalNoise processor received invalid parameters");
        apply_fractal_noise(fb, *p, context.time_seconds, context.clip);
    }
};

class SoftwareCheckerboardEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<CheckerboardParams>(&params);
        if (!p) throw std::runtime_error("Checkerboard processor received invalid parameters");
        apply_checkerboard(fb, *p, context.clip);
    }
};

class SoftwareFourColorGradientEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<FourColorGradientParams>(&params);
        if (!p) throw std::runtime_error("FourColorGradient processor received invalid parameters");
        apply_four_color_gradient(fb, *p, context.clip);
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

class SoftwareEchoEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<EchoParams>(&params);
        if (!p) throw std::runtime_error("Echo processor received invalid parameters");
        if (p->echoes == 0 || p->echoes > 8 || !std::isfinite(p->decay) ||
            p->decay < 0.0f || p->decay > 1.0f ||
            p->time_offset_frames == 0 || p->time_offset_frames > 120)
            throw std::runtime_error("Echo parameters are outside supported bounds");
        if (context.temporal_history_key.empty()) return;
        if (!context.effect_scratch)
            throw std::runtime_error("Echo processor requires EffectScratchResources");

        auto& scratch = *context.effect_scratch;
        const auto current_frame = context.frame.integral();
        const auto history = scratch.echo_frames(context.temporal_history_key,
            current_frame, p->echoes, p->time_offset_frames);
        const auto keep_frames = static_cast<std::size_t>(p->echoes) *
            p->time_offset_frames + 1U;
        scratch.record_echo_frame(context.temporal_history_key, current_frame,
                                  fb, keep_frames);
        const Framebuffer current = fb;

        int x0 = 0, y0 = 0, x1 = fb.width(), y1 = fb.height();
        if (context.clip) {
            x0 = std::clamp(context.clip->x0, 0, x1);
            y0 = std::clamp(context.clip->y0, 0, y1);
            x1 = std::clamp(context.clip->x1, 0, x1);
            y1 = std::clamp(context.clip->y1, 0, y1);
        }
        for (int y = y0; y < y1; ++y) {
            Color* dst = fb.pixels_row(y);
            const Color* src = current.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                Color trail = Color::transparent();
                // Composite older, dimmer samples first so nearer echoes sit
                // in front of the long tail while the current source remains
                // the topmost image.
                for (std::uint32_t i = p->echoes; i > 0; --i) {
                    const auto& sample = history[i - 1];
                    if (!sample || sample->width() != fb.width() ||
                        sample->height() != fb.height()) continue;
                    Color echo = sample->pixels_row(y)[x];
                    const float weight = std::pow(p->decay, static_cast<float>(i));
                    echo.r *= weight; echo.g *= weight; echo.b *= weight; echo.a *= weight;
                    const float remain = 1.0f - std::clamp(echo.a, 0.0f, 1.0f);
                    const Color behind = trail;
                    trail = {echo.r + behind.r * remain,
                             echo.g + behind.g * remain,
                             echo.b + behind.b * remain,
                             echo.a + behind.a * remain};
                }
                const Color foreground = src[x];
                const float remain = 1.0f - std::clamp(foreground.a, 0.0f, 1.0f);
                dst[x] = {foreground.r + trail.r * remain,
                          foreground.g + trail.g * remain,
                          foreground.b + trail.b * remain,
                          foreground.a + trail.a * remain};
            }
        }
    }
};

class SoftwareKeyingEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<KeyingParams>(&params);
        if (!p) throw std::runtime_error("Keying processor received invalid parameters");
        if (!std::isfinite(p->threshold) || !std::isfinite(p->softness) ||
            !std::isfinite(p->spill) || p->threshold < 0.0f || p->threshold > 1.0f ||
            p->softness < 0.0f || p->softness > 1.0f || p->spill < 0.0f || p->spill > 1.0f ||
            !std::isfinite(p->key_color.r) || !std::isfinite(p->key_color.g) ||
            !std::isfinite(p->key_color.b) || p->key_color.r < 0.0f || p->key_color.r > 1.0f ||
            p->key_color.g < 0.0f || p->key_color.g > 1.0f ||
            p->key_color.b < 0.0f || p->key_color.b > 1.0f)
            throw std::runtime_error("Keying parameters are outside supported bounds");

        int x0 = 0, y0 = 0, x1 = fb.width(), y1 = fb.height();
        if (context.clip) {
            x0 = std::clamp(context.clip->x0, 0, x1);
            y0 = std::clamp(context.clip->y0, 0, y1);
            x1 = std::clamp(context.clip->x1, 0, x1);
            y1 = std::clamp(context.clip->y1, 0, y1);
        }
        for (int y = y0; y < y1; ++y) {
            Color* row = fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                auto& pixel = row[x];
                const float source_alpha = std::clamp(pixel.a, 0.0f, 1.0f);
                if (source_alpha <= 1.0e-8f) continue;
                float r = pixel.r / source_alpha;
                float g = pixel.g / source_alpha;
                float b = pixel.b / source_alpha;
                float measure = 0.0f;
                if (p->mode == KeyingMode::Chroma) {
                    const float dr = r - p->key_color.r;
                    const float dg = g - p->key_color.g;
                    const float db = b - p->key_color.b;
                    measure = std::sqrt(dr * dr + dg * dg + db * db) * 0.57735026919f;
                } else if (p->mode == KeyingMode::Luma) {
                    measure = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                } else {
                    throw std::runtime_error("Keying mode is unsupported");
                }
                float keep = 0.0f;
                if (p->softness <= 0.0f) keep = measure >= p->threshold ? 1.0f : 0.0f;
                else {
                    const float t = std::clamp((measure - p->threshold) / p->softness, 0.0f, 1.0f);
                    keep = t * t * (3.0f - 2.0f * t);
                }
                const float gray = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                const float spill = p->spill * (1.0f - keep);
                r += (gray - r) * spill;
                g += (gray - g) * spill;
                b += (gray - b) * spill;
                const float alpha = source_alpha * keep;
                pixel = {r * alpha, g * alpha, b * alpha, alpha};
            }
        }
    }
};

class SoftwareThresholdEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<ThresholdParams>(&params);
        if (!p) throw std::runtime_error("Threshold processor received invalid parameters");
        if (!std::isfinite(p->threshold) || !std::isfinite(p->softness) ||
            p->threshold < 0.0f || p->threshold > 1.0f ||
            p->softness < 0.0f || p->softness > 1.0f)
            throw std::runtime_error("Threshold parameters must be in [0, 1]");
        int x0 = 0, y0 = 0, x1 = fb.width(), y1 = fb.height();
        if (context.clip) {
            x0 = std::clamp(context.clip->x0, 0, x1);
            y0 = std::clamp(context.clip->y0, 0, y1);
            x1 = std::clamp(context.clip->x1, 0, x1);
            y1 = std::clamp(context.clip->y1, 0, y1);
        }
        for (int y = y0; y < y1; ++y) {
            Color* row = fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                auto& pixel = row[x];
                const float alpha = std::clamp(pixel.a, 0.0f, 1.0f);
                if (alpha <= 1.0e-8f) { pixel = Color::transparent(); continue; }
                const float luma = (0.2126f * pixel.r + 0.7152f * pixel.g + 0.0722f * pixel.b) / alpha;
                float value = 0.0f;
                if (p->softness <= 0.0f) value = luma >= p->threshold ? 1.0f : 0.0f;
                else {
                    const float t = std::clamp((luma - p->threshold) / p->softness, 0.0f, 1.0f);
                    value = t * t * (3.0f - 2.0f * t);
                }
                pixel = {value * alpha, value * alpha, value * alpha, alpha};
            }
        }
    }
};

class SoftwareMosaicEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<MosaicParams>(&params);
        if (!p) throw std::runtime_error("Mosaic processor received invalid parameters");
        if (p->block_size == 0 || p->block_size > 512)
            throw std::runtime_error("Mosaic block_size must be in [1, 512]");
        const Framebuffer source = fb;
        const int block = static_cast<int>(p->block_size);
        int x0 = 0, y0 = 0, x1 = fb.width(), y1 = fb.height();
        if (context.clip) {
            x0 = std::clamp(context.clip->x0, 0, x1);
            y0 = std::clamp(context.clip->y0, 0, y1);
            x1 = std::clamp(context.clip->x1, 0, x1);
            y1 = std::clamp(context.clip->y1, 0, y1);
        }
        const auto block_center = [block](int coordinate) {
            const int cell = static_cast<int>(std::floor(static_cast<double>(coordinate) / block));
            return cell * block + block / 2;
        };
        for (int y = y0; y < y1; ++y) {
            Color* dst = fb.pixels_row(y);
            const int global_y = fb.origin_y() + y;
            const int sample_global_y = block_center(global_y);
            const int sample_y = std::clamp(sample_global_y - fb.origin_y(), 0, fb.height() - 1);
            for (int x = x0; x < x1; ++x) {
                const int global_x = fb.origin_x() + x;
                const int sample_global_x = block_center(global_x);
                const int sample_x = std::clamp(sample_global_x - fb.origin_x(), 0, fb.width() - 1);
                dst[x] = source.get_pixel(sample_x, sample_y);
            }
        }
    }
};

class SoftwareLightRaysEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<LightRaysParams>(&params);
        if (!p) throw std::runtime_error("LightRays processor received invalid parameters");
        if (!context.effect_scratch)
            throw std::runtime_error("LightRays processor requires EffectScratchResources");
        apply_light_rays(fb, *p, *context.effect_scratch, context.clip);
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
                         *context.effect_scratch);
        }
    }
};

// ── Radial Blur ────────────────────────────────────────────────────────────

class SoftwareRadialBlurEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<RadialBlurParams>(&params)) {
            apply_radial_blur(fb, p->center.x, p->center.y,
                              p->amount, p->render_samples,
                              *context.effect_scratch);
        }
    }
};

// ── Directional Blur ─────────────────────────────────────────────────────────

class SoftwareDirectionalBlurEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<DirectionalBlurParams>(&params)) {
            apply_directional_blur(fb, p->angle, p->length,
                                   *context.effect_scratch, p->samples);
        }
    }
};

// ── Offset ───────────────────────────────────────────────────────────────────

class SoftwareOffsetEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        if (auto* p = std::get_if<OffsetParams>(&params)) {
            apply_offset(fb, p->offset.x, p->offset.y,
                         p->edge_mode, p->filter,
                         *context.effect_scratch, context.clip);
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

std::unique_ptr<EffectProcessor> create_fractal_noise_effect_processor() {
    return std::make_unique<SoftwareFractalNoiseEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_checkerboard_effect_processor() {
    return std::make_unique<SoftwareCheckerboardEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_four_color_gradient_effect_processor() {
    return std::make_unique<SoftwareFourColorGradientEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_lut_3d_effect_processor() {
    return std::make_unique<SoftwareLut3DEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_echo_effect_processor() {
    return std::make_unique<SoftwareEchoEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_keying_effect_processor() {
    return std::make_unique<SoftwareKeyingEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_threshold_effect_processor() {
    return std::make_unique<SoftwareThresholdEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_mosaic_effect_processor() {
    return std::make_unique<SoftwareMosaicEffectProcessor>();
}

std::unique_ptr<EffectProcessor> create_light_rays_effect_processor() {
    return std::make_unique<SoftwareLightRaysEffectProcessor>();
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
