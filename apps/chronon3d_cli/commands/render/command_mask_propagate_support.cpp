#include "command_mask_propagate_detail.hpp"
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/media/video/media_probe.hpp>
#include <chronon3d/media/video/video_sink_factory.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace chronon3d::cli::mask_propagation_detail {
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
} // namespace chronon3d::cli::mask_propagation_detail
