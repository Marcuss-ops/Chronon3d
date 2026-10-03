#include "../../cli_context.hpp"

#include <chronon3d/backends/image/image_loader.hpp>
#include <chronon3d/backends/image/exr_mmap.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/color/video_scopes.hpp>
#include <chronon3d/media/video/media_probe.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::cli {
namespace {

using json = nlohmann::json;

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
                        json source_metadata = json::object()) {
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

int analyze_images(const std::vector<std::string>& inputs,
                   const std::string& output_path,
                   std::uint32_t waveform_width,
                   std::uint32_t waveform_height,
                   std::uint32_t vectorscope_size,
                   color::WaveformSignal waveform_signal,
                   float signal_min, float signal_max,
                   const std::string& input_transfer_override,
                   const std::string& receipt_path,
                   bool require_render_receipt,
                   std::int64_t requested_frame_index_start,
                   std::uint32_t video_frame_step,
                   const ScopeQualityGates& quality_gates) {
    if (!std::isfinite(signal_min) || !std::isfinite(signal_max) ||
        signal_max <= signal_min) {
        spdlog::error("Scope signal range must be finite with max greater than min");
        return 1;
    }
    if (require_render_receipt && receipt_path.empty()) {
        spdlog::error("--require-render-receipt requires --render-receipt");
        return 1;
    }
    json report = {
        {"schema", "chronon.video_scopes.v1"},
        {"frames", json::array()},
    };
    json receipt;
    std::string receipt_color_space;
    std::int64_t frame_index_start = 0;
    if (!receipt_path.empty()) {
        std::ifstream receipt_file(receipt_path, std::ios::binary);
        if (!receipt_file) {
            spdlog::error("Could not open render receipt: {}", receipt_path);
            return 1;
        }
        receipt_file >> receipt;
        if (!receipt.is_object() || receipt.value("schema", std::string{}) !=
                "chronon3d.render-receipt.v1" ||
            !receipt.contains("identity") || !receipt["identity"].is_object() ||
            !receipt.contains("render") || !receipt["render"].is_object() ||
            !receipt.contains("output") || !receipt["output"].is_object() ||
            !receipt["output"].contains("color_space") ||
            !receipt["output"].contains("reference_white_nits")) {
            spdlog::error("Render receipt has no supported color contract: {}", receipt_path);
            return 1;
        }
        receipt_color_space = receipt["output"]["color_space"].get<std::string>();
        if (receipt["output"].contains("hdr_mastering_metadata")) {
            const auto metadata_status = receipt.contains("verification") &&
                    receipt["verification"].is_object()
                ? receipt["verification"].value(
                    "hdr_mastering_metadata", std::string{"missing"})
                : std::string{"missing"};
            if (metadata_status != "pass") {
                spdlog::error(
                    "Render receipt HDR mastering metadata verification is '{}': {}",
                    metadata_status, receipt_path);
                return 1;
            }
        }
        const auto frames = receipt["render"].value("frames", std::int64_t{-1});
        const auto receipt_first_frame = receipt["render"].value("first_frame", std::int64_t{0});
        if (requested_frame_index_start < -1 || frames <= 0 || receipt_first_frame < 0 ||
            frames > std::numeric_limits<std::int64_t>::max() - receipt_first_frame) {
            spdlog::error("Render receipt contains an invalid frame range");
            return 1;
        }
        const auto receipt_last_exclusive = receipt_first_frame + frames;
        const bool has_frame_hashes = receipt["output"].contains("frame_images") &&
            receipt["output"]["frame_images"].is_array() &&
            !receipt["output"]["frame_images"].empty();
        if (receipt["output"].contains("frame_images") &&
            !receipt["output"]["frame_images"].is_array()) {
            spdlog::error("Render receipt frame image hashes are malformed");
            return 1;
        }
        if (receipt.contains("verification") && receipt["verification"].is_object() &&
            receipt["verification"].value("frame_images", std::string{}) == "pass" &&
            !has_frame_hashes) {
            spdlog::error("Render receipt claims frame image verification but has no frame hashes");
            return 1;
        }
        if (has_frame_hashes) {
            const auto& frame_images = receipt["output"]["frame_images"];
            if (frame_images.size() != static_cast<std::size_t>(frames)) {
                spdlog::error("Render receipt frame image hash count does not match its frame range");
                return 1;
            }
            for (std::int64_t offset = 0; offset < frames; ++offset) {
                const auto& item = frame_images[static_cast<std::size_t>(offset)];
                if (!item.is_object() ||
                    item.value("frame_index", std::int64_t{-1}) != receipt_first_frame + offset ||
                    item.value("bytes", std::int64_t{-1}) <= 0 ||
                    item.value("sha256", std::string{}).size() != 64) {
                    spdlog::error("Render receipt contains an invalid frame image hash entry");
                    return 1;
                }
            }
        }
        frame_index_start = requested_frame_index_start < 0
            ? receipt_first_frame : requested_frame_index_start;
        if (frame_index_start < receipt_first_frame ||
            frame_index_start > receipt_last_exclusive ||
            inputs.size() > static_cast<std::uint64_t>(receipt_last_exclusive - frame_index_start)) {
            spdlog::error("Scope frame range does not fit the render receipt frame count");
            return 1;
        }
        const auto& identity = receipt["identity"];
        report["render_receipt"] = {
            {"path", std::filesystem::path(receipt_path).lexically_normal().string()},
            {"job_id", identity.value("job_id", std::string{})},
            {"render_plan_schema", identity.value("render_plan_schema", std::string{})},
            {"content_digest", identity.value("content_digest", std::string{})},
            {"request_digest", identity.value("request_digest", std::string{})},
            {"asset_manifest_digest", identity.value("asset_manifest_digest", std::string{})},
            {"render", receipt["render"]},
            {"output_color_space", receipt_color_space},
            {"reference_white_nits", receipt["output"]["reference_white_nits"]},
            {"ocio_display", receipt["output"].value("ocio_display", std::string{})},
            {"ocio_view", receipt["output"].value("ocio_view", std::string{})},
            {"hdr_mastering_metadata",
                receipt["output"].value("hdr_mastering_metadata", json(nullptr))},
            {"hdr_mastering_metadata_verification",
                receipt.contains("verification") && receipt["verification"].is_object()
                    ? receipt["verification"].value(
                        "hdr_mastering_metadata", std::string{"skip"})
                    : std::string{"skip"}},
            {"frame_association", has_frame_hashes
                ? "per_frame_sha256_and_frame_index_verified_against_receipt"
                : "caller_asserted_order_checked_against_receipt_range_and_dimensions"},
        };
        report["render_receipt"]["scope_frame_index_start"] = frame_index_start;
    } else if (requested_frame_index_start >= 0) {
        spdlog::error("--frame-index-start requires --render-receipt");
        return 1;
    }

    const bool contains_video = std::any_of(inputs.begin(), inputs.end(), is_video_input);
    const bool complete_video_scan = !contains_video || video_frame_step == 1;
    if (!receipt_path.empty() && contains_video && inputs.size() != 1) {
        spdlog::error("A render receipt can be cryptographically matched only when one video file is analyzed");
        return 1;
    }

    for (std::size_t input_index = 0; input_index < inputs.size(); ++input_index) {
        const auto& input = inputs[input_index];
        if (is_video_input(input)) {
#if defined(CHRONON3D_ENABLE_NATIVE_FFMPEG)
            if (input_transfer_override != "auto") {
                spdlog::error("Video scopes use the embedded stream color metadata; --input-transfer applies only to PNG/JPEG");
                return 1;
            }
            const auto probe = media::video::probe_media(input);
            if (!probe) {
                spdlog::error("Could not probe scope video '{}': {}", input,
                              probe.error().message);
                return 1;
            }
            const auto stream_it = std::find_if(
                probe->streams.begin(), probe->streams.end(), [](const auto& stream) {
                    return stream.kind == media::video::MediaStreamKind::Video;
                });
            if (stream_it == probe->streams.end()) {
                spdlog::error("Scope video has no video stream: {}", input);
                return 1;
            }
            const auto expected_width = receipt_path.empty()
                ? stream_it->width : receipt["render"].value("width", 0);
            const auto expected_height = receipt_path.empty()
                ? stream_it->height : receipt["render"].value("height", 0);
            if (stream_it->width != expected_width || stream_it->height != expected_height) {
                spdlog::error("Scope video dimensions do not match its render receipt: {}", input);
                return 1;
            }
            media::NativeVideoFrameDecoder decoder;
            decoder.set_software_only(true);
            const auto sample_count = decoder.indexed_sample_count(input);
            if (sample_count == 0 ||
                sample_count > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                spdlog::error("Scope video decoder could not build a bounded presentation sample index: {}", input);
                return 1;
            }
            if (!receipt_path.empty()) {
                const auto receipt_frames = receipt["render"].value("frames", std::int64_t{-1});
                if (receipt_frames != static_cast<std::int64_t>(sample_count)) {
                    spdlog::error("Scope video frame count does not match its render receipt: {}", input);
                    return 1;
                }
                const auto digest = assets::sha256_file(input);
                const auto expected_digest = receipt["output"].value("sha256", std::string{});
                if (!digest || expected_digest.empty() || digest->hex() != expected_digest) {
                    spdlog::error("Scope video SHA-256 does not match the render receipt: {}", input);
                    return 1;
                }
                report["render_receipt"]["frame_association"] =
                    "video_sha256_and_frame_count_verified_against_receipt";
            }

            report["video_sampling"] = {
                {"source_frame_step", video_frame_step},
                {"frame_count", sample_count},
                {"selection", "source_sample_table_presentation_order"},
                {"source_time_base_num", stream_it->time_base.numerator},
                {"source_time_base_den", stream_it->time_base.denominator},
            };
            const auto transfer_name = stream_it->color_transfer;
            const auto scope_encoding = transfer_name == "smpte2084"
                ? color::ScopeEncoding::PQ
                : (transfer_name == "arib-std-b67"
                    ? color::ScopeEncoding::HLG
                    : color::ScopeEncoding::LinearSRGB);
            const auto scope_primaries =
                (transfer_name == "smpte2084" || transfer_name == "arib-std-b67" ||
                 stream_it->color_primaries == "bt2020")
                ? color::ScopePrimaries::BT2020 : color::ScopePrimaries::BT709;
            if (!receipt_path.empty()) {
                const auto expected_transfer = receipt_color_space == "srgb"
                    ? "iec61966-2-1"
                    : (receipt_color_space == "rec709" ? "bt709"
                        : (receipt_color_space == "pq" ? "smpte2084"
                            : (receipt_color_space == "hlg" ? "arib-std-b67" : "")));
                const auto expected_primaries =
                    (receipt_color_space == "pq" || receipt_color_space == "hlg")
                    ? "bt2020" : "bt709";
                const auto expected_matrix =
                    (receipt_color_space == "pq" || receipt_color_space == "hlg")
                    ? "bt2020nc" : "bt709";
                if (expected_transfer[0] == '\0' ||
                    stream_it->color_transfer != expected_transfer ||
                    stream_it->color_primaries != expected_primaries ||
                    stream_it->color_space != expected_matrix ||
                    stream_it->color_range != "tv") {
                    spdlog::error(
                        "Scope video color metadata does not match RenderPlan receipt output '{}' "
                        "(transfer={}, primaries={}, matrix={}, range={})",
                        receipt_color_space, stream_it->color_transfer,
                        stream_it->color_primaries, stream_it->color_space,
                        stream_it->color_range);
                    return 1;
                }
            }
            report["video_sampling"]["scope_signal_encoding"] =
                transfer_name == "smpte2084" ? "smpte2084_pq"
                : (transfer_name == "arib-std-b67" ? "arib_std_b67_hlg"
                    : (transfer_name == "iec61966-2-1"
                        ? "srgb_iec61966_2_1" : "rec709"));
            report["video_sampling"]["scope_primaries"] =
                scope_primaries == color::ScopePrimaries::BT2020 ? "BT.2020" : "BT.709";
            double sampled_peak_pixel_nits = 0.0;
            double sampled_max_frame_average_nits = 0.0;
            std::uint64_t measured_pq_frames = 0;
            for (std::int64_t frame_index = 0;
                 static_cast<std::uint64_t>(frame_index) < sample_count;) {
                auto decoded = decoder.decode_frame_index_at(
                    input, frame_index, stream_it->width, stream_it->height);
                const auto* frame = media::decoded_frame_if(decoded);
                if (frame == nullptr || !frame->framebuffer) {
                    const auto* failure = media::decode_failure_if(decoded);
                    spdlog::error("Could not decode scope video '{}' frame {}{}{}", input,
                        frame_index,
                        failure ? ": " : "",
                        failure ? failure->diagnostic.message : std::string{});
                    return 1;
                }
                json source_metadata = {
                    {"source_frame_index", frame_index},
                    {"source_pts", frame->pts},
                    {"source_dts", frame->dts},
                    {"source_order", frame->source_order},
                    {"source_time_base_num", stream_it->time_base.numerator},
                    {"source_time_base_den", stream_it->time_base.denominator},
                    {"source_color_range", stream_it->color_range},
                    {"source_color_space", stream_it->color_space},
                    {"source_color_transfer", stream_it->color_transfer},
                    {"source_color_primaries", stream_it->color_primaries},
                };
                if (scope_encoding == color::ScopeEncoding::PQ) {
                    const auto [peak_pixel_nits, frame_average_nits] =
                        measure_pq_luminance_nits(*frame->framebuffer);
                    source_metadata["pq_luminance_measurements_nits"] = {
                        {"peak_pixel", peak_pixel_nits},
                        {"frame_average", frame_average_nits},
                        {"meaning", "measured_from_decoded_pq_signal_not_mastering_metadata"}};
                    sampled_peak_pixel_nits = std::max(
                        sampled_peak_pixel_nits, peak_pixel_nits);
                    sampled_max_frame_average_nits = std::max(
                        sampled_max_frame_average_nits, frame_average_nits);
                    ++measured_pq_frames;
                }
            const auto render_index = receipt_path.empty()
                    ? std::nullopt
                    : std::optional<std::int64_t>{frame_index_start + frame_index};
                report["frames"].push_back(scope_frame_report(
                    *frame->framebuffer, input, false, "native_linear", render_index,
                    transfer_name == "iec61966-2-1"
                        ? color::ScopeEncoding::LinearSRGBTransfer : scope_encoding,
                    scope_primaries,
                    waveform_width, waveform_height, vectorscope_size, waveform_signal,
                    signal_min, signal_max, std::move(source_metadata)));
                if (sample_count - static_cast<std::uint64_t>(frame_index) <= video_frame_step) break;
                frame_index += video_frame_step;
            }
            if (scope_encoding == color::ScopeEncoding::PQ) {
                report["video_sampling"]["pq_luminance_measurements"] = {
                    {"sampled_frame_count", measured_pq_frames},
                    {"sampled_frame_step", video_frame_step},
                    {"maximum_sampled_pixel_nits", sampled_peak_pixel_nits},
                    {"maximum_sampled_frame_average_nits", sampled_max_frame_average_nits},
                    {"complete_content_maximum", video_frame_step == 1},
                    {"measurement_basis", "decoded_absolute_pq_luminance"}};
            }
            continue;
#else
            spdlog::error("Video scopes require a build with CHRONON3D_ENABLE_NATIVE_FFMPEG: {}", input);
            return 1;
#endif
        }
        if (!supported_scope_image(input)) {
            spdlog::error(
                "Scope input '{}' is unsupported: use a supported video, 8-bit PNG/JPEG (sRGB, Rec.709, Display P3 or linear via --input-transfer)"
#ifdef CHRONON3D_ENABLE_EXR
                " or RGBA scanline EXR interpreted as linear BT.709"
#else
                "; EXR requires a build with CHRONON3D_ENABLE_EXR"
#endif
                ,
                input);
            return 1;
        }
        auto framebuffer = load_scope_image(input);
        if (!framebuffer) {
            spdlog::error("Could not decode scope input image: {}", input);
            return 1;
        }
        if (!receipt_path.empty() && receipt.contains("output") &&
            receipt["output"].contains("frame_images") &&
            receipt["output"]["frame_images"].is_array() &&
            !receipt["output"]["frame_images"].empty()) {
            const auto frame_index = frame_index_start + static_cast<std::int64_t>(input_index);
            const auto receipt_first_frame = receipt["render"].value("first_frame", std::int64_t{0});
            const auto offset = static_cast<std::size_t>(frame_index - receipt_first_frame);
            const auto& expected = receipt["output"]["frame_images"][offset];
            std::error_code size_error;
            const auto actual_bytes = std::filesystem::file_size(input, size_error);
            const auto actual_digest = assets::sha256_file(input);
            if (size_error || actual_bytes != static_cast<std::uint64_t>(expected["bytes"].get<std::int64_t>()) ||
                !actual_digest || actual_digest->hex() != expected["sha256"].get<std::string>()) {
                spdlog::error("Scope image bytes or SHA-256 do not match render receipt frame {}: {}",
                              frame_index, input);
                return 1;
            }
        }
        const bool exr_input = is_exr_input(input);
        json source_metadata = json::object();
        if (exr_input) {
            const auto white_luminance = image::read_exr_white_luminance_nits(input);
            source_metadata["exr_white_luminance_nits"] = white_luminance
                ? json(*white_luminance) : json(nullptr);
            source_metadata["exr_white_luminance_status"] = white_luminance
                ? "present_and_valid" : "not_declared";
            if (!receipt_path.empty()) {
                const float expected = receipt["output"]["reference_white_nits"].get<float>();
                if (!white_luminance) {
                    spdlog::error(
                        "EXR scope input has no whiteLuminance attribute required by its render receipt: {}",
                        input);
                    return 1;
                }
                const float tolerance = std::max(0.01f, expected * 1.0e-4f);
                const float difference = std::abs(*white_luminance - expected);
                if (difference > tolerance) {
                    spdlog::error(
                        "EXR whiteLuminance {} nits differs from render receipt reference white {} nits: {}",
                        *white_luminance, expected, input);
                    return 1;
                }
                source_metadata["exr_white_luminance_status"] = "verified_against_render_receipt";
                source_metadata["exr_white_luminance_difference_nits"] = difference;
                source_metadata["exr_white_luminance_tolerance_nits"] = tolerance;
            }
        }
        if (!receipt_path.empty() &&
            static_cast<int>(framebuffer->width()) != receipt["render"].value("width", 0)) {
            spdlog::error("Scope input width does not match render receipt: {}", input);
            return 1;
        }
        if (!receipt_path.empty() &&
            static_cast<int>(framebuffer->height()) != receipt["render"].value("height", 0)) {
            spdlog::error("Scope input height does not match render receipt: {}", input);
            return 1;
        }
        std::string input_transfer = input_transfer_override;
        if (input_transfer == "auto") {
            if (exr_input) {
                input_transfer = "linearsrgb";
            } else if (!receipt_path.empty() &&
                (receipt_color_space == "srgb" || receipt_color_space == "rec709"))
                input_transfer = receipt_color_space;
            else
                input_transfer = "srgb";
        }
        if (exr_input && input_transfer != "linearsrgb") {
            spdlog::error("EXR scope inputs are already linear BT.709; interpretation must be linearsrgb: {}", input);
            return 1;
        }
        if (!exr_input && !receipt_path.empty() &&
            receipt_color_space != input_transfer) {
            spdlog::error("Scope input interpretation '{}' disagrees with render receipt output '{}'",
                          input_transfer, receipt_color_space);
            return 1;
        }
        if (!exr_input && input_transfer == "rec709") {
            // The image loader has already decoded PNG/JPEG samples as sRGB
            // into premultiplied linear RGB. Recover the encoded code values,
            // decode those as Rec.709, then continue the scope calculation in
            // Chronon's linear working space.
            for (i32 y = 0; y < framebuffer->height(); ++y) {
                for (i32 x = 0; x < framebuffer->width(); ++x) {
                    auto pixel = framebuffer->get_pixel(x, y);
                    const auto straight = pixel.unpremultiplied();
                    pixel.r = color::rec709_to_linear_float(
                        color::linear_srgb_float(straight.r)) * pixel.a;
                    pixel.g = color::rec709_to_linear_float(
                        color::linear_srgb_float(straight.g)) * pixel.a;
                    pixel.b = color::rec709_to_linear_float(
                        color::linear_srgb_float(straight.b)) * pixel.a;
                    framebuffer->set_pixel(x, y, pixel);
                }
            }
        } else if (!exr_input && input_transfer == "displayp3") {
            for (i32 y = 0; y < framebuffer->height(); ++y) {
                for (i32 x = 0; x < framebuffer->width(); ++x) {
                    auto pixel = framebuffer->get_pixel(x, y);
                    const auto straight = pixel.unpremultiplied();
                    const auto converted = color::display_p3_to_bt709_linear(
                        straight.r, straight.g, straight.b);
                    pixel.r = converted[0] * pixel.a;
                    pixel.g = converted[1] * pixel.a;
                    pixel.b = converted[2] * pixel.a;
                    framebuffer->set_pixel(x, y, pixel);
                }
            }
        } else if (!exr_input && input_transfer == "linearsrgb") {
            // The still-image loader assumes byte-backed images are sRGB.
            // Recover the encoded byte-domain values and treat them as linear.
            for (i32 y = 0; y < framebuffer->height(); ++y) {
                for (i32 x = 0; x < framebuffer->width(); ++x) {
                    auto pixel = framebuffer->get_pixel(x, y);
                    const auto straight = pixel.unpremultiplied();
                    pixel.r = color::linear_srgb_float(straight.r) * pixel.a;
                    pixel.g = color::linear_srgb_float(straight.g) * pixel.a;
                    pixel.b = color::linear_srgb_float(straight.b) * pixel.a;
                    framebuffer->set_pixel(x, y, pixel);
                }
            }
        }
        report["frames"].push_back(scope_frame_report(
            *framebuffer, input, exr_input, input_transfer,
            receipt_path.empty() ? std::nullopt
                : std::optional<std::int64_t>{frame_index_start + static_cast<std::int64_t>(input_index)},
            !exr_input && input_transfer == "srgb"
                ? color::ScopeEncoding::LinearSRGBTransfer
                : color::ScopeEncoding::LinearSRGB,
            color::ScopePrimaries::BT709,
            waveform_width, waveform_height, vectorscope_size, waveform_signal,
            signal_min, signal_max, std::move(source_metadata)));
    }

    const bool frame_gates_requested = quality_gates.max_clipped_fraction ||
        quality_gates.max_clipped_chroma_fraction ||
        quality_gates.max_non_finite_fraction ||
        quality_gates.min_opaque_fraction ||
        quality_gates.max_transparent_fraction;
    const bool any_gate_requested = frame_gates_requested ||
        quality_gates.max_pq_peak_nits ||
        quality_gates.max_pq_frame_average_nits;
    json gate_report = {
        {"status", any_gate_requested ? "pass" : "not_requested"},
        {"source_coverage_complete", complete_video_scan},
        {"per_frame", json::array()},
    };
    const auto check_fraction = [](double value, const std::optional<double>& limit,
                                   bool minimum) {
        return !limit || (minimum ? value >= *limit : value <= *limit);
    };
    for (std::size_t index = 0; index < report["frames"].size(); ++index) {
        const auto& frame = report["frames"][index];
        const auto& waveform = frame["waveform"];
        const auto& vectorscope = frame["vectorscope"];
        const auto& alpha = waveform["alpha_coverage"];
        const auto samples = alpha.value("samples", std::uint64_t{0});
        const auto opaque = alpha.value("fully_opaque", std::uint64_t{0});
        const auto transparent = alpha.value("fully_transparent", std::uint64_t{0});
        const auto channels = waveform.value("channels", std::uint32_t{1});
        const auto clipped = waveform.value("clipped_above", std::uint64_t{0}) +
                             waveform.value("clipped_below", std::uint64_t{0});
        const auto pixel_samples = static_cast<std::uint64_t>(
            frame.value("width", 0u)) * frame.value("height", 0u);
        const auto ignored_waveform_samples = waveform.value("ignored_non_finite", std::uint64_t{0}) +
            (frame.value("waveform_signal", std::string{"luma"}) == "alpha"
                ? 0 : waveform.value("ignored_transparent", std::uint64_t{0}));
        const auto valid_signal_samples = frame.value(
            "waveform_signal", std::string{"luma"}) == "alpha"
            ? samples
            : (pixel_samples > ignored_waveform_samples
                ? pixel_samples - ignored_waveform_samples : 0);
        const double denominator = static_cast<double>(valid_signal_samples) * channels;
        const double clipped_fraction = denominator > 0.0 ? clipped / denominator : 0.0;
        const auto non_finite_samples = waveform.value("ignored_non_finite", std::uint64_t{0});
        const double non_finite_fraction = pixel_samples > 0
            ? static_cast<double>(non_finite_samples) / static_cast<double>(pixel_samples)
            : 1.0;
        const auto ignored_chroma = vectorscope.value("ignored_non_finite", std::uint64_t{0}) +
            vectorscope.value("ignored_transparent", std::uint64_t{0});
        const auto valid_chroma_samples = pixel_samples > ignored_chroma
            ? pixel_samples - ignored_chroma : 0;
        const double clipped_chroma_fraction = valid_chroma_samples > 0
            ? static_cast<double>(vectorscope.value("clipped_chroma", std::uint64_t{0})) /
                static_cast<double>(valid_chroma_samples)
            : 0.0;
        const double opaque_fraction = samples > 0
            ? static_cast<double>(opaque) / static_cast<double>(samples) : 0.0;
        const double transparent_fraction = samples > 0
            ? static_cast<double>(transparent) / static_cast<double>(samples) : 0.0;
        const bool clipping_gate_pass = !quality_gates.max_clipped_fraction ||
            (denominator > 0.0 && clipped_fraction <=
                *quality_gates.max_clipped_fraction);
        const bool pass = complete_video_scan && clipping_gate_pass &&
            check_fraction(non_finite_fraction,
                quality_gates.max_non_finite_fraction, false) &&
            (!quality_gates.max_clipped_chroma_fraction ||
                (valid_chroma_samples > 0 && clipped_chroma_fraction <=
                    *quality_gates.max_clipped_chroma_fraction)) &&
            check_fraction(opaque_fraction, quality_gates.min_opaque_fraction, true) &&
            check_fraction(transparent_fraction, quality_gates.max_transparent_fraction, false);
        gate_report["per_frame"].push_back({
            {"frame_index", frame.value("render_frame_index", json(nullptr))},
            {"input", frame.value("input", std::string{})},
            {"valid_signal_samples", valid_signal_samples},
            {"clipped_signal_samples", clipped},
            {"clipped_fraction", clipped_fraction},
            {"non_finite_samples", non_finite_samples},
            {"non_finite_fraction", non_finite_fraction},
            {"valid_chroma_samples", valid_chroma_samples},
            {"clipped_chroma_fraction", clipped_chroma_fraction},
            {"source_coverage_complete", complete_video_scan},
            {"opaque_fraction", opaque_fraction},
            {"transparent_fraction", transparent_fraction},
            {"status", !frame_gates_requested ? "not_evaluated"
                : (pass ? "pass" : "fail")},
        });
        if (frame_gates_requested && !pass) gate_report["status"] = "fail";
    }
    if (quality_gates.max_pq_peak_nits ||
        quality_gates.max_pq_frame_average_nits) {
        double measured_peak_nits = 0.0;
        double measured_frame_average_nits = 0.0;
        std::uint64_t measured_frames = 0;
        for (const auto& frame : report["frames"]) {
            const auto& source = frame;
            if (!source.contains("pq_luminance_measurements_nits") ||
                !source["pq_luminance_measurements_nits"].is_object()) continue;
            const auto& measurement = source["pq_luminance_measurements_nits"];
            measured_peak_nits = std::max(measured_peak_nits,
                measurement.value("peak_pixel", 0.0));
            measured_frame_average_nits = std::max(measured_frame_average_nits,
                measurement.value("frame_average", 0.0));
            ++measured_frames;
        }
        bool coverage_complete = inputs.size() == 1 && is_video_input(inputs.front()) &&
                                 measured_frames > 0 && video_frame_step == 1;
        if (coverage_complete && report.contains("video_sampling")) {
            const auto expected = report["video_sampling"].value("frame_count", std::uint64_t{0});
            coverage_complete = expected == measured_frames;
        } else {
            coverage_complete = false;
        }
        const bool peak_pass = !quality_gates.max_pq_peak_nits ||
            (coverage_complete && measured_peak_nits <= *quality_gates.max_pq_peak_nits);
        const bool average_pass = !quality_gates.max_pq_frame_average_nits ||
            (coverage_complete && measured_frame_average_nits <=
                *quality_gates.max_pq_frame_average_nits);
        const bool hdr_gate_pass = peak_pass && average_pass;
        gate_report["pq_luminance"] = {
            {"status", hdr_gate_pass ? "pass" : "fail"},
            {"coverage_complete", coverage_complete},
            {"measured_frame_count", measured_frames},
            {"maximum_sampled_peak_pixel_nits", measured_peak_nits},
            {"maximum_sampled_frame_average_nits", measured_frame_average_nits},
            {"max_peak_pixel_nits", quality_gates.max_pq_peak_nits
                ? json(*quality_gates.max_pq_peak_nits) : json(nullptr)},
            {"max_frame_average_nits", quality_gates.max_pq_frame_average_nits
                ? json(*quality_gates.max_pq_frame_average_nits) : json(nullptr)},
        };
        if (!hdr_gate_pass) gate_report["status"] = "fail";
    }
    gate_report["thresholds"] = {
        {"max_clipped_fraction", quality_gates.max_clipped_fraction
            ? json(*quality_gates.max_clipped_fraction) : json(nullptr)},
        {"max_clipped_chroma_fraction", quality_gates.max_clipped_chroma_fraction
            ? json(*quality_gates.max_clipped_chroma_fraction) : json(nullptr)},
        {"max_non_finite_fraction", quality_gates.max_non_finite_fraction
            ? json(*quality_gates.max_non_finite_fraction) : json(nullptr)},
        {"min_opaque_fraction", quality_gates.min_opaque_fraction
            ? json(*quality_gates.min_opaque_fraction) : json(nullptr)},
        {"max_transparent_fraction", quality_gates.max_transparent_fraction
            ? json(*quality_gates.max_transparent_fraction) : json(nullptr)},
        {"max_pq_peak_nits", quality_gates.max_pq_peak_nits
            ? json(*quality_gates.max_pq_peak_nits) : json(nullptr)},
        {"max_pq_frame_average_nits", quality_gates.max_pq_frame_average_nits
            ? json(*quality_gates.max_pq_frame_average_nits) : json(nullptr)},
    };
    report["quality_gates"] = std::move(gate_report);
    const bool gates_passed = report["quality_gates"]["status"] != "fail";

    const auto serialized = report.dump(2) + "\n";
    if (output_path.empty()) {
        std::cout << serialized;
        return std::cout.good() ? (gates_passed ? 0 : 2) : 1;
    }
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        spdlog::error("Could not open scope report for writing: {}", output_path);
        return 1;
    }
    output << serialized;
    if (!output.good()) {
        spdlog::error("Could not write scope report: {}", output_path);
        return 1;
    }
    return gates_passed ? 0 : 2;
}

} // namespace

void register_scope_analysis_command(CLI::App& app, CliContext& ctx) {
    struct State {
        std::vector<std::string> inputs;
        std::string output;
        std::uint32_t waveform_width{0};
        std::uint32_t waveform_height{256};
        std::uint32_t vectorscope_size{256};
        std::string waveform_signal{"luma"};
        float signal_min{0.0f};
        float signal_max{1.0f};
        std::string input_transfer{"auto"};
        std::string render_receipt;
        bool require_render_receipt{false};
        std::int64_t frame_index_start{-1};
        std::uint32_t video_frame_step{1};
        ScopeQualityGates quality_gates;
    };
    auto state = std::make_shared<State>();
    auto* command = app.add_subcommand(
        "scopes", "Analyze rendered video or image frames and emit machine-readable waveform/vectorscope data");
    command->add_option("inputs", state->inputs,
                        "Rendered video files or still images supported by the image loader")
        ->required();
    command->add_option("-o,--output", state->output,
                        "Write JSON to this file (default: stdout)");
    command->add_option("--waveform-width", state->waveform_width,
                        "Waveform columns (0 uses the source width)")
        ->check(CLI::Range(0u, 16384u));
    command->add_option("--waveform-height", state->waveform_height,
                        "Waveform signal bins")
        ->check(CLI::Range(2u, 4096u));
    command->add_option("--waveform-signal", state->waveform_signal,
                        "Waveform channels: luma, RGB parade, or alpha coverage")
        ->check(CLI::IsMember(std::set<std::string>{"luma", "rgb", "alpha"}));
    command->add_option("--vectorscope-size", state->vectorscope_size,
                        "Vectorscope bins per axis")
        ->check(CLI::Range(16u, 2048u));
    command->add_option("--signal-min", state->signal_min,
                        "Waveform signal range minimum (linear input is Rec.709 encoded)");
    command->add_option("--signal-max", state->signal_max,
                        "Waveform signal range maximum; raise it to inspect highlight headroom");
    command->add_option("--input-transfer", state->input_transfer,
                        "PNG/JPEG interpretation: auto from receipt (otherwise srgb), srgb, rec709, displayp3, or linearsrgb; EXR is linearsrgb")
        ->check(CLI::IsMember(std::set<std::string>{
            "auto", "srgb", "rec709", "displayp3", "linearsrgb"}));
    command->add_option("--render-receipt", state->render_receipt,
                        "Associate inputs with this RenderPlan receipt; checks frame range, dimensions and declared transfer");
    command->add_flag("--require-render-receipt", state->require_render_receipt,
                      "Fail before analysis unless --render-receipt binds the inputs to a rendered artifact");
    command->add_option("--frame-index-start", state->frame_index_start,
                        "RenderPlan frame index for the first input (default: receipt first_frame)")
        ->check(CLI::Range(std::int64_t{-1}, std::numeric_limits<std::int64_t>::max()));
    command->add_option("--video-frame-step", state->video_frame_step,
                        "Analyze every Nth source video frame (default: every frame)")
        ->check(CLI::Range(1u, 100000u));
    command->add_option("--max-clipped-fraction", state->quality_gates.max_clipped_fraction,
                        "Fail with exit 2 when a frame's waveform clipping fraction exceeds this value")
        ->check(CLI::Range(0.0, 1.0));
    command->add_option("--max-clipped-chroma-fraction",
                        state->quality_gates.max_clipped_chroma_fraction,
                        "Fail with exit 2 when the vectorscope clips more than this fraction of valid chroma samples")
        ->check(CLI::Range(0.0, 1.0));
    command->add_option("--max-non-finite-fraction",
                        state->quality_gates.max_non_finite_fraction,
                        "Fail with exit 2 when the fraction of non-finite waveform pixels exceeds this value")
        ->check(CLI::Range(0.0, 1.0));
    command->add_option("--min-opaque-fraction", state->quality_gates.min_opaque_fraction,
                        "Fail with exit 2 when a frame's fully opaque alpha fraction is below this value")
        ->check(CLI::Range(0.0, 1.0));
    command->add_option("--max-transparent-fraction", state->quality_gates.max_transparent_fraction,
                        "Fail with exit 2 when a frame's fully transparent alpha fraction exceeds this value")
        ->check(CLI::Range(0.0, 1.0));
    command->add_option("--max-pq-peak-nits", state->quality_gates.max_pq_peak_nits,
                        "Require a complete single-video PQ scan and fail if any decoded pixel exceeds this luminance")
        ->check(CLI::Range(0.0, 10000.0));
    command->add_option("--max-pq-frame-average-nits", state->quality_gates.max_pq_frame_average_nits,
                        "Require a complete single-video PQ scan and fail if any frame average exceeds this luminance")
        ->check(CLI::Range(0.0, 10000.0));
    command->callback([state, &ctx]() {
        try {
            ctx.exit_code = analyze_images(
                state->inputs, state->output, state->waveform_width,
                state->waveform_height, state->vectorscope_size,
                state->waveform_signal == "rgb"
                    ? color::WaveformSignal::RGB
                    : (state->waveform_signal == "alpha"
                        ? color::WaveformSignal::Alpha
                        : color::WaveformSignal::Luma),
                state->signal_min, state->signal_max,
                state->input_transfer, state->render_receipt,
                state->require_render_receipt,
                state->frame_index_start, state->video_frame_step,
                state->quality_gates);
            if (ctx.exit_code != 0) spdlog::error("Scope analysis failed");
        } catch (const std::exception& error) {
            spdlog::error("Scope analysis failed: {}", error.what());
            ctx.exit_code = 1;
        }
    });
}

} // namespace chronon3d::cli
