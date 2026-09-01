VisualPresetDescriptor make_simple_2d_preset(
    std::string id,
    std::string semantic_role,
    VisualLayerKind layer,
    std::string base_preset,
    std::string animation,
    std::string unit,
    std::string anchor,
    std::string alignment,
    float font_size,
    int font_weight) {
    VisualPresetDescriptor preset;
    preset.id = std::move(id);
    preset.semantic_role = std::move(semantic_role);
    preset.version = 1;
    preset.supported_layer = layer;
    preset.base_preset = std::move(base_preset);
    preset.anchor = AnchorSpec{
        .type = std::move(anchor), .safe_margin = 0.06f,
        .alignment = std::move(alignment)};
    preset.animation = AnimationSpec{
        .preset = std::move(animation), .unit = std::move(unit),
        .enter_duration_frames = 8,
        .exit_duration_frames = (layer == VisualLayerKind::Text ? std::optional<int>{6} : std::nullopt)};
    preset.fallback_anchors = {"center", "top_left", "top_right", "bottom_left"};
    preset.capabilities = {"2d", "collision_avoid"};

    if (layer == VisualLayerKind::Text) {
        preset.style.font_family = "Poppins";
        preset.style.font_asset = "assets/fonts/Poppins-Bold.ttf";
        preset.style.font_weight = font_weight;
        preset.style.font_size = font_size;
        preset.style.fill = "#FFFFFF";
        preset.style.stroke_color = "#111827";
        preset.style.stroke_width = 1.5f;
        preset.style.shadow_color = "#000000";
        preset.style.shadow_opacity = 0.72f;
        preset.style.shadow_blur = 10.0f;
        preset.style.shadow_offset = std::array<float, 2>{0.0f, 4.0f};
        preset.style.background_color = "#050509";
        preset.style.background_opacity = 0.88f;
        preset.style.radius = 10.0f;
        preset.style.padding = std::array<float, 2>{20.0f, 12.0f};
        preset.capabilities.push_back("local_background");
        preset.capabilities.push_back("card");
    } else {
        preset.fallback_anchors = {"image_left", "image_right", "top_right", "top_left"};
        preset.capabilities.push_back("image_transform");
        // Image presets own their canonical box + fit (the former PipelineGen
        // IMAGE_OVERLAY transport shape): 260×260 contain, matched across the
        // 2D image showcase family (ADR-029 — geometry lives in Chronon).
        preset.box_width = 260.0f;
        preset.box_height = 260.0f;
        preset.fit = "contain";
    }
    return preset;
}

VisualPresetDescriptor make_name_glow_preset(
    std::string id,
    std::string motion,
    std::string unit) {
    const bool typewriter = unit == "glyph";
    auto preset = make_simple_2d_preset(
        std::move(id), "name", VisualLayerKind::Text, "lower_third",
        std::move(motion), std::move(unit), "lower_third", "left", 58.0f, 700);
    // The glow is local to the name card, keeping the treatment readable
    // without a global veil or a full-frame effect.
    preset.style.shadow_color = "#38BDF8";
    preset.style.shadow_opacity = 0.82f;
    preset.style.shadow_blur = 14.0f;
    preset.style.shadow_offset = std::array<float, 2>{0.0f, 2.0f};
    preset.capabilities.push_back("glow");
    if (typewriter) preset.capabilities.push_back("typewriter");
    return preset;
}
