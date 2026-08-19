#include "semantic_script.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace chronon3d::cli::semantic {
namespace {

// ── String → enum helpers (mirror the render-plan decoder's vocabulary) ────

SemanticKind parse_kind(const std::string& value) {
    if (value == "important_phrase") return SemanticKind::ImportantPhrase;
    if (value == "important_word") return SemanticKind::ImportantWord;
    return SemanticKind::ImageOverlay;
}

render_plan::FitMode parse_fit(const std::string& value) {
    if (value == "contain") return render_plan::FitMode::Contain;
    if (value == "stretch") return render_plan::FitMode::Stretch;
    if (value == "none") return render_plan::FitMode::None;
    return render_plan::FitMode::Cover;
}

render_plan::OutputFormat parse_output_format(const std::string& value) {
    if (value == "mp4") return render_plan::OutputFormat::Mp4;
    if (value == "mkv") return render_plan::OutputFormat::Mkv;
    if (value == "webm") return render_plan::OutputFormat::WebM;
    return render_plan::OutputFormat::Png;
}

render_plan::VideoCodec parse_codec(const std::string& value) {
    if (value == "h264") return render_plan::VideoCodec::H264;
    if (value == "h265") return render_plan::VideoCodec::H265;
    if (value == "vp9") return render_plan::VideoCodec::VP9;
    if (value == "av1") return render_plan::VideoCodec::AV1;
    return render_plan::VideoCodec::Auto;
}

// ── enum → string helpers (serialization) ──────────────────────────────────

std::string kind_name(render_plan::LayerType type) {
    switch (type) {
        case render_plan::LayerType::Image: return "image";
        case render_plan::LayerType::Video: return "video";
        case render_plan::LayerType::Text: return "text";
        case render_plan::LayerType::Color: return "color";
        case render_plan::LayerType::SubtitleTrack: return "subtitle_track";
    }
    return "color";
}

std::string fit_name(render_plan::FitMode fit) {
    switch (fit) {
        case render_plan::FitMode::Contain: return "contain";
        case render_plan::FitMode::Stretch: return "stretch";
        case render_plan::FitMode::None: return "none";
        case render_plan::FitMode::Cover: return "cover";
    }
    return "cover";
}

std::string format_name(render_plan::OutputFormat format) {
    switch (format) {
        case render_plan::OutputFormat::Mp4: return "mp4";
        case render_plan::OutputFormat::Mkv: return "mkv";
        case render_plan::OutputFormat::WebM: return "webm";
        case render_plan::OutputFormat::Png: return "png";
    }
    return "png";
}

std::string codec_name(render_plan::VideoCodec codec) {
    switch (codec) {
        case render_plan::VideoCodec::H264: return "h264";
        case render_plan::VideoCodec::H265: return "h265";
        case render_plan::VideoCodec::VP9: return "vp9";
        case render_plan::VideoCodec::AV1: return "av1";
        case render_plan::VideoCodec::Auto: return "auto";
    }
    return "auto";
}

SemanticError make_error(std::string path, std::string message) {
    return SemanticError{std::move(path), std::move(message)};
}

bool decode_background(const nlohmann::json& value, SemanticBackground& out) {
    if (value.is_string()) {
        out.asset = value.get<std::string>();
        return true;
    }
    if (!value.is_object()) return false;
    out.asset = value.value("asset", std::string{});
    if (value.contains("fit")) out.fit = parse_fit(value.at("fit").get<std::string>());
    if (value.contains("color")) {
        const auto& color = value.at("color");
        for (std::size_t index = 0; index < color.size() && index < out.color.size(); ++index)
            out.color[index] = color.at(index).get<float>();
    }
    return true;
}

}  // namespace

Result<SemanticScript, SemanticError> decode_semantic_script(
    const nlohmann::json& root) {
    if (!root.is_object())
        return make_error("$", "semantic script must be a JSON object");

    SemanticScript script;

    script.job_id = root.value("job_id", std::string{"script_overlay"});

    if (!root.contains("canvas"))
        return make_error("canvas", "missing required canvas object");
    const auto& canvas = root.at("canvas");
    script.canvas.width = canvas.value("width", 1920);
    script.canvas.height = canvas.value("height", 1080);
    script.canvas.fps = canvas.value("fps", 30);
    if (canvas.contains("duration_frames"))
        script.canvas.duration = Frame{canvas.at("duration_frames").get<std::int64_t>()};

    if (root.contains("background")) {
        SemanticBackground background;
        if (!decode_background(root.at("background"), background))
            return make_error("background",
                              "must be an asset string or an {asset,color,fit} object");
        script.background = std::move(background);
    }

    if (!root.contains("events"))
        return make_error("events", "missing required events array");
    const auto& events = root.at("events");
    if (!events.is_array())
        return make_error("events", "events must be an array");

    for (std::size_t index = 0; index < events.size(); ++index) {
        const auto& value = events.at(index);
        const std::string path = "events[" + std::to_string(index) + "]";
        if (!value.is_object())
            return make_error(path, "event must be an object");
        if (!value.contains("id"))
            return make_error(path + ".id", "missing required id");
        if (!value.contains("kind"))
            return make_error(path + ".kind", "missing required kind");

        SemanticOverlay overlay;
        overlay.id = value.at("id").get<std::string>();
        const std::string kind = value.at("kind").get<std::string>();
        if (kind != "important_phrase" && kind != "important_word" &&
            kind != "image_overlay")
            return make_error(path + ".kind",
                              "unknown semantic kind '" + kind +
                                  "' (expected important_phrase | important_word | image_overlay)");
        overlay.kind = parse_kind(kind);
        overlay.text = value.value("text", std::string{});
        overlay.asset = value.value("asset", std::string{});
        if (value.contains("start_frame"))
            overlay.start_frame = Frame{value.at("start_frame").get<std::int64_t>()};
        if (value.contains("duration_frames"))
            overlay.duration_frames = Frame{value.at("duration_frames").get<std::int64_t>()};
        if (value.contains("preset"))
            overlay.preset = value.at("preset").get<std::string>();
        if (value.contains("fit"))
            overlay.fit = value.at("fit").get<std::string>();
        if (value.contains("box_width"))
            overlay.box_width = value.at("box_width").get<float>();
        if (value.contains("box_height"))
            overlay.box_height = value.at("box_height").get<float>();
        if (value.contains("position")) {
            const auto& position = value.at("position");
            std::array<float, 2> pos{0.0f, 0.0f};
            for (std::size_t axis = 0; axis < position.size() && axis < pos.size(); ++axis)
                pos[axis] = position.at(axis).get<float>();
            overlay.position = pos;
        }
        if (value.contains("animation")) {
            const auto& animation = value.at("animation");
            render_plan::AnimationTiming timing;
            timing.preset = animation.value("preset", std::string{});
            if (animation.contains("start_frame"))
                timing.start_frame = Frame{animation.at("start_frame").get<std::int64_t>()};
            if (animation.contains("duration_frames"))
                timing.duration_frames =
                    Frame{animation.at("duration_frames").get<std::int64_t>()};
            overlay.animation = std::move(timing);
        }
        script.events.push_back(std::move(overlay));
    }

    if (root.contains("output")) {
        const auto& output = root.at("output");
        script.output.path = output.value("path", std::string{});
        if (output.contains("format"))
            script.output.format = parse_output_format(output.at("format").get<std::string>());
        if (output.contains("codec"))
            script.output.codec = parse_codec(output.at("codec").get<std::string>());
        script.output.bitrate = output.value("bitrate", std::int64_t{0});
        script.output.crf = output.value("crf", 0);
    }

    return script;
}

render_plan::RenderPlan compile_semantic_script(const SemanticScript& script) {
    render_plan::RenderPlan plan;
    plan.job_id = script.job_id;
    plan.canvas = script.canvas;
    plan.output = script.output;

    // Backdrop first so every overlay composites above it.
    if (script.background) {
        render_plan::LayerPlan background;
        background.id = "background";
        background.start_frame = Frame{0};
        background.duration_frames = script.canvas.duration;
        if (!script.background->asset.empty()) {
            background.type = render_plan::LayerType::Image;
            background.asset = script.background->asset;
            background.box_width = static_cast<float>(script.canvas.width);
            background.box_height = static_cast<float>(script.canvas.height);
            background.fit = script.background->fit;
        } else {
            background.type = render_plan::LayerType::Color;
            background.color = script.background->color;
        }
        plan.layers.push_back(std::move(background));
    }

    for (const auto& overlay : script.events) {
        render_plan::LayerPlan layer;
        layer.id = overlay.id;
        layer.start_frame = overlay.start_frame;
        layer.duration_frames = overlay.duration_frames > Frame{0}
            ? overlay.duration_frames
            : Frame{script.canvas.duration.integral() - overlay.start_frame.integral()};
        layer.box_width = overlay.box_width;
        layer.box_height = overlay.box_height;
        if (overlay.position) {
            layer.position[0] = (*overlay.position)[0];
            layer.position[1] = (*overlay.position)[1];
            layer.position_dimensions = 2;
        }
        layer.animation = overlay.animation;

        switch (overlay.kind) {
            case SemanticKind::ImportantPhrase:
                layer.type = render_plan::LayerType::Text;
                layer.text = overlay.text;
                layer.preset = overlay.preset.value_or("caption_safe_area");
                break;
            case SemanticKind::ImportantWord:
                layer.type = render_plan::LayerType::Text;
                layer.text = overlay.text;
                layer.preset = overlay.preset.value_or("kinetic_word");
                break;
            case SemanticKind::ImageOverlay:
                layer.type = render_plan::LayerType::Image;
                layer.asset = overlay.asset;
                layer.fit = overlay.fit
                    ? parse_fit(*overlay.fit)
                    : render_plan::FitMode::Contain;
                break;
        }
        plan.layers.push_back(std::move(layer));
    }

    return plan;
}

nlohmann::json render_plan_to_json(const render_plan::RenderPlan& plan) {
    nlohmann::json root;
    root["schema"] = "chronon.render-plan";
    root["version"] = 1;
    root["job_id"] = plan.job_id;

    root["canvas"] = {
        {"width", plan.canvas.width},
        {"height", plan.canvas.height},
        {"fps", plan.canvas.fps},
        {"duration_frames", plan.canvas.duration.integral()},
    };

    root["layers"] = nlohmann::json::array();
    for (const auto& layer : plan.layers) {
        nlohmann::json value;
        value["id"] = layer.id;
        value["type"] = kind_name(layer.type);
        if (!layer.asset.empty()) value["asset"] = layer.asset;
        if (!layer.source.empty()) value["source"] = layer.source;
        if (!layer.text.empty()) value["text"] = layer.text;
        if (!layer.font.empty()) value["font"] = layer.font;
        if (!layer.preset.empty()) value["preset"] = layer.preset;
        if (layer.font_size) value["font_size"] = *layer.font_size;
        if (layer.box_width) value["box_width"] = *layer.box_width;
        if (layer.box_height) value["box_height"] = *layer.box_height;
        if (layer.type == render_plan::LayerType::Color) {
            value["color"] = {layer.color[0], layer.color[1], layer.color[2], layer.color[3]};
        }
        if (layer.position_dimensions >= 2) {
            if (layer.position_dimensions == 2)
                value["position"] = {layer.position[0], layer.position[1]};
            else
                value["position"] = {layer.position[0], layer.position[1], layer.position[2]};
        }
        if (layer.start_frame)
            value["start_frame"] = layer.start_frame->integral();
        if (layer.duration_frames)
            value["duration_frames"] = layer.duration_frames->integral();
        if (layer.fit) value["fit"] = fit_name(*layer.fit);
        if (layer.animation) {
            nlohmann::json animation;
            animation["preset"] = layer.animation->preset;
            if (layer.animation->start_frame)
                animation["start_frame"] = layer.animation->start_frame->integral();
            if (layer.animation->duration_frames)
                animation["duration_frames"] = layer.animation->duration_frames->integral();
            value["animation"] = std::move(animation);
        }
        root["layers"].push_back(std::move(value));
    }

    root["output"] = {
        {"path", plan.output.path},
        {"format", format_name(plan.output.format)},
        {"codec", codec_name(plan.output.codec)},
    };
    if (plan.output.bitrate != 0) root["output"]["bitrate"] = plan.output.bitrate;
    if (plan.output.crf != 0) root["output"]["crf"] = plan.output.crf;

    return root;
}

}  // namespace chronon3d::cli::semantic
