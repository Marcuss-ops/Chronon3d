namespace {

struct MaterializedText {
    chronon3d::TextDefinition definition;
    std::optional<ResolvedLayoutIntent> layout;
};

chronon3d::layout::OverlayLayoutRequest layout_request(
    const LayerPlan& layer,
    const ResolvedLayoutIntent& layout,
    std::int64_t composition_frames) {
    chronon3d::layout::OverlayLayoutRequest request;
    request.id = layer.id;
    request.intent = layout.intent;
    request.fallback_intents = layout.fallback_intents;
    request.width = layout.width;
    request.height = layout.height;
    request.safe_margin = layout.safe_margin;
    request.start_frame = layer.start_frame.value_or(Frame{0}).value;
    request.end_frame = request.start_frame +
        layer.duration_frames.value_or(Frame{composition_frames}).value;
    return request;
}

chronon3d::FitMode fit_mode(FitMode value) {
    switch (value) {
        case FitMode::Contain: return chronon3d::FitMode::Contain;
        case FitMode::Stretch: return chronon3d::FitMode::Stretch;
        case FitMode::None: return chronon3d::FitMode::None;
        case FitMode::Cover: return chronon3d::FitMode::Cover;
    }
    return chronon3d::FitMode::Cover;
}

MaterializedText materialize_text(const LayerPlan& layer,
                                  const chronon3d::CanvasInfo& canvas,
                                  std::string_view style_profile,
                                  std::int64_t composition_frames,
                                  chronon3d::FontEngine& font_engine) {
    MaterializedText out;
    // RenderingGen has already lowered editorial presets into generic text
    // style/layout data. Chronon only consumes those primitives.
    std::string base_preset;
    if (base_preset == "title_centered")
        out.definition = chronon3d::presets::text::title_centered(layer.text, canvas);
    else if (base_preset == "subtitle_bottom")
        out.definition = chronon3d::presets::text::subtitle_bottom(layer.text, canvas);
    else if (base_preset == "caption_safe_area")
        out.definition = chronon3d::presets::text::caption_safe_area(layer.text, canvas);
    else if (base_preset == "kinetic_word")
        out.definition = chronon3d::presets::text::kinetic_word(layer.text, canvas);
    else if (base_preset == "lower_third")
        out.definition = chronon3d::presets::text::lower_third(layer.text, canvas);
    else {
        out.definition.content.value = layer.text;
        out.definition.style.font.font_size = layer.font_size.value_or(48.0f);
        out.definition.style.color = {layer.color[0], layer.color[1], layer.color[2], layer.color[3]};
        out.definition.frame.size = {
            layer.box_width.value_or(static_cast<float>(canvas.width)),
            layer.box_height.value_or(static_cast<float>(canvas.height))};
        out.definition.frame.align = chronon3d::TextAlign::Center;
        out.definition.frame.vertical_align = chronon3d::VerticalAlign::Middle;
    }
    if (!layer.font.empty())
        out.definition.style.font.font_path = layer.font;
    if (layer.font_size)
        out.definition.style.font.font_size = *layer.font_size;
    apply_text_animation_intent(out.definition, layer, std::nullopt,
                                composition_frames);
    return out;
}

std::uint64_t render_settings_fingerprint(
    const RenderPlanFingerprintSettings& settings) {
    return chronon3d::core::hash::HashBuilder{}
        .add("chronon3d.render-settings.fingerprint.v1")
        .add(settings.width)
        .add(settings.height)
        .add(settings.antialiasing_samples)
        .add(settings.ssaa_factor)
        .add(settings.motion_blur)
        .add(settings.dirty_rects)
        .add(settings.deterministic)
        .add(settings.force_scalar_normal_blend)
        .add(settings.dirty_bitmask)
        .add(settings.dirty_tiles)
        .add(settings.parallel_tiles)
        .add(settings.tile_size)
        .add(settings.tile_dirty_ratio_threshold)
        .add(settings.optimize_compositing)
        .finish();
}

RenderJobFingerprint render_job_fingerprint(
    const RenderPlan& plan,
    const chronon3d::assets::PreparedAssetManifest& assets,
    const RenderPlanFingerprintOptions& options) {
    auto content_plan = plan;
    content_plan.job_id.clear();
    content_plan.output = {};
    auto request_plan = plan;
    request_plan.job_id.clear();
    request_plan.output.path.clear();

    const auto content_hash = compute_render_plan_content_fingerprint(content_plan);
    const auto request_hash = compute_render_plan_content_fingerprint(request_plan);
    const auto settings_hash = render_settings_fingerprint(options.render_settings);
    const auto material = [&](std::string_view domain, std::uint64_t plan_hash,
                              bool include_output_settings) {
        auto hash = chronon3d::core::hash::HashBuilder{}
            .add(domain)
            .add(options.schema_version)
            .add(options.engine_compatibility_version)
            .add(plan_hash)
            .add(settings_hash)
            .add_bytes(assets.manifest_digest().bytes.data(),
                       assets.manifest_digest().bytes.size());
        if (include_output_settings) {
            hash.add_enum(request_plan.output.format)
                .add_enum(request_plan.output.codec)
                .add(static_cast<int>(request_plan.output.rate_control))
                .add(request_plan.output.bitrate)
                .add(request_plan.output.crf)
                .add(request_plan.output.qp)
                .add(request_plan.output.profile_id);
        }
        return std::to_string(hash.finish());
    };
    return {
        chronon3d::assets::sha256_string(
            material("chronon.render-content.v2", content_hash, false)),
        chronon3d::assets::sha256_string(
            material("chronon.render-request.v2", request_hash, true))};
}

void apply_layer_timing(chronon3d::LayerBuilder& builder, const LayerPlan& layer,
                        std::string_view style_profile,
                        Frame composition_frames,
                        bool position_already_resolved = false) {
    if (layer.start_frame) builder.from(*layer.start_frame);
    if (layer.duration_frames) builder.duration(*layer.duration_frames);
    if (!position_already_resolved && layer.position_dimensions >= 2)
        builder.position({layer.position[0], layer.position[1], layer.position[2]});
    if (layer.blend_mode) builder.blend(*layer.blend_mode);
    if (layer.opacity) builder.opacity(*layer.opacity);
    if (layer.animation) {
        if (layer.animation->start_frame) builder.from(*layer.animation->start_frame);
    }

    // RenderingGen lowers preset motion into generic layer animation data;
    // Chronon does not resolve editorial preset ids here.
    std::optional<chronon3d::registry::AnimationSpec> preset_animation;
    if (!layer.animation && !layer.transition_in && !layer.transition_out) return;

    const auto resolved = resolve_animation(preset_animation, layer, composition_frames);
    if (!resolved.preset.empty()) {
        presets::MotionParameters motion_params;
        motion_params.duration = resolved.enter_duration;
        builder.motion(resolved.preset, motion_params);
    }
    if (layer.transition_in) {
        builder.transition_in(*layer.transition_in);
    }
    if (layer.transition_out) {
        builder.transition_out(*layer.transition_out);
    }
}

} // namespace
