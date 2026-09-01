namespace {

chronon3d::FitMode fit_mode(FitMode value) {
    switch (value) {
        case FitMode::Contain: return chronon3d::FitMode::Contain;
        case FitMode::Stretch: return chronon3d::FitMode::Stretch;
        case FitMode::None: return chronon3d::FitMode::None;
        case FitMode::Cover: return chronon3d::FitMode::Cover;
    }
    return chronon3d::FitMode::Cover;
}

chronon3d::EasingCurve track_easing(std::string_view value) {
    using chronon3d::Easing;
    if (value == "linear") return {Easing::Linear};
    if (value == "in_quad") return {Easing::InQuad};
    if (value == "out_quad") return {Easing::OutQuad};
    if (value == "in_out_quad") return {Easing::InOutQuad};
    if (value == "in_cubic") return {Easing::InCubic};
    if (value == "out_cubic") return {Easing::OutCubic};
    if (value == "in_out_cubic") return {Easing::InOutCubic};
    if (value == "in_expo") return {Easing::InExpo};
    if (value == "out_expo") return {Easing::OutExpo};
    if (value == "in_out_expo") return {Easing::InOutExpo};
    if (value == "in_sine") return {Easing::InSine};
    if (value == "out_sine") return {Easing::OutSine};
    if (value == "in_out_sine") return {Easing::InOutSine};
    if (value == "in_back") return {Easing::InBack};
    if (value == "out_back") return {Easing::OutBack};
    if (value == "in_out_back") return {Easing::InOutBack};
    if (value == "in_elastic") return {Easing::InElastic};
    if (value == "out_elastic") return {Easing::OutElastic};
    if (value == "in_out_elastic") return {Easing::InOutElastic};
    if (value == "in_bounce") return {Easing::InBounce};
    if (value == "out_bounce") return {Easing::OutBounce};
    if (value == "in_out_bounce") return {Easing::InOutBounce};
    if (value == "smoothstep") return {Easing::Smoothstep};
    if (value == "hold") return {Easing::Hold};
    throw std::runtime_error("unsupported animation easing: " + std::string(value));
}

float scalar_value(const AnimationKeyframePlan& key, std::string_view property) {
    if (key.value.size() != 1)
        throw std::runtime_error("animation property '" + std::string(property) +
                                 "' requires scalar keyframe values");
    return key.value.front();
}

chronon3d::Vec3 vector_value(const AnimationKeyframePlan& key,
                             std::string_view property,
                             chronon3d::Vec3 fallback) {
    if (key.value.size() < 2 || key.value.size() > 3)
        throw std::runtime_error("animation property '" + std::string(property) +
                                 "' requires 2 or 3 keyframe components");
    fallback.x = key.value[0];
    fallback.y = key.value[1];
    if (key.value.size() == 3) fallback.z = key.value[2];
    return fallback;
}

chronon3d::TextDefinition materialize_text(const LayerPlan& layer,
                                           const chronon3d::CanvasInfo& canvas) {
    if (!layer.style || layer.font.empty() || !layer.style->font_size ||
        layer.style->fill.empty()) {
        throw std::runtime_error("text layer '" + layer.id +
                                 "' is missing concrete font/font_size/fill style");
    }

    chronon3d::TextDefinition definition;
    definition.content.value = layer.text;
    definition.style.font.font_path = layer.font;
    definition.style.font.font_size = *layer.style->font_size;
    const auto fill = parse_hex_color(layer.style->fill);
    if (!fill)
        throw std::runtime_error("text layer '" + layer.id + "' has invalid fill color");
    definition.style.color = *fill;
    definition.style.paint.fill = *fill;

    if (layer.style->stroke) {
        const auto stroke_color = parse_hex_color(layer.style->stroke->color);
        if (!stroke_color)
            throw std::runtime_error("text layer '" + layer.id + "' has invalid stroke color");
        definition.style.paint.stroke_enabled = true;
        definition.style.paint.stroke_color = *stroke_color;
        definition.style.paint.stroke_width = layer.style->stroke->width.value_or(1.0f);
    }
    if (layer.style->shadow) {
        const auto shadow_color = parse_hex_color(
            layer.style->shadow->color,
            layer.style->shadow->opacity.value_or(1.0f));
        if (!shadow_color)
            throw std::runtime_error("text layer '" + layer.id + "' has invalid shadow color");
        chronon3d::TextShadow shadow;
        shadow.enabled = true;
        shadow.color = *shadow_color;
        shadow.opacity = layer.style->shadow->opacity.value_or(1.0f);
        shadow.blur = layer.style->shadow->blur.value_or(0.0f);
        if (layer.style->shadow->offset_dimensions == 2) {
            shadow.offset = {layer.style->shadow->offset[0], layer.style->shadow->offset[1]};
        }
        definition.style.shadows.push_back(shadow);
    }
    if (layer.style->background) {
        const auto background_color = parse_hex_color(
            layer.style->background->color,
            layer.style->background->opacity.value_or(1.0f));
        if (!background_color)
            throw std::runtime_error("text layer '" + layer.id + "' has invalid background color");
        definition.style.box_style.enabled = true;
        definition.style.box_style.background = *background_color;
        definition.style.box_style.radius = layer.style->background->radius.value_or(0.0f);
        if (layer.style->background->padding_dimensions == 2) {
            definition.style.box_style.padding = {
                layer.style->background->padding[0],
                layer.style->background->padding[1]};
        }
    }

    definition.frame.size = layer.size_dimensions == 2
        ? chronon3d::Vec2{layer.size[0], layer.size[1]}
        : chronon3d::Vec2{static_cast<float>(canvas.width), static_cast<float>(canvas.height)};
    definition.frame.anchor = chronon3d::TextAnchor::Center;
    definition.frame.align = chronon3d::TextAlign::Center;
    definition.frame.vertical_align = chronon3d::VerticalAlign::Middle;
    definition.frame.placement = chronon3d::TextPlacement{
        chronon3d::TextPlacementKind::Absolute, {0.0f, 0.0f}};
    return definition;
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
            material("chronon.render-content.v3", content_hash, false)),
        chronon3d::assets::sha256_string(
            material("chronon.render-request.v3", request_hash, true))};
}

void apply_animation_tracks(chronon3d::LayerBuilder& builder, const LayerPlan& layer) {
    if (!layer.animation) return;
    const chronon3d::Vec3 base_position{layer.position[0], layer.position[1], layer.position[2]};
    const chronon3d::Vec3 base_scale{layer.scale[0], layer.scale[1], layer.scale[2]};
    const chronon3d::Vec3 base_rotation{layer.rotation[0], layer.rotation[1], layer.rotation[2]};

    bool position_claimed = false;
    bool scale_claimed = false;
    bool rotation_claimed = false;
    bool opacity_claimed = false;

    for (const auto& track : layer.animation->tracks) {
        const auto easing = track_easing(track.easing);
        const auto component_track = [&](std::string_view prefix) {
            return track.property == std::string(prefix) + "_x" ||
                   track.property == std::string(prefix) + "_y" ||
                   track.property == std::string(prefix) + "_z";
        };
        if (track.property == "position" || component_track("position")) {
            if (position_claimed)
                throw std::runtime_error("layer '" + layer.id + "' has overlapping position tracks");
            position_claimed = true;
            auto& animated = builder.position_anim();
            animated.set(base_position);
            for (const auto& key : track.keyframes) {
                auto value = base_position;
                if (track.property == "position") {
                    value = vector_value(key, track.property, base_position);
                } else {
                    const float offset = scalar_value(key, track.property);
                    if (track.property == "position_x") value.x += offset;
                    else if (track.property == "position_y") value.y += offset;
                    else value.z += offset;
                }
                animated.add_keyframe(key.frame, value, easing);
            }
        } else if (track.property == "scale" || component_track("scale")) {
            if (scale_claimed)
                throw std::runtime_error("layer '" + layer.id + "' has overlapping scale tracks");
            scale_claimed = true;
            auto& animated = builder.scale_anim();
            animated.set(base_scale);
            for (const auto& key : track.keyframes) {
                auto value = base_scale;
                if (track.property == "scale") {
                    value = vector_value(key, track.property, base_scale);
                } else {
                    const float scalar = scalar_value(key, track.property);
                    if (track.property == "scale_x") value.x = scalar;
                    else if (track.property == "scale_y") value.y = scalar;
                    else value.z = scalar;
                }
                animated.add_keyframe(key.frame, value, easing);
            }
        } else if (track.property == "rotation" || component_track("rotation")) {
            if (rotation_claimed)
                throw std::runtime_error("layer '" + layer.id + "' has overlapping rotation tracks");
            rotation_claimed = true;
            auto& animated = builder.rotate_anim();
            animated.set(base_rotation);
            for (const auto& key : track.keyframes) {
                auto value = base_rotation;
                if (track.property == "rotation") {
                    value = vector_value(key, track.property, base_rotation);
                } else {
                    const float scalar = scalar_value(key, track.property);
                    if (track.property == "rotation_x") value.x = scalar;
                    else if (track.property == "rotation_y") value.y = scalar;
                    else value.z = scalar;
                }
                animated.add_keyframe(key.frame, value, easing);
            }
        } else if (track.property == "opacity") {
            if (opacity_claimed)
                throw std::runtime_error("layer '" + layer.id + "' has duplicate opacity tracks");
            opacity_claimed = true;
            auto& animated = builder.opacity_anim();
            animated.set(layer.opacity.value_or(1.0f));
            for (const auto& key : track.keyframes)
                animated.add_keyframe(key.frame, scalar_value(key, track.property), easing);
        } else {
            throw std::runtime_error("unsupported primitive animation property: " + track.property);
        }
    }
}

void apply_layer_primitives(chronon3d::LayerBuilder& builder, const LayerPlan& layer) {
    if (layer.start_frame) builder.from(*layer.start_frame);
    if (layer.duration_frames) builder.duration(*layer.duration_frames);
    if (layer.position_dimensions >= 2)
        builder.position({layer.position[0], layer.position[1], layer.position[2]});
    if (layer.scale_dimensions >= 2)
        builder.scale({layer.scale[0], layer.scale[1], layer.scale[2]});
    if (layer.rotation_dimensions > 0)
        builder.rotate({layer.rotation[0], layer.rotation[1], layer.rotation[2]});
    if (layer.blend_mode) builder.blend(*layer.blend_mode);
    if (layer.opacity) builder.opacity(*layer.opacity);
    apply_animation_tracks(builder, layer);
}

} // namespace
