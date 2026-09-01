namespace {

LayerPlan decode_layer(const nlohmann::json& value) {
    LayerPlan layer;
    layer.id = value.at("id").get<std::string>();
    layer.type = layer_type(value.at("type").get<std::string>());
    layer.asset = value.value("asset", std::string{});
    layer.source = value.value("source", std::string{});
    layer.text = value.value("text", std::string{});

    if (value.contains("size")) {
        const auto& size = value.at("size");
        layer.size_dimensions = size.size();
        for (std::size_t i = 0; i < size.size() && i < layer.size.size(); ++i)
            layer.size[i] = size.at(i).get<float>();
    }
    if (value.contains("color")) {
        const auto& color = value.at("color");
        for (std::size_t i = 0; i < color.size() && i < layer.color.size(); ++i)
            layer.color[i] = color.at(i).get<float>();
    }
    if (value.contains("position")) {
        const auto& position = value.at("position");
        layer.position_dimensions = position.size();
        for (std::size_t i = 0; i < position.size() && i < layer.position.size(); ++i)
            layer.position[i] = position.at(i).get<float>();
    }
    if (value.contains("scale")) {
        const auto& scale = value.at("scale");
        layer.scale_dimensions = scale.size();
        for (std::size_t i = 0; i < scale.size() && i < layer.scale.size(); ++i)
            layer.scale[i] = scale.at(i).get<float>();
    }
    if (value.contains("rotation")) {
        const auto& rotation = value.at("rotation");
        layer.rotation_dimensions = rotation.size();
        for (std::size_t i = 0; i < rotation.size() && i < layer.rotation.size(); ++i)
            layer.rotation[i] = rotation.at(i).get<float>();
    }

    layer.start_frame = optional_frame(value, "start_frame");
    layer.duration_frames = optional_frame(value, "duration_frames");
    if (value.contains("fit")) layer.fit = fit_mode(value.at("fit").get<std::string>());

    if (value.contains("style")) {
        const auto& style = value.at("style");
        LayerStylePlan style_plan;
        style_plan.font = style.value("font", std::string{});
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
        layer.font = style_plan.font;
        layer.style = std::move(style_plan);
    }

    if (value.contains("animation")) {
        AnimationPlan animation;
        for (const auto& track_value : value.at("animation").at("tracks")) {
            AnimationTrackPlan track;
            track.property = track_value.at("property").get<std::string>();
            track.easing = track_value.value("easing", std::string{"linear"});
            for (const auto& key_value : track_value.at("keyframes")) {
                AnimationKeyframePlan key;
                key.frame = Frame{key_value.at("frame").get<std::int64_t>()};
                const auto& raw = key_value.at("value");
                if (raw.is_array()) {
                    for (const auto& component : raw) key.value.push_back(component.get<float>());
                } else {
                    key.value.push_back(raw.get<float>());
                }
                track.keyframes.push_back(std::move(key));
            }
            animation.tracks.push_back(std::move(track));
        }
        layer.animation = std::move(animation);
    }

    if (value.contains("blend_mode")) {
        if (const auto mode = blend_mode(value.at("blend_mode").get<std::string>()))
            layer.blend_mode = mode;
    }
    layer.opacity = optional_value<float>(value, "opacity");
    layer.loop = value.value("loop", false);
    return layer;
}

} // namespace
