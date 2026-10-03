#include "composite_backdrop_detail.hpp"

#include <chronon3d/compositor/blend_mode.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace chronon3d::graph::detail {

NodeExecResult execute_backdrop_software(
    RenderGraphContext& ctx, const Framebuffer& bottom, const Framebuffer& top,
    const BackdropBlurParams& settings, BlendMode mode) {
    if (ctx.policy.require_native_gpu)
        return NodeExecutionError{RenderBackendErrorCode::ExecutionFailure,
            "CompositeNode", "backdrop blur requires software composition; strict GPU-native mode rejects this layer"};
    if (!bottom.is_cpu_authoritative() || !top.is_cpu_authoritative() ||
        bottom.width() != ctx.frame_input.width || bottom.height() != ctx.frame_input.height ||
        bottom.origin_x() != 0 || bottom.origin_y() != 0)
        return NodeExecutionError{RenderBackendErrorCode::ExecutionFailure,
            "CompositeNode", "software backdrop blur requires CPU-resident full-canvas inputs"};

    const i32 width = bottom.width();
    const i32 height = bottom.height();
    const std::size_t count = static_cast<std::size_t>(width) * height;
    std::vector<Color> source(count), horizontal(count), blurred(count);
    for (i32 y = 0; y < height; ++y) {
        const auto* row = bottom.pixels_row(y);
        std::copy_n(row, width, source.data() + static_cast<std::size_t>(y) * width);
    }

    const int extent = static_cast<int>(std::ceil(settings.blur));
    if (extent > 0) {
        const float sigma = std::max(settings.blur / 3.0f, 0.5f);
        std::vector<float> kernel(static_cast<std::size_t>(extent * 2 + 1));
        float weight_sum = 0.0f;
        for (int k = -extent; k <= extent; ++k) {
            const float value = std::exp(-0.5f * (static_cast<float>(k) / sigma) *
                                         (static_cast<float>(k) / sigma));
            kernel[static_cast<std::size_t>(k + extent)] = value;
            weight_sum += value;
        }
        for (auto& value : kernel) value /= weight_sum;
        const auto sample = [&](const std::vector<Color>& input, i32 x, i32 y) {
            x = std::clamp(x, 0, width - 1);
            y = std::clamp(y, 0, height - 1);
            return input[static_cast<std::size_t>(y) * width + x];
        };
        for (i32 y = 0; y < height; ++y) for (i32 x = 0; x < width; ++x) {
            Color out{0, 0, 0, 0};
            for (int k = -extent; k <= extent; ++k) {
                const Color c = sample(source, x + k, y);
                const float w = kernel[static_cast<std::size_t>(k + extent)];
                out.r += c.r * w; out.g += c.g * w; out.b += c.b * w; out.a += c.a * w;
            }
            horizontal[static_cast<std::size_t>(y) * width + x] = out;
        }
        for (i32 y = 0; y < height; ++y) for (i32 x = 0; x < width; ++x) {
            Color out{0, 0, 0, 0};
            for (int k = -extent; k <= extent; ++k) {
                const Color c = sample(horizontal, x, y + k);
                const float w = kernel[static_cast<std::size_t>(k + extent)];
                out.r += c.r * w; out.g += c.g * w; out.b += c.b * w; out.a += c.a * w;
            }
            blurred[static_cast<std::size_t>(y) * width + x] = out;
        }
    } else {
        blurred = source;
    }

    const auto sample_bilinear = [width, height](const std::vector<Color>& pixels,
                                                  float sx, float sy) {
        sx = std::clamp(sx, 0.0f, static_cast<float>(width - 1));
        sy = std::clamp(sy, 0.0f, static_cast<float>(height - 1));
        const i32 x0 = static_cast<i32>(std::floor(sx));
        const i32 y0 = static_cast<i32>(std::floor(sy));
        const i32 x1 = std::min(x0 + 1, width - 1);
        const i32 y1 = std::min(y0 + 1, height - 1);
        const float tx = sx - static_cast<float>(x0);
        const float ty = sy - static_cast<float>(y0);
        const auto at = [&pixels, width](i32 x, i32 y) {
            return pixels[static_cast<std::size_t>(y) * width + x];
        };
        const Color a = at(x0, y0), b = at(x1, y0);
        const Color c = at(x0, y1), d = at(x1, y1);
        const auto mix = [tx, ty](float av, float bv, float cv, float dv) {
            return (av * (1.0f - tx) + bv * tx) * (1.0f - ty) +
                   (cv * (1.0f - tx) + dv * tx) * ty;
        };
        return Color{mix(a.r, b.r, c.r, d.r), mix(a.g, b.g, c.g, d.g),
                     mix(a.b, b.b, c.b, d.b), mix(a.a, b.a, c.a, d.a)};
    };
    float max_alpha = 0.0f;
    for (i32 y = 0; y < top.height(); ++y) {
        const auto* row = top.pixels_row(y);
        for (i32 x = 0; x < top.width(); ++x) max_alpha = std::max(max_alpha, row[x].a);
    }
    const auto mask_alpha = [&top, max_alpha](i32 x, i32 y) {
        if (max_alpha <= 0.0f || x < top.origin_x() || y < top.origin_y() ||
            x >= top.origin_x() + top.width() || y >= top.origin_y() + top.height())
            return 0.0f;
        return std::clamp(top.pixels_row(y - top.origin_y())[x - top.origin_x()].a /
                          max_alpha, 0.0f, 1.0f);
    };
    auto output = ctx.acquire_owned_fb(width, height, false);
    for (i32 y = 0; y < height; ++y) {
        Color* dst = output->pixels_row(y);
        const Color* top_row = y >= top.origin_y() && y < top.origin_y() + top.height()
            ? top.pixels_row(y - top.origin_y()) : nullptr;
        for (i32 x = 0; x < width; ++x) {
            Color backdrop = source[static_cast<std::size_t>(y) * width + x];
            float coverage = 0.0f;
            if (top_row && x >= top.origin_x() && x < top.origin_x() + top.width() && max_alpha > 0.0f)
                coverage = std::clamp(top_row[x - top.origin_x()].a / max_alpha, 0.0f, 1.0f);
            const float phase_x = static_cast<float>(x) * 0.043f;
            const float phase_y = static_cast<float>(y) * 0.047f;
            const float refract_x = settings.refraction *
                (0.62f * std::sin(phase_y + phase_x) +
                 0.38f * std::sin(phase_y * 1.7f - phase_x * 0.6f));
            const float refract_y = settings.refraction *
                (0.62f * std::cos(phase_x - phase_y) +
                 0.38f * std::sin(phase_x * 1.4f + phase_y * 0.8f));
            Color glass = settings.refraction > 0.0f
                ? sample_bilinear(blurred, static_cast<float>(x) + refract_x,
                                  static_cast<float>(y) + refract_y)
                : blurred[static_cast<std::size_t>(y) * width + x];
            if (glass.a > 1e-6f) {
                float r = glass.r / glass.a * settings.brightness;
                float g = glass.g / glass.a * settings.brightness;
                float b = glass.b / glass.a * settings.brightness;
                const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                r = luma + (r - luma) * settings.saturation;
                g = luma + (g - luma) * settings.saturation;
                b = luma + (b - luma) * settings.saturation;
                const float noise = settings.surface_noise *
                    std::sin(static_cast<float>(x) * 12.9898f +
                             static_cast<float>(y) * 78.233f) *
                    std::sin(static_cast<float>(x) * 4.1414f -
                             static_cast<float>(y) * 9.201f);
                r += noise; g += noise; b += noise;
                glass.r = r * glass.a; glass.g = g * glass.a; glass.b = b * glass.a;
            }
            backdrop.r = backdrop.r * (1.0f - coverage) + glass.r * coverage;
            backdrop.g = backdrop.g * (1.0f - coverage) + glass.g * coverage;
            backdrop.b = backdrop.b * (1.0f - coverage) + glass.b * coverage;
            backdrop.a = backdrop.a * (1.0f - coverage) + glass.a * coverage;
            if (coverage > 0.0f && settings.rim_light > 0.0f) {
                const float gx = mask_alpha(x + 1, y) - mask_alpha(x - 1, y);
                const float gy = mask_alpha(x, y + 1) - mask_alpha(x, y - 1);
                const float normal_length = std::sqrt(gx * gx + gy * gy);
                if (normal_length > 1e-5f) {
                    // The mask gradient points inward. Light arrives from the
                    // upper-left, producing a restrained white acrylic edge.
                    const float facing = std::max(0.0f,
                        ((-gx / normal_length) * -0.6f +
                         (-gy / normal_length) * -0.8f));
                    const float rim = std::min(1.0f,
                        settings.rim_light * facing * coverage);
                    backdrop.r += rim;
                    backdrop.g += rim;
                    backdrop.b += rim;
                }
            }
            if (top_row && x >= top.origin_x() && x < top.origin_x() + top.width())
                backdrop = compositor::blend(top_row[x - top.origin_x()], backdrop, mode);
            dst[x] = backdrop;
        }
    }
    output->set_origin(0, 0);
    if (ctx.node_exec.counters)
        ctx.node_exec.counters->full_frame_passes.fetch_add(1, std::memory_order_relaxed);
    return NodeExecResult{std::move(output)};
}

} // namespace chronon3d::graph::detail
