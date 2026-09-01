namespace {

std::uint64_t fingerprint_render_plan_impl(const RenderPlan& plan,
                                          bool include_output_settings) {
    auto hash = chronon3d::core::hash::HashBuilder{}
        .add("chronon3d.render-plan.fingerprint.v3")
        .add(plan.schema)
        .add(plan.canvas.width)
        .add(plan.canvas.height)
        .add(plan.canvas.fps.num())
        .add(plan.canvas.fps.den())
        .add(plan.canvas.duration)
        .add(plan.budget.max_temporal_pixels)
        .add(plan.layers.size());

    for (const auto& layer : plan.layers) {
        hash.add(layer.id).add_enum(layer.type).add(layer.asset).add(layer.source)
            .add(layer.text).add(layer.font);
        hash.add(layer.style.has_value());
        if (layer.style) {
            const auto& style = *layer.style;
            hash.add(style.font).add(style.font_size.has_value());
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
                for (const auto v : style.shadow->offset) hash.add(v);
                hash.add(style.shadow->offset_dimensions);
            }
            hash.add(style.background.has_value());
            if (style.background) {
                hash.add(style.background->color)
                    .add(style.background->opacity.has_value());
                if (style.background->opacity) hash.add(*style.background->opacity);
                hash.add(style.background->radius.has_value());
                if (style.background->radius) hash.add(*style.background->radius);
                for (const auto v : style.background->padding) hash.add(v);
                hash.add(style.background->padding_dimensions);
            }
        }
        for (const auto v : layer.size) hash.add(v);
        hash.add(layer.size_dimensions);
        for (const auto v : layer.color) hash.add(v);
        for (const auto v : layer.position) hash.add(v);
        hash.add(layer.position_dimensions);
        for (const auto v : layer.scale) hash.add(v);
        hash.add(layer.scale_dimensions);
        for (const auto v : layer.rotation) hash.add(v);
        hash.add(layer.rotation_dimensions);
        hash.add(layer.start_frame.has_value());
        if (layer.start_frame) hash.add(*layer.start_frame);
        hash.add(layer.duration_frames.has_value());
        if (layer.duration_frames) hash.add(*layer.duration_frames);
        hash.add(layer.fit.has_value());
        if (layer.fit) hash.add_enum(*layer.fit);
        hash.add(layer.animation.has_value());
        if (layer.animation) {
            hash.add(layer.animation->tracks.size());
            for (const auto& track : layer.animation->tracks) {
                hash.add(track.property).add(track.easing).add(track.keyframes.size());
                for (const auto& key : track.keyframes) {
                    hash.add(key.frame).add(key.value.size());
                    for (const auto v : key.value) hash.add(v);
                }
            }
        }
        hash.add(layer.blend_mode.has_value());
        if (layer.blend_mode) hash.add_enum(*layer.blend_mode);
        hash.add(layer.opacity.has_value());
        if (layer.opacity) hash.add(*layer.opacity);
        hash.add(layer.loop);
    }

    if (include_output_settings) {
        hash.add_enum(plan.output.format)
            .add_enum(plan.output.codec)
            .add_enum(plan.output.rate_control)
            .add(plan.output.bitrate)
            .add(plan.output.crf)
            .add(plan.output.qp)
            .add(plan.output.profile_id);
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
    if (value == "color") return LayerType::Color;
    throw std::runtime_error("unsupported primitive layer type: " + value);
}

FitMode fit_mode(const std::string& value) {
    if (value == "contain") return FitMode::Contain;
    if (value == "stretch") return FitMode::Stretch;
    if (value == "none") return FitMode::None;
    return FitMode::Cover;
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
