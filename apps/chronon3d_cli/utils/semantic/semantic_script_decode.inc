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
    script.canvas.fps = FrameRate{canvas.value("fps_num", canvas.value("fps", 30)),
                                   canvas.value("fps_den", 1)};
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
