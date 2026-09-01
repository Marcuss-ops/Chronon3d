Result<RenderPlan, PlanDecodeError> decode_render_plan(const nlohmann::json& root) {
    const auto validation = validate_render_plan(root);
    if (!validation.ok())
        return PlanDecodeError{"", validation.format()};

    try {
        RenderPlan plan;
        plan.job_id = root.value("job_id", std::string{"chronon_plan"});
        plan.style_profile = root.value("style_profile", std::string{"discovery"});
        if (plan.style_profile != "discovery" && plan.style_profile != "young" &&
            plan.style_profile != "crime") {
            return PlanDecodeError{"style_profile", "must be discovery, young, or crime"};
        }
        const auto& canvas = root.at("canvas");
        plan.canvas = CanvasSpec{
            canvas.at("width").get<int>(),
            canvas.at("height").get<int>(),
            FrameRate{canvas.at("fps_num").get<int>(), canvas.at("fps_den").get<int>()},
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
            if (decoded_layer.background &&
                invalid_logical_path(decoded_layer.background->asset)) {
                return PlanDecodeError{"layers[].background.asset",
                    "background references must be relative logical paths"};
            }
            if (invalid_logical_path(decoded_layer.font)) {
                return PlanDecodeError{"layers[].font",
                    "font references must be relative logical paths"};
            }
            plan.layers.push_back(std::move(decoded_layer));
        }
        const auto& output = root.at("output");
        plan.output.path = output.at("path").get<std::string>();
        if (output.contains("format"))
            plan.output.format = output_format(output.at("format").get<std::string>());
        if (output.contains("codec"))
            plan.output.codec = video_codec(output.at("codec").get<std::string>());
        plan.output.bitrate = output.value("bitrate", std::int64_t{0});
        plan.output.crf = output.value("crf", 0);
        plan.output.qp = output.value("qp", -1);
        const auto rate_control = output.value("rate_control", "crf");
        if (rate_control == "qp") plan.output.rate_control = render_plan::RateControlMode::ConstantQp;
        else if (rate_control == "bitrate") plan.output.rate_control = render_plan::RateControlMode::Bitrate;
        else if (rate_control == "crf") plan.output.rate_control = render_plan::RateControlMode::Crf;
        else return PlanDecodeError{"output.rate_control", "must be crf, qp, or bitrate"};
        plan.output.profile_id = output.value("profile_id", std::string{});
        if (plan.output.profile_id == "velox-h264-1080p30-v1" &&
            (plan.canvas.width != 1920 || plan.canvas.height != 1080 ||
             plan.canvas.fps != FrameRate{30, 1})) {
            return PlanDecodeError{"canvas", "velox-h264-1080p30-v1 requires 1920x1080 at 30 fps"};
        }
        if (root.contains("budget")) {
            const auto& budget = root.at("budget");
            plan.budget.max_temporal_pixels = budget.value(
                "max_temporal_pixels", plan.budget.max_temporal_pixels);
        }
        if (const auto budget_error = validate_render_budget(plan))
            return *budget_error;
        plan.content_fingerprint = compute_render_plan_content_fingerprint(plan);
        return plan;
    } catch (const std::exception& error) {
        return PlanDecodeError{"", error.what()};
    }
}
