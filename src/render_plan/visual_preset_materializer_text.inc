namespace chronon3d::render_plan {

ResolvedVisualLayer VisualPresetMaterializer::materialize(
    const LayerPlan& layer,
    const chronon3d::CanvasInfo& canvas,
    std::string_view style_profile,
    const registry::VisualPresetRegistry& registry,
    Frame composition_frames) const {
    const auto visual = registry.get_for_profile(layer.preset, style_profile);
    if (visual.supported_layer != registry::VisualLayerKind::Text) {
        throw std::runtime_error("visual preset '" + layer.preset +
                                 "' cannot be used on a text layer");
    }
    if (visual.base_preset.empty()) {
        throw std::runtime_error("visual preset '" + layer.preset +
                                 "' has no text materialization");
    }

    ResolvedVisualLayer resolved;
    resolved.preset_id = layer.preset;

    // 1) Base text materializer — the registry's base_preset is the single
    //    source of this dispatch; the consumer never re-maps preset ids.
    const std::string_view base = visual.base_preset;
    if (base == "title_centered")
        resolved.text = chronon3d::presets::text::title_centered(layer.text, canvas);
    else if (base == "subtitle_bottom")
        resolved.text = chronon3d::presets::text::subtitle_bottom(layer.text, canvas);
    else if (base == "caption_safe_area")
        resolved.text = chronon3d::presets::text::caption_safe_area(layer.text, canvas);
    else if (base == "kinetic_word")
        resolved.text = chronon3d::presets::text::kinetic_word(layer.text, canvas);
    else
        resolved.text = chronon3d::presets::text::lower_third(layer.text, canvas);

    // 2) Style: preset defaults + plan overrides = ResolvedVisualStyle.
    const auto resolved_style = registry::VisualStyleResolver{}.resolve(
        visual.style, layer.style ? &*layer.style : nullptr);
    apply_resolved_visual_style(resolved.text, resolved_style);

    // 3) Anchor intent → placement + alignment (layout INTENT, not coords).
    resolved.text.frame.placement = chronon3d::TextPlacement{
        placement_kind(visual.anchor.type)};
    resolved.text.frame.align = visual.anchor.alignment == "right"
        ? chronon3d::TextAlign::Right
        : visual.anchor.alignment == "center"
            ? chronon3d::TextAlign::Center : chronon3d::TextAlign::Left;

    // 4) Font asset + explicit plan overrides (job is authoritative).
    apply_visual_plan_overrides(resolved.text, layer);

    // 5) Animation intent: registry defaults + plan overrides.  The per-unit
    //    text animator is applied here; the layer motion runs separately in
    //    the compiler's apply_layer_timing.
    resolved.animation = resolve_animation(
        visual.animation, layer, composition_frames);
    apply_text_animation_intent(resolved.text, layer, visual.animation,
                                composition_frames.integral());

    // 6) Layout INTENT for the scene-wide OverlayLayoutResolver phase.
    resolved.layout.intent = visual.anchor.type;
    resolved.layout.fallback_intents = visual.fallback_anchors;
    resolved.layout.safe_margin =
        layer.anchor ? layer.anchor->safe_margin : visual.anchor.safe_margin;
    resolved.layout.width = resolved.text.frame.size.x;
    resolved.layout.height = resolved.text.frame.size.y;

    return resolved;
}

}  // namespace chronon3d::render_plan
