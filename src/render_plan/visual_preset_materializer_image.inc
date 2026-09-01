namespace chronon3d::render_plan {

ResolvedImageLayer VisualPresetMaterializer::materialize_image(
    const LayerPlan& layer,
    const chronon3d::CanvasInfo& canvas,
    std::string_view style_profile,
    const registry::VisualPresetRegistry& registry,
    Frame composition_frames) const {
    const auto visual = registry.get_for_profile(layer.preset, style_profile);
    if (visual.supported_layer != registry::VisualLayerKind::Image) {
        throw std::runtime_error("visual preset '" + layer.preset +
                                 "' cannot be used on an image layer");
    }

    ResolvedImageLayer resolved;
    resolved.preset_id = layer.preset;

    // Layout INTENT: the image's anchor (image_left / image_right / center / …)
    // + fallback order + content bounds (explicit box or full canvas).
    resolved.layout.intent = visual.anchor.type;
    resolved.layout.fallback_intents = visual.fallback_anchors;
    resolved.layout.safe_margin =
        layer.anchor ? layer.anchor->safe_margin : visual.anchor.safe_margin;
    // Image geometry: preset defaults + plan overrides. The plan box wins when
    // present; otherwise the preset's canonical box (image presets own their
    // box — ADR-029); otherwise the full canvas.
    resolved.box_width = layer.box_width.value_or(
        visual.box_width.value_or(canvas.width));
    resolved.box_height = layer.box_height.value_or(
        visual.box_height.value_or(canvas.height));
    resolved.fit = layer.fit.value_or(fit_mode_from_string(visual.fit));
    resolved.layout.width = resolved.box_width;
    resolved.layout.height = resolved.box_height;

    // Animation intent: registry defaults + plan overrides (motion + exit).
    resolved.animation = resolve_animation(
        visual.animation, layer, composition_frames);
    return resolved;
}

}  // namespace chronon3d::render_plan
