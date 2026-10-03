#pragma once

// CPU analysis scopes for final-frame review and agent-driven grading.
// Inputs default to Chronon's premultiplied linear framebuffer pixels in
// row-major order. The analyzer is intentionally independent of the renderer and does not mutate
// or retain the source frame.

#include <chronon3d/math/color.hpp>
#include <chronon3d/color/output_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace chronon3d::color {

enum class ScopeEncoding : std::uint8_t {
    LinearSRGB, // Linear BT.709 framebuffer -> Rec.709 OETF.
    LinearSRGBTransfer, // Linear framebuffer -> sRGB IEC 61966-2-1 OETF.
    SRGB,       // Already sRGB-encoded samples; pass through.
    Rec709,     // Already Rec.709-encoded samples; pass through.
    PQ,         // Linear BT.709 framebuffer -> BT.2020 ST 2084 signal.
    HLG,        // Linear BT.709 framebuffer -> BT.2020 ARIB STD-B67 signal.
};

enum class ScopePrimaries : std::uint8_t {
    BT709,
    BT2020,
};

enum class WaveformSignal : std::uint8_t {
    Luma,
    RGB,
    Alpha,
};

struct ScopeOptions {
    ScopeEncoding encoding{ScopeEncoding::LinearSRGB};
    ScopePrimaries primaries{ScopePrimaries::BT709};
    // Signal range shown vertically. Raising max above 1 exposes highlight
    // headroom instead of folding all super-white samples onto the top row.
    float signal_min{0.0f};
    float signal_max{1.0f};
    bool premultiplied_alpha{true};
    // Pixels at or below this alpha are excluded. Set to zero to include all.
    float transparent_cutoff{0.0f};
};

struct Waveform {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t channels{0};
    std::vector<std::uint64_t> bins{};
    std::uint64_t ignored_non_finite{0};
    std::uint64_t ignored_transparent{0};
    std::uint64_t clipped_below{0};
    std::uint64_t clipped_above{0};
    std::uint64_t alpha_samples{0};
    std::uint64_t fully_transparent_samples{0};
    std::uint64_t partial_alpha_samples{0};
    std::uint64_t fully_opaque_samples{0};
    double alpha_sum{0.0};

    // Layout is [channel][x][y], with y=0 at the bottom of the scope.
    [[nodiscard]] std::uint64_t at(std::uint32_t channel,
                                   std::uint32_t x,
                                   std::uint32_t y) const {
        if (channel >= channels || x >= width || y >= height)
            throw std::out_of_range("waveform bin coordinate out of range");
        const auto index = (static_cast<std::size_t>(channel) * width + x) * height + y;
        return bins[index];
    }
};

struct Vectorscope {
    std::uint32_t size{0};
    // Cb/Cr plane, indexed [cr_y][cb_x]; both axes cover [-chroma_limit,+limit].
    std::vector<std::uint64_t> bins{};
    std::uint64_t ignored_non_finite{0};
    std::uint64_t ignored_transparent{0};
    std::uint64_t clipped_chroma{0};
};

namespace scope_detail {

inline constexpr std::uint64_t kMaximumScopeBins = 16'777'216;

inline bool finite(const Color& c) noexcept {
    return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) &&
           std::isfinite(c.a);
}

inline float encode(float value, ScopeEncoding encoding) noexcept {
    switch (encoding) {
        case ScopeEncoding::LinearSRGB:
            // Scopes display the Rec.709 encoded video signal by default.
            return linear_rec709_float(value);
        case ScopeEncoding::LinearSRGBTransfer:
            return linear_srgb_float(value);
        case ScopeEncoding::SRGB:
        case ScopeEncoding::Rec709:
            return value;
        case ScopeEncoding::PQ:
            return linear_to_pq_100_nits(value);
        case ScopeEncoding::HLG:
            return linear_to_hlg_scene(value);
    }
    return value;
}

inline bool valid_options(const ScopeOptions& options) noexcept {
    return std::isfinite(options.signal_min) && std::isfinite(options.signal_max) &&
           options.signal_max > options.signal_min &&
           std::isfinite(options.transparent_cutoff) &&
           options.transparent_cutoff >= 0.0f && options.transparent_cutoff <= 1.0f;
}

inline std::size_t pixel_count(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0)
        throw std::invalid_argument("video scope dimensions must be positive");
    const auto count = static_cast<std::uint64_t>(width) * height;
    if (count > std::numeric_limits<std::size_t>::max())
        throw std::length_error("video scope dimensions overflow address space");
    return static_cast<std::size_t>(count);
}

inline std::uint32_t bin(float value, float low, float high,
                        std::uint32_t count) noexcept {
    const float normalized = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
    return static_cast<std::uint32_t>(std::lround(
        normalized * static_cast<float>(count - 1)));
}

} // namespace scope_detail

/// Build a waveform from a row-major frame. Alpha representation follows
/// `options.premultiplied_alpha`. Luma mode uses luma coefficients for the
/// selected output primaries after applying its signal encoding. Alpha mode
/// bins linear coverage directly, including fully transparent pixels.
/// BT.2020 selection also changes the vectorscope matrix.
/// A super-white display range can be requested with signal_max > 1.
[[nodiscard]] inline Waveform make_waveform(
    std::span<const Color> pixels,
    std::uint32_t source_width,
    std::uint32_t source_height,
    std::uint32_t scope_width,
    std::uint32_t scope_height,
    WaveformSignal signal = WaveformSignal::Luma,
    ScopeOptions options = {},
    std::uint32_t source_stride = 0) {
    if (source_width == 0 || source_height == 0)
        throw std::invalid_argument("waveform source dimensions must be positive");
    if (source_stride == 0) source_stride = source_width;
    if (source_stride < source_width || pixels.size() <
            scope_detail::pixel_count(source_stride, source_height))
        throw std::invalid_argument("waveform pixel storage does not cover its row stride");
    if (scope_width == 0 || scope_height == 0)
        throw std::invalid_argument("waveform dimensions must be positive");
    if (!scope_detail::valid_options(options))
        throw std::invalid_argument("invalid waveform options");

    Waveform out;
    out.width = scope_width;
    out.height = scope_height;
    out.channels = signal == WaveformSignal::RGB ? 3u : 1u;
    const auto plane_bins = static_cast<std::uint64_t>(out.width) * out.height;
    if (plane_bins > scope_detail::kMaximumScopeBins / out.channels)
        throw std::length_error("waveform exceeds the 16,777,216-bin memory limit");
    const auto bin_count = plane_bins * out.channels;
    if (bin_count > std::numeric_limits<std::size_t>::max())
        throw std::length_error("waveform allocation exceeds address space");
    out.bins.assign(static_cast<std::size_t>(bin_count), 0);

    for (std::uint32_t y = 0; y < source_height; ++y) {
        for (std::uint32_t x = 0; x < source_width; ++x) {
            const auto& pixel = pixels[static_cast<std::size_t>(y) * source_stride + x];
            if (std::isfinite(pixel.a)) {
                ++out.alpha_samples;
                out.alpha_sum += static_cast<double>(pixel.a);
                if (pixel.a <= 0.0f) ++out.fully_transparent_samples;
                else if (pixel.a >= 1.0f) ++out.fully_opaque_samples;
                else ++out.partial_alpha_samples;
            }
            if (signal == WaveformSignal::Alpha) {
                if (!std::isfinite(pixel.a)) {
                    ++out.ignored_non_finite;
                    continue;
                }
                const auto sx = std::min(out.width - 1,
                    static_cast<std::uint32_t>((static_cast<std::uint64_t>(x) * out.width) /
                                               source_width));
                if (pixel.a < options.signal_min) ++out.clipped_below;
                if (pixel.a > options.signal_max) ++out.clipped_above;
                const auto sy = scope_detail::bin(pixel.a, options.signal_min,
                                                  options.signal_max, out.height);
                ++out.bins[static_cast<std::size_t>(sx) * out.height + sy];
                continue;
            }
            if (!scope_detail::finite(pixel)) {
                ++out.ignored_non_finite;
                continue;
            }
            if (pixel.a <= options.transparent_cutoff) {
                ++out.ignored_transparent;
                continue;
            }

            const Color straight = options.premultiplied_alpha
                ? pixel.unpremultiplied() : pixel;
            auto linear = std::array<float, 3>{straight.r, straight.g, straight.b};
            if (options.primaries == ScopePrimaries::BT2020)
                linear = bt709_to_bt2020_linear(linear[0], linear[1], linear[2]);
            const float channels[3]{scope_detail::encode(linear[0], options.encoding),
                                    scope_detail::encode(linear[1], options.encoding),
                                    scope_detail::encode(linear[2], options.encoding)};
            float values[3]{};
            if (signal == WaveformSignal::RGB) {
                values[0] = channels[0]; values[1] = channels[1]; values[2] = channels[2];
            } else {
                if (options.primaries == ScopePrimaries::BT2020) {
                    values[0] = 0.2627f * channels[0] + 0.6780f * channels[1] +
                                0.0593f * channels[2];
                } else {
                    values[0] = 0.2126f * channels[0] + 0.7152f * channels[1] +
                                0.0722f * channels[2];
                }
            }

            const auto sx = std::min(out.width - 1,
                static_cast<std::uint32_t>((static_cast<std::uint64_t>(x) * out.width) /
                                           source_width));
            for (std::uint32_t channel = 0; channel < out.channels; ++channel) {
                const float value = values[channel];
                if (value < options.signal_min) ++out.clipped_below;
                if (value > options.signal_max) ++out.clipped_above;
                const auto sy = scope_detail::bin(value, options.signal_min,
                                                  options.signal_max, out.height);
                const auto index = (static_cast<std::size_t>(channel) * out.width + sx) *
                                   out.height + sy;
                ++out.bins[index];
            }
        }
    }
    return out;
}

/// Build a Rec.709 Cb/Cr vectorscope. Chroma outside the configured limit is
/// clamped to the edge and counted, so agent tooling can detect gamut overflow.
[[nodiscard]] inline Vectorscope make_vectorscope(
    std::span<const Color> pixels,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t resolution = 256,
    ScopeOptions options = {},
    float chroma_limit = 0.6f,
    std::uint32_t source_stride = 0) {
    if (width == 0 || height == 0)
        throw std::invalid_argument("vectorscope source dimensions must be positive");
    if (source_stride == 0) source_stride = width;
    if (source_stride < width || pixels.size() <
            scope_detail::pixel_count(source_stride, height))
        throw std::invalid_argument("vectorscope pixel storage does not cover its row stride");
    if (resolution < 2)
        throw std::invalid_argument("vectorscope resolution must be at least 2");
    if (!scope_detail::valid_options(options) || !std::isfinite(chroma_limit) ||
        chroma_limit <= 0.0f)
        throw std::invalid_argument("invalid vectorscope options");

    Vectorscope out;
    out.size = resolution;
    const auto bins = static_cast<std::uint64_t>(resolution) * resolution;
    if (bins > scope_detail::kMaximumScopeBins)
        throw std::length_error("vectorscope exceeds the 16,777,216-bin memory limit");
    if (bins > std::numeric_limits<std::size_t>::max())
        throw std::length_error("vectorscope allocation exceeds address space");
    out.bins.assign(static_cast<std::size_t>(bins), 0);

    for (std::uint32_t y = 0; y < height; ++y) {
      for (std::uint32_t x = 0; x < width; ++x) {
        const auto& pixel = pixels[static_cast<std::size_t>(y) * source_stride + x];
        if (!scope_detail::finite(pixel)) {
            ++out.ignored_non_finite;
            continue;
        }
        if (pixel.a <= options.transparent_cutoff) {
            ++out.ignored_transparent;
            continue;
        }
        const Color straight = options.premultiplied_alpha
            ? pixel.unpremultiplied() : pixel;
        auto linear = std::array<float, 3>{straight.r, straight.g, straight.b};
        const bool bt2020 = options.primaries == ScopePrimaries::BT2020;
        if (bt2020)
            linear = bt709_to_bt2020_linear(linear[0], linear[1], linear[2]);
        const float r = scope_detail::encode(linear[0], options.encoding);
        const float g = scope_detail::encode(linear[1], options.encoding);
        const float b = scope_detail::encode(linear[2], options.encoding);
        const float kr = bt2020 ? 0.2627f : 0.2126f;
        const float kb = bt2020 ? 0.0593f : 0.0722f;
        const float luma = kr * r + (1.0f - kr - kb) * g + kb * b;
        const float cb = (b - luma) / (2.0f * (1.0f - kb));
        const float cr = (r - luma) / (2.0f * (1.0f - kr));
        if (std::abs(cb) > chroma_limit || std::abs(cr) > chroma_limit)
            ++out.clipped_chroma;
        const auto x_bin = scope_detail::bin(cb, -chroma_limit, chroma_limit, resolution);
        const auto y_bin = scope_detail::bin(cr, -chroma_limit, chroma_limit, resolution);
        ++out.bins[static_cast<std::size_t>(resolution - 1 - y_bin) * resolution + x_bin];
      }
    }
    return out;
}

} // namespace chronon3d::color
