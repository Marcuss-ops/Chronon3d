namespace chronon3d::render_plan {

void apply_text_animation_intent(
    chronon3d::TextDefinition& definition,
    const LayerPlan& layer,
    const std::optional<registry::AnimationSpec>& preset_animation,
    std::int64_t composition_frames) {
    // Single resolution path: registry defaults + plan overrides, with the
    // same deterministic window clamp used by the layer motion.
    const auto resolved = resolve_animation(
        preset_animation, layer, Frame{composition_frames});
    // No per-unit intent anywhere → leave the text run static.
    if (!resolved.text_intent) return;
    // TextRunShape is sampled in layer-local time (Layer::local_time), while
    // the render-plan layer start is composition-global.  The animator's
    // window must therefore begin at local frame zero; passing the global
    // layer start leaves every glyph at opacity zero for the whole layer.
    definition.animation.animators.push_back(build_unit_reveal_animator(
        resolved.unit, Frame{0}, resolved.layer_duration,
        resolved.enter_duration,
        resolved.exit_duration > Frame{0}
            ? std::optional<Frame>{resolved.exit_duration} : std::nullopt));
}

VisualBounds measure_visual_bounds(const ResolvedVisualLayer& layer,
                                  chronon3d::FontEngine& engine) {
    const auto& def = layer.text;
    const chronon3d::FontSpec& font = def.style.font;
    const float font_size = font.font_size > 0.0f ? font.font_size : 48.0f;

    // 1) Split on explicit line breaks and shape each line with the REAL
    //    font at the REAL size (HarfBuzz advance width, not a canvas fraction).
    std::vector<std::string_view> lines;
    {
        const std::string_view text = def.content.value;
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t nl = text.find('\n', start);
            if (nl == std::string_view::npos) {
                lines.push_back(text.substr(start));
                break;
            }
            lines.push_back(text.substr(start, nl - start));
            start = nl + 1;
        }
        if (lines.empty()) lines.push_back({});
    }
    float text_width = 0.0f;
    for (const auto line : lines) {
        if (line.empty()) continue;
        text_width = std::max(
            text_width, engine.measure_text(line, font, font_size));
    }

    // 2) Real vertical metrics from the font face, with the preset's
    //    authored line-height multiplier applied (frame.line_height).
    const auto metrics = engine.get_font_metrics(font, font_size);
    const float natural_line = metrics.line_height > 0.0f
        ? metrics.line_height : font_size * 1.2f;
    const float text_height =
        natural_line * def.frame.line_height * static_cast<float>(lines.size());

    // 3) Layout extents: card padding, centered stroke, shadow blur/offset.
    const float pad_x =
        def.style.box_style.enabled ? def.style.box_style.padding.x : 0.0f;
    const float pad_y =
        def.style.box_style.enabled ? def.style.box_style.padding.y : 0.0f;
    const float stroke =
        def.style.paint.stroke_enabled ? def.style.paint.stroke_width : 0.0f;
    float shadow_x = 0.0f;
    float shadow_y = 0.0f;
    for (const auto& shadow : def.style.shadows) {
        if (!shadow.enabled) continue;
        shadow_x = std::max(shadow_x, shadow.blur + std::abs(shadow.offset.x));
        shadow_y = std::max(shadow_y, shadow.blur + std::abs(shadow.offset.y));
    }

    VisualBounds bounds;
    if (text_width <= 0.0f) {
        // Font unavailable (or empty text): keep the preset's authored box so
        // the resolver still receives a sane, non-zero footprint.
        bounds.width = def.frame.size.x;
        bounds.height = def.frame.size.y;
        return bounds;
    }
    bounds.width = text_width + 2.0f * pad_x + stroke + 2.0f * shadow_x;
    // The text shaper above measures the unwrapped line.  Word-wrapped
    // presets deliberately author a finite frame so long phrases can wrap
    // inside the safe area; exposing the unwrapped width to the scene-wide
    // resolver makes an otherwise valid phrase impossible to place.  Keep
    // the resolver footprint aligned with that authored frame.  The renderer
    // still owns the actual wrapping and clipping through `def.frame`.
    if (def.frame.wrap == TextWrap::Word && def.frame.size.x > 0.0f)
        bounds.width = std::min(bounds.width, def.frame.size.x);
    bounds.height = text_height + 2.0f * pad_y + stroke + 2.0f * shadow_y;
    return bounds;
}

}  // namespace chronon3d::render_plan
