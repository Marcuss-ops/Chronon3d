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
        if (!std::isfinite(p->clip_black) || !std::isfinite(p->clip_white) ||
            p->clip_black < 0.0f || p->clip_white > 1.0f || p->clip_white <= p->clip_black ||
            !std::isfinite(p->choke) || p->choke < 0.0f || p->choke >= 1.0f ||
            !std::isfinite(p->garbage_x) || !std::isfinite(p->garbage_y) ||
            !std::isfinite(p->garbage_w) || !std::isfinite(p->garbage_h) ||
            !std::isfinite(p->garbage_feather) ||
            p->garbage_x < 0.0f || p->garbage_x > 1.0f ||
            p->garbage_y < 0.0f || p->garbage_y > 1.0f ||
            p->garbage_w < 0.0f || p->garbage_w > 1.0f ||
            p->garbage_h < 0.0f || p->garbage_h > 1.0f ||
            p->garbage_feather < 0.0f || p->garbage_feather > 0.5f)
            throw std::runtime_error("Keying parameters are outside supported bounds");
        const bool garbage_enabled = p->garbage_w > 0.0f && p->garbage_h > 0.0f;
        const float inv_width = 1.0f / static_cast<float>(std::max(fb.width(), 1));
        const float inv_height = 1.0f / static_cast<float>(std::max(fb.height(), 1));

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
                // Screen-matte refinement: remap through [clip_black, clip_white]
                // then erode the matte edge by `choke` before it reaches alpha.
                keep = std::clamp(
                    (keep - p->clip_black) / std::max(p->clip_white - p->clip_black, 1.0e-6f),
                    0.0f, 1.0f);
                if (p->choke > 0.0f)
                    keep = std::clamp((keep - p->choke) / (1.0f - p->choke), 0.0f, 1.0f);
                float garbage = 1.0f;
                if (garbage_enabled) {
                    const float u = (static_cast<float>(x) + 0.5f) * inv_width;
                    const float v = (static_cast<float>(y) + 0.5f) * inv_height;
                    const float dx = std::min(u - p->garbage_x, p->garbage_x + p->garbage_w - u);
                    const float dy = std::min(v - p->garbage_y, p->garbage_y + p->garbage_h - v);
                    const float d = std::min(dx, dy);
                    if (p->garbage_feather > 0.0f)
                        garbage = std::clamp(d / p->garbage_feather, 0.0f, 1.0f);
                    else
                        garbage = d >= 0.0f ? 1.0f : 0.0f;
                }
                const float gray = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                const float spill = p->spill * (1.0f - keep);
                r += (gray - r) * spill;
                g += (gray - g) * spill;
                b += (gray - b) * spill;
                const float alpha = source_alpha * keep * garbage;
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

class SoftwareChromaticAberrationEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<ChromaticAberrationParams>(&params);
        if (!p) throw std::runtime_error("ChromaticAberration processor received invalid parameters");
        if (!std::isfinite(p->red_offset) || !std::isfinite(p->blue_offset) ||
            std::abs(p->red_offset) > 128.0f || std::abs(p->blue_offset) > 128.0f)
            throw std::runtime_error("ChromaticAberration offsets must be finite and within [-128, 128]");
        const Framebuffer source = fb;
        int x0 = 0, y0 = 0, x1 = fb.width(), y1 = fb.height();
        if (context.clip) {
            x0 = std::clamp(context.clip->x0, 0, x1);
            y0 = std::clamp(context.clip->y0, 0, y1);
            x1 = std::clamp(context.clip->x1, 0, x1);
            y1 = std::clamp(context.clip->y1, 0, y1);
        }
        const int w = fb.width();
        const int h = fb.height();
        const Vec2 center_px{p->center.x * static_cast<float>(w), p->center.y * static_cast<float>(h)};

        Vec2 vel_dir{1.0f, 0.0f};
        float vel_len = glm::length(p->velocity);
        if (vel_len > 1.0e-5f) {
            vel_dir = p->velocity / vel_len;
        }

        for (int y = y0; y < y1; ++y) {
            Color* dst = fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                const Color center = source.get_pixel(x, y);
                int rx = x, ry = y, bx = x, by = y;

                if (p->offset_mode == ChromaticOffsetMode::Constant) {
                    const int red = static_cast<int>(std::lround(p->red_offset));
                    const int blue = static_cast<int>(std::lround(p->blue_offset));
                    rx = std::clamp(x + red, 0, w - 1);
                    bx = std::clamp(x + blue, 0, w - 1);
                } else if (p->offset_mode == ChromaticOffsetMode::Velocity) {
                    const int r_dx = static_cast<int>(std::lround(vel_dir.x * p->red_offset));
                    const int r_dy = static_cast<int>(std::lround(vel_dir.y * p->red_offset));
                    const int b_dx = static_cast<int>(std::lround(vel_dir.x * p->blue_offset));
                    const int b_dy = static_cast<int>(std::lround(vel_dir.y * p->blue_offset));
                    rx = std::clamp(x + r_dx, 0, w - 1);
                    ry = std::clamp(y + r_dy, 0, h - 1);
                    bx = std::clamp(x + b_dx, 0, w - 1);
                    by = std::clamp(y + b_dy, 0, h - 1);
                } else if (p->offset_mode == ChromaticOffsetMode::Radial) {
                    const Vec2 diff = Vec2{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f} - center_px;
                    const float dist = glm::length(diff);
                    const Vec2 r_dir = dist > 1.0e-5f ? (diff / dist) : Vec2{0.0f, 0.0f};
                    const int r_dx = static_cast<int>(std::lround(r_dir.x * p->red_offset));
                    const int r_dy = static_cast<int>(std::lround(r_dir.y * p->red_offset));
                    const int b_dx = static_cast<int>(std::lround(r_dir.x * p->blue_offset));
                    const int b_dy = static_cast<int>(std::lround(r_dir.y * p->blue_offset));
                    rx = std::clamp(x + r_dx, 0, w - 1);
                    ry = std::clamp(y + r_dy, 0, h - 1);
                    bx = std::clamp(x + b_dx, 0, w - 1);
                    by = std::clamp(y + b_dy, 0, h - 1);
                }

                const Color r = source.get_pixel(rx, ry);
                const Color b = source.get_pixel(bx, by);
                const float red_channel = center.a > 1.0e-8f && r.a > 1.0e-8f
                    ? (r.r / r.a) * center.a : 0.0f;
                const float blue_channel = center.a > 1.0e-8f && b.a > 1.0e-8f
                    ? (b.b / b.a) * center.a : 0.0f;
                dst[x] = {red_channel, center.g, blue_channel, center.a};
            }
        }
    }
};

class SoftwareHalftoneEffectProcessor final : public EffectProcessor {
public:
    void apply(Framebuffer& fb, const EffectParams& params,
               const effects::EffectExecutionContext& context) override {
        const auto* p = std::get_if<HalftoneParams>(&params);
        if (!p) throw std::runtime_error("Halftone processor received invalid parameters");
        if (p->size == 0 || p->size > 128 || !std::isfinite(p->angle_degrees) ||
            !std::isfinite(p->amount) || p->amount < 0.0f || p->amount > 1.0f)
            throw std::runtime_error("Halftone requires size in [1, 128], finite angle, and amount in [0, 1]");
        const Framebuffer source = fb;
        int x0 = 0, y0 = 0, x1 = fb.width(), y1 = fb.height();
        if (context.clip) {
            x0 = std::clamp(context.clip->x0, 0, x1);
            y0 = std::clamp(context.clip->y0, 0, y1);
            x1 = std::clamp(context.clip->x1, 0, x1);
            y1 = std::clamp(context.clip->y1, 0, y1);
        }
        const float cell = static_cast<float>(p->size);
        const float angle = p->angle_degrees * 0.01745329251994329577f;
        const float cs = std::cos(angle), sn = std::sin(angle);
        const auto luma_at = [&](int x, int y) {
            const Color c = source.get_pixel(std::clamp(x, 0, fb.width() - 1),
                                             std::clamp(y, 0, fb.height() - 1));
            if (c.a <= 1.0e-8f) return 1.0f;
            return std::clamp((0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) / c.a,
                              0.0f, 1.0f);
        };
        for (int y = y0; y < y1; ++y) {
            Color* dst = fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                const float rx = cs * static_cast<float>(x) + sn * static_cast<float>(y);
                const float ry = -sn * static_cast<float>(x) + cs * static_cast<float>(y);
                const float cx = (std::floor(rx / cell) + 0.5f) * cell;
                const float cy = (std::floor(ry / cell) + 0.5f) * cell;
                const int sample_x = static_cast<int>(std::lround(cs * cx - sn * cy));
                const int sample_y = static_cast<int>(std::lround(sn * cx + cs * cy));
                const float luma = luma_at(sample_x, sample_y);
                const float radius = std::sqrt(1.0f - luma) * cell * 0.5f * p->amount;
                const float dx = rx - cx, dy = ry - cy;
                const Color original = source.get_pixel(x, y);
                const float ink = dx * dx + dy * dy < radius * radius ? 0.0f : 1.0f;
                dst[x] = {original.a * ink, original.a * ink, original.a * ink, original.a};
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

std::unique_ptr<EffectProcessor> create_noise_effect_processor() {
    return std::make_unique<SoftwareNoiseEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_fractal_noise_effect_processor() {
    return std::make_unique<SoftwareFractalNoiseEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_checkerboard_effect_processor() {
    return std::make_unique<SoftwareCheckerboardEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_four_color_gradient_effect_processor() {
    return std::make_unique<SoftwareFourColorGradientEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_echo_effect_processor() {
    return std::make_unique<SoftwareEchoEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_keying_effect_processor() {
    return std::make_unique<SoftwareKeyingEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_threshold_effect_processor() {
    return std::make_unique<SoftwareThresholdEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_mosaic_effect_processor() {
    return std::make_unique<SoftwareMosaicEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_chromatic_aberration_effect_processor() {
    return std::make_unique<SoftwareChromaticAberrationEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_halftone_effect_processor() {
    return std::make_unique<SoftwareHalftoneEffectProcessor>();
};

std::unique_ptr<EffectProcessor> create_light_rays_effect_processor() {
    return std::make_unique<SoftwareLightRaysEffectProcessor>();
}

} // namespace chronon3d::renderer
