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
