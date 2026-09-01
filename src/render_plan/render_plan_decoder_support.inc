namespace {

std::uint64_t fingerprint_render_plan_impl(const RenderPlan& plan,
                                          bool include_output_settings) {
    auto hash = chronon3d::core::hash::HashBuilder{}
        .add("chronon3d.render-plan.fingerprint.v2")
        .add("")
        .add(plan.style_profile)
        .add(plan.canvas.width)
        .add(plan.canvas.height)
        .add(plan.canvas.fps.num())
        .add(plan.canvas.fps.den())
        .add(plan.canvas.duration)
        .add(plan.budget.max_temporal_pixels)
        .add(plan.layers.size());

    for (const auto& layer : plan.layers) {
        hash.add(layer.id).add_enum(layer.type).add(layer.asset).add(layer.source)
            .add(layer.text).add(layer.font).add(layer.preset)
            .add(layer.semantic_role)
            .add(layer.font_size.has_value());
        if (layer.font_size) hash.add(*layer.font_size);
        hash.add(layer.font_asset.has_value());
        if (layer.font_asset) {
            hash.add(layer.font_asset->asset).add(layer.font_asset->family)
                .add(layer.font_asset->weight.has_value());
            if (layer.font_asset->weight) hash.add(*layer.font_asset->weight);
        }
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
            hash.add(layer.animation->preset).add(layer.animation->unit);
            hash.add(layer.animation->enter_duration_frames.has_value());
            if (layer.animation->enter_duration_frames)
                hash.add(*layer.animation->enter_duration_frames);
            hash.add(layer.animation->exit_duration_frames.has_value());
            if (layer.animation->exit_duration_frames)
                hash.add(*layer.animation->exit_duration_frames);
        }
        hash.add(layer.anchor.has_value());
        if (layer.anchor) {
            hash.add(layer.anchor->type).add(layer.anchor->safe_margin)
                .add(layer.anchor->alignment);
        }
        for (const auto value : layer.offset) hash.add(value);
        hash.add(layer.offset_dimensions);
        hash.add(layer.style.has_value());
        if (layer.style) {
            const auto& style = *layer.style;
            hash.add(style.font_family).add(style.font_weight.has_value());
            if (style.font_weight) hash.add(*style.font_weight);
            hash.add(style.font_size.has_value());
            if (style.font_size) hash.add(*style.font_size);
            hash.add(style.fill);
            hash.add(style.stroke.has_value());
            if (style.stroke) {
                hash.add(style.stroke->color).add(style.stroke->width.has_value());
                if (style.stroke->width) hash.add(*style.stroke->width);
            }
            hash.add(style.shadow.has_value());
            if (style.shadow) {
                hash.add(style.shadow->color).add(style.shadow->opacity.has_value());
                if (style.shadow->opacity) hash.add(*style.shadow->opacity);
                hash.add(style.shadow->blur.has_value());
                if (style.shadow->blur) hash.add(*style.shadow->blur);
                for (const auto value : style.shadow->offset) hash.add(value);
                hash.add(style.shadow->offset_dimensions);
            }
            hash.add(style.background.has_value());
            if (style.background) {
                hash.add(style.background->color)
                    .add(style.background->opacity.has_value());
                if (style.background->opacity) hash.add(*style.background->opacity);
                hash.add(style.background->radius.has_value());
                if (style.background->radius) hash.add(*style.background->radius);
                for (const auto value : style.background->padding) hash.add(value);
                hash.add(style.background->padding_dimensions);
            }
        }
        hash.add(layer.blend_mode.has_value());
        if (layer.blend_mode) hash.add_enum(*layer.blend_mode);
        hash.add(layer.opacity.has_value());
        if (layer.opacity) hash.add(*layer.opacity);
        hash.add(layer.loop);
        hash.add(layer.background.has_value());
        if (layer.background) {
            hash.add(layer.background->asset);
            hash.add(layer.background->fit.has_value());
            if (layer.background->fit) hash.add_enum(*layer.background->fit);
            hash.add(layer.background->opacity.has_value());
            if (layer.background->opacity) hash.add(*layer.background->opacity);
        }
    }

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

LayerType visual_layer_type(chronon3d::registry::VisualLayerKind kind) {
    using chronon3d::registry::VisualLayerKind;
    switch (kind) {
        case VisualLayerKind::Image: return LayerType::Image;
        case VisualLayerKind::Video: return LayerType::Video;
        case VisualLayerKind::Text: return LayerType::Text;
        case VisualLayerKind::Color: return LayerType::Color;
    }
    return LayerType::Color;
}

FitMode fit_mode(const std::string& value) {
    if (value == "contain") return FitMode::Contain;
    if (value == "stretch") return FitMode::Stretch;
    if (value == "none") return FitMode::None;
    return FitMode::Cover;
}

std::optional<chronon3d::LayerTransitionSpec> decode_transition(const nlohmann::json& value) {
    if (!value.is_object()) return std::nullopt;
    chronon3d::LayerTransitionSpec spec;
    spec.transition_id = value.value("type", value.value("transition_id", std::string{"none"}));
    if (value.contains("duration")) {
        spec.duration = value.at("duration").get<double>();
    } else if (value.contains("duration_seconds")) {
        spec.duration = value.at("duration_seconds").get<double>();
    }
    if (value.contains("delay")) {
        spec.delay = value.at("delay").get<double>();
    }
    if (value.contains("direction")) {
        spec.direction = chronon3d::string_to_transition_direction(value.at("direction").get<std::string>());
    }
    if (value.contains("easing")) {
        spec.easing = chronon3d::string_to_easing(value.at("easing").get<std::string>());
    }
    return spec;
}

std::optional<BlendMode> blend_mode(const std::string& value) {
    if (value == "normal") return BlendMode::Normal;
    if (value == "add") return BlendMode::Add;
    if (value == "multiply") return BlendMode::Multiply;
    if (value == "screen") return BlendMode::Screen;
    if (value == "overlay") return BlendMode::Overlay;
    if (value == "darken") return BlendMode::Darken;
    if (value == "lighten") return BlendMode::Lighten;
    if (value == "difference") return BlendMode::Difference;
    if (value == "exclusion") return BlendMode::Exclusion;
    if (value == "soft_light") return BlendMode::SoftLight;
    if (value == "hard_light") return BlendMode::HardLight;
    if (value == "color_dodge") return BlendMode::ColorDodge;
    if (value == "color_burn") return BlendMode::ColorBurn;
    return std::nullopt;
}

SubtitleFormat subtitle_format(const std::string& value) {
    if (value == "vtt") return SubtitleFormat::Vtt;
    if (value == "json") return SubtitleFormat::Json;
    if (value == "ass") return SubtitleFormat::Ass;
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

} // namespace
