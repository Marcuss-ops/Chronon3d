// ---------------------------------------------------------------------------
// effect_stack.cpp — Effect stack dispatcher
//
// Orchestrates effect application by delegating to specialized implementations:
//   effect_glow_impl.cpp    — Glow (multi-pass blur + accumulation)
//   effect_shadow_impl.cpp  — DropShadow + node-level draw_shadow
//   effect_bloom_impl.cpp   — Bloom (bright-pass extract + blur + additive)
//   effect_blur.cpp         — Gaussian blur
//   effect_color.cpp        — Tint, Brightness, Contrast, Saturation, HueRotate, Invert, Vignette
//   effect_wave.cpp         — Fake3DWave
// ---------------------------------------------------------------------------

#include "../render_effects_processor.hpp"
#include "../../primitive_renderer.hpp"
#include "effects_internal.hpp"
#include "effect_helpers.hpp"
#include "../../processors/effects/color/exposure_levels.hpp"
#include "../../processors/effects/generate/fill_noise_offset.hpp"
#include "../../processors/effects/blur/directional_blur.hpp"
#include "../../processors/effects/blur/radial_blur.hpp"
#include "../../processors/effects/stroke/stroke.hpp"
#include <chronon3d/effects/curves.hpp>
#include <chronon3d/effects/color_pipeline.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace chronon3d {
namespace renderer {

// ── Forward declarations ─────────────────────────────────────────────────────

void apply_effect_stack(Framebuffer& fb, const EffectStack& stack,
                        const effects::EffectExecutionContext& context) {
    const auto& clip = context.clip;
    const float time_seconds = context.time_seconds;
    const bool diagnostics_enabled = context.diagnostics_enabled;
    using enum effects::EffectType;
    const auto stack_start = profiling::now();
    double blur_ms = 0.0;
    double tint_ms = 0.0;
    double brightness_ms = 0.0;
    double contrast_ms = 0.0;
    double glow_ms = 0.0;
    double shadow_ms = 0.0;
    double bloom_ms = 0.0;
    double fake3d_ms = 0.0;

    for (const auto& inst : stack) {
        if (!inst.enabled) continue;

        switch (inst.effect_type) {

        case Blur: {
            auto* p = std::get_if<BlurParams>(&inst.params);
            if (p && p->radius > 0.0f) {
                const auto t0 = diagnostics_enabled ? profiling::now() : profiling::Clock::time_point{};
                auto effect_clip = expand_effect_clip(clip, fb.width(), fb.height(), p->radius);
                apply_blur(fb, p->radius, effect_clip);
                if (diagnostics_enabled) {
                    blur_ms += profiling::duration_ms(t0, profiling::now());
                }
            }
            break;
        }

        case Tint: {
            auto* p = std::get_if<TintParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? profiling::now() : profiling::Clock::time_point{};
                LayerEffect e;
                e.tint = Color{p->color.r, p->color.g, p->color.b, p->color.a * p->amount};
                apply_color_effects(fb, e, clip);
                if (diagnostics_enabled) {
                    tint_ms += profiling::duration_ms(t0, profiling::now());
                }
            }
            break;
        }

        case Brightness: {
            auto* p = std::get_if<BrightnessParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? profiling::now() : profiling::Clock::time_point{};
                LayerEffect e; e.brightness = p->value;
                apply_color_effects(fb, e, clip);
                if (diagnostics_enabled) {
                    brightness_ms += profiling::duration_ms(t0, profiling::now());
                }
            }
            break;
        }

        case Contrast: {
            auto* p = std::get_if<ContrastParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? profiling::now() : profiling::Clock::time_point{};
                LayerEffect e; e.contrast = p->value;
                apply_color_effects(fb, e, clip);
                if (diagnostics_enabled) {
                    contrast_ms += profiling::duration_ms(t0, profiling::now());
                }
            }
            break;
        }

        case Glow: {
            auto* p = std::get_if<GlowParams>(&inst.params);
            if (p && p->intensity > 0.0f) {
                const auto t0 = diagnostics_enabled ? profiling::now() : profiling::Clock::time_point{};
                apply_glow_effect(fb, *p, clip);
                if (diagnostics_enabled) {
                    glow_ms += profiling::duration_ms(t0, profiling::now());
                }
            }
            break;
        }

        case DropShadow: {
            auto* p = std::get_if<DropShadowParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? profiling::now() : profiling::Clock::time_point{};
                apply_shadow_effect(fb, *p, clip, diagnostics_enabled);
                if (diagnostics_enabled) {
                    shadow_ms += profiling::duration_ms(t0, profiling::now());
                }
            }
            break;
        }

        case Bloom: {
            auto* p = std::get_if<BloomParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? profiling::now() : profiling::Clock::time_point{};
                apply_bloom_effect(fb, *p, clip, diagnostics_enabled);
                if (diagnostics_enabled) {
                    bloom_ms += profiling::duration_ms(t0, profiling::now());
                }
            }
            break;
        }

        case Fake3DWave: {
            auto* p = std::get_if<Fake3DWaveParams>(&inst.params);
            if (p) {
                const auto t0 = diagnostics_enabled ? profiling::now() : profiling::Clock::time_point{};
                apply_fake_3d_wave(fb, *p, time_seconds);
                if (diagnostics_enabled) {
                    fake3d_ms += profiling::duration_ms(t0, profiling::now());
                }
            }
            break;
        }

        case Saturation: {
            auto* p = std::get_if<SaturationParams>(&inst.params);
            if (p) apply_saturation(fb, p->value, clip);
            break;
        }

        case HueRotate: {
            auto* p = std::get_if<HueRotateParams>(&inst.params);
            if (p) apply_hue_rotate(fb, p->degrees, clip);
            break;
        }

        case Invert: {
            auto* p = std::get_if<InvertParams>(&inst.params);
            if (p) apply_invert(fb, p->amount, clip);
            break;
        }

        case Vignette: {
            auto* p = std::get_if<VignetteParams>(&inst.params);
            if (p) apply_vignette(fb, p->radius, p->softness, p->amount, p->color, clip);
            break;
        }

        case Exposure: {
            auto* p = std::get_if<ExposureParams>(&inst.params);
            if (p) apply_exposure(fb, p->stops, clip);
            break;
        }

        case Levels: {
            auto* p = std::get_if<LevelsParams>(&inst.params);
            if (p) {
                const auto& l = *p;
                apply_levels(fb,
                             l.master.input_black, l.master.input_white,
                             l.master.gamma,
                             l.master.output_black, l.master.output_white,
                             l.red.input_black, l.red.input_white,
                             l.red.gamma,
                             l.red.output_black, l.red.output_white,
                             l.green.input_black, l.green.input_white,
                             l.green.gamma,
                             l.green.output_black, l.green.output_white,
                             l.blue.input_black, l.blue.input_white,
                             l.blue.gamma,
                             l.blue.output_black, l.blue.output_white,
                             clip);
            }
            break;
        }

        case Fill: {
            auto* p = std::get_if<FillParams>(&inst.params);
            if (p) apply_fill(fb, p->color, p->amount,
                              p->mode == FillMode::Replace, clip);
            break;
        }

        case Noise: {
            auto* p = std::get_if<NoiseParams>(&inst.params);
            if (p) apply_noise(fb, p->amount, p->seed,
                               p->animated, p->color_mode == NoiseColorMode::RGB,
                               static_cast<uint32_t>(0), clip);
            break;
        }

        case Offset: {
            auto* p = std::get_if<OffsetParams>(&inst.params);
            if (p) apply_offset(fb, p->offset.x, p->offset.y,
                                p->edge_mode, p->filter, clip);
            break;
        }

        case DirectionalBlur: {
            auto* p = std::get_if<DirectionalBlurParams>(&inst.params);
            if (p && p->length > 0.0f) {
                auto effect_clip = expand_effect_clip(clip, fb.width(), fb.height(),
                    std::max(static_cast<float>(directional_blur_margins(p->angle, p->length).first),
                             static_cast<float>(directional_blur_margins(p->angle, p->length).second)));
                apply_directional_blur(fb, p->angle, p->length, p->samples, effect_clip);
            }
            break;
        }

        case RadialBlur: {
            auto* p = std::get_if<RadialBlurParams>(&inst.params);
            if (p && p->amount > 0.0f) {
                // Radial blur affects the entire frame — no clip expansion needed
                apply_radial_blur(fb, p->center.x, p->center.y,
                                  p->amount, p->render_samples);
            }
            break;
        }

        case Stroke: {
            auto* p = std::get_if<StrokeParams>(&inst.params);
            if (p && p->width > 0.0f) {
                auto margins = stroke_margins(p->width, p->softness, p->mode);
                auto effect_clip = expand_effect_clip(clip, fb.width(), fb.height(),
                    static_cast<float>(std::max(margins.first, margins.second)));
                apply_stroke(fb, p->color, p->width, p->softness, p->mode, effect_clip);
            }
            break;
        }

        case Curves: {
            auto* p = std::get_if<CurvesParams>(&inst.params);
            if (p) {
                // Use global cache to avoid re-compiling identical curves
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
                pipeline.apply(fb, clip);
            }
            break;
        }

        case Unknown:
            break;
        }
    }

    if (diagnostics_enabled) {
        const double total_ms = profiling::duration_ms(stack_start, profiling::now());
        spdlog::info(
            "[EffectStack] total={:.2f}ms blur={:.2f} tint={:.2f} brightness={:.2f} contrast={:.2f} glow={:.2f} shadow={:.2f} bloom={:.2f} fake3d={:.2f}",
            total_ms, blur_ms, tint_ms, brightness_ms, contrast_ms, glow_ms, shadow_ms, bloom_ms, fake3d_ms
        );
    }

    // ── Per-effect wall-time counters (always emitted, not just diagnostics) ──
    {
        const double total_ms = profiling::duration_ms(stack_start, profiling::now());
        if (auto* cnt = profiling::g_current_counters) {
            cnt->effect_stack_total_ms.fetch_add(
                static_cast<uint64_t>(std::llround(total_ms)), std::memory_order_relaxed);
        }
    }
}

// ── Node-level glow primitive ────────────────────────────────────────────────

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

