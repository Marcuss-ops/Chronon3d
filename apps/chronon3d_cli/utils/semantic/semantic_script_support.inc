namespace {

// ── String → enum helpers (mirror the render-plan decoder's vocabulary) ────

SemanticKind parse_kind(const std::string& value) {
    if (value == "important_phrase") return SemanticKind::ImportantPhrase;
    if (value == "important_word") return SemanticKind::ImportantWord;
    return SemanticKind::ImageOverlay;
}

render_plan::FitMode parse_fit(const std::string& value) {
    if (value == "contain") return render_plan::FitMode::Contain;
    if (value == "stretch") return render_plan::FitMode::Stretch;
    if (value == "none") return render_plan::FitMode::None;
    return render_plan::FitMode::Cover;
}

render_plan::OutputFormat parse_output_format(const std::string& value) {
    if (value == "mp4") return render_plan::OutputFormat::Mp4;
    if (value == "mkv") return render_plan::OutputFormat::Mkv;
    if (value == "webm") return render_plan::OutputFormat::WebM;
    return render_plan::OutputFormat::Png;
}

render_plan::VideoCodec parse_codec(const std::string& value) {
    if (value == "h264") return render_plan::VideoCodec::H264;
    if (value == "h265") return render_plan::VideoCodec::H265;
    if (value == "vp9") return render_plan::VideoCodec::VP9;
    if (value == "av1") return render_plan::VideoCodec::AV1;
    return render_plan::VideoCodec::Auto;
}

// ── enum → string helpers (serialization) ──────────────────────────────────

std::string kind_name(render_plan::LayerType type) {
    switch (type) {
        case render_plan::LayerType::Image: return "image";
        case render_plan::LayerType::Video: return "video";
        case render_plan::LayerType::Text: return "text";
        case render_plan::LayerType::Color: return "color";
    }
    return "color";
}

std::string fit_name(render_plan::FitMode fit) {
    switch (fit) {
        case render_plan::FitMode::Contain: return "contain";
        case render_plan::FitMode::Stretch: return "stretch";
        case render_plan::FitMode::None: return "none";
        case render_plan::FitMode::Cover: return "cover";
    }
    return "cover";
}

std::string format_name(render_plan::OutputFormat format) {
    switch (format) {
        case render_plan::OutputFormat::Mp4: return "mp4";
        case render_plan::OutputFormat::Mkv: return "mkv";
        case render_plan::OutputFormat::WebM: return "webm";
        case render_plan::OutputFormat::Png: return "png";
    }
    return "png";
}

std::string codec_name(render_plan::VideoCodec codec) {
    switch (codec) {
        case render_plan::VideoCodec::H264: return "h264";
        case render_plan::VideoCodec::H265: return "h265";
        case render_plan::VideoCodec::VP9: return "vp9";
        case render_plan::VideoCodec::AV1: return "av1";
        case render_plan::VideoCodec::Auto: return "auto";
    }
    return "auto";
}

SemanticError make_error(std::string path, std::string message) {
    return SemanticError{std::move(path), std::move(message)};
}

bool decode_background(const nlohmann::json& value, SemanticBackground& out) {
    if (value.is_string()) {
        out.asset = value.get<std::string>();
        return true;
    }
    if (!value.is_object()) return false;
    out.asset = value.value("asset", std::string{});
    if (value.contains("fit")) out.fit = parse_fit(value.at("fit").get<std::string>());
    if (value.contains("color")) {
        const auto& color = value.at("color");
        for (std::size_t index = 0; index < color.size() && index < out.color.size(); ++index)
            out.color[index] = color.at(index).get<float>();
    }
    return true;
}

}  // namespace
