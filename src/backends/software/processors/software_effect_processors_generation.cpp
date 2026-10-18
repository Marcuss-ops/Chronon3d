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
#include <chronon3d/effects/keying_matte.hpp>
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
        if ((p->mode != KeyingMode::Chroma && p->mode != KeyingMode::Luma) ||
            !std::isfinite(p->threshold) || !std::isfinite(p->softness) ||
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
                const auto [keep, garbage] = effects::keying_components(
                    Color{r, g, b, source_alpha}, x, y, fb.width(), fb.height(), *p);
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
        const auto valid_transform = [](const RgbChannelTransform& channel) {
            return std::isfinite(channel.translation.x) && std::isfinite(channel.translation.y) &&
                std::abs(channel.translation.x) <= 2048.0f && std::abs(channel.translation.y) <= 2048.0f &&
                std::isfinite(channel.scale.x) && std::isfinite(channel.scale.y) &&
                channel.scale.x >= 0.01f && channel.scale.x <= 100.0f &&
                channel.scale.y >= 0.01f && channel.scale.y <= 100.0f &&
                std::isfinite(channel.rotation_degrees) && std::abs(channel.rotation_degrees) <= 3600.0f &&
                std::isfinite(channel.opacity) && channel.opacity >= 0.0f && channel.opacity <= 1.0f;
        };
        if (!valid_transform(p->red) || !valid_transform(p->green) || !valid_transform(p->blue))
            throw std::runtime_error("RGB channel transforms are outside supported bounds");
        const auto identity = [](const RgbChannelTransform& channel) {
            return channel.translation.x == 0.0f && channel.translation.y == 0.0f &&
                channel.scale.x == 1.0f && channel.scale.y == 1.0f &&
                channel.rotation_degrees == 0.0f && channel.opacity == 1.0f;
        };
        const bool legacy_alpha = identity(p->red) && identity(p->green) && identity(p->blue);
        const auto sample_bilinear = [&source, w, h](float x, float y) {
            x = std::clamp(x, 0.0f, static_cast<float>(w - 1));
            y = std::clamp(y, 0.0f, static_cast<float>(h - 1));
            const int x0 = static_cast<int>(std::floor(x));
            const int y0 = static_cast<int>(std::floor(y));
            const int x1 = std::min(x0 + 1, w - 1);
            const int y1 = std::min(y0 + 1, h - 1);
            const float tx = x - static_cast<float>(x0);
            const float ty = y - static_cast<float>(y0);
            const Color a = source.get_pixel(x0, y0), b = source.get_pixel(x1, y0);
            const Color c = source.get_pixel(x0, y1), d = source.get_pixel(x1, y1);
            const auto mix = [tx, ty](float av, float bv, float cv, float dv) {
                return (av * (1.0f - tx) + bv * tx) * (1.0f - ty) +
                       (cv * (1.0f - tx) + dv * tx) * ty;
            };
            return Color{mix(a.r,b.r,c.r,d.r), mix(a.g,b.g,c.g,d.g),
                         mix(a.b,b.b,c.b,d.b), mix(a.a,b.a,c.a,d.a)};
        };
        const auto source_position = [center_px](const RgbChannelTransform& channel,
                                                float x, float y, float extra_x, float extra_y) {
            const float radians = channel.rotation_degrees * 0.01745329251994329577f;
            const float cs = std::cos(radians), sn = std::sin(radians);
            const float dx = x - center_px.x - channel.translation.x;
            const float dy = y - center_px.y - channel.translation.y;
            return Vec2{center_px.x + (cs * dx + sn * dy) / channel.scale.x + extra_x,
                        center_px.y + (-sn * dx + cs * dy) / channel.scale.y + extra_y};
        };

        Vec2 vel_dir{1.0f, 0.0f};
        float vel_len = glm::length(p->velocity);
        if (vel_len > 1.0e-5f) {
            vel_dir = p->velocity / vel_len;
        }

        for (int y = y0; y < y1; ++y) {
            Color* dst = fb.pixels_row(y);
            for (int x = x0; x < x1; ++x) {
                const Color center = source.get_pixel(x, y);
                float red_dx = 0.0f, red_dy = 0.0f, blue_dx = 0.0f, blue_dy = 0.0f;

                if (p->offset_mode == ChromaticOffsetMode::Constant) {
                    red_dx = static_cast<float>(std::lround(p->red_offset));
                    blue_dx = static_cast<float>(std::lround(p->blue_offset));
                } else if (p->offset_mode == ChromaticOffsetMode::Velocity) {
                    red_dx = std::round(vel_dir.x * p->red_offset);
                    red_dy = std::round(vel_dir.y * p->red_offset);
                    blue_dx = std::round(vel_dir.x * p->blue_offset);
                    blue_dy = std::round(vel_dir.y * p->blue_offset);
                } else if (p->offset_mode == ChromaticOffsetMode::Radial) {
                    const Vec2 diff = Vec2{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f} - center_px;
                    const float dist = glm::length(diff);
                    const Vec2 r_dir = dist > 1.0e-5f ? (diff / dist) : Vec2{0.0f, 0.0f};
                    red_dx = std::round(r_dir.x * p->red_offset);
                    red_dy = std::round(r_dir.y * p->red_offset);
                    blue_dx = std::round(r_dir.x * p->blue_offset);
                    blue_dy = std::round(r_dir.y * p->blue_offset);
                }

                const Vec2 rp = source_position(p->red, static_cast<float>(x), static_cast<float>(y), red_dx, red_dy);
                const Vec2 gp = source_position(p->green, static_cast<float>(x), static_cast<float>(y), 0.0f, 0.0f);
                const Vec2 bp = source_position(p->blue, static_cast<float>(x), static_cast<float>(y), blue_dx, blue_dy);
                const Color r = sample_bilinear(rp.x, rp.y), g = sample_bilinear(gp.x, gp.y),
                            b = sample_bilinear(bp.x, bp.y);
                if (legacy_alpha) {
                    const float red_channel = center.a > 1.0e-8f && r.a > 1.0e-8f
                        ? (r.r / r.a) * center.a : 0.0f;
                    const float blue_channel = center.a > 1.0e-8f && b.a > 1.0e-8f
                        ? (b.b / b.a) * center.a : 0.0f;
                    const float green_channel = center.a > 1.0e-8f && g.a > 1.0e-8f
                        ? (g.g / g.a) * center.a : 0.0f;
                    dst[x] = {red_channel, green_channel, blue_channel, center.a};
                } else {
                    const float ar = r.a * p->red.opacity;
                    const float ag = g.a * p->green.opacity;
                    const float ab = b.a * p->blue.opacity;
                    dst[x] = {r.a > 1.0e-8f ? r.r / r.a * ar : 0.0f,
                              g.a > 1.0e-8f ? g.g / g.a * ag : 0.0f,
                              b.a > 1.0e-8f ? b.b / b.a * ab : 0.0f,
                              std::max({ar, ag, ab})};
                }
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
