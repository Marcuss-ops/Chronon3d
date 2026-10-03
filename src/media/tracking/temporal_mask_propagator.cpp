#include <chronon3d/media/tracking/temporal_mask_propagator.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace chronon3d::media::tracking {
namespace {

struct GrayLevel {
    int width{0};
    int height{0};
    std::vector<float> pixels{};
    std::vector<float> coverage{};
};

struct FlowVector {
    float x{0.0f};
    float y{0.0f};
    float confidence{0.0f};
    bool valid{false};
};

struct FlowField {
    int width{0};
    int height{0};
    std::vector<FlowVector> vectors{};
};

using Pyramid = std::vector<GrayLevel>;

std::vector<float> box_weighted_mean(std::span<const float> input,
                                     std::span<const float> weights,
                                     std::uint32_t width,
                                     std::uint32_t height,
                                     std::uint32_t radius) {
    const std::size_t count = static_cast<std::size_t>(width) * height;
    std::vector<float> horizontal_value(count), horizontal_weight(count), output(count);
    std::vector<double> prefix_value(static_cast<std::size_t>(width) + 1);
    std::vector<double> prefix_weight(static_cast<std::size_t>(width) + 1);
    for (std::uint32_t y = 0; y < height; ++y) {
        prefix_value[0] = 0.0;
        prefix_weight[0] = 0.0;
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto index = static_cast<std::size_t>(y) * width + x;
            const double weight = std::clamp(static_cast<double>(weights[index]), 0.0, 1.0);
            prefix_value[x + 1] = prefix_value[x] + input[index] * weight;
            prefix_weight[x + 1] = prefix_weight[x] + weight;
        }
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto left = x > radius ? x - radius : 0;
            const auto right = std::min<std::uint64_t>(
                width, static_cast<std::uint64_t>(x) + radius + 1);
            const auto index = static_cast<std::size_t>(y) * width + x;
            horizontal_value[index] = static_cast<float>(
                prefix_value[right] - prefix_value[left]);
            horizontal_weight[index] = static_cast<float>(
                prefix_weight[right] - prefix_weight[left]);
        }
    }
    for (std::uint32_t x = 0; x < width; ++x) {
        double sum_value = 0.0;
        double sum_weight = 0.0;
        std::uint32_t bottom = 0;
        std::uint32_t top = 0;
        for (std::uint32_t y = 0; y < height; ++y) {
            const auto wanted_bottom = std::min<std::uint64_t>(
                height, static_cast<std::uint64_t>(y) + radius + 1);
            while (bottom < wanted_bottom) {
                const auto index = static_cast<std::size_t>(bottom) * width + x;
                sum_value += horizontal_value[index];
                sum_weight += horizontal_weight[index];
                ++bottom;
            }
            const auto wanted_top = y > radius ? y - radius : 0;
            while (top < wanted_top) {
                const auto index = static_cast<std::size_t>(top) * width + x;
                sum_value -= horizontal_value[index];
                sum_weight -= horizontal_weight[index];
                ++top;
            }
            output[static_cast<std::size_t>(y) * width + x] = sum_weight > 1.0e-12
                ? static_cast<float>(sum_value / sum_weight) : 0.0f;
        }
    }
    return output;
}

std::size_t checked_storage_size(std::uint32_t width, std::uint32_t height,
                                 std::uint32_t stride, std::string_view name) {
    if (width == 0 || height == 0)
        throw std::invalid_argument(std::string(name) + " dimensions must be positive");
    if (stride == 0) stride = width;
    if (stride < width || width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument(std::string(name) + " has invalid dimensions or row stride");
    const auto required = static_cast<std::uint64_t>(stride) * height;
    if (required > std::numeric_limits<std::size_t>::max())
        throw std::length_error(std::string(name) + " storage size overflows address space");
    return static_cast<std::size_t>(required);
}

void validate_options(const TemporalFlowOptions& options) {
    if (options.pyramid_levels == 0 || options.pyramid_levels > 12 ||
        options.window_radius < 1 || options.window_radius > 12 ||
        options.iterations_per_level == 0 || options.iterations_per_level > 20 ||
        !std::isfinite(options.minimum_eigenvalue) || options.minimum_eigenvalue <= 0.0f ||
        !std::isfinite(options.minimum_confidence) || options.minimum_confidence < 0.0f ||
        options.minimum_confidence > 1.0f ||
        !std::isfinite(options.forward_backward_tolerance) ||
        options.forward_backward_tolerance <= 0.0f ||
        !std::isfinite(options.maximum_motion_per_frame) ||
        options.maximum_motion_per_frame <= 0.0f ||
        !std::isfinite(options.photometric_scale) ||
        options.photometric_scale <= 0.0f || options.maximum_work_units == 0)
        throw std::invalid_argument("invalid temporal flow options");
}

std::uint64_t estimate_work(std::uint32_t width, std::uint32_t height,
                            const TemporalFlowOptions& options) {
    std::uint64_t level_pixels = 0;
    std::uint32_t w = width, h = height;
    for (std::uint32_t level = 0; level < options.pyramid_levels; ++level) {
        level_pixels += static_cast<std::uint64_t>(w) * h;
        if (w < 16 || h < 16) break;
        w = (w + 1) / 2;
        h = (h + 1) / 2;
    }
    const std::uint64_t side = 2ull * options.window_radius + 1ull;
    const std::uint64_t factor = side * side * options.iterations_per_level * 2ull;
    if (level_pixels > std::numeric_limits<std::uint64_t>::max() / factor)
        return std::numeric_limits<std::uint64_t>::max();
    return level_pixels * factor;
}

Pyramid build_pyramid(TrackingFrameView frame, std::uint32_t stride,
                      std::uint32_t max_levels) {
    GrayLevel base;
    base.width = static_cast<int>(frame.width);
    base.height = static_cast<int>(frame.height);
    base.pixels.resize(static_cast<std::size_t>(base.width) * base.height);
    base.coverage.resize(base.pixels.size());
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            Color pixel = frame.pixels[static_cast<std::size_t>(y) * stride + x];
            const float coverage = std::isfinite(pixel.a)
                ? std::clamp(pixel.a, 0.0f, 1.0f) : 0.0f;
            if (frame.premultiplied_alpha) pixel = pixel.unpremultiplied();
            const float luma = 0.2126f * pixel.r + 0.7152f * pixel.g + 0.0722f * pixel.b;
            base.pixels[static_cast<std::size_t>(y) * base.width + x] =
                coverage > 1.0e-6f && std::isfinite(luma) ? luma : 0.0f;
            base.coverage[static_cast<std::size_t>(y) * base.width + x] = coverage;
        }
    }

    Pyramid pyramid;
    pyramid.push_back(std::move(base));
    while (pyramid.size() < max_levels) {
        const GrayLevel& source = pyramid.back();
        if (source.width < 16 || source.height < 16) break;
        GrayLevel down;
        down.width = (source.width + 1) / 2;
        down.height = (source.height + 1) / 2;
        down.pixels.resize(static_cast<std::size_t>(down.width) * down.height);
        down.coverage.resize(down.pixels.size());
        for (int y = 0; y < down.height; ++y) {
            for (int x = 0; x < down.width; ++x) {
                float weighted_luma_sum = 0.0f;
                float coverage_sum = 0.0f;
                int count = 0;
                for (int oy = 0; oy < 2; ++oy) {
                    for (int ox = 0; ox < 2; ++ox) {
                        const int sx = x * 2 + ox;
                        const int sy = y * 2 + oy;
                        if (sx >= source.width || sy >= source.height) continue;
                        const auto source_index =
                            static_cast<std::size_t>(sy) * source.width + sx;
                        const float coverage = std::clamp(
                            source.coverage[source_index], 0.0f, 1.0f);
                        weighted_luma_sum += source.pixels[source_index] * coverage;
                        coverage_sum += coverage;
                        ++count;
                    }
                }
                down.pixels[static_cast<std::size_t>(y) * down.width + x] =
                    coverage_sum > 1.0e-6f
                        ? weighted_luma_sum / coverage_sum : 0.0f;
                down.coverage[static_cast<std::size_t>(y) * down.width + x] =
                    coverage_sum / static_cast<float>(count);
            }
        }
        pyramid.push_back(std::move(down));
    }
    return pyramid;
}

bool sample_bilinear(const GrayLevel& image, float x, float y, float& value) noexcept {
    if (x < 0.0f || y < 0.0f ||
        x > static_cast<float>(image.width - 1) ||
        y > static_cast<float>(image.height - 1))
        return false;
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, image.width - 1);
    const int y1 = std::min(y0 + 1, image.height - 1);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const auto at = [&](int sx, int sy) {
        return image.pixels[static_cast<std::size_t>(sy) * image.width + sx];
    };
    const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
    const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
    value = top + (bottom - top) * ty;
    return std::isfinite(value);
}

float sample_coverage(const GrayLevel& image, float x, float y) noexcept {
    if (x < 0.0f || y < 0.0f ||
        x > static_cast<float>(image.width - 1) ||
        y > static_cast<float>(image.height - 1)) return 0.0f;
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, image.width - 1);
    const int y1 = std::min(y0 + 1, image.height - 1);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const auto at = [&](int sx, int sy) {
        return image.coverage[static_cast<std::size_t>(sy) * image.width + sx];
    };
    const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
    const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
    return std::clamp(top + (bottom - top) * ty, 0.0f, 1.0f);
}

FlowVector sample_flow(const FlowField& flow, float x, float y) noexcept {
    if (x < 0.0f || y < 0.0f ||
        x > static_cast<float>(flow.width - 1) ||
        y > static_cast<float>(flow.height - 1))
        return {};
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, flow.width - 1);
    const int y1 = std::min(y0 + 1, flow.height - 1);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const auto at = [&](int sx, int sy) -> const FlowVector& {
        return flow.vectors[static_cast<std::size_t>(sy) * flow.width + sx];
    };
    const FlowVector& p00 = at(x0, y0);
    const FlowVector& p10 = at(x1, y0);
    const FlowVector& p01 = at(x0, y1);
    const FlowVector& p11 = at(x1, y1);
    if (!p00.valid || !p10.valid || !p01.valid || !p11.valid) return {};
    const auto interpolate = [&](float FlowVector::*member) {
        const float top = p00.*member + (p10.*member - p00.*member) * tx;
        const float bottom = p01.*member + (p11.*member - p01.*member) * tx;
        return top + (bottom - top) * ty;
    };
    return {interpolate(&FlowVector::x), interpolate(&FlowVector::y),
            interpolate(&FlowVector::confidence), true};
}

FlowField estimate_one_way(const Pyramid& source, const Pyramid& target,
                           const TemporalFlowOptions& options) {
    FlowField flow;
    for (std::size_t level_index = source.size(); level_index-- > 0;) {
        const GrayLevel& a = source[level_index];
        const GrayLevel& b = target[level_index];
        FlowField current;
        current.width = a.width;
        current.height = a.height;
        current.vectors.resize(static_cast<std::size_t>(a.width) * a.height);
        if (!flow.vectors.empty()) {
            for (int y = 0; y < a.height; ++y) {
                for (int x = 0; x < a.width; ++x) {
                    const auto coarse = sample_flow(flow,
                        static_cast<float>(x) * 0.5f, static_cast<float>(y) * 0.5f);
                    current.vectors[static_cast<std::size_t>(y) * a.width + x] =
                        FlowVector{coarse.x * 2.0f, coarse.y * 2.0f, coarse.confidence, true};
                }
            }
        } else {
            for (auto& vector : current.vectors) vector.valid = true;
        }

        const int radius = static_cast<int>(options.window_radius);
        for (std::uint32_t iteration = 0; iteration < options.iterations_per_level; ++iteration) {
            FlowField next = current;
            for (int y = 0; y < a.height; ++y) {
                for (int x = 0; x < a.width; ++x) {
                    const auto index = static_cast<std::size_t>(y) * a.width + x;
                    const FlowVector old = current.vectors[index];
                    float gxx = 0.0f, gxy = 0.0f, gyy = 0.0f;
                    float gxt = 0.0f, gyt = 0.0f, residual2 = 0.0f;
                    std::uint32_t samples = 0;
                    float weighted_samples = 0.0f;
                    for (int wy = -radius; wy <= radius; ++wy) {
                        for (int wx = -radius; wx <= radius; ++wx) {
                            const int sx = x + wx, sy = y + wy;
                            // Keep the LK window clipped to available image
                            // support. Rejecting every vector within `radius`
                            // of a frame edge created a fixed-width dead band
                            // where masks were erased on every propagated frame.
                            if (sx <= 0 || sy <= 0 || sx + 1 >= a.width ||
                                sy + 1 >= a.height)
                                continue;
                            const auto source_index = static_cast<std::size_t>(sy) * a.width + sx;
                            const float source_coverage = std::min({
                                a.coverage[source_index],
                                a.coverage[static_cast<std::size_t>(sy) * a.width + sx - 1],
                                a.coverage[static_cast<std::size_t>(sy) * a.width + sx + 1],
                                a.coverage[static_cast<std::size_t>(sy - 1) * a.width + sx],
                                a.coverage[static_cast<std::size_t>(sy + 1) * a.width + sx]});
                            if (source_coverage <= 0.01f) continue;
                            const auto source_at = [&](int px, int py) {
                                return a.pixels[static_cast<std::size_t>(py) * a.width + px];
                            };
                            const float gx = 0.5f * (source_at(sx + 1, sy) - source_at(sx - 1, sy));
                            const float gy = 0.5f * (source_at(sx, sy + 1) - source_at(sx, sy - 1));
                            float warped = 0.0f;
                            if (!sample_bilinear(b, static_cast<float>(sx) + old.x,
                                                 static_cast<float>(sy) + old.y, warped)) continue;
                            const float target_coverage = sample_coverage(
                                b, static_cast<float>(sx) + old.x,
                                static_cast<float>(sy) + old.y);
                            const float weight = std::min(source_coverage, target_coverage);
                            if (weight <= 0.01f) continue;
                            const float temporal = warped - source_at(sx, sy);
                            gxx += weight * gx * gx;
                            gxy += weight * gx * gy;
                            gyy += weight * gy * gy;
                            gxt += weight * gx * temporal;
                            gyt += weight * gy * temporal;
                            residual2 += weight * temporal * temporal;
                            weighted_samples += weight;
                            ++samples;
                        }
                    }
                    const float determinant = gxx * gyy - gxy * gxy;
                    const float trace = gxx + gyy;
                    const float discriminant = std::sqrt(std::max(
                        trace * trace - 4.0f * determinant, 0.0f));
                    const float min_eigenvalue = 0.5f * (trace - discriminant);
                    if (samples == 0 || determinant <= 1.0e-12f ||
                        min_eigenvalue < options.minimum_eigenvalue) {
                        next.vectors[index] = FlowVector{old.x, old.y, 0.0f, false};
                        continue;
                    }
                    const float dx = (-gyy * gxt + gxy * gyt) / determinant;
                    const float dy = ( gxy * gxt - gxx * gyt) / determinant;
                    const float updated_x = old.x + std::clamp(dx, -2.0f, 2.0f);
                    const float updated_y = old.y + std::clamp(dy, -2.0f, 2.0f);
                    if (std::hypot(updated_x, updated_y) > options.maximum_motion_per_frame) {
                        next.vectors[index] = FlowVector{updated_x, updated_y, 0.0f, false};
                        continue;
                    }
                    const float texture_confidence = std::clamp(
                        min_eigenvalue / (options.minimum_eigenvalue * 8.0f), 0.0f, 1.0f);
                    const float mean_residual = std::sqrt(residual2 / weighted_samples);
                    const float photometric_confidence = std::exp(
                        -mean_residual * (8.0f / options.photometric_scale));
                    next.vectors[index] = FlowVector{
                        updated_x, updated_y,
                        texture_confidence * photometric_confidence, true};
                }
            }
            current = std::move(next);
        }
        flow = std::move(current);
    }
    return flow;
}

float sample_mask(std::span<const float> mask, std::uint32_t stride,
                  std::uint32_t width, std::uint32_t height,
                  float x, float y) noexcept {
    if (x < 0.0f || y < 0.0f ||
        x > static_cast<float>(width - 1) ||
        y > static_cast<float>(height - 1)) return 0.0f;
    const auto x0 = static_cast<std::uint32_t>(x), y0 = static_cast<std::uint32_t>(y);
    const auto x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const auto at = [&](std::uint32_t sx, std::uint32_t sy) {
        const float value = mask[static_cast<std::size_t>(sy) * stride + sx];
        return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
    };
    const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
    const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
    return top + (bottom - top) * ty;
}

} // namespace

std::uint64_t TemporalMaskPropagator::estimate_pair_work_units(
    std::uint32_t width, std::uint32_t height, TemporalFlowOptions options) noexcept {
    return estimate_work(width, height, options);
}

std::vector<float> TemporalMaskPropagator::refine_mask_edges(
    TrackingFrameView frame, SoftMaskView mask,
    GuidedMaskRefinementOptions options) {
    if (frame.width == 0 || frame.height == 0 ||
        frame.width != mask.width || frame.height != mask.height ||
        options.radius == 0 || options.radius > 32 ||
        !std::isfinite(options.epsilon) || options.epsilon <= 0.0f)
        throw std::invalid_argument("invalid guided mask refinement dimensions or options");
    const auto frame_stride = frame.row_stride == 0 ? frame.width : frame.row_stride;
    const auto mask_stride = mask.row_stride == 0 ? mask.width : mask.row_stride;
    const auto frame_size = checked_storage_size(frame.width, frame.height,
                                                  frame_stride, "tracking frame");
    const auto mask_size = checked_storage_size(mask.width, mask.height,
                                                 mask_stride, "soft mask");
    if (frame.pixels.size() < frame_size)
        throw std::invalid_argument("tracking frame storage is too small");
    if (mask.alpha.size() < mask_size)
        throw std::invalid_argument("soft mask storage is too small");

    const std::size_t count = static_cast<std::size_t>(frame.width) * frame.height;
    std::vector<float> guide(count), source(count), coverage(count),
        squared_guide(count), product(count);
    double guide_scale = 1.0;
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const auto index = static_cast<std::size_t>(y) * frame.width + x;
            Color pixel = frame.pixels[static_cast<std::size_t>(y) * frame_stride + x];
            if (!std::isfinite(pixel.a))
                throw std::invalid_argument("guided mask frame contains non-finite alpha");
            coverage[index] = std::clamp(pixel.a, 0.0f, 1.0f);
            if (frame.premultiplied_alpha) pixel = pixel.unpremultiplied();
            const float luma = 0.2126f * pixel.r + 0.7152f * pixel.g + 0.0722f * pixel.b;
            const float alpha = mask.alpha[static_cast<std::size_t>(y) * mask_stride + x];
            if (!std::isfinite(luma) || !std::isfinite(alpha))
                throw std::invalid_argument("guided mask refinement input contains non-finite pixels");
            guide[index] = coverage[index] > 0.0f ? luma : 0.0f;
            if (coverage[index] > 0.0f)
                guide_scale = std::max(guide_scale, std::abs(static_cast<double>(luma)));
            source[index] = std::clamp(alpha, 0.0f, 1.0f);
        }
    }

    // Keep the second-order statistics bounded even for finite HDR values
    // whose square would overflow float. Scaling epsilon by the same squared
    // range preserves its meaning in the caller's original linear-light units.
    const double scale_squared = guide_scale * guide_scale;
    const float scaled_epsilon = static_cast<float>(std::max(
        static_cast<double>(std::numeric_limits<float>::min()),
        static_cast<double>(options.epsilon) / scale_squared));
    for (std::size_t i = 0; i < count; ++i) {
        guide[i] = static_cast<float>(static_cast<double>(guide[i]) / guide_scale);
        squared_guide[i] = guide[i] * guide[i];
        product[i] = guide[i] * source[i];
    }

    const auto mean_guide = box_weighted_mean(
        guide, coverage, frame.width, frame.height, options.radius);
    const auto mean_mask = box_weighted_mean(
        source, coverage, frame.width, frame.height, options.radius);
    const auto mean_squared_guide = box_weighted_mean(
        squared_guide, coverage, frame.width, frame.height, options.radius);
    const auto mean_product = box_weighted_mean(
        product, coverage, frame.width, frame.height, options.radius);
    std::vector<float> coefficient(count), offset(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float variance = std::max(0.0f, mean_squared_guide[i] -
            mean_guide[i] * mean_guide[i]);
        const float covariance = mean_product[i] - mean_guide[i] * mean_mask[i];
        coefficient[i] = covariance / (variance + scaled_epsilon);
        offset[i] = mean_mask[i] - coefficient[i] * mean_guide[i];
    }
    const auto mean_coefficient = box_weighted_mean(
        coefficient, coverage, frame.width, frame.height, options.radius);
    const auto mean_offset = box_weighted_mean(
        offset, coverage, frame.width, frame.height, options.radius);
    std::vector<float> refined(count);
    for (std::size_t i = 0; i < count; ++i) {
        refined[i] = coverage[i] <= 1.0e-6f ? 0.0f
            : std::clamp(mean_coefficient[i] * guide[i] + mean_offset[i],
                         0.0f, 1.0f);
    }
    return refined;
}

PropagatedMask TemporalMaskPropagator::propagate(
    TrackingFrameView source_frame,
    TrackingFrameView target_frame,
    SoftMaskView source_mask,
    TemporalFlowOptions options) {
    validate_options(options);
    const auto source_stride = source_frame.row_stride == 0
        ? source_frame.width : source_frame.row_stride;
    const auto target_stride = target_frame.row_stride == 0
        ? target_frame.width : target_frame.row_stride;
    const auto mask_stride = source_mask.row_stride == 0
        ? source_mask.width : source_mask.row_stride;
    const auto confidence_stride = source_mask.confidence_row_stride == 0
        ? mask_stride : source_mask.confidence_row_stride;
    const auto source_size = checked_storage_size(source_frame.width, source_frame.height,
                                                  source_stride, "source frame");
    const auto target_size = checked_storage_size(target_frame.width, target_frame.height,
                                                  target_stride, "target frame");
    const auto mask_size = checked_storage_size(source_mask.width, source_mask.height,
                                                mask_stride, "source mask");
    const auto confidence_size = source_mask.confidence.empty() ? 0
        : checked_storage_size(source_mask.width, source_mask.height,
                               confidence_stride, "source mask confidence");
    if (source_frame.width != target_frame.width || source_frame.height != target_frame.height ||
        source_frame.width != source_mask.width || source_frame.height != source_mask.height)
        throw std::invalid_argument("temporal mask propagation requires matching frame and mask dimensions");
    if (source_frame.pixels.size() < source_size || target_frame.pixels.size() < target_size ||
        source_mask.alpha.size() < mask_size ||
        source_mask.confidence.size() < confidence_size)
        throw std::invalid_argument("temporal mask propagation input span is smaller than its stride and dimensions");
    if (estimate_work(source_frame.width, source_frame.height, options) >
        options.maximum_work_units)
        throw std::length_error("temporal mask propagation exceeds its configured work budget");

    const Pyramid source_pyramid = build_pyramid(source_frame, source_stride, options.pyramid_levels);
    const Pyramid target_pyramid = build_pyramid(target_frame, target_stride, options.pyramid_levels);
    const FlowField forward = estimate_one_way(source_pyramid, target_pyramid, options);
    const FlowField backward = estimate_one_way(target_pyramid, source_pyramid, options);

    PropagatedMask result;
    result.width = source_frame.width;
    result.height = source_frame.height;
    const auto count = static_cast<std::size_t>(result.width) * result.height;
    result.alpha.assign(count, 0.0f);
    result.confidence.assign(count, 0.0f);
    for (std::uint32_t y = 0; y < result.height; ++y) {
        for (std::uint32_t x = 0; x < result.width; ++x) {
            const auto index = static_cast<std::size_t>(y) * result.width + x;
            float candidate_alpha = sample_mask(source_mask.alpha, mask_stride,
                source_mask.width, source_mask.height,
                static_cast<float>(x), static_cast<float>(y));
            bool foreground_candidate = candidate_alpha > 0.01f;
            const auto reject_foreground = [&] {
                if (foreground_candidate) {
                    ++result.foreground_samples;
                    ++result.rejected_foreground_samples;
                }
            };
            const FlowVector reverse = backward.vectors[index];
            if (!reverse.valid) {
                ++result.rejected_low_texture;
                reject_foreground();
                continue;
            }
            const float source_x = static_cast<float>(x) + reverse.x;
            const float source_y = static_cast<float>(y) + reverse.y;
            if (source_x < 0.0f || source_y < 0.0f ||
                source_x > static_cast<float>(result.width - 1) ||
                source_y > static_cast<float>(result.height - 1) ||
                std::hypot(reverse.x, reverse.y) > options.maximum_motion_per_frame) {
                ++result.rejected_large_motion;
                reject_foreground();
                continue;
            }
            candidate_alpha = sample_mask(source_mask.alpha, mask_stride,
                source_mask.width, source_mask.height, source_x, source_y);
            foreground_candidate = candidate_alpha > 0.01f;
            const FlowVector ahead = sample_flow(forward, source_x, source_y);
            if (!ahead.valid) {
                ++result.rejected_low_texture;
                reject_foreground();
                continue;
            }
            const float consistency_error = std::hypot(reverse.x + ahead.x,
                                                       reverse.y + ahead.y);
            if (consistency_error > options.forward_backward_tolerance) {
                ++result.rejected_occlusion;
                reject_foreground();
                continue;
            }
            const float consistency = std::exp(-consistency_error /
                                               options.forward_backward_tolerance);
            float confidence = std::sqrt(std::max(reverse.confidence * ahead.confidence, 0.0f)) *
                               consistency;
            if (!source_mask.confidence.empty()) {
                const float historical = sample_mask(source_mask.confidence,
                    confidence_stride, source_mask.width, source_mask.height,
                    source_x, source_y);
                confidence = std::min(confidence, historical);
            }
            if (confidence < options.minimum_confidence) {
                ++result.rejected_low_texture;
                reject_foreground();
                continue;
            }
            const float alpha = candidate_alpha;
            result.alpha[index] = alpha;
            result.confidence[index] = confidence;
            if (foreground_candidate) ++result.foreground_samples;
        }
    }
    return result;
}

std::vector<PropagatedMask> TemporalMaskPropagator::propagate_sequence(
    std::span<const TrackingFrameView> frames,
    std::size_t seed_frame,
    SoftMaskView seed_mask,
    TemporalFlowOptions options) {
    validate_options(options);
    if (frames.empty() || seed_frame >= frames.size())
        throw std::invalid_argument("temporal mask seed frame is outside the supplied clip");
    if (frames[seed_frame].width != seed_mask.width ||
        frames[seed_frame].height != seed_mask.height)
        throw std::invalid_argument("temporal mask seed dimensions do not match the seed frame");

    const auto seed_stride = seed_mask.row_stride == 0
        ? seed_mask.width : seed_mask.row_stride;
    const auto seed_size = checked_storage_size(seed_mask.width, seed_mask.height,
                                                seed_stride, "seed mask");
    if (seed_mask.alpha.size() < seed_size)
        throw std::invalid_argument("temporal mask seed span is smaller than its stride and dimensions");

    std::vector<PropagatedMask> result(frames.size());
    auto& seed = result[seed_frame];
    seed.width = seed_mask.width;
    seed.height = seed_mask.height;
    const auto seed_pixels = static_cast<std::size_t>(seed.width) * seed.height;
    seed.alpha.resize(seed_pixels);
    seed.confidence.resize(seed_pixels);
    for (std::uint32_t y = 0; y < seed.height; ++y) {
        for (std::uint32_t x = 0; x < seed.width; ++x) {
            const auto source_index = static_cast<std::size_t>(y) * seed_stride + x;
            const auto target_index = static_cast<std::size_t>(y) * seed.width + x;
            const float value = seed_mask.alpha[source_index];
            seed.alpha[target_index] = std::isfinite(value)
                ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
            seed.confidence[target_index] = std::isfinite(value) ? 1.0f : 0.0f;
        }
    }

    for (std::size_t i = seed_frame + 1; i < frames.size(); ++i) {
        const auto& previous = result[i - 1];
        result[i] = propagate(frames[i - 1], frames[i],
            SoftMaskView{previous.alpha, previous.width, previous.height, 0,
                         previous.confidence, 0}, options);
    }
    for (std::size_t i = seed_frame; i > 0; --i) {
        const auto& next = result[i];
        result[i - 1] = propagate(frames[i], frames[i - 1],
            SoftMaskView{next.alpha, next.width, next.height, 0,
                         next.confidence, 0}, options);
    }
    return result;
}

std::vector<PropagatedMask> TemporalMaskPropagator::propagate_anchored_sequence(
    std::span<const TrackingFrameView> frames,
    std::span<const MaskAnchor> anchors,
    TemporalFlowOptions options) {
    validate_options(options);
    if (frames.empty() || anchors.empty())
        throw std::invalid_argument("anchored mask propagation requires frames and at least one anchor");

    const auto make_anchor_result = [&](const MaskAnchor& anchor) {
        if (anchor.frame_index >= frames.size())
            throw std::invalid_argument("mask correction frame is outside the supplied clip");
        const auto& frame = frames[anchor.frame_index];
        if (anchor.mask.width != frame.width || anchor.mask.height != frame.height)
            throw std::invalid_argument("mask correction dimensions do not match its frame");
        const auto stride = anchor.mask.row_stride == 0
            ? anchor.mask.width : anchor.mask.row_stride;
        const auto required = checked_storage_size(anchor.mask.width, anchor.mask.height,
                                                   stride, "anchor mask");
        if (anchor.mask.alpha.size() < required)
            throw std::invalid_argument("mask correction span is smaller than its stride and dimensions");
        PropagatedMask result;
        result.width = anchor.mask.width;
        result.height = anchor.mask.height;
        const auto count = static_cast<std::size_t>(result.width) * result.height;
        result.alpha.resize(count);
        result.confidence.resize(count, 1.0f);
        for (std::uint32_t y = 0; y < result.height; ++y) {
            for (std::uint32_t x = 0; x < result.width; ++x) {
                const float value = anchor.mask.alpha[static_cast<std::size_t>(y) * stride + x];
                if (!std::isfinite(value))
                    throw std::invalid_argument(
                        "mask correction contains a non-finite alpha sample");
                const auto index = static_cast<std::size_t>(y) * result.width + x;
                result.alpha[index] = std::clamp(value, 0.0f, 1.0f);
                result.confidence[index] = 1.0f;
            }
        }
        return result;
    };

    for (std::size_t i = 0; i < anchors.size(); ++i) {
        if (anchors[i].frame_index >= frames.size())
            throw std::invalid_argument("mask correction frame is outside the supplied clip");
        if (i != 0 && anchors[i - 1].frame_index >= anchors[i].frame_index)
            throw std::invalid_argument("mask correction frame indices must be strictly increasing");
        if (frames[anchors[i].frame_index].width != frames[0].width ||
            frames[anchors[i].frame_index].height != frames[0].height)
            throw std::invalid_argument("all tracking frames must have matching dimensions");
    }
    for (const auto& frame : frames) {
        if (frame.width != frames[0].width || frame.height != frames[0].height)
            throw std::invalid_argument("all tracking frames must have matching dimensions");
        const auto stride = frame.row_stride == 0 ? frame.width : frame.row_stride;
        if (frame.pixels.size() < checked_storage_size(frame.width, frame.height,
                                                       stride, "tracking frame"))
            throw std::invalid_argument("tracking frame span is smaller than its stride and dimensions");
    }

    std::vector<PropagatedMask> result(frames.size());
    for (const auto& anchor : anchors)
        result[anchor.frame_index] = make_anchor_result(anchor);

    const auto propagate_one = [&](std::size_t from, std::size_t to,
                                   const PropagatedMask& source) {
        return propagate(frames[from], frames[to],
            SoftMaskView{source.alpha, source.width, source.height, 0,
                         source.confidence, 0}, options);
    };

    const std::size_t first = anchors.front().frame_index;
    for (std::size_t i = first; i > 0; --i)
        result[i - 1] = propagate_one(i, i - 1, result[i]);
    const std::size_t last = anchors.back().frame_index;
    for (std::size_t i = last; i + 1 < frames.size(); ++i)
        result[i + 1] = propagate_one(i, i + 1, result[i]);

    for (std::size_t interval = 1; interval < anchors.size(); ++interval) {
        const std::size_t left = anchors[interval - 1].frame_index;
        const std::size_t right = anchors[interval].frame_index;
        std::vector<PropagatedMask> forward(right - left + 1);
        std::vector<PropagatedMask> backward(right - left + 1);
        forward[0] = result[left];
        backward[right - left] = result[right];
        for (std::size_t i = left; i < right; ++i)
            forward[i + 1 - left] = propagate_one(i, i + 1, forward[i - left]);
        for (std::size_t i = right; i > left; --i)
            backward[i - 1 - left] = propagate_one(i, i - 1, backward[i - left]);

        for (std::size_t i = left + 1; i < right; ++i) {
            auto& combined = result[i];
            const auto& from_left = forward[i - left];
            const auto& from_right = backward[i - left];
            combined.width = frames[i].width;
            combined.height = frames[i].height;
            const auto count = static_cast<std::size_t>(combined.width) * combined.height;
            combined.alpha.assign(count, 0.0f);
            combined.confidence.assign(count, 0.0f);
            for (std::size_t pixel = 0; pixel < count; ++pixel) {
                const float a = std::clamp(from_left.confidence[pixel], 0.0f, 1.0f);
                const float b = std::clamp(from_right.confidence[pixel], 0.0f, 1.0f);
                const float total = a + b;
                combined.confidence[pixel] = 0.5f * total;
                combined.alpha[pixel] = total > 1.0e-6f
                    ? (from_left.alpha[pixel] * a + from_right.alpha[pixel] * b) / total
                    : 0.0f;
            }
            combined.rejected_low_texture = from_left.rejected_low_texture + from_right.rejected_low_texture;
            combined.rejected_occlusion = from_left.rejected_occlusion + from_right.rejected_occlusion;
            combined.rejected_large_motion = from_left.rejected_large_motion + from_right.rejected_large_motion;
            combined.foreground_samples = from_left.foreground_samples +
                                          from_right.foreground_samples;
            combined.rejected_foreground_samples = from_left.rejected_foreground_samples +
                from_right.rejected_foreground_samples;
        }
    }
    return result;
}

} // namespace chronon3d::media::tracking
