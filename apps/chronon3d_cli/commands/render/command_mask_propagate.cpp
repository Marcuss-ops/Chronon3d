#include "../../cli_context.hpp"

#include <chronon3d/backends/image/image_loader.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/color/output_transform.hpp>
#include <chronon3d/effects/keying_matte.hpp>
#include <chronon3d/media/decode_result.hpp>
#include <chronon3d/media/tracking/temporal_mask_propagator.hpp>
#include <chronon3d/media/video/media_probe.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/media/video/video_sink_factory.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace chronon3d::cli {
namespace {

using media::tracking::PropagatedMask;
using media::tracking::SoftMaskView;
using media::tracking::TemporalFlowOptions;
using media::tracking::TemporalMaskPropagator;
using media::tracking::TrackingFrameView;

class TemporaryMaskSpool final {
public:
    TemporaryMaskSpool() {
        static std::atomic<std::uint64_t> sequence{0};
        const auto root = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 32; ++attempt) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path_ = root / ("chronon-mask-spool-" + std::to_string(stamp) + "-" +
                std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) return;
            if (error && error != std::errc::file_exists)
                throw std::runtime_error("could not create temporary mask spool: " + error.message());
        }
        throw std::runtime_error("could not allocate a unique temporary mask spool directory");
    }
    ~TemporaryMaskSpool() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    TemporaryMaskSpool(const TemporaryMaskSpool&) = delete;
    TemporaryMaskSpool& operator=(const TemporaryMaskSpool&) = delete;

    [[nodiscard]] std::string frame_path(std::size_t index, bool confidence) const {
        return (path_ / (std::string(confidence ? "confidence-" : "mask-") +
            std::to_string(index) + ".png")).string();
    }
    [[nodiscard]] std::string source_frame_path(std::size_t index) const {
        return (path_ / ("source-" + std::to_string(index) + ".chrmaskframe")).string();
    }
    [[nodiscard]] std::string staged_output_path(std::size_t index,
                                                bool confidence) const {
        return (path_ / (std::string(confidence ? "output-confidence-" :
                                                   "output-mask-") +
                         std::to_string(index) + ".png")).string();
    }

private:
    std::filesystem::path path_;
};

struct MaskPropagationState {
    std::vector<std::string> frames;
    std::string source_video;
    std::string source_video_transfer;
    std::string source_video_sha256;
    std::vector<RationalTime> source_presentation_times;
    std::string seed_mask;
    std::string output_pattern{"mask_####.png"};
    std::string input_transfer{"srgb"};
    std::size_t seed_frame{0};
    float minimum_confidence{0.12f};
    float minimum_mean_confidence{0.0f};
    float minimum_mean_foreground_confidence{0.0f};
    float maximum_foreground_loss{1.0f};
    std::uint64_t maximum_total_work_units{100'000'000'000ULL};
    std::uint64_t maximum_video_spool_bytes{8ULL * 1024ULL * 1024ULL * 1024ULL};
    std::uint32_t refinement_radius{0};
    float refinement_epsilon{1.0e-3f};
    std::string plan_input;
    std::string plan_output;
    std::string target_layer;
    std::string matte_video;
    bool encode_matte_video{false};
    std::string matte_layer_id;
    std::string matte_type{"luma"};
    std::vector<std::string> correction_masks;
};

void decode_sdr_video_frames(MaskPropagationState& state,
                             const TemporaryMaskSpool& spool) {
    if (state.source_video.empty()) return;
    if (!state.frames.empty())
        throw std::runtime_error("provide either ordered frame images or --video-input, not both");
#if defined(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    const auto video_digest = assets::sha256_file(state.source_video);
    if (!video_digest)
        throw std::runtime_error("could not hash tracking video input: " +
                                 state.source_video);
    state.source_video_sha256 = video_digest->hex();
    const auto probe = media::video::probe_media(state.source_video);
    if (!probe) throw std::runtime_error("could not probe video input: " + probe.error().message);
    const auto stream = std::find_if(probe->streams.begin(), probe->streams.end(),
        [](const auto& candidate) {
            return candidate.kind == media::video::MediaStreamKind::Video;
        });
    if (stream == probe->streams.end())
        throw std::runtime_error("video input has no video stream");
    if (stream->width <= 0 || stream->height <= 0 ||
        static_cast<std::uint64_t>(stream->width) *
            static_cast<std::uint64_t>(stream->height) > 8'000'000)
        throw std::runtime_error("video frame dimensions exceed the 8-megapixel tracking limit");
    const bool sdr_transfer = stream->color_transfer == "bt709" ||
                              stream->color_transfer == "iec61966-2-1";
    const bool hdr_transfer = stream->color_transfer == "smpte2084" ||
                              stream->color_transfer == "arib-std-b67";
    if (!sdr_transfer && !hdr_transfer)
        throw std::runtime_error(
            "direct mask tracking requires explicit BT.709/sRGB SDR, PQ, or HLG transfer metadata");
    const bool hdr_primaries = stream->color_primaries == "bt2020" &&
                               stream->color_space == "bt2020nc";
    const bool sdr_primaries = stream->color_primaries == "bt709" &&
                               stream->color_space == "bt709";
    if ((hdr_transfer && !hdr_primaries) || (sdr_transfer && !sdr_primaries) ||
        stream->color_range != "tv")
        throw std::runtime_error(
            "direct tracking requires HDR BT.2020 non-constant-luminance or SDR BT.709 primaries/matrix with limited range");
    state.source_video_transfer = stream->color_transfer;

    media::NativeVideoFrameDecoder decoder;
    decoder.set_software_only(true);
    const auto count = decoder.indexed_sample_count(state.source_video);
    if (count == 0 || count > 10'000)
        throw std::runtime_error("video input must contain between 1 and 10000 indexed frames");
    constexpr std::uint64_t kRawFrameHeaderBytes = 20;
    const auto frame_bytes = kRawFrameHeaderBytes +
        static_cast<std::uint64_t>(stream->width) *
        static_cast<std::uint64_t>(stream->height) * 4ULL * sizeof(float);
    if (frame_bytes != 0 && count > state.maximum_video_spool_bytes / frame_bytes)
        throw std::runtime_error(
            "estimated decoded video spool exceeds --maximum-video-spool-bytes");
    TemporalFlowOptions work_options;
    work_options.minimum_confidence = state.minimum_confidence;
    const auto pair_work = TemporalMaskPropagator::estimate_pair_work_units(
        static_cast<std::uint32_t>(stream->width),
        static_cast<std::uint32_t>(stream->height), work_options);
    const auto estimated_steps = count > std::numeric_limits<std::uint64_t>::max() / 2
        ? std::numeric_limits<std::uint64_t>::max() : count * 2;
    const auto estimated_work = pair_work != 0 &&
        estimated_steps > std::numeric_limits<std::uint64_t>::max() / pair_work
        ? std::numeric_limits<std::uint64_t>::max() : estimated_steps * pair_work;
    if (estimated_work > state.maximum_total_work_units)
        throw std::runtime_error(
            "estimated video tracking work exceeds --maximum-total-work-units");
    state.frames.reserve(static_cast<std::size_t>(count));
    state.source_presentation_times.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
        const auto presentation_time = decoder.indexed_presentation_time(
            state.source_video, index);
        if (!presentation_time)
            throw std::runtime_error("could not resolve exact presentation timestamp for source frame " +
                                     std::to_string(index));
        auto decoded = decoder.decode_frame_index_at(
            state.source_video, static_cast<std::int64_t>(index), stream->width, stream->height);
        const auto* frame = media::decoded_frame_if(decoded);
        if (!frame || !frame->framebuffer)
            throw std::runtime_error("could not decode tracking source frame " +
                                     std::to_string(index));
        const auto path = spool.source_frame_path(static_cast<std::size_t>(index));
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        constexpr std::array<char, 8> magic{'C', 'H', 'M', 'F', '3', '2', '\0', '\0'};
        const auto width = static_cast<std::uint32_t>(stream->width);
        const auto height = static_cast<std::uint32_t>(stream->height);
        const std::uint32_t channels = 4;
        output.write(magic.data(), static_cast<std::streamsize>(magic.size()));
        output.write(reinterpret_cast<const char*>(&width), sizeof(width));
        output.write(reinterpret_cast<const char*>(&height), sizeof(height));
        output.write(reinterpret_cast<const char*>(&channels), sizeof(channels));
        for (i32 y = 0; y < stream->height && output; ++y) {
            for (i32 x = 0; x < stream->width; ++x) {
                const auto pixel = frame->framebuffer->get_pixel(x, y);
                const std::array<float, 4> rgba{pixel.r, pixel.g, pixel.b, pixel.a};
                output.write(reinterpret_cast<const char*>(rgba.data()),
                             static_cast<std::streamsize>(sizeof(rgba)));
            }
        }
        output.flush();
        if (!output.good())
            throw std::runtime_error("could not spool linear tracking frame " +
                                     std::to_string(index));
        state.frames.push_back(path);
        state.source_presentation_times.push_back(*presentation_time);
    }
    state.input_transfer = "native_linear_bt709";
#else
    (void)spool;
    throw std::runtime_error("direct video mask tracking requires native FFmpeg support");
#endif
}

bool has_plan_patch_options(const MaskPropagationState& state) {
    return !state.plan_input.empty() || !state.plan_output.empty() ||
           !state.target_layer.empty() || !state.matte_video.empty() ||
           !state.matte_layer_id.empty() || state.encode_matte_video;
}

void validate_plan_patch_option_set(const MaskPropagationState& state) {
    if (!has_plan_patch_options(state)) return;
    if (state.plan_input.empty() || state.plan_output.empty() ||
        state.target_layer.empty() || state.matte_video.empty() ||
        state.matte_layer_id.empty()) {
        throw std::runtime_error(
            "RenderPlan integration requires --plan-input, --plan-output, "
            "--target-layer, --matte-video and --matte-layer-id together");
    }
    if (!std::filesystem::is_regular_file(state.plan_input))
        throw std::runtime_error("RenderPlan input file does not exist: " + state.plan_input);
    if (!state.encode_matte_video && !std::filesystem::is_regular_file(state.matte_video))
        throw std::runtime_error("matte video file does not exist: " + state.matte_video);
    if (state.encode_matte_video) {
        const auto container = media::video::classify_video_container(
            std::filesystem::path(state.matte_video).extension().string());
        if (!container || (*container != media::video::VideoContainer::Mp4 &&
                           *container != media::video::VideoContainer::Mkv))
            throw std::runtime_error("encoded matte video output must use .mp4 or .mkv");
    }
}

void attach_matte_video_to_render_plan(const MaskPropagationState& state) {
    validate_plan_patch_option_set(state);
    std::ifstream input(state.plan_input, std::ios::binary);
    if (!input) throw std::runtime_error("could not open RenderPlan: " + state.plan_input);
    nlohmann::json plan;
    input >> plan;
    if (!plan.is_object() || plan.value("schema", std::string{}) != "chronon.render-plan.v3")
        throw std::runtime_error("matte integration requires a chronon.render-plan.v3 document");
    if (!plan.contains("layers") || !plan["layers"].is_array())
        throw std::runtime_error("RenderPlan must contain a layers array");

    nlohmann::json* target = nullptr;
    bool matte_id_exists = false;
    for (auto& layer : plan["layers"]) {
        if (!layer.is_object()) continue;
        const auto id = layer.value("id", std::string{});
        if (id == state.target_layer) {
            if (target) throw std::runtime_error("target layer id is ambiguous: " + state.target_layer);
            target = &layer;
        }
        if (id == state.matte_layer_id) matte_id_exists = true;
    }
    if (!target) throw std::runtime_error("target layer was not found: " + state.target_layer);
    if (matte_id_exists) throw std::runtime_error("matte layer id already exists: " + state.matte_layer_id);
    if (target->contains("track_matte"))
        throw std::runtime_error("target layer already has a track_matte relation");
    if (state.matte_type != "luma" && state.matte_type != "luma_inverted")
        throw std::runtime_error("grayscale propagated matte video requires luma or luma_inverted mode");

    nlohmann::json matte = {
        {"id", state.matte_layer_id},
        {"type", "video"},
        {"asset", std::filesystem::absolute(state.matte_video).lexically_normal().string()},
    };
    // Match placement and temporal sampling so the matte is evaluated in the
    // same canvas coordinates and frame interval as its target. The render
    // graph recognizes referenced matte layers as source-only and omits them
    // from the visible composite.
    static constexpr const char* kSynchronizedFields[] = {
        "parent", "position", "size", "scale", "rotation",
        "start_frame", "duration_frames", "fit", "enable_3d", "screen_space",
        "depth_offset",
        "loop", "time_remap"};
    for (const char* field : kSynchronizedFields)
        if (target->contains(field)) matte[field] = target->at(field);

    // A matte source must follow the target's canvas transform over time.
    // Opacity and blur tracks intentionally stay on the target: applying them
    // to the matte source too would attenuate the track matte a second time.
    if (target->contains("animation") && target->at("animation").is_object() &&
        target->at("animation").contains("tracks") &&
        target->at("animation").at("tracks").is_array()) {
        static const std::set<std::string> transform_tracks{
            "position", "position_x", "position_y", "position_z",
            "scale", "scale_x", "scale_y", "scale_z",
            "rotation", "rotation_x", "rotation_y", "rotation_z"};
        nlohmann::json tracks = nlohmann::json::array();
        for (const auto& track : target->at("animation").at("tracks")) {
            if (track.is_object() && track.contains("property") &&
                track.at("property").is_string() &&
                transform_tracks.contains(track.at("property").get<std::string>()))
                tracks.push_back(track);
        }
        if (!tracks.empty()) matte["animation"] = {{"tracks", std::move(tracks)}};
    }

    (*target)["track_matte"] = {
        {"source", state.matte_layer_id}, {"type", state.matte_type}};
    plan["layers"].push_back(std::move(matte));

    const auto decoded_plan = render_plan::decode_render_plan(plan);
    if (!decoded_plan) {
        const auto& error = decoded_plan.error();
        throw std::runtime_error("generated matte RenderPlan failed canonical validation" +
            (error.path.empty() ? std::string{} : " at " + error.path) + ": " + error.message);
    }

    const std::filesystem::path destination(state.plan_output);
    if (destination.has_parent_path())
        std::filesystem::create_directories(destination.parent_path());
    const auto temporary = destination.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("could not open RenderPlan output: " + temporary);
        output << plan.dump(2) << '\n';
        if (!output.good()) throw std::runtime_error("could not write RenderPlan output: " + temporary);
    }
    std::error_code rename_error;
    std::filesystem::rename(temporary, destination, rename_error);
    if (rename_error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("could not publish RenderPlan output: " + rename_error.message());
    }
}

std::string output_path(const std::string& pattern, std::size_t frame,
                        const std::string& suffix = {}) {
    const auto marker = pattern.find("####");
    std::string index = std::to_string(frame);
    if (index.size() < 4) index.insert(0, 4 - index.size(), '0');
    if (marker != std::string::npos) {
        auto path = pattern;
        path.replace(marker, 4, index);
        const auto extension = std::filesystem::path(path).extension().string();
        const auto ext_at = path.size() - extension.size();
        path.insert(ext_at, suffix);
        return path;
    }
    const auto path = std::filesystem::path(pattern);
    return (path.parent_path() /
            (path.stem().string() + "_" + index + suffix + ".png")).string();
}

std::shared_ptr<Framebuffer> load_frame(const std::string& path) {
    const auto extension = std::filesystem::path(path).extension().string();
    std::string lower = extension;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower != ".png" && lower != ".jpg" && lower != ".jpeg")
        throw std::runtime_error("only 8-bit PNG/JPEG input is supported: " + path);
    auto frame = io::load_image_as_framebuffer(path);
    if (!frame) throw std::runtime_error("could not decode frame: " + path);
    return frame;
}

std::shared_ptr<Framebuffer> load_tracking_frame(const std::string& path,
                                                  const std::string& transfer) {
    if (std::filesystem::path(path).extension() == ".chrmaskframe") {
        std::ifstream input(path, std::ios::binary);
        constexpr std::array<char, 8> expected_magic{'C', 'H', 'M', 'F', '3', '2', '\0', '\0'};
        std::array<char, 8> magic{};
        std::uint32_t width = 0, height = 0, channels = 0;
        input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        input.read(reinterpret_cast<char*>(&width), sizeof(width));
        input.read(reinterpret_cast<char*>(&height), sizeof(height));
        input.read(reinterpret_cast<char*>(&channels), sizeof(channels));
        if (!input || magic != expected_magic || channels != 4 || width == 0 || height == 0 ||
            static_cast<std::uint64_t>(width) * height > 8'000'000)
            throw std::runtime_error("invalid linear tracking spool frame: " + path);
        const auto expected_size = 20ULL + static_cast<std::uint64_t>(width) * height *
            4ULL * sizeof(float);
        std::error_code size_error;
        if (std::filesystem::file_size(path, size_error) != expected_size || size_error)
            throw std::runtime_error("linear tracking spool frame has a truncated or invalid size: " + path);
        auto frame = std::make_shared<Framebuffer>(static_cast<i32>(width), static_cast<i32>(height));
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                std::array<float, 4> rgba{};
                input.read(reinterpret_cast<char*>(rgba.data()),
                           static_cast<std::streamsize>(sizeof(rgba)));
                if (!input || !std::all_of(rgba.begin(), rgba.end(),
                        [](float value) { return std::isfinite(value); }) ||
                    rgba[3] < 0.0f || rgba[3] > 1.0f)
                    throw std::runtime_error("linear tracking spool contains invalid pixels: " + path);
                frame->set_pixel(static_cast<i32>(x), static_cast<i32>(y),
                    Color{rgba[0], rgba[1], rgba[2], rgba[3]});
            }
        }
        return frame;
    }
    auto frame = load_frame(path);
    if (transfer != "rec709") return frame;
    // The image loader decodes PNG/JPEG as sRGB. Recover the encoded code
    // values and decode them with the Rec.709 transfer declared by the
    // extracted source video, preserving alpha and premultiplication.
    for (i32 y = 0; y < frame->height(); ++y) {
        for (i32 x = 0; x < frame->width(); ++x) {
            auto pixel = frame->get_pixel(x, y);
            const auto straight = pixel.unpremultiplied();
            pixel.r = color::rec709_to_linear_float(
                color::linear_srgb_float(straight.r)) * pixel.a;
            pixel.g = color::rec709_to_linear_float(
                color::linear_srgb_float(straight.g)) * pixel.a;
            pixel.b = color::rec709_to_linear_float(
                color::linear_srgb_float(straight.b)) * pixel.a;
            frame->set_pixel(x, y, pixel);
        }
    }
    return frame;
}

nlohmann::json encode_propagated_matte_video(const MaskPropagationState& state,
                                            std::uint32_t width,
                                            std::uint32_t height) {
#if defined(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    if ((width & 1u) != 0 || (height & 1u) != 0)
        throw std::runtime_error("H.264 YUV420 matte output requires even frame dimensions");

    std::ifstream plan_input(state.plan_input, std::ios::binary);
    if (!plan_input) throw std::runtime_error("could not reopen RenderPlan for matte frame rate");
    nlohmann::json plan;
    plan_input >> plan;
    if (!plan.contains("canvas") || !plan["canvas"].is_object())
        throw std::runtime_error("RenderPlan canvas is required to encode matte video");
    const int fps_num = plan["canvas"].value("fps_num", 0);
    const int fps_den = plan["canvas"].value("fps_den", 0);
    if (fps_num <= 0 || fps_den <= 0)
        throw std::runtime_error("RenderPlan has an invalid frame rate for matte encoding");
    const auto& layers = plan.at("layers");
    const auto target = std::find_if(layers.begin(), layers.end(), [&](const auto& layer) {
        return layer.is_object() && layer.value("id", std::string{}) == state.target_layer;
    });
    if (target == layers.end())
        throw std::runtime_error("matte target layer was not found in RenderPlan");
    if (target->contains("time_remap") || target->value("loop", false))
        throw std::runtime_error(
            "native matte encoding requires an unlooped target without time_remap; provide a pre-encoded matte for retimed sources");
    const auto canvas_frames = plan["canvas"].value("duration_frames", std::int64_t{0});
    const auto start_frame = target->value("start_frame", std::int64_t{0});
    const auto target_frames = target->value("duration_frames", canvas_frames - start_frame);
    if (target_frames <= 0 ||
        static_cast<std::uint64_t>(target_frames) != state.frames.size())
        throw std::runtime_error(
            "input frame count must match the target layer duration for synchronized matte encoding");

    const auto container = media::video::classify_video_container(
        std::filesystem::path(state.matte_video).extension().string());
    media::video::VideoSinkConfig config;
    config.stream.width = static_cast<int>(width);
    config.stream.height = static_cast<int>(height);
    config.stream.frame_rate_num = fps_num;
    config.stream.frame_rate_den = fps_den;
    config.stream.submitted_format = media::video::PixelFormat::RGBA8;
    config.encoder.codec = media::video::VideoCodec::H264;
    config.encoder.preset = "ultrafast";
    config.encoder.rate_control_mode = media::video::RateControlMode::Crf;
    config.encoder.crf = 0;
    config.encoder.encoded_pixel_format = media::video::PixelFormat::YUV420P;
    config.output.container = *container;
    config.output.output_path = std::filesystem::absolute(state.matte_video).lexically_normal();
    config.output.overwrite = true;
    if (config.output.output_path.has_parent_path())
        std::filesystem::create_directories(config.output.output_path.parent_path());

    auto sink = media::video::create_video_sink(config);
    if (!sink || !sink->open(config))
        throw std::runtime_error("could not open native matte video encoder: " +
            (sink ? sink->last_error_message() : std::string("native FFmpeg support is unavailable")));

    struct OutputRollback {
        std::filesystem::path path;
        bool active{true};
        ~OutputRollback() {
            if (!active) return;
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } rollback{config.output.output_path};

    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t frame_index = 0; frame_index < state.frames.size(); ++frame_index) {
        auto mask = load_frame(output_path(state.output_pattern, frame_index));
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const float coverage = std::clamp(mask->get_pixel(
                    static_cast<i32>(x), static_cast<i32>(y)).r, 0.0f, 1.0f);
                const auto code = static_cast<std::uint8_t>(std::clamp(
                    std::lround(color::linear_rec709_float(coverage) * 255.0f), 0l, 255l));
                const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
                rgba[offset] = code;
                rgba[offset + 1] = code;
                rgba[offset + 2] = code;
                rgba[offset + 3] = 255;
            }
        }
        const media::video::VideoFrameView frame{
            .data = rgba.data(),
            .stride_bytes = static_cast<std::size_t>(width) * 4,
            .width = static_cast<int>(width),
            .height = static_cast<int>(height),
            .pixel_format = media::video::PixelFormat::RGBA8,
            .pts = static_cast<std::int64_t>(frame_index),
        };
        if (!sink->submit(frame))
            throw std::runtime_error("native matte video frame encoding failed: " +
                                     sink->last_error_message());
    }
    if (!sink->flush())
        throw std::runtime_error("native matte video flush failed: " + sink->last_error_message());
    if (!sink->close())
        throw std::runtime_error("native matte video finalization failed: " + sink->last_error_message());
    std::error_code output_error;
    if (!std::filesystem::is_regular_file(config.output.output_path, output_error) || output_error ||
        std::filesystem::file_size(config.output.output_path, output_error) == 0 || output_error)
        throw std::runtime_error("native matte encoder did not produce a non-empty video file");

    const auto probe = media::video::probe_media(config.output.output_path);
    if (!probe)
        throw std::runtime_error("could not probe encoded matte video: " + probe.error().message);
    const auto video_stream = std::find_if(
        probe->streams.begin(), probe->streams.end(), [](const auto& stream) {
            return stream.kind == media::video::MediaStreamKind::Video;
        });
    const Rational expected_frame_rate = normalize_rational(Rational{fps_num, fps_den});
    const bool frame_rate_matches = video_stream != probe->streams.end() &&
        video_stream->frame_rate.numerator > 0 && video_stream->frame_rate.denominator > 0 &&
        normalize_rational(video_stream->frame_rate) == expected_frame_rate;
    if (video_stream == probe->streams.end() || video_stream->width != static_cast<int>(width) ||
        video_stream->height != static_cast<int>(height) ||
        video_stream->codec != "h264" || video_stream->pixel_format != "yuv420p" ||
        !frame_rate_matches ||
        video_stream->color_range != "tv" || video_stream->color_space != "bt709" ||
        video_stream->color_transfer != "bt709" ||
        video_stream->color_primaries != "bt709")
        throw std::runtime_error(
            "encoded matte video failed its H.264/YUV420P frame-rate, dimensions, or BT.709 color contract");

    media::NativeVideoFrameDecoder decoder;
    decoder.set_software_only(true);
    const auto decoded_count = decoder.indexed_sample_count(
        config.output.output_path.string());
    if (decoded_count != state.frames.size())
        throw std::runtime_error("encoded matte video frame count does not match the input sequence");

    constexpr double kMaximumMattePixelError = 0.02;
    double absolute_error_sum = 0.0;
    double maximum_absolute_error = 0.0;
    std::uint64_t compared_pixels = 0;
    for (std::size_t frame_index = 0; frame_index < state.frames.size(); ++frame_index) {
        const auto path = config.output.output_path.string();
        auto decoded_result = decoder.decode_frame_index_at(
            path, static_cast<std::int64_t>(frame_index),
            static_cast<int>(width), static_cast<int>(height));
        const auto* decoded = media::decoded_frame_if(decoded_result);
        if (!decoded || !decoded->framebuffer)
            throw std::runtime_error("encoded matte video failed software decode at frame " +
                                     std::to_string(frame_index));
        auto expected = load_frame(output_path(state.output_pattern, frame_index));
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const float coverage = std::clamp(expected->get_pixel(
                    static_cast<i32>(x), static_cast<i32>(y)).r, 0.0f, 1.0f);
                const Color actual = decoded->framebuffer->get_pixel(
                    static_cast<i32>(x), static_cast<i32>(y));
                const double error = std::max({
                    std::abs(static_cast<double>(actual.r) - coverage),
                    std::abs(static_cast<double>(actual.g) - coverage),
                    std::abs(static_cast<double>(actual.b) - coverage)});
                if (!std::isfinite(error) || error > kMaximumMattePixelError)
                    throw std::runtime_error(
                        "encoded matte pixel error exceeded 0.02 at frame " +
                        std::to_string(frame_index));
                maximum_absolute_error = std::max(maximum_absolute_error, error);
                absolute_error_sum += error;
                ++compared_pixels;
            }
        }
    }
    const auto verification = nlohmann::json{
        {"status", "pass"},
        {"container", probe->format_name},
        {"codec", video_stream->codec},
        {"pixel_format", video_stream->pixel_format},
        {"width", video_stream->width},
        {"height", video_stream->height},
        {"frame_rate_num", fps_num},
        {"frame_rate_den", fps_den},
        {"color_range", video_stream->color_range},
        {"color_space", video_stream->color_space},
        {"color_transfer", video_stream->color_transfer},
        {"color_primaries", video_stream->color_primaries},
        {"decoded_frames", decoded_count},
        {"compared_pixels", compared_pixels},
        {"maximum_absolute_coverage_error", maximum_absolute_error},
        {"mean_absolute_coverage_error", compared_pixels == 0 ? 0.0
            : absolute_error_sum / static_cast<double>(compared_pixels)},
        {"maximum_absolute_coverage_error_allowed", kMaximumMattePixelError},
    };
    rollback.active = false;
    return verification;
#else
    (void)state;
    (void)width;
    (void)height;
    throw std::runtime_error(
        "encoding matte video requires a build with CHRONON3D_ENABLE_NATIVE_FFMPEG");
#endif
}

void append_encoded_video_verification(const MaskPropagationState& state,
                                       const nlohmann::json& verification) {
    const std::filesystem::path report_path = state.output_pattern + ".json";
    std::ifstream input(report_path, std::ios::binary);
    if (!input) throw std::runtime_error("could not reopen mask propagation report");
    nlohmann::json report;
    input >> report;
    if (!report.is_object() || report.value("schema", std::string{}) !=
            "chronon.temporal_mask_propagation.v1")
        throw std::runtime_error("mask propagation report has an unsupported schema");
    report["encoded_matte_video"] = {
        {"path", std::filesystem::absolute(state.matte_video).lexically_normal().string()},
        {"verification", verification},
    };
    const auto temporary = report_path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("could not open temporary matte report");
        output << report.dump(2) << '\n';
        if (!output.good()) throw std::runtime_error("could not write temporary matte report");
    }
    std::error_code rename_error;
    std::filesystem::rename(temporary, report_path, rename_error);
    if (rename_error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("could not publish matte video verification report: " +
                                 rename_error.message());
    }
}

TrackingFrameView view_of(const Framebuffer& frame) {
    const auto count = static_cast<std::size_t>(frame.allocated_width()) *
                       static_cast<std::size_t>(frame.height());
    return {std::span<const Color>(frame.data(), count),
            static_cast<std::uint32_t>(frame.width()),
            static_cast<std::uint32_t>(frame.height()),
            static_cast<std::uint32_t>(frame.allocated_width()), true};
}

SoftMaskView view_of(const std::vector<float>& alpha,
                     std::uint32_t width, std::uint32_t height) {
    return {std::span<const float>(alpha), width, height, width};
}

bool write_gray(const std::string& path, std::span<const float> values,
                std::uint32_t width, std::uint32_t height) {
    Framebuffer output(static_cast<i32>(width), static_cast<i32>(height));
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto index = static_cast<std::size_t>(y) * width + x;
            const float coverage = std::clamp(values[index], 0.0f, 1.0f);
            output.set_pixel(static_cast<i32>(x), static_cast<i32>(y),
                             Color{coverage, coverage, coverage, 1.0f});
        }
    }
    const std::filesystem::path destination(path);
    if (destination.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) return false;
    }
    return chronon3d::save_png(output, path);
}

void publish_staged_file(const std::string& staged_path,
                         const std::string& output_path) {
    const std::filesystem::path destination(output_path);
    if (destination.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error)
            throw std::runtime_error("could not create matte output directory: " +
                                     error.message());
    }
    const auto temporary = output_path + ".chronon-tmp.png";
    std::error_code error;
    std::filesystem::copy_file(staged_path, temporary,
        std::filesystem::copy_options::overwrite_existing, error);
    if (error)
        throw std::runtime_error("could not stage matte for publication: " +
                                 error.message());
    std::filesystem::rename(temporary, output_path, error);
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("could not publish matte output: " + error.message());
    }
}

void write_json_report_atomic(const std::string& report_path,
                              const nlohmann::json& report) {
    const std::filesystem::path destination(report_path);
    if (destination.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error)
            throw std::runtime_error("could not create report directory: " +
                                     error.message());
    }
    const auto temporary = report_path + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("could not open report staging file: " + temporary);
        output << report.dump(2) << '\n';
        if (!output.good()) {
            output.close();
            std::filesystem::remove(temporary);
            throw std::runtime_error("could not write report staging file: " + temporary);
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, report_path, error);
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("could not publish confidence report: " +
                                 error.message());
    }
}

PropagatedMask seed_result(const std::vector<float>& alpha,
                           std::uint32_t width, std::uint32_t height) {
    PropagatedMask result;
    result.width = width;
    result.height = height;
    result.alpha = alpha;
    result.confidence.assign(alpha.size(), 1.0f);
    return result;
}

int propagate(const MaskPropagationState& state, float minimum_confidence) {
    validate_plan_patch_option_set(state);
    if (state.frames.empty() || state.seed_frame >= state.frames.size())
        throw std::runtime_error("seed-frame index is outside the input sequence");
    if (state.frames.size() > 10'000)
        throw std::runtime_error("at most 10000 frames per propagation job are allowed");
    if (std::filesystem::path(state.output_pattern).extension() != ".png")
        throw std::runtime_error("mask output pattern must end in .png");

    auto seed_frame = load_tracking_frame(state.frames[state.seed_frame], state.input_transfer);

    const auto width = static_cast<std::uint32_t>(seed_frame->width());
    const auto height = static_cast<std::uint32_t>(seed_frame->height());
    if (static_cast<std::uint64_t>(width) * height > 8'000'000)
        throw std::runtime_error("frame size exceeds the 8-megapixel CPU tracking limit");
    TemporaryMaskSpool spool;

    const auto read_mask = [&](const std::string& path) {
        auto image = load_frame(path);
        if (image->width() != seed_frame->width() ||
            image->height() != seed_frame->height())
            throw std::runtime_error("seed and correction masks must match the video frame dimensions: " + path);
        bool has_transparency = false;
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x)
                has_transparency |= image->get_pixel(static_cast<i32>(x),
                                                      static_cast<i32>(y)).a < 0.999f;
        std::vector<float> alpha(static_cast<std::size_t>(width) * height);
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const auto pixel = image->get_pixel(static_cast<i32>(x),
                                                     static_cast<i32>(y));
                const float coverage = has_transparency
                    ? pixel.a
                    : 0.2126f * pixel.r + 0.7152f * pixel.g + 0.0722f * pixel.b;
                alpha[static_cast<std::size_t>(y) * width + x] =
                    std::clamp(coverage, 0.0f, 1.0f);
            }
        }
        return alpha;
    };
    std::map<std::size_t, std::string> anchors{{state.seed_frame, state.seed_mask}};
    for (const auto& specification : state.correction_masks) {
        const auto separator = specification.find('=');
        if (separator == std::string::npos || separator == 0 || separator + 1 >= specification.size())
            throw std::runtime_error("correction mask must use FRAME=PATH syntax: " + specification);
        std::size_t frame_index = 0;
        try {
            const auto frame_text = specification.substr(0, separator);
            std::size_t parsed = 0;
            frame_index = std::stoull(frame_text, &parsed);
            if (parsed != frame_text.size()) throw std::invalid_argument("trailing characters");
        } catch (const std::exception&) {
            throw std::runtime_error("invalid correction frame index: " + specification);
        }
        if (frame_index >= state.frames.size())
            throw std::runtime_error("correction frame index is outside the input sequence: " + specification);
        if (!anchors.emplace(frame_index, specification.substr(separator + 1)).second)
            throw std::runtime_error("more than one seed/correction mask was supplied for frame " +
                                     std::to_string(frame_index));
    }
    const auto seed_alpha = read_mask(anchors.at(state.seed_frame));

    TemporalFlowOptions options;
    options.minimum_confidence = minimum_confidence;
    if (state.source_video_transfer == "smpte2084")
        options.photometric_scale = 100.0f;
    std::uint64_t propagation_steps = 0;
    const auto add_steps = [&](std::uint64_t count) {
        if (count > std::numeric_limits<std::uint64_t>::max() - propagation_steps)
            throw std::runtime_error("estimated propagation step count overflows");
        propagation_steps += count;
    };
    add_steps(static_cast<std::uint64_t>(anchors.begin()->first));
    add_steps(static_cast<std::uint64_t>(state.frames.size() - 1 - anchors.rbegin()->first));
    for (auto previous = anchors.begin(), current = std::next(previous);
         current != anchors.end(); ++previous, ++current) {
        const auto gap = current->first - previous->first - 1;
        if (gap > std::numeric_limits<std::uint64_t>::max() / 2)
            throw std::runtime_error("estimated propagation step count overflows");
        add_steps(static_cast<std::uint64_t>(gap) * 2);
    }
    const auto pair_work = TemporalMaskPropagator::estimate_pair_work_units(width, height, options);
    const auto projected_work = pair_work != 0 && propagation_steps >
        std::numeric_limits<std::uint64_t>::max() / pair_work
        ? std::numeric_limits<std::uint64_t>::max() : propagation_steps * pair_work;
    if (projected_work > state.maximum_total_work_units)
        throw std::runtime_error("projected propagation work " + std::to_string(projected_work) +
            " exceeds --maximum-total-work-units " +
            std::to_string(state.maximum_total_work_units));
    nlohmann::json report{
        {"schema", "chronon.temporal_mask_propagation.v1"},
        {"seed_frame", state.seed_frame},
        {"width", width},
        {"height", height},
        {"confidence_threshold", minimum_confidence},
        {"minimum_mean_confidence", state.minimum_mean_confidence},
        {"minimum_mean_foreground_confidence", state.minimum_mean_foreground_confidence},
        {"maximum_foreground_loss", state.maximum_foreground_loss},
        {"input_transfer", state.input_transfer},
        {"source_video", state.source_video.empty()
            ? nlohmann::json(nullptr) : nlohmann::json(state.source_video)},
        {"source_video_transfer", state.source_video_transfer.empty()
            ? nlohmann::json(nullptr) : nlohmann::json(state.source_video_transfer)},
        {"photometric_scale", options.photometric_scale},
        {"tracking_working_space", "linear_bt709"},
        {"edge_refinement", {
            {"method", state.refinement_radius == 0 ? "none" : "guided_filter"},
            {"radius", state.refinement_radius},
            {"epsilon", state.refinement_epsilon},
            {"authored_anchors_refined", false},
            {"confidence_measured_before_refinement", true},
        }},
        {"anchor_count", anchors.size()},
        {"propagation_steps", propagation_steps},
        {"estimated_work_units_per_pair", pair_work},
        {"projected_work_units", projected_work},
        {"maximum_total_work_units", state.maximum_total_work_units},
        {"between_anchor_merge", "bidirectional_confidence_weighted"},
        {"temporary_spool", "compressed_png_intermediates_cleaned_on_exit"},
        {"frames", nlohmann::json::array()},
    };

    const auto write_report = [&](std::size_t index, const PropagatedMask& mask) {
        const auto path = output_path(state.output_pattern, index);
        const auto confidence_path = output_path(state.output_pattern, index, "_confidence");
        auto frame = load_tracking_frame(state.frames[index], state.input_transfer);
        std::vector<float> refined_alpha;
        const bool did_refine = state.refinement_radius != 0 && !anchors.contains(index);
        if (did_refine) {
            refined_alpha = TemporalMaskPropagator::refine_mask_edges(
                view_of(*frame), view_of(mask.alpha, mask.width, mask.height),
                {state.refinement_radius, state.refinement_epsilon});
        }
        const auto& output_alpha = did_refine ? refined_alpha : mask.alpha;
        if (!write_gray(spool.staged_output_path(index, false), output_alpha,
                        mask.width, mask.height))
            throw std::runtime_error("could not stage propagated mask: " + path);
        if (!write_gray(spool.staged_output_path(index, true), mask.confidence,
                        mask.width, mask.height))
            throw std::runtime_error("could not stage confidence matte: " +
                                     confidence_path);
        const auto source_digest = state.source_video.empty()
            ? assets::sha256_file(state.frames[index]) : std::nullopt;
        const std::string source_sha256 = state.source_video.empty()
            ? (source_digest ? source_digest->hex() : std::string{})
            : state.source_video_sha256;
        if (source_sha256.empty())
            throw std::runtime_error("could not hash tracking source frame: " +
                                     state.frames[index]);
        nlohmann::json source_presentation_time = nullptr;
        if (!state.source_video.empty()) {
            if (index >= state.source_presentation_times.size())
                throw std::runtime_error("source presentation timestamp is missing for frame " +
                                         std::to_string(index));
            const auto& time = state.source_presentation_times[index];
            source_presentation_time = {
                {"pts", time.ticks()},
                {"time_base_num", time.time_base.numerator},
                {"time_base_den", time.time_base.denominator},
            };
        }
        double visible_coverage = 0.0;
        double visible_confidence_sum = 0.0;
        double foreground_confidence_sum = 0.0;
        double foreground_visible_coverage = 0.0;
        std::uint64_t foreground_confidence_samples = 0;
        for (std::size_t pixel = 0; pixel < mask.alpha.size(); ++pixel) {
            const auto x = static_cast<i32>(pixel % mask.width);
            const auto y = static_cast<i32>(pixel / mask.width);
            const float alpha = frame->get_pixel(x, y).a;
            const double coverage = std::isfinite(alpha)
                ? std::clamp(static_cast<double>(alpha), 0.0, 1.0) : 0.0;
            visible_coverage += coverage;
            visible_confidence_sum += coverage * mask.confidence[pixel];
            if (mask.alpha[pixel] <= 0.01f || coverage <= 0.0) continue;
            foreground_confidence_sum += coverage * mask.confidence[pixel];
            foreground_visible_coverage += coverage;
            ++foreground_confidence_samples;
        }
        const double mean_confidence = visible_coverage > 0.0
            ? visible_confidence_sum / visible_coverage : 0.0;
        const double mean_foreground_confidence = foreground_confidence_samples == 0
            ? 0.0 : foreground_confidence_sum / foreground_visible_coverage;
        const double foreground_loss = mask.foreground_samples == 0 ? 0.0
            : static_cast<double>(mask.rejected_foreground_samples) /
                static_cast<double>(mask.foreground_samples);
        const bool authored_anchor = anchors.contains(index);
        const bool confidence_gate_passed = authored_anchor ||
            (mean_confidence >= state.minimum_mean_confidence &&
             mean_foreground_confidence >= state.minimum_mean_foreground_confidence &&
             foreground_loss <= state.maximum_foreground_loss);
        report["frames"].push_back({
            {"index", index},
            {"source", state.source_video.empty()
                ? state.frames[index]
                : state.source_video + "#presentation_frame=" + std::to_string(index)},
            {"source_sha256", source_sha256},
            {"source_presentation_time", source_presentation_time},
            {"mask", path},
            {"confidence_map", confidence_path},
            {"mean_confidence", mean_confidence},
            {"visible_alpha_coverage", visible_coverage},
            {"mean_foreground_confidence", mean_foreground_confidence},
            {"foreground_visible_alpha_coverage", foreground_visible_coverage},
            {"foreground_confidence_samples", foreground_confidence_samples},
            {"foreground_samples", mask.foreground_samples},
            {"rejected_foreground_samples", mask.rejected_foreground_samples},
            {"foreground_loss_fraction", foreground_loss},
            {"authored_anchor", authored_anchor},
            {"confidence_gate_passed", confidence_gate_passed},
            {"edge_refinement", did_refine ? "guided_filter" : "none"},
            {"rejected_low_texture", mask.rejected_low_texture},
            {"rejected_occlusion", mask.rejected_occlusion},
            {"rejected_large_motion", mask.rejected_large_motion},
        });
    };

    for (const auto& [frame_index, path] : anchors) {
        const auto alpha = frame_index == state.seed_frame
            ? seed_alpha : read_mask(path);
        write_report(frame_index, seed_result(alpha, width, height));
    }

    const auto propagate_outward = [&](std::size_t anchor_index, int direction) {
        auto current_frame = load_tracking_frame(state.frames[anchor_index], state.input_transfer);
        auto current_alpha = read_mask(anchors.at(anchor_index));
        std::vector<float> current_confidence(current_alpha.size(), 1.0f);
        std::size_t index = anchor_index;
        while (direction < 0 ? index > 0 : index + 1 < state.frames.size()) {
            const auto next_index = direction < 0 ? index - 1 : index + 1;
            auto next_frame = load_tracking_frame(state.frames[next_index], state.input_transfer);
            auto propagated = TemporalMaskPropagator::propagate(
                view_of(*current_frame), view_of(*next_frame),
                SoftMaskView{current_alpha, width, height, 0,
                             current_confidence, 0}, options);
            write_report(next_index, propagated);
            current_alpha = std::move(propagated.alpha);
            current_confidence = std::move(propagated.confidence);
            current_frame = std::move(next_frame);
            index = next_index;
        }
    };

    const auto first_anchor = anchors.begin()->first;
    const auto last_anchor = anchors.rbegin()->first;
    propagate_outward(first_anchor, -1);
    propagate_outward(last_anchor, 1);

    // Between authored corrections, compute independent forward and backward
    // mattes, then blend by per-pixel tracking confidence. Intermediate PNGs
    // are losslessly represented at the command's 8-bit output precision and
    // keep memory bounded to a few frames rather than the whole clip.
    for (auto interval = std::next(anchors.begin()); interval != anchors.end(); ++interval) {
        const auto left = std::prev(interval);
        const std::size_t begin = left->first;
        const std::size_t end = interval->first;
        if (end <= begin + 1) continue;

        std::vector<std::array<std::uint64_t, 5>> forward_rejections(end - begin + 1);
        auto forward_frame = load_tracking_frame(state.frames[begin], state.input_transfer);
        auto forward_alpha = read_mask(left->second);
        std::vector<float> forward_confidence(forward_alpha.size(), 1.0f);
        for (std::size_t i = begin + 1; i < end; ++i) {
            auto target_frame = load_tracking_frame(state.frames[i], state.input_transfer);
            auto propagated = TemporalMaskPropagator::propagate(
                view_of(*forward_frame), view_of(*target_frame),
                SoftMaskView{forward_alpha, width, height, 0,
                             forward_confidence, 0}, options);
            if (!write_gray(spool.frame_path(i, false), propagated.alpha, width, height) ||
                !write_gray(spool.frame_path(i, true), propagated.confidence, width, height))
                throw std::runtime_error("could not spool forward mask propagation frame " +
                                         std::to_string(i));
            forward_rejections[i - begin] = {
                propagated.rejected_low_texture,
                propagated.rejected_occlusion,
                propagated.rejected_large_motion,
                propagated.foreground_samples,
                propagated.rejected_foreground_samples};
            forward_alpha = std::move(propagated.alpha);
            forward_confidence = std::move(propagated.confidence);
            forward_frame = std::move(target_frame);
        }

        auto backward_frame = load_tracking_frame(state.frames[end], state.input_transfer);
        auto backward_alpha = read_mask(interval->second);
        std::vector<float> backward_confidence(backward_alpha.size(), 1.0f);
        for (std::size_t i = end - 1; i > begin; --i) {
            auto target_frame = load_tracking_frame(state.frames[i], state.input_transfer);
            auto backward = TemporalMaskPropagator::propagate(
                view_of(*backward_frame), view_of(*target_frame),
                SoftMaskView{backward_alpha, width, height, 0,
                             backward_confidence, 0}, options);
            const auto forward_mask = read_mask(spool.frame_path(i, false));
            const auto forward_confidence = read_mask(spool.frame_path(i, true));

            PropagatedMask combined;
            combined.width = width;
            combined.height = height;
            combined.alpha.resize(backward.alpha.size());
            combined.confidence.resize(backward.alpha.size());
            for (std::size_t pixel = 0; pixel < combined.alpha.size(); ++pixel) {
                const float forward_weight = std::clamp(forward_confidence[pixel], 0.0f, 1.0f);
                const float backward_weight = std::clamp(backward.confidence[pixel], 0.0f, 1.0f);
                const float total = forward_weight + backward_weight;
                combined.alpha[pixel] = total > 1.0e-6f
                    ? (forward_mask[pixel] * forward_weight +
                       backward.alpha[pixel] * backward_weight) / total
                    : 0.0f;
                combined.confidence[pixel] = 0.5f * total;
            }
            const auto& rejected = forward_rejections[i - begin];
            combined.rejected_low_texture = rejected[0] + backward.rejected_low_texture;
            combined.rejected_occlusion = rejected[1] + backward.rejected_occlusion;
            combined.rejected_large_motion = rejected[2] + backward.rejected_large_motion;
            combined.foreground_samples = rejected[3] + backward.foreground_samples;
            combined.rejected_foreground_samples = rejected[4] +
                backward.rejected_foreground_samples;
            write_report(i, combined);

            backward_alpha = std::move(backward.alpha);
            backward_confidence = std::move(backward.confidence);
            backward_frame = std::move(target_frame);
        }
    }

    std::sort(report["frames"].begin(), report["frames"].end(),
        [](const nlohmann::json& a, const nlohmann::json& b) {
            return a.at("index").get<std::size_t>() < b.at("index").get<std::size_t>();
        });

    std::vector<std::size_t> confidence_gate_failures;
    for (const auto& frame : report["frames"]) {
        if (!frame.at("authored_anchor").get<bool>() &&
            !frame.at("confidence_gate_passed").get<bool>())
            confidence_gate_failures.push_back(frame.at("index").get<std::size_t>());
    }
    report["confidence_gate"] = {
        {"status", confidence_gate_failures.empty() ? "pass" : "fail"},
        {"minimum_mean_confidence", state.minimum_mean_confidence},
        {"minimum_mean_foreground_confidence", state.minimum_mean_foreground_confidence},
        {"maximum_foreground_loss", state.maximum_foreground_loss},
        {"authored_anchor_frames_exempt", true},
        {"failed_frames", confidence_gate_failures},
    };

    report["outputs_published"] = confidence_gate_failures.empty();
    if (confidence_gate_failures.empty()) {
        for (std::size_t index = 0; index < state.frames.size(); ++index) {
            publish_staged_file(spool.staged_output_path(index, false),
                                output_path(state.output_pattern, index));
            publish_staged_file(spool.staged_output_path(index, true),
                                output_path(state.output_pattern, index, "_confidence"));
        }
    }

    const auto report_path = state.output_pattern + ".json";
    write_json_report_atomic(report_path, report);
    if (!confidence_gate_failures.empty()) {
        spdlog::error(
            "Mask propagation confidence gate rejected {} frame(s); see {}. No matte outputs or plan attachment were published.",
            confidence_gate_failures.size(), report_path);
        return 2;
    }
    return 0;
}

struct SegmentPoint {
    float x{0.0f};
    float y{0.0f};
};

std::vector<SegmentPoint> parse_segment_points(
    const std::vector<std::string>& values, const char* label) {
    if (values.empty())
        throw std::runtime_error(std::string("at least one --") + label + " point is required");
    std::vector<SegmentPoint> points;
    points.reserve(values.size());
    for (const auto& value : values) {
        const auto comma = value.find(',');
        if (comma == std::string::npos || comma == 0 || comma + 1 >= value.size())
            throw std::runtime_error(std::string("invalid --") + label + " point; expected normalized X,Y in [0,1]: " + value);
        try {
            std::size_t x_end = 0, y_end = 0;
            const float x = std::stof(value.substr(0, comma), &x_end);
            const float y = std::stof(value.substr(comma + 1), &y_end);
            if (x_end != comma || y_end != value.size() - comma - 1 ||
                !std::isfinite(x) || !std::isfinite(y) ||
                x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f)
                throw std::runtime_error("point is not finite normalized X,Y");
            points.push_back({x, y});
        } catch (const std::exception&) {
            throw std::runtime_error(std::string("invalid --") + label + " point; expected normalized X,Y in [0,1]: " + value);
        }
    }
    return points;
}

struct SegmentColorModel {
    std::array<double, 3> mean{};
    // Row-major inverse covariance for a regularized full-RGB Gaussian.
    std::array<double, 9> inverse_covariance{};
    double log_determinant{0.0};
};

int run_scribble_segmentation(
    const std::string& input_path, const std::string& output_path,
    const std::string& transfer, const std::vector<std::string>& foreground_values,
    const std::vector<std::string>& background_values, std::uint32_t seed_radius,
    std::uint32_t iterations, std::uint64_t maximum_work_units,
    float minimum_foreground_fraction, float maximum_foreground_fraction,
    float maximum_mean_uncertainty) {
    auto frame = load_tracking_frame(input_path, transfer);
    const auto width = static_cast<std::uint32_t>(frame->width());
    const auto height = static_cast<std::uint32_t>(frame->height());
    const std::uint64_t count64 = static_cast<std::uint64_t>(width) * height;
    if (count64 == 0 || count64 > 8'000'000)
        throw std::runtime_error("mask segmentation supports images up to 8 megapixels");
    if (iterations == 0 || maximum_work_units == 0 ||
        !std::isfinite(minimum_foreground_fraction) ||
        !std::isfinite(maximum_foreground_fraction) ||
        minimum_foreground_fraction < 0.0f || maximum_foreground_fraction > 1.0f ||
        minimum_foreground_fraction > maximum_foreground_fraction ||
        !std::isfinite(maximum_mean_uncertainty) ||
        maximum_mean_uncertainty < 0.0f || maximum_mean_uncertainty > 1.0f)
        throw std::runtime_error("invalid segmentation gate or work settings");
    const auto count = static_cast<std::size_t>(count64);
    // Include the extra fixed matrix-inversion and Mahalanobis work for the
    // full-covariance color models in the agent-configurable work budget.
    const auto work_per_pixel = 5ULL * static_cast<std::uint64_t>(iterations) + 16ULL;
    if (count64 > maximum_work_units / work_per_pixel)
        throw std::runtime_error("segmentation work estimate exceeds --maximum-work-units");
    const auto estimated_work_units = count64 * work_per_pixel;
    const auto foreground = parse_segment_points(foreground_values, "foreground");
    const auto background = parse_segment_points(background_values, "background");

    std::vector<std::array<float, 3>> colors(count);
    std::vector<float> source_coverage(count, 1.0f);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto premultiplied = frame->get_pixel(static_cast<i32>(x), static_cast<i32>(y));
            if (!std::isfinite(premultiplied.a) || premultiplied.a < 0.0f ||
                premultiplied.a > 1.0f)
                throw std::runtime_error("mask-segment source contains invalid alpha values");
            const auto index = static_cast<std::size_t>(y) * width + x;
            source_coverage[index] = premultiplied.a;
            const auto pixel = premultiplied.unpremultiplied();
            if (premultiplied.a > 0.0f && (!std::isfinite(pixel.r) ||
                !std::isfinite(pixel.g) || !std::isfinite(pixel.b)))
                throw std::runtime_error("mask-segment source contains non-finite RGB values");
            colors[index] = {pixel.r, pixel.g, pixel.b};
        }
    }
    std::vector<std::int8_t> labels(count, -1);
    const auto mark_points = [&](const std::vector<SegmentPoint>& points, std::int8_t label) {
        for (const auto& point : points) {
            const auto cx = static_cast<std::int32_t>(std::lround(point.x * (width - 1)));
            const auto cy = static_cast<std::int32_t>(std::lround(point.y * (height - 1)));
            const auto r = static_cast<std::int32_t>(seed_radius);
            for (auto y = std::max(0, cy - r); y <= std::min(static_cast<std::int32_t>(height) - 1, cy + r); ++y) {
                for (auto x = std::max(0, cx - r); x <= std::min(static_cast<std::int32_t>(width) - 1, cx + r); ++x) {
                    const auto dx = x - cx, dy = y - cy;
                    if (dx * dx + dy * dy > r * r) continue;
                    const auto index = static_cast<std::size_t>(y) * width + static_cast<std::uint32_t>(x);
                    if (source_coverage[index] <= 0.01f) continue;
                    if (labels[index] >= 0 && labels[index] != label)
                        throw std::runtime_error("foreground/background scribble regions overlap; move the seed points apart");
                    labels[index] = label;
                }
            }
        }
    };
    mark_points(foreground, 1);
    mark_points(background, 0);

    const auto fit_model = [&](std::int8_t label) {
        SegmentColorModel model;
        model.mean = {};
        double samples = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            if (labels[i] != label || source_coverage[i] <= 0.01f) continue;
            const double weight = source_coverage[i];
            for (std::size_t c = 0; c < 3; ++c) model.mean[c] += colors[i][c] * weight;
            samples += weight;
        }
        if (samples == 0) throw std::runtime_error("segmentation scribble has no covered pixels");
        for (auto& channel : model.mean) channel /= static_cast<double>(samples);
        std::array<double, 9> covariance{};
        for (std::size_t i = 0; i < count; ++i) {
            if (labels[i] != label || source_coverage[i] <= 0.01f) continue;
            std::array<double, 3> delta{};
            for (std::size_t c = 0; c < 3; ++c)
                delta[c] = colors[i][c] - model.mean[c];
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t column = 0; column < 3; ++column) {
                    covariance[row * 3 + column] += delta[row] * delta[column] *
                        source_coverage[i];
                }
            }
        }
        for (double& value : covariance) value /= static_cast<double>(samples);
        // A small diagonal regularizer keeps flat-color scribbles invertible
        // without discarding cross-channel covariance.
        covariance[0] += 0.0025;
        covariance[4] += 0.0025;
        covariance[8] += 0.0025;
        const double a = covariance[0], b = covariance[1], c = covariance[2];
        const double d = covariance[4], e = covariance[5], f = covariance[8];
        const double determinant = a * (d * f - e * e) -
            b * (b * f - c * e) + c * (b * e - c * d);
        if (!std::isfinite(determinant) || determinant <= 0.0)
            throw std::runtime_error("scribble color covariance is not positive definite");
        model.log_determinant = std::log(determinant);
        model.inverse_covariance = {
            (d * f - e * e) / determinant,
            (c * e - b * f) / determinant,
            (b * e - c * d) / determinant,
            (c * e - b * f) / determinant,
            (a * f - c * c) / determinant,
            (b * c - a * e) / determinant,
            (b * e - c * d) / determinant,
            (b * c - a * e) / determinant,
            (a * d - b * b) / determinant};
        return model;
    };
    const auto foreground_model = fit_model(1);
    const auto background_model = fit_model(0);
    std::vector<float> prior(count), alpha(count), next(count);
    const auto log_likelihood = [](const SegmentColorModel& model,
                                   const std::array<float, 3>& sample) {
        std::array<double, 3> delta{};
        for (std::size_t channel = 0; channel < 3; ++channel)
            delta[channel] = sample[channel] - model.mean[channel];
        double mahalanobis = 0.0;
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                mahalanobis += delta[row] *
                    model.inverse_covariance[row * 3 + column] * delta[column];
            }
        }
        return -0.5 * (model.log_determinant + std::max(mahalanobis, 0.0));
    };
    for (std::size_t i = 0; i < count; ++i) {
        const double foreground_log_likelihood =
            log_likelihood(foreground_model, colors[i]);
        const double background_log_likelihood =
            log_likelihood(background_model, colors[i]);
        const double logit = std::clamp(foreground_log_likelihood - background_log_likelihood,
                                        -30.0, 30.0);
        prior[i] = static_cast<float>(1.0 / (1.0 + std::exp(-logit)));
        alpha[i] = labels[i] == 1 ? 1.0f : (labels[i] == 0 ? 0.0f : prior[i]);
    }

    // Edge weights are derived once from linear BT.709 color distances.
    double mean_edge_distance = 0.0;
    std::uint64_t edge_count = 0;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto i = static_cast<std::size_t>(y) * width + x;
            for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{0, 1}}) {
                const auto nx = x + static_cast<std::uint32_t>(dx);
                const auto ny = y + static_cast<std::uint32_t>(dy);
                if ((dx && nx >= width) || (dy && ny >= height)) continue;
                const auto& a = colors[i];
                const auto& b = colors[static_cast<std::size_t>(ny) * width + nx];
                double distance = 0.0;
                for (std::size_t c = 0; c < 3; ++c) {
                    const double delta = a[c] - b[c];
                    distance += delta * delta;
                }
                const double coverage_delta = source_coverage[i] -
                    source_coverage[static_cast<std::size_t>(ny) * width + nx];
                distance += coverage_delta * coverage_delta;
                mean_edge_distance += distance;
                ++edge_count;
            }
        }
    }
    const double beta = 0.5 / std::max(mean_edge_distance / std::max<std::uint64_t>(edge_count, 1), 1.0e-8);
    std::vector<std::array<float, 4>> weights(count);
    constexpr std::array<std::pair<int, int>, 4> directions{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto i = static_cast<std::size_t>(y) * width + x;
            for (std::size_t direction = 0; direction < directions.size(); ++direction) {
                const auto nx = static_cast<std::int32_t>(x) + directions[direction].first;
                const auto ny = static_cast<std::int32_t>(y) + directions[direction].second;
                if (nx < 0 || ny < 0 || nx >= static_cast<std::int32_t>(width) || ny >= static_cast<std::int32_t>(height)) continue;
                const auto& a = colors[i];
                const auto& b = colors[static_cast<std::size_t>(ny) * width + static_cast<std::uint32_t>(nx)];
                double distance = 0.0;
                for (std::size_t c = 0; c < 3; ++c) {
                    const double delta = a[c] - b[c];
                    distance += delta * delta;
                }
                const double coverage_delta = source_coverage[i] -
                    source_coverage[static_cast<std::size_t>(ny) * width +
                                    static_cast<std::uint32_t>(nx)];
                distance += coverage_delta * coverage_delta;
                weights[i][direction] = static_cast<float>(std::exp(-beta * distance));
            }
        }
    }
    constexpr float data_weight = 0.2f;
    for (std::uint32_t iteration = 0; iteration < iterations; ++iteration) {
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const auto i = static_cast<std::size_t>(y) * width + x;
                if (labels[i] >= 0) {
                    next[i] = labels[i] == 1 ? 1.0f : 0.0f;
                    continue;
                }
                float numerator = data_weight * prior[i];
                float denominator = data_weight;
                for (std::size_t direction = 0; direction < directions.size(); ++direction) {
                    const auto nx = static_cast<std::int32_t>(x) + directions[direction].first;
                    const auto ny = static_cast<std::int32_t>(y) + directions[direction].second;
                    if (nx < 0 || ny < 0 || nx >= static_cast<std::int32_t>(width) || ny >= static_cast<std::int32_t>(height)) continue;
                    const float weight = weights[i][direction];
                    numerator += weight * alpha[static_cast<std::size_t>(ny) * width + static_cast<std::uint32_t>(nx)];
                    denominator += weight;
                }
                next[i] = numerator / denominator;
            }
        }
        alpha.swap(next);
    }
    std::uint64_t foreground_pixels = 0;
    double uncertainty_sum = 0.0;
    double unseeded_coverage = 0.0;
    std::uint64_t transparent_pixels = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (source_coverage[i] <= 0.0f) ++transparent_pixels;
        if (labels[i] < 0 && source_coverage[i] > 0.0f) {
            uncertainty_sum += 4.0 * alpha[i] * (1.0 - alpha[i]) *
                source_coverage[i];
            unseeded_coverage += source_coverage[i];
        }
        alpha[i] *= source_coverage[i];
        foreground_pixels += alpha[i] > 0.5f;
    }
    const double foreground_fraction = static_cast<double>(foreground_pixels) / count;
    const double mean_uncertainty = unseeded_coverage <= 0.0
        ? 0.0 : uncertainty_sum / unseeded_coverage;
    const bool gates_passed = foreground_fraction >= minimum_foreground_fraction &&
        foreground_fraction <= maximum_foreground_fraction &&
        mean_uncertainty <= maximum_mean_uncertainty;
    const auto report_path = output_path + ".json";
    nlohmann::json report{
        {"schema", "chronon.scribble_segmentation.v1"},
        {"method", "seeded_linear_rgb_full_covariance_gaussian_models_with_edge_weighted_diffusion"},
        {"input", input_path}, {"output", output_path},
        {"matte_written", false},
        {"input_transfer", transfer}, {"working_space", "linear_bt709"},
        {"width", width}, {"height", height},
        {"iterations", iterations}, {"seed_radius_pixels", seed_radius},
        {"estimated_work_units", estimated_work_units},
        {"maximum_work_units", maximum_work_units},
        {"source_alpha_used_as_coverage", true},
        {"fully_transparent_pixels", transparent_pixels},
        {"foreground_seed_points", foreground_values},
        {"background_seed_points", background_values},
        {"seeded_pixel_count", static_cast<std::uint64_t>(count) -
            static_cast<std::uint64_t>(std::count(labels.begin(), labels.end(), std::int8_t{-1}))},
        {"foreground_fraction", foreground_fraction},
        {"mean_uncertainty", mean_uncertainty},
        {"uncertainty_definition", "source-alpha-weighted mean(4*soft_segmentation_alpha*(1-soft_segmentation_alpha)) over unseeded visible coverage"},
        {"gate", {
            {"status", gates_passed ? "pass" : "fail"},
            {"minimum_foreground_fraction", minimum_foreground_fraction},
            {"maximum_foreground_fraction", maximum_foreground_fraction},
            {"maximum_mean_uncertainty", maximum_mean_uncertainty}}},
        {"next_step", "inspect and correct matte, then use as mask-propagate seed"},
    };
    std::ofstream report_file(report_path, std::ios::binary | std::ios::trunc);
    if (!report_file) throw std::runtime_error("could not write segmentation report: " + report_path);
    report_file << report.dump(2) << '\n';
    if (!report_file.good()) throw std::runtime_error("could not finish segmentation report: " + report_path);
    if (!gates_passed) {
        spdlog::error("Scribble segmentation gate failed; inspect {}", report_path);
        return 2;
    }
    if (!write_gray(output_path, alpha, width, height))
        throw std::runtime_error("could not write segmented seed matte: " + output_path);
    report["matte_written"] = true;
    std::ofstream completed_report(report_path, std::ios::binary | std::ios::trunc);
    if (!completed_report)
        throw std::runtime_error("could not update segmentation report: " + report_path);
    completed_report << report.dump(2) << '\n';
    if (!completed_report.good())
        throw std::runtime_error("could not finish segmentation report: " + report_path);
    return 0;
}

} // namespace

void register_mask_propagation_command(CLI::App& app, CliContext& ctx) {
    struct SegmentState {
        std::string input;
        std::string output{"seed_matte.png"};
        std::string transfer{"srgb"};
        std::vector<std::string> foreground;
        std::vector<std::string> background;
        std::uint32_t seed_radius{5};
        std::uint32_t iterations{64};
        std::uint64_t maximum_work_units{2'000'000'000ULL};
        float minimum_foreground_fraction{0.001f};
        float maximum_foreground_fraction{0.999f};
        float maximum_mean_uncertainty{1.0f};
    };
    auto segment_state = std::make_shared<SegmentState>();
    auto* segment_command = app.add_subcommand(
        "mask-segment",
        "Create an agent-guided soft subject matte from foreground/background points on CPU");
    segment_command->add_option("input", segment_state->input,
                                "8-bit SDR PNG/JPEG source image")
        ->required();
    segment_command->add_option("--foreground", segment_state->foreground,
                                "Normalized foreground seed point X,Y; repeat to add points")
        ->required();
    segment_command->add_option("--background", segment_state->background,
                                "Normalized background seed point X,Y; repeat to add points")
        ->required();
    segment_command->add_option("--input-transfer", segment_state->transfer,
                                "Input interpretation: srgb (default) or rec709")
        ->check(CLI::IsMember(std::set<std::string>{"srgb", "rec709"}))
        ->default_val("srgb");
    segment_command->add_option("--seed-radius", segment_state->seed_radius,
                                "Radius in pixels assigned to each hard foreground/background point")
        ->check(CLI::Range(0U, 256U))
        ->default_val(5U);
    segment_command->add_option("--iterations", segment_state->iterations,
                                "Edge-weighted matte diffusion iterations")
        ->check(CLI::Range(1U, 500U))
        ->default_val(64U);
    segment_command->add_option("--maximum-work-units", segment_state->maximum_work_units,
                                "Fail before solving if pixel-iteration work exceeds this budget")
        ->check(CLI::Range(std::uint64_t{1}, std::numeric_limits<std::uint64_t>::max()))
        ->default_val(2'000'000'000ULL);
    segment_command->add_option("--minimum-foreground-fraction", segment_state->minimum_foreground_fraction,
                                "Fail the report gate when too little of the frame is foreground")
        ->check(CLI::Range(0.0f, 1.0f))
        ->default_val(0.001f);
    segment_command->add_option("--maximum-foreground-fraction", segment_state->maximum_foreground_fraction,
                                "Fail the report gate when too much of the frame is foreground")
        ->check(CLI::Range(0.0f, 1.0f))
        ->default_val(0.999f);
    segment_command->add_option("--maximum-mean-uncertainty", segment_state->maximum_mean_uncertainty,
                                "Gate on average matte ambiguity 4*alpha*(1-alpha); 1 disables this gate")
        ->check(CLI::Range(0.0f, 1.0f))
        ->default_val(1.0f);
    segment_command->add_option("-o,--output", segment_state->output,
                                "Output soft matte PNG (a JSON gate report is written beside it)")
        ->default_val("seed_matte.png");
    segment_command->callback([segment_state, &ctx] {
        try {
            ctx.exit_code = run_scribble_segmentation(
                segment_state->input, segment_state->output,
                segment_state->transfer, segment_state->foreground,
                segment_state->background, segment_state->seed_radius,
                segment_state->iterations, segment_state->maximum_work_units,
                segment_state->minimum_foreground_fraction,
                segment_state->maximum_foreground_fraction,
                segment_state->maximum_mean_uncertainty);
        } catch (const std::exception& error) {
            spdlog::error("Scribble segmentation failed: {}", error.what());
            ctx.exit_code = 1;
        }
    });

    auto state = std::make_shared<MaskPropagationState>();
    auto* command = app.add_subcommand(
        "mask-propagate",
        "Propagate an authored soft mask over ordered frames or tagged video on CPU");
    command->add_option("frames", state->frames,
                        "Ordered 8-bit SDR frame images");
    command->add_option("--video-input", state->source_video,
                        "Decode explicitly tagged SDR/HDR video directly instead of supplying frame images");
    command->add_option("--seed-mask", state->seed_mask,
                        "PNG/JPEG seed matte: alpha if transparent, otherwise linear luma")
        ->required();
    command->add_option("--seed-frame", state->seed_frame,
                        "Zero-based index of the frame matching the seed matte")
        ->required();
    command->add_option("--input-transfer", state->input_transfer,
                        "Transfer for extracted frame RGB; matte images remain linear coverage data")
        ->check(CLI::IsMember(std::set<std::string>{"srgb", "rec709"}))
        ->default_val("srgb");
    command->add_option("--correction-mask", state->correction_masks,
                        "Authored correction matte as FRAME=PATH; may be repeated and anchors propagation in both directions");
    command->add_option("-o,--output", state->output_pattern,
                        "Output PNG path pattern containing #### (default: mask_####.png)")
        ->default_val("mask_####.png");
    command->add_option("--minimum-confidence", state->minimum_confidence,
                        "Reject propagated pixels below this per-pixel confidence")
        ->check(CLI::Range(0.0f, 1.0f));
    command->add_option("--minimum-mean-confidence", state->minimum_mean_confidence,
                        "Fail the job when a non-anchor frame's mean pixel confidence is lower; 0 disables this gate")
        ->check(CLI::Range(0.0f, 1.0f))
        ->default_val(0.0f);
    command->add_option("--minimum-mean-foreground-confidence",
                        state->minimum_mean_foreground_confidence,
                        "Fail when mean confidence over propagated matte coverage is lower; 0 disables this gate")
        ->check(CLI::Range(0.0f, 1.0f))
        ->default_val(0.0f);
    command->add_option("--maximum-foreground-loss", state->maximum_foreground_loss,
                        "Fail when more than this fraction of propagated foreground samples lose a valid track; 1 disables this gate")
        ->check(CLI::Range(0.0f, 1.0f))
        ->default_val(1.0f);
    command->add_option("--maximum-total-work-units", state->maximum_total_work_units,
                        "Reject jobs whose estimated total CPU tracking work exceeds this budget (default: 100 billion)")
        ->check(CLI::Range(std::uint64_t{1}, std::numeric_limits<std::uint64_t>::max()))
        ->default_val(100'000'000'000ULL);
    command->add_option("--maximum-video-spool-bytes", state->maximum_video_spool_bytes,
                        "Reject direct video jobs whose estimated temporary decoded frames exceed this byte budget (default: 8 GiB)")
        ->check(CLI::Range(std::uint64_t{1}, std::numeric_limits<std::uint64_t>::max()))
        ->default_val(8ULL * 1024ULL * 1024ULL * 1024ULL);
    command->add_option("--refine-radius", state->refinement_radius,
                        "Apply edge-aware guided-filter refinement to propagated mattes; 0 disables it and authored anchors remain exact")
        ->check(CLI::Range(0U, 32U))
        ->default_val(0U);
    command->add_option("--refine-epsilon", state->refinement_epsilon,
                        "Regularization for the normalized linear-light guided filter")
        ->check(CLI::Range(1.0e-6f, 1.0f))
        ->default_val(1.0e-3f);
    command->add_option("--plan-input", state->plan_input,
                        "Optional V3 RenderPlan to update with a track-matte relation");
    command->add_option("--plan-output", state->plan_output,
                        "Write the matte-linked RenderPlan to this path");
    command->add_option("--target-layer", state->target_layer,
                        "Existing RenderPlan layer that receives the propagated matte");
    command->add_option("--matte-video", state->matte_video,
                        "Encoded matte video path, or output path with --encode-matte-video");
    command->add_flag("--encode-matte-video", state->encode_matte_video,
                      "Encode generated opaque grayscale masks with the native CPU H.264 encoder at the RenderPlan frame rate");
    command->add_option("--matte-layer-id", state->matte_layer_id,
                        "New video layer id for the matte source");
    command->add_option("--matte-type", state->matte_type,
                        "Track matte operation for the opaque grayscale matte video")
        ->check(CLI::IsMember(std::set<std::string>{
            "luma", "luma_inverted"}));
    command->callback([state, &ctx] {
        try {
            std::unique_ptr<TemporaryMaskSpool> source_spool;
            if (!state->source_video.empty()) {
                source_spool = std::make_unique<TemporaryMaskSpool>();
                decode_sdr_video_frames(*state, *source_spool);
            } else if (state->frames.empty()) {
                throw std::runtime_error("provide ordered frame images or --video-input");
            }
            ctx.exit_code = propagate(*state, state->minimum_confidence);
            if (ctx.exit_code == 0 && state->encode_matte_video) {
                const auto first = load_tracking_frame(
                    state->frames.front(), state->input_transfer);
                const auto verification = encode_propagated_matte_video(*state,
                    static_cast<std::uint32_t>(first->width()),
                    static_cast<std::uint32_t>(first->height()));
                append_encoded_video_verification(*state, verification);
            }
            if (ctx.exit_code == 0 && has_plan_patch_options(*state))
                attach_matte_video_to_render_plan(*state);
        } catch (const std::exception& error) {
            spdlog::error("Mask propagation failed: {}", error.what());
            ctx.exit_code = 1;
        }
    });

    auto input = std::make_shared<std::string>();
    auto output = std::make_shared<std::string>("seed_matte.png");
    auto transfer = std::make_shared<std::string>("srgb");
    auto key_r = std::make_shared<float>(0.0f);
    auto key_g = std::make_shared<float>(1.0f);
    auto key_b = std::make_shared<float>(0.0f);
    auto threshold = std::make_shared<float>(0.20f);
    auto softness = std::make_shared<float>(0.08f);
    auto clip_black = std::make_shared<float>(0.0f);
    auto clip_white = std::make_shared<float>(1.0f);
    auto choke = std::make_shared<float>(0.0f);
    auto garbage_x = std::make_shared<float>(0.0f);
    auto garbage_y = std::make_shared<float>(0.0f);
    auto garbage_w = std::make_shared<float>(0.0f);
    auto garbage_h = std::make_shared<float>(0.0f);
    auto garbage_feather = std::make_shared<float>(0.0f);
    auto min_foreground_fraction = std::make_shared<float>(0.0f);
    auto max_foreground_fraction = std::make_shared<float>(1.0f);
    auto* key_command = app.add_subcommand(
        "mask-key",
        "Generate a CPU chroma-key seed matte from one PNG/JPEG frame");
    key_command->add_option("frame", *input, "Input PNG/JPEG frame")->required();
    key_command->add_option("-o,--output", *output, "Output opaque grayscale matte PNG")
        ->default_val("seed_matte.png");
    key_command->add_option("--input-transfer", *transfer,
        "Frame transfer function (sRGB default or Rec.709)")
        ->check(CLI::IsMember(std::set<std::string>{"srgb", "rec709"}))
        ->default_val("srgb");
    key_command->add_option("--key-r", *key_r, "Linear BT.709 key color red component")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.0f);
    key_command->add_option("--key-g", *key_g, "Linear BT.709 key color green component")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(1.0f);
    key_command->add_option("--key-b", *key_b, "Linear BT.709 key color blue component")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.0f);
    key_command->add_option("--threshold", *threshold,
        "Normalized chroma distance below which the screen is removed")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.20f);
    key_command->add_option("--softness", *softness,
        "Soft transition width above the chroma threshold")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.08f);
    key_command->add_option("--clip-black", *clip_black,
        "Remap matte values at or below this value to zero")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.0f);
    key_command->add_option("--clip-white", *clip_white,
        "Remap matte values at or above this value to one")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(1.0f);
    key_command->add_option("--choke", *choke,
        "Erode the keyed edge in coverage space")
        ->check(CLI::Range(0.0f, 0.999f))->default_val(0.0f);
    key_command->add_option("--garbage-x", *garbage_x,
        "Normalized keep-rectangle left edge (0 disables garbage matte)")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.0f);
    key_command->add_option("--garbage-y", *garbage_y,
        "Normalized keep-rectangle top edge")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.0f);
    key_command->add_option("--garbage-width", *garbage_w,
        "Normalized keep-rectangle width (0 disables garbage matte)")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.0f);
    key_command->add_option("--garbage-height", *garbage_h,
        "Normalized keep-rectangle height (0 disables garbage matte)")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.0f);
    key_command->add_option("--garbage-feather", *garbage_feather,
        "Normalized soft edge for the garbage rectangle")
        ->check(CLI::Range(0.0f, 0.5f))->default_val(0.0f);
    key_command->add_option("--minimum-foreground-fraction", *min_foreground_fraction,
        "Fail if fewer than this fraction of pixels have coverage above 0.5")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(0.0f);
    key_command->add_option("--maximum-foreground-fraction", *max_foreground_fraction,
        "Fail if more than this fraction of pixels have coverage above 0.5")
        ->check(CLI::Range(0.0f, 1.0f))->default_val(1.0f);
    key_command->callback([input, output, transfer, key_r, key_g, key_b,
                           threshold, softness, clip_black, clip_white, choke,
                           garbage_x, garbage_y, garbage_w, garbage_h,
                           garbage_feather, min_foreground_fraction,
                           max_foreground_fraction, &ctx] {
        try {
            if (*clip_white <= *clip_black)
                throw std::runtime_error("--clip-white must be greater than --clip-black");
            if (*min_foreground_fraction > *max_foreground_fraction)
                throw std::runtime_error(
                    "--minimum-foreground-fraction must not exceed --maximum-foreground-fraction");
            if ((*garbage_w == 0.0f) != (*garbage_h == 0.0f) ||
                *garbage_x + *garbage_w > 1.0f ||
                *garbage_y + *garbage_h > 1.0f)
                throw std::runtime_error(
                    "garbage matte requires both width and height, and its rectangle must fit normalized image bounds");
            auto frame = load_tracking_frame(*input, *transfer);
            const auto width = static_cast<std::uint32_t>(frame->width());
            const auto height = static_cast<std::uint32_t>(frame->height());
            if (static_cast<std::uint64_t>(width) * height > 8'000'000)
                throw std::runtime_error("frame size exceeds the 8-megapixel CPU keying limit");
            KeyingParams params;
            params.mode = KeyingMode::Chroma;
            params.key_color = Color{*key_r, *key_g, *key_b, 1.0f};
            params.threshold = *threshold;
            params.softness = *softness;
            params.spill = 0.0f;
            params.clip_black = *clip_black;
            params.clip_white = *clip_white;
            params.choke = *choke;
            params.garbage_x = *garbage_x;
            params.garbage_y = *garbage_y;
            params.garbage_w = *garbage_w;
            params.garbage_h = *garbage_h;
            params.garbage_feather = *garbage_feather;
            std::vector<float> alpha(static_cast<std::size_t>(width) * height);
            double coverage_sum = 0.0;
            std::uint64_t foreground_pixels = 0;
            std::uint64_t soft_edge_pixels = 0;
            float coverage_min = 1.0f;
            float coverage_max = 0.0f;
            for (std::uint32_t y = 0; y < height; ++y) {
                for (std::uint32_t x = 0; x < width; ++x) {
                    const auto pixel = frame->get_pixel(static_cast<i32>(x), static_cast<i32>(y));
                    const float coverage =
                        effects::keying_coverage(pixel.unpremultiplied(),
                            static_cast<int>(x), static_cast<int>(y),
                            static_cast<int>(width), static_cast<int>(height), params);
                    alpha[static_cast<std::size_t>(y) * width + x] = coverage;
                    coverage_sum += coverage;
                    coverage_min = std::min(coverage_min, coverage);
                    coverage_max = std::max(coverage_max, coverage);
                    foreground_pixels += coverage > 0.5f;
                    soft_edge_pixels += coverage > 0.0f && coverage < 1.0f;
                }
            }
            const double pixel_count = static_cast<double>(alpha.size());
            const double foreground_fraction = pixel_count == 0.0
                ? 0.0 : static_cast<double>(foreground_pixels) / pixel_count;
            const bool gate_passed = foreground_fraction >= *min_foreground_fraction &&
                                     foreground_fraction <= *max_foreground_fraction;
            // The foreground-fraction gate is an acceptance boundary: do not
            // publish a seed matte that the caller has explicitly rejected.
            if (gate_passed && !write_gray(*output, alpha, width, height))
                throw std::runtime_error("could not write generated seed matte: " + *output);
            std::cout << nlohmann::json{
                {"schema", "chronon.chroma_key_matte.v1"},
                {"input", *input}, {"output", *output},
                {"output_written", gate_passed},
                {"width", width}, {"height", height},
                {"input_transfer", *transfer},
                {"coverage_statistics", {
                    {"minimum", coverage_min}, {"maximum", coverage_max},
                    {"mean", pixel_count == 0.0 ? 0.0 : coverage_sum / pixel_count},
                    {"foreground_threshold", 0.5f},
                    {"foreground_pixels", foreground_pixels},
                    {"foreground_fraction", foreground_fraction},
                    {"soft_edge_pixels", soft_edge_pixels}}},
                {"foreground_fraction_gate", {
                    {"minimum", *min_foreground_fraction},
                    {"maximum", *max_foreground_fraction},
                    {"passed", gate_passed}}},
                {"key_color_linear_bt709", {*key_r, *key_g, *key_b}},
                {"threshold", *threshold}, {"softness", *softness},
                {"clip_black", *clip_black}, {"clip_white", *clip_white},
                {"choke", *choke},
                {"garbage_matte", {
                    {"x", *garbage_x}, {"y", *garbage_y},
                    {"width", *garbage_w}, {"height", *garbage_h},
                    {"feather", *garbage_feather}}},
                {"intended_use", "mask-propagate seed matte"}
            }.dump(2) << '\n';
            if (!gate_passed) {
                spdlog::error(
                    "Chroma-key foreground fraction {} is outside [{}, {}]",
                    foreground_fraction, *min_foreground_fraction,
                    *max_foreground_fraction);
                ctx.exit_code = 2;
            } else {
                ctx.exit_code = 0;
            }
        } catch (const std::exception& error) {
            spdlog::error("Mask keying failed: {}", error.what());
            ctx.exit_code = 1;
        }
    });
}

} // namespace chronon3d::cli
