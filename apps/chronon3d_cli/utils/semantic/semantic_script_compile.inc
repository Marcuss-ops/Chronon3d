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
            background.size = {static_cast<float>(script.canvas.width),
                               static_cast<float>(script.canvas.height)};
            background.size_dimensions = 2;
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
        if (overlay.box_width || overlay.box_height) {
            layer.size[0] = overlay.box_width.value_or(0.0f);
            layer.size[1] = overlay.box_height.value_or(0.0f);
            layer.size_dimensions = 2;
        }
        if (overlay.position) {
            layer.position[0] = (*overlay.position)[0];
            layer.position[1] = (*overlay.position)[1];
            layer.position_dimensions = 2;
        }
        if (overlay.animation) {
            render_plan::AnimationTrackPlan track;
            track.property = "opacity";
            track.easing = "in_quad";
            const auto start = overlay.animation->start_frame.value_or(Frame{0});
            const auto dur = overlay.animation->duration_frames.value_or(Frame{15});
            track.keyframes.push_back({.frame = start, .value = {0.0f}});
            track.keyframes.push_back({.frame = Frame{start.integral() + dur.integral()}, .value = {1.0f}});
            layer.animation = render_plan::AnimationPlan{.tracks = {std::move(track)}};
        }

        switch (overlay.kind) {
            case SemanticKind::ImportantPhrase:
            case SemanticKind::ImportantWord:
                layer.type = render_plan::LayerType::Text;
                layer.text = overlay.text;
                layer.font = "fonts/roboto.ttf";
                layer.style = render_plan::LayerStylePlan{
                    .font = "fonts/roboto.ttf",
                    .font_size = (overlay.kind == SemanticKind::ImportantWord ? 64.0f : 48.0f),
                    .fill = "#FFFFFF"
                };
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
