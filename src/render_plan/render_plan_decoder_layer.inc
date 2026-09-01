namespace {

LayerPlan decode_layer(const nlohmann::json& value) {
    LayerPlan layer;
    layer.id = value.at("id").get<std::string>();
    if (value.contains("type")) {
        layer.type = layer_type(value.at("type").get<std::string>());
    } else {
        throw std::runtime_error(
            "layer '" + layer.id + "' must declare a generic type");
    }
    layer.asset = value.value("asset", std::string{});
    layer.source = value.value("source", std::string{});
    layer.text = value.value("text", std::string{});
    layer.font = value.value("font", std::string{});
    if (value.contains("font_asset")) {
        const auto& font_asset = value.at("font_asset");
        FontAssetPlan asset_plan;
        asset_plan.asset = font_asset.value("asset", std::string{});
        asset_plan.family = font_asset.value("family", std::string{});
        asset_plan.weight = optional_value<int>(font_asset, "weight");
        layer.font_asset = std::move(asset_plan);
        if (layer.font.empty()) layer.font = layer.font_asset->asset;
    }
    layer.preset = value.value("preset", std::string{});
    layer.semantic_role = value.value("semantic_role", std::string{});
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
    if (value.contains("offset")) {
        const auto& offset = value.at("offset");
        layer.offset_dimensions = offset.size();
        for (std::size_t index = 0; index < offset.size() && index < layer.offset.size(); ++index)
            layer.offset[index] = offset.at(index).get<float>();
    }
    if (value.contains("fit")) layer.fit = fit_mode(value.at("fit").get<std::string>());
    if (value.contains("format"))
        layer.subtitle_format = subtitle_format(value.at("format").get<std::string>());
    if (value.contains("animation")) {
        const auto& animation = value.at("animation");
        AnimationTiming timing;
        timing.start_frame = optional_frame(animation, "start_frame");
        timing.duration_frames = optional_frame(animation, "duration_frames");
        timing.preset = animation.at("preset").get<std::string>();
        timing.unit = animation.value("unit", std::string{});
        if (animation.contains("enter"))
            timing.enter_duration_frames =
                optional_frame(animation.at("enter"), "duration_frames");
        if (animation.contains("exit"))
            timing.exit_duration_frames =
                optional_frame(animation.at("exit"), "duration_frames");
        layer.animation = std::move(timing);
    }
    if (value.contains("anchor")) {
        const auto& anchor = value.at("anchor");
        AnchorPlan anchor_plan;
        anchor_plan.type = anchor.value("type", std::string{});
        anchor_plan.safe_margin = anchor.value("safe_margin", 0.06f);
        anchor_plan.alignment = anchor.value("alignment", std::string{"left"});
        layer.anchor = std::move(anchor_plan);
    }
    if (value.contains("style")) {
        const auto& style = value.at("style");
        LayerStylePlan style_plan;
        style_plan.font_family = style.value("font_family", std::string{});
        style_plan.font_weight = optional_value<int>(style, "font_weight");
        style_plan.font_size = optional_value<float>(style, "font_size");
        style_plan.fill = style.value("fill", std::string{});
        if (style.contains("stroke")) {
            const auto& stroke = style.at("stroke");
            style_plan.stroke = StrokeStyle{
                stroke.value("color", std::string{}),
                optional_value<float>(stroke, "width")};
        }
        if (style.contains("shadow")) {
            const auto& shadow = style.at("shadow");
            ShadowStyle shadow_plan;
            shadow_plan.color = shadow.value("color", std::string{});
            shadow_plan.opacity = optional_value<float>(shadow, "opacity");
            shadow_plan.blur = optional_value<float>(shadow, "blur");
            if (shadow.contains("offset")) {
                const auto& offset = shadow.at("offset");
                shadow_plan.offset_dimensions = offset.size();
                for (std::size_t i = 0; i < offset.size() && i < shadow_plan.offset.size(); ++i)
                    shadow_plan.offset[i] = offset.at(i).get<float>();
            }
            style_plan.shadow = std::move(shadow_plan);
        }
        if (style.contains("background")) {
            const auto& background = style.at("background");
            BackgroundStyle background_plan;
            background_plan.color = background.value("color", std::string{});
            background_plan.opacity = optional_value<float>(background, "opacity");
            background_plan.radius = optional_value<float>(background, "radius");
            if (background.contains("padding")) {
                const auto& padding = background.at("padding");
                background_plan.padding_dimensions = padding.size();
                for (std::size_t i = 0; i < padding.size() && i < background_plan.padding.size(); ++i)
                    background_plan.padding[i] = padding.at(i).get<float>();
            }
            style_plan.background = std::move(background_plan);
        }
        layer.style = std::move(style_plan);
    }
    if (value.contains("background")) {
        const auto& background = value.at("background");
        LayerBackgroundPlan background_plan;
        background_plan.asset = background.value("asset", std::string{});
        if (background.contains("fit"))
            background_plan.fit = fit_mode(background.at("fit").get<std::string>());
        background_plan.opacity = optional_value<float>(background, "opacity");
        layer.background = std::move(background_plan);
    }
    if (value.contains("blend_mode")) {
        if (const auto mode = blend_mode(value.at("blend_mode").get<std::string>())) {
            layer.blend_mode = mode;
        }
    }
    layer.opacity = optional_value<float>(value, "opacity");
    layer.loop = value.value("loop", false);
    if (value.contains("transition_in")) {
        layer.transition_in = decode_transition(value.at("transition_in"));
    }
    if (value.contains("transition_out")) {
        layer.transition_out = decode_transition(value.at("transition_out"));
    }
    return layer;
}

} // namespace
