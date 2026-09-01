nlohmann::json render_plan_to_json(const render_plan::RenderPlan& plan) {
    nlohmann::json root;
    root["schema"] = "chronon.render-plan.v2";
    root["version"] = 2;
    root["job_id"] = plan.job_id;

    root["canvas"] = {
        {"width", plan.canvas.width},
        {"height", plan.canvas.height},
        {"fps_num", plan.canvas.fps.num()},
        {"fps_den", plan.canvas.fps.den()},
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
        if (layer.style) {
            nlohmann::json style;
            style["font"] = layer.style->font;
            if (layer.style->font_size) style["font_size"] = *layer.style->font_size;
            style["fill"] = layer.style->fill;
            value["style"] = std::move(style);
        }
        if (layer.size_dimensions == 2) {
            value["size"] = {layer.size[0], layer.size[1]};
        }
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
            nlohmann::json tracks = nlohmann::json::array();
            for (const auto& track : layer.animation->tracks) {
                nlohmann::json track_json;
                track_json["property"] = track.property;
                track_json["easing"] = track.easing;
                nlohmann::json kfs = nlohmann::json::array();
                for (const auto& kf : track.keyframes) {
                    if (kf.value.size() == 1)
                        kfs.push_back({{"frame", kf.frame.integral()}, {"value", kf.value[0]}});
                    else
                        kfs.push_back({{"frame", kf.frame.integral()}, {"value", kf.value}});
                }
                track_json["keyframes"] = std::move(kfs);
                tracks.push_back(std::move(track_json));
            }
            animation["tracks"] = std::move(tracks);
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
