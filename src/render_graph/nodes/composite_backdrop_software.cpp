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
    const BackdropBlurParams& settings, BlendMode mode, bool gel_transmission) {
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

    // Compute `top`'s active bounding box and max alpha before allocating
    // scratch framebuffers so empty layers skip blur altogether and non-empty
    // layers only blur the covered ROI (plus refraction margin).
    float max_alpha = 0.0f;
    i32 active_x0 = width, active_y0 = height, active_x1 = 0, active_y1 = 0;
    for (i32 y = 0; y < top.height(); ++y) {
        const i32 cy = top.origin_y() + y;
        const auto* row = top.pixels_row(y);
        for (i32 x = 0; x < top.width(); ++x) {
            const float a = row[x].a;
            if (a > 0.0f) {
                max_alpha = std::max(max_alpha, a);
                const i32 cx = top.origin_x() + x;
                if (cx >= 0 && cx < width && cy >= 0 && cy < height) {
                    active_x0 = std::min(active_x0, cx);
                    active_y0 = std::min(active_y0, cy);
                    active_x1 = std::max(active_x1, cx + 1);
                    active_y1 = std::max(active_y1, cy + 1);
                }
            }
        }
    }

    const int extent = static_cast<int>(std::ceil(settings.blur));
    const bool has_active_roi = max_alpha > 0.0f && active_x0 < active_x1 && active_y0 < active_y1;
    const bool blurred_is_bottom = extent <= 0 || !has_active_roi;
    OwnedFB horizontal_fb;
    OwnedFB blurred_fb;
    if (!blurred_is_bottom) {
        horizontal_fb = ctx.acquire_owned_fb(width, height, false);
        blurred_fb = ctx.acquire_owned_fb(width, height, false);
    }
    const auto backdrop_at = [&](i32 x, i32 y) -> Color {
        return bottom.pixels_row(y)[x];
    };
    const auto blurred_at = [&](i32 x, i32 y) -> Color {
        if (blurred_is_bottom) return bottom.pixels_row(y)[x];
        return blurred_fb->pixels_row(y)[x];
    };

    if (!blurred_is_bottom) {
        const i32 refract_margin = settings.refraction > 0.0f
            ? static_cast<i32>(std::ceil(settings.refraction)) + 1 : 0;
        const i32 roi_x0 = std::max(0, active_x0 - refract_margin);
        const i32 roi_y0 = std::max(0, active_y0 - refract_margin);
        const i32 roi_x1 = std::min(width, active_x1 + refract_margin);
        const i32 roi_y1 = std::min(height, active_y1 + refract_margin);
        const i32 horiz_y0 = std::max(0, roi_y0 - extent);
        const i32 horiz_y1 = std::min(height, roi_y1 + extent);

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
        for (i32 y = horiz_y0; y < horiz_y1; ++y) {
            const Color* src_row = bottom.pixels_row(y);
            Color* dst_row = horizontal_fb->pixels_row(y);
            for (i32 x = roi_x0; x < roi_x1; ++x) {
                Color out{0, 0, 0, 0};
                for (int k = -extent; k <= extent; ++k) {
                    const Color c = src_row[std::clamp(x + k, 0, width - 1)];
                    const float w = kernel[static_cast<std::size_t>(k + extent)];
                    out.r += c.r * w; out.g += c.g * w; out.b += c.b * w; out.a += c.a * w;
                }
                dst_row[x] = out;
            }
        }
        for (i32 y = roi_y0; y < roi_y1; ++y) {
            Color* dst_row = blurred_fb->pixels_row(y);
            for (i32 x = roi_x0; x < roi_x1; ++x) {
                Color out{0, 0, 0, 0};
                for (int k = -extent; k <= extent; ++k) {
                    const Color c = horizontal_fb->pixels_row(std::clamp(y + k, 0, height - 1))[x];
                    const float w = kernel[static_cast<std::size_t>(k + extent)];
                    out.r += c.r * w; out.g += c.g * w; out.b += c.b * w; out.a += c.a * w;
                }
                dst_row[x] = out;
            }
        }
    }

    const auto sample_bilinear = [width, height, &blurred_at](float sx, float sy) {
        sx = std::clamp(sx, 0.0f, static_cast<float>(width - 1));
        sy = std::clamp(sy, 0.0f, static_cast<float>(height - 1));
        const i32 x0 = static_cast<i32>(std::floor(sx));
        const i32 y0 = static_cast<i32>(std::floor(sy));
        const i32 x1 = std::min(x0 + 1, width - 1);
        const i32 y1 = std::min(y0 + 1, height - 1);
        const float tx = sx - static_cast<float>(x0);
        const float ty = sy - static_cast<float>(y0);
        const auto at = [&blurred_at](i32 x, i32 y) {
            return blurred_at(x, y);
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
            Color backdrop = backdrop_at(x, y);
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
                ? sample_bilinear(static_cast<float>(x) + refract_x,
                                  static_cast<float>(y) + refract_y)
                : blurred_at(x, y);
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
            // Light wrap: bleed the backdrop's blurred luminance over the
            // layer's inner alpha edge. The edge mask peaks where the layer's
            // own alpha is above its local neighbourhood average, i.e. just
            // inside the contour, so the glow never spills outside the layer.
            if (settings.light_wrap > 0.0f && coverage > 0.0f) {
                float neighbour_sum = 0.0f;
                int neighbour_count = 0;
                for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
                    if (ox == 0 && oy == 0) continue;
                    const int nx = x + ox, ny = y + oy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                    if (nx < top.origin_x() || ny < top.origin_y() ||
                        nx >= top.origin_x() + top.width() ||
                        ny >= top.origin_y() + top.height()) continue;
                    neighbour_sum += std::clamp(
                        top.pixels_row(ny - top.origin_y())[nx - top.origin_x()].a /
                            max_alpha, 0.0f, 1.0f);
                    ++neighbour_count;
                }
                const float average = neighbour_count > 0
                    ? neighbour_sum / static_cast<float>(neighbour_count) : coverage;
                const float inner_edge = std::clamp((coverage - average) * 2.0f, 0.0f, 1.0f);
                if (inner_edge > 0.0f) {
                    const Color wrap_source = blurred_at(x, y);
                    const float wrap_luma =
                        0.2126f * wrap_source.r + 0.7152f * wrap_source.g +
                        0.0722f * wrap_source.b;
                    const float wrap = wrap_luma * inner_edge * settings.light_wrap;
                    backdrop.r += wrap;
                    backdrop.g += wrap;
                    backdrop.b += wrap;
                }
            }
            if (top_row && x >= top.origin_x() && x < top.origin_x() + top.width()) {
                const Color& gel = top_row[x - top.origin_x()];
                if (gel_transmission && mode == BlendMode::Normal) {
                    const float alpha = std::clamp(gel.a, 0.0f, 1.0f);
                    const float da = std::clamp(backdrop.a, 0.0f, 1.0f);
                    const float inv_da = da > 1.0e-6f ? 1.0f / da : 0.0f;
                    const float bg_luma = 0.2126f * backdrop.r * inv_da +
                                          0.7152f * backdrop.g * inv_da +
                                          0.0722f * backdrop.b * inv_da;
                    const float t = std::clamp((bg_luma - 0.45f) / 0.45f, 0.0f, 1.0f);
                    const float backlit = t * t * (3.0f - 2.0f * t);
                    // Backlit gel becomes more transmissive and its surface
                    // washes toward white; alpha=0 still leaves the backdrop
                    // untouched, so no rectangle can leak outside the glyph.
                    const float effective_alpha =
                        alpha * (1.0f - 0.45f * backlit);
                    const float red_transmission =
                        std::max(1.0f - effective_alpha, 1e-6f);
                    const float wash = 0.85f * backlit;
                    const float surface_r = gel.r * (1.0f - wash) +
                                            effective_alpha * wash;
                    const float surface_g = gel.g * (1.0f - wash) +
                                            effective_alpha * wash;
                    const float surface_b = gel.b * (1.0f - wash) +
                                            effective_alpha * wash;
                    const float body_lift =
                        0.12f * backlit * effective_alpha;
                    backdrop = Color{
                        surface_r + backdrop.r * red_transmission + body_lift,
                        surface_g + backdrop.g * std::pow(red_transmission, 1.0f / 3.0f) + body_lift,
                        surface_b + backdrop.b * std::pow(red_transmission, 1.0f / 24.0f) + body_lift,
                        effective_alpha + da * (1.0f - effective_alpha)};
                } else {
                    backdrop = compositor::blend(gel, backdrop, mode);
                }
            }
            dst[x] = backdrop;
        }
    }
    output->set_origin(0, 0);
    if (ctx.node_exec.counters)
        ctx.node_exec.counters->full_frame_passes.fetch_add(1, std::memory_order_relaxed);
    return NodeExecResult{std::move(output)};
}

} // namespace chronon3d::graph::detail
