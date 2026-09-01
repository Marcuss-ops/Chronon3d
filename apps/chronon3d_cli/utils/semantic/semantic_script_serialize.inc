nlohmann::json render_plan_to_json(const render_plan::RenderPlan& plan) {
    nlohmann::json root;
    root["schema"] = "chronon.render-plan";
    root["version"] = 1;
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
