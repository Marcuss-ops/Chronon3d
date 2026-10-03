#include "scope_analysis_detail.hpp"

#include <chronon3d/backends/image/image_loader.hpp>
#include <chronon3d/backends/image/exr_mmap.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <utility>

namespace chronon3d::cli::scope_detail {

bool supported_sdr_image(const std::string& path) {
    auto extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
}

bool supported_scope_image(const std::string& path) {
    if (supported_sdr_image(path)) return true;
#ifdef CHRONON3D_ENABLE_EXR
    auto extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension == ".exr";
#else
    return false;
#endif
}

bool is_exr_input(const std::string& path) {
    auto extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension == ".exr";
}

bool is_video_input(const std::string& path) {
    auto extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension == ".mp4" || extension == ".mov" || extension == ".mkv" ||
           extension == ".webm" || extension == ".avi" || extension == ".m4v" ||
           extension == ".mpeg" || extension == ".mpg" || extension == ".ts" ||
           extension == ".mxf" || extension == ".flv" || extension == ".3gp" ||
           extension == ".wmv" || extension == ".ogv";
}

struct ScopeQualityGates {
    std::optional<double> max_clipped_fraction;
    std::optional<double> max_clipped_chroma_fraction;
    std::optional<double> max_non_finite_fraction;
    std::optional<double> min_opaque_fraction;
    std::optional<double> max_transparent_fraction;
    std::optional<double> max_pq_peak_nits;
    std::optional<double> max_pq_frame_average_nits;
};

std::shared_ptr<Framebuffer> load_scope_image(const std::string& path) {
    auto extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
#ifdef CHRONON3D_ENABLE_EXR
    if (extension == ".exr")
        return image::load_exr_as_framebuffer_float(path);
#endif
    return io::load_image_as_framebuffer(path);
}

json sparse_waveform(const color::Waveform& waveform,
                     color::WaveformSignal waveform_signal) {
    json channels = json::array();
    for (std::uint32_t channel = 0; channel < waveform.channels; ++channel) {
        json bins = json::array();
        for (std::uint32_t x = 0; x < waveform.width; ++x) {
            for (std::uint32_t y = 0; y < waveform.height; ++y) {
                const auto count = waveform.at(channel, x, y);
                if (count != 0) bins.push_back({x, y, count});
            }
        }
        channels.push_back(std::move(bins));
    }
    return {
        {"width", waveform.width},
        {"height", waveform.height},
        {"channels", waveform.channels},
        {"channel_order", waveform.channels == 1
            ? json{waveform_signal == color::WaveformSignal::Alpha ? "alpha" : "luma"}
            : json{"red", "green", "blue"}},
        {"bins_xyz_count", std::move(channels)},
        {"clipped_below", waveform.clipped_below},
        {"clipped_above", waveform.clipped_above},
        {"ignored_non_finite", waveform.ignored_non_finite},
        {"ignored_transparent", waveform.ignored_transparent},
        {"alpha_coverage", {
            {"samples", waveform.alpha_samples},
            {"fully_transparent", waveform.fully_transparent_samples},
            {"partial", waveform.partial_alpha_samples},
            {"fully_opaque", waveform.fully_opaque_samples},
            {"mean", waveform.alpha_samples == 0 ? 0.0
                : waveform.alpha_sum / static_cast<double>(waveform.alpha_samples)},
        }},
    };
}

json sparse_vectorscope(const color::Vectorscope& vectorscope) {
    json bins = json::array();
    for (std::uint32_t y = 0; y < vectorscope.size; ++y) {
        for (std::uint32_t x = 0; x < vectorscope.size; ++x) {
            const auto count = vectorscope.bins[
                static_cast<std::size_t>(y) * vectorscope.size + x];
            if (count != 0) bins.push_back({x, y, count});
        }
    }
    return {
        {"size", vectorscope.size},
        {"axis_order", "Cr_vertical_Cb_horizontal"},
        {"chroma_range", {-0.6f, 0.6f}},
        {"bins_xyz_count", std::move(bins)},
        {"clipped_chroma", vectorscope.clipped_chroma},
        {"ignored_non_finite", vectorscope.ignored_non_finite},
        {"ignored_transparent", vectorscope.ignored_transparent},
    };
}

json scope_frame_report(const Framebuffer& framebuffer, const std::string& input,
                        bool exr_input, const std::string& input_transfer,
                        std::optional<std::int64_t> render_frame_index,
                        color::ScopeEncoding scope_encoding,
                        color::ScopePrimaries scope_primaries,
                        std::uint32_t waveform_width,
                        std::uint32_t waveform_height,
                        std::uint32_t vectorscope_size,
                        color::WaveformSignal waveform_signal,
                        float signal_min, float signal_max,
                        json source_metadata) {
    const auto width = static_cast<std::uint32_t>(framebuffer.width());
    const auto height = static_cast<std::uint32_t>(framebuffer.height());
    const auto stride = static_cast<std::uint32_t>(framebuffer.allocated_width());
    const auto pixel_count = static_cast<std::size_t>(stride) * height;
    const std::span<const Color> pixels(framebuffer.data(), pixel_count);

    color::ScopeOptions scope_options;
    scope_options.encoding = scope_encoding;
    scope_options.primaries = scope_primaries;
    scope_options.premultiplied_alpha = true;
    scope_options.signal_min = signal_min;
    scope_options.signal_max = signal_max;
    const auto waveform = color::make_waveform(
        pixels, width, height, waveform_width == 0 ? width : waveform_width,
        waveform_height, waveform_signal, scope_options, stride);
    const auto vectorscope = color::make_vectorscope(
        pixels, width, height, vectorscope_size, scope_options, 0.6f, stride);
    const std::string signal_encoding = scope_encoding == color::ScopeEncoding::PQ
        ? "smpte2084_pq"
        : (scope_encoding == color::ScopeEncoding::HLG
            ? "arib_std_b67_hlg"
            : (scope_encoding == color::ScopeEncoding::LinearSRGBTransfer
                ? "srgb_iec61966_2_1" : "rec709"));
    json frame = {
        {"input", std::filesystem::path(input).string()},
        {"width", width},
        {"height", height},
        {"working_encoding", "linear_bt709"},
        {"scope_signal_encoding", waveform_signal == color::WaveformSignal::Alpha
            ? "alpha_coverage" : signal_encoding},
        {"scope_signal_range", {
            {"minimum", signal_min},
            {"maximum", signal_max},
            {"units", waveform_signal == color::WaveformSignal::Alpha
                ? "linear alpha coverage"
                : signal_encoding + " encoded signal values"},
            {"range_kind", "full_float"},
        }},
        {"luma_coefficients", scope_primaries == color::ScopePrimaries::BT2020
            ? "BT.2020" : "BT.709"},
        {"vectorscope_matrix", scope_primaries == color::ScopePrimaries::BT2020
            ? "BT.2020 YCbCr" : "BT.709 YCbCr"},
        {"waveform_signal", waveform_signal == color::WaveformSignal::Luma
            ? "luma" : (waveform_signal == color::WaveformSignal::RGB ? "rgb" : "alpha")},
        {"input_assumption", exr_input
            ? "linear_float_exr_converted_to_bt709_d65"
            : (input_transfer == "native_linear"
                ? "native_video_decoder_working_linear_bt709"
                : (input_transfer == "rec709"
                    ? "8_bit_sdr_rec709_rgba"
                    : (input_transfer == "displayp3"
                        ? "8_bit_sdr_displayp3_d65_rgba"
                        : (input_transfer == "linearsrgb"
                            ? "8_bit_linear_bt709_rgba"
                            : "8_bit_sdr_srgb_rgba"))))},
        {"render_frame_index", render_frame_index
            ? json(*render_frame_index) : json(nullptr)},
        {"signal_min", signal_min},
        {"signal_max", signal_max},
        {"waveform", sparse_waveform(waveform, waveform_signal)},
        {"vectorscope", sparse_vectorscope(vectorscope)},
    };
    for (auto it = source_metadata.begin(); it != source_metadata.end(); ++it)
        frame[it.key()] = std::move(it.value());
    return frame;
}

std::pair<double, double> measure_pq_luminance_nits(const Framebuffer& framebuffer) {
    double peak_pixel_nits = 0.0;
    double sum_luminance_nits = 0.0;
    std::uint64_t visible_pixels = 0;
    for (i32 y = 0; y < framebuffer.height(); ++y) {
        for (i32 x = 0; x < framebuffer.width(); ++x) {
            const auto pixel = framebuffer.get_pixel(x, y);
            if (!std::isfinite(pixel.a) || pixel.a <= 1.0e-8f) continue;
            const auto straight = pixel.unpremultiplied();
            const double luminance_nits = std::max(0.0,
                (0.2126 * straight.r + 0.7152 * straight.g + 0.0722 * straight.b) * 100.0);
            peak_pixel_nits = std::max(peak_pixel_nits, luminance_nits);
            sum_luminance_nits += luminance_nits;
            ++visible_pixels;
        }
    }
    const double frame_average_nits = visible_pixels == 0 ? 0.0
        : sum_luminance_nits / static_cast<double>(visible_pixels);
    return {peak_pixel_nits, frame_average_nits};
}


} // namespace chronon3d::cli::scope_detail
