#include <chronon3d/render_plan/render_plan.hpp>

#include <chronon3d/core/hash/hash_builder.hpp>
#include <chronon3d/render_plan/render_plan_validator.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace chronon3d::render_plan {
namespace {

std::uint64_t fingerprint_render_plan_impl(const RenderPlan& plan,
                                          bool include_output_settings) {
    auto hash = chronon3d::core::hash::HashBuilder{}
        .add("chronon3d.render-plan.fingerprint.v2")
        // job_id is runtime routing metadata, not content identity.
        .add("")
        .add(plan.canvas.width)
        .add(plan.canvas.height)
        .add(plan.canvas.fps)
        .add(plan.canvas.duration)
        .add(plan.layers.size());

    for (const auto& layer : plan.layers) {
        hash.add(layer.id).add_enum(layer.type).add(layer.asset).add(layer.source)
            .add(layer.text).add(layer.font).add(layer.preset)
            .add(layer.font_size.has_value());
        if (layer.font_size) hash.add(*layer.font_size);
        hash.add(layer.box_width.has_value());
        if (layer.box_width) hash.add(*layer.box_width);
        hash.add(layer.box_height.has_value());
        if (layer.box_height) hash.add(*layer.box_height);
        for (const auto value : layer.color) hash.add(value);
        for (const auto value : layer.position) hash.add(value);
        hash.add(layer.position_dimensions).add(layer.start_frame.has_value());
        if (layer.start_frame) hash.add(*layer.start_frame);
        hash.add(layer.duration_frames.has_value());
        if (layer.duration_frames) hash.add(*layer.duration_frames);
        hash.add(layer.fit.has_value());
        if (layer.fit) hash.add_enum(*layer.fit);
        hash.add(layer.subtitle_format.has_value());
        if (layer.subtitle_format) hash.add_enum(*layer.subtitle_format);
        hash.add(layer.animation.has_value());
        if (layer.animation) {
            hash.add(layer.animation->start_frame.has_value());
            if (layer.animation->start_frame) hash.add(*layer.animation->start_frame);
            hash.add(layer.animation->duration_frames.has_value());
            if (layer.animation->duration_frames) hash.add(*layer.animation->duration_frames);
            hash.add(layer.animation->preset);
        }
    }

    hash.add(plan.audio_tracks.size());
    for (const auto& track : plan.audio_tracks) {
        hash.add(track.source).add(track.volume).add(track.start_time_offset)
            .add(track.duration_seconds).add(track.role).add(track.loop)
            .add(track.fade_in_seconds).add(track.fade_out_seconds)
            .add(track.ducking_enabled);
    }

    // Output paths are always excluded because they identify a machine-local
    // destination. Request identity may retain the deterministic output
    // encoding settings; content identity must not.
    if (include_output_settings) {
        hash.add_enum(plan.output.format)
            .add_enum(plan.output.codec)
            .add(plan.output.bitrate)
            .add(plan.output.crf);
    }
    return hash.finish();
}

template <typename T>
std::optional<T> optional_value(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) return std::nullopt;
    return object.at(key).get<T>();
}

std::optional<Frame> optional_frame(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) return std::nullopt;
    return Frame{object.at(key).get<std::int64_t>()};
}

bool invalid_logical_path(const std::string& value) {
    if (value.empty()) return false;
    const std::filesystem::path path{value};
    if (path.is_absolute()) return true;
    for (const auto& component : path) {
        if (component == std::filesystem::path("..")) return true;
    }
    return false;
}

LayerType layer_type(const std::string& value) {
    if (value == "image") return LayerType::Image;
    if (value == "video") return LayerType::Video;
    if (value == "text") return LayerType::Text;
    if (value == "subtitle_track") return LayerType::SubtitleTrack;
    return LayerType::Color;
}

FitMode fit_mode(const std::string& value) {
    if (value == "contain") return FitMode::Contain;
    if (value == "stretch") return FitMode::Stretch;
    if (value == "none") return FitMode::None;
    return FitMode::Cover;
}

SubtitleFormat subtitle_format(const std::string& value) {
    if (value == "vtt") return SubtitleFormat::Vtt;
    if (value == "json") return SubtitleFormat::Json;
    return SubtitleFormat::Srt;
}

OutputFormat output_format(const std::string& value) {
    if (value == "mp4") return OutputFormat::Mp4;
    if (value == "mkv") return OutputFormat::Mkv;
    if (value == "webm") return OutputFormat::WebM;
    return OutputFormat::Png;
}

VideoCodec video_codec(const std::string& value) {
    if (value == "h264") return VideoCodec::H264;
    if (value == "h265") return VideoCodec::H265;
    if (value == "vp9") return VideoCodec::VP9;
    if (value == "av1") return VideoCodec::AV1;
    return VideoCodec::Auto;
}

LayerPlan decode_layer(const nlohmann::json& value) {
    LayerPlan layer;
    layer.id = value.at("id").get<std::string>();
    layer.type = layer_type(value.at("type").get<std::string>());
    layer.asset = value.value("asset", std::string{});
    layer.source = value.value("source", std::string{});
    layer.text = value.value("text", std::string{});
    layer.font = value.value("font", std::string{});
    layer.preset = value.value("preset", std::string{});
    layer.font_size = optional_value<float>(value, "font_size");
    layer.box_width = optional_value<float>(value, "box_width");
    layer.box_height = optional_value<float>(value, "box_height");
    if (value.contains("color")) {
        const auto& color = value.at("color");
        for (std::size_t index = 0; index < color.size() && index < layer.color.size(); ++index)
            layer.color[index] = color.at(index).get<float>();
    }
    layer.start_frame = optional_frame(value, "start_frame");
    layer.duration_frames = optional_frame(value, "duration_frames");
    if (value.contains("position")) {
        const auto& position = value.at("position");
        layer.position_dimensions = position.size();
        for (std::size_t index = 0; index < position.size(); ++index)
            layer.position[index] = position.at(index).get<float>();
    }
    if (value.contains("fit")) layer.fit = fit_mode(value.at("fit").get<std::string>());
    if (value.contains("format"))
        layer.subtitle_format = subtitle_format(value.at("format").get<std::string>());
    if (value.contains("animation")) {
        const auto& animation = value.at("animation");
        layer.animation = AnimationTiming{
            optional_frame(animation, "start_frame"),
            optional_frame(animation, "duration_frames"),
            animation.at("preset").get<std::string>()};
    }
    return layer;
}

}  // namespace

std::optional<PlanDecodeError> validate_render_plan_budget(
    const RenderPlan& plan, const RenderBudget& budget) {
    const auto fail = [](std::string path, std::string message) {
        return std::optional<PlanDecodeError>{
            PlanDecodeError{std::move(path), std::move(message)}};
    };
    if (plan.canvas.width > static_cast<int>(budget.max_width))
        return fail("canvas.width", "render budget max_width exceeded");
    if (plan.canvas.height > static_cast<int>(budget.max_height))
        return fail("canvas.height", "render budget max_height exceeded");
    if (plan.layers.size() > budget.max_layers)
        return fail("layers", "render budget max_layers exceeded");
    if (plan.audio_tracks.size() > budget.max_audio_tracks)
        return fail("audio_tracks", "render budget max_audio_tracks exceeded");

    const auto width = static_cast<std::uint64_t>(plan.canvas.width);
    const auto height = static_cast<std::uint64_t>(plan.canvas.height);
    if (height != 0 && width > budget.max_total_pixels / height)
        return fail("canvas", "render budget max_total_pixels exceeded");
    const auto total_pixels = width * height;
    if (total_pixels > budget.max_total_pixels)
        return fail("canvas", "render budget max_total_pixels exceeded");

    const auto frames = static_cast<std::uint64_t>(plan.canvas.duration.integral());
    if (frames > budget.max_frames)
        return fail("canvas.duration_frames", "render budget max_frames exceeded");

    std::uint64_t text_bytes = 0;
    std::uint64_t asset_reference_bytes = 0;
    const auto add_bytes = [](std::uint64_t& total, std::size_t value,
                              std::uint64_t limit) {
        if (value > limit - std::min(total, limit)) return false;
        total += static_cast<std::uint64_t>(value);
        return true;
    };
    for (const auto& layer : plan.layers) {
        if (!add_bytes(text_bytes, layer.text.size(), budget.max_text_bytes))
            return fail("layers[].text", "render budget max_text_bytes exceeded");
        for (const auto* reference : {&layer.asset, &layer.source, &layer.font}) {
            if (!add_bytes(asset_reference_bytes, reference->size(),
                           budget.max_asset_reference_bytes))
                return fail("layers[]", "render budget max_asset_reference_bytes exceeded");
        }
    }
    for (const auto& track : plan.audio_tracks) {
        if (!add_bytes(asset_reference_bytes, track.source.size(),
                       budget.max_asset_reference_bytes))
            return fail("audio_tracks[].source",
                        "render budget max_asset_reference_bytes exceeded");
    }

    if (total_pixels != 0 && total_pixels > budget.max_peak_memory_bytes / 16)
        return fail("canvas", "render budget max_peak_memory_bytes exceeded");
    if (frames != 0 && total_pixels != 0 &&
        (total_pixels > std::numeric_limits<std::uint64_t>::max() / 4 / frames ||
         total_pixels * 4 * frames > budget.max_estimated_output_bytes)) {
        return fail("canvas", "render budget max_estimated_output_bytes exceeded");
    }
    return std::nullopt;
}

std::uint64_t compute_render_plan_content_fingerprint(const RenderPlan& plan) {
    return fingerprint_render_plan_impl(plan, false);
}

Result<RenderPlan, PlanDecodeError> decode_render_plan(const nlohmann::json& root) {
    const auto validation = validate_render_plan(root);
    if (!validation.ok())
        return PlanDecodeError{"", validation.format()};

    try {
        RenderPlan plan;
        plan.job_id = root.value("job_id", std::string{"chronon_plan"});
        const auto& canvas = root.at("canvas");
        plan.canvas = CanvasSpec{
            canvas.at("width").get<int>(),
            canvas.at("height").get<int>(),
            canvas.at("fps").get<int>(),
            Frame{canvas.at("duration_frames").get<std::int64_t>()}};
        for (const auto& layer : root.at("layers")) {
            auto decoded_layer = decode_layer(layer);
            if (invalid_logical_path(decoded_layer.asset)) {
                return PlanDecodeError{"layers[].asset",
                    "asset references must be relative logical paths"};
            }
            if (invalid_logical_path(decoded_layer.source)) {
                return PlanDecodeError{"layers[].source",
                    "source references must be relative logical paths"};
            }
            if (invalid_logical_path(decoded_layer.font)) {
                return PlanDecodeError{"layers[].font",
                    "font references must be relative logical paths"};
            }
            plan.layers.push_back(std::move(decoded_layer));
        }
        for (const auto& value : root.value("audio_tracks", nlohmann::json::array())) {
            const auto source = value.at("source").get<std::string>();
            if (invalid_logical_path(source)) {
                return PlanDecodeError{"audio_tracks[].source",
                    "audio references must be relative logical paths"};
            }
            plan.audio_tracks.push_back(AudioTrackPlan{
                source,
                value.value("volume", 1.0),
                value.value("start_time_offset", 0.0),
                value.value("duration_seconds", 0.0),
                value.value("role", std::string{}),
                value.value("loop", false),
                value.value("fade_in_seconds", 0.0),
                value.value("fade_out_seconds", 0.0),
                value.value("ducking_enabled", false)});
        }
        if (const auto budget_error = validate_render_plan_budget(plan))
            return *budget_error;
        const auto& output = root.at("output");
        plan.output.path = output.at("path").get<std::string>();
        if (output.contains("format"))
            plan.output.format = output_format(output.at("format").get<std::string>());
        if (output.contains("codec"))
            plan.output.codec = video_codec(output.at("codec").get<std::string>());
        plan.output.bitrate = output.value("bitrate", std::int64_t{0});
        plan.output.crf = output.value("crf", 0);
        plan.content_fingerprint = compute_render_plan_content_fingerprint(plan);
        return plan;
    } catch (const std::exception& error) {
        return PlanDecodeError{"", error.what()};
    }
}

}  // namespace chronon3d::render_plan
