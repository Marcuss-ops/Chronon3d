namespace chronon3d::render_plan {
namespace {

// parse_hex_color lives in color_utils.hpp — the SINGLE shared parser for
// every style-consuming path (text materializer + subtitle track), so custom
// colors are lowered identically everywhere.

chronon3d::TextPlacementKind placement_kind(std::string_view type) {
    using K = chronon3d::TextPlacementKind;
    if (type == "top_left") return K::TopLeft;
    if (type == "top_right") return K::TopRight;
    if (type == "bottom_left") return K::BottomLeft;
    if (type == "bottom_right" || type == "lower_third") return K::BottomLeft;
    if (type == "safe_area" || type == "safe_area_center") return K::SafeAreaCenter;
    if (type == "top") return K::SafeAreaTop;
    if (type == "bottom") return K::SafeAreaBottom;
    return K::CanvasCenter;
}

// Canonical preset fit vocabulary ("cover" | "contain" | "stretch" | "none")
// mirrors the render-plan `fit` enum. Empty → Cover (renderer default).
FitMode fit_mode_from_string(std::string_view value) {
    if (value == "contain") return FitMode::Contain;
    if (value == "stretch") return FitMode::Stretch;
    if (value == "none") return FitMode::None;
    return FitMode::Cover;
}

void apply_resolved_visual_style(
    chronon3d::TextDefinition& definition,
    const chronon3d::registry::ResolvedVisualStyle& style) {
    if (!style.font_family.empty())
        definition.style.font.font_family = style.font_family;
    // Canonical preset font asset (byte-identity anchor). apply_visual_plan_
    // overrides() runs after this and can replace font_path from the plan's
    // explicit `font` / `font_asset` fields.
    if (!style.font_asset.empty())
        definition.style.font.font_path = style.font_asset;
    if (style.font_weight)
        definition.style.font.font_weight = *style.font_weight;
    if (style.font_size)
        definition.style.font.font_size = *style.font_size;
    if (const auto fill = parse_hex_color(style.fill))
        definition.style.color = *fill;

    if (style.stroke_enabled) {
        definition.style.paint.stroke_enabled = true;
        if (const auto color = parse_hex_color(style.stroke_color))
            definition.style.paint.stroke_color = *color;
        if (style.stroke_width)
            definition.style.paint.stroke_width = *style.stroke_width;
    }
    if (style.shadow_enabled) {
        chronon3d::TextShadow shadow;
        shadow.enabled = true;
        if (const auto color = parse_hex_color(style.shadow_color))
            shadow.color = *color;
        shadow.opacity = style.shadow_opacity.value_or(1.0f);
        shadow.blur = style.shadow_blur.value_or(0.0f);
        if (style.shadow_offset)
            shadow.offset = {(*style.shadow_offset)[0], (*style.shadow_offset)[1]};
        definition.style.shadows = {shadow};
    }
    if (style.background_enabled) {
        definition.style.box_style.enabled = true;
        const float opacity = style.background_opacity.value_or(1.0f);
        if (const auto color = parse_hex_color(style.background_color, opacity))
            definition.style.box_style.background = *color;
        if (style.radius)
            definition.style.box_style.radius = *style.radius;
        if (style.padding)
            definition.style.box_style.padding = {
                (*style.padding)[0], (*style.padding)[1]};
    }
}

void apply_visual_plan_overrides(chronon3d::TextDefinition& definition,
                                 const LayerPlan& layer) {
    if (layer.anchor) {
        definition.frame.placement = chronon3d::TextPlacement{
            placement_kind(layer.anchor->type)};
        definition.frame.align = layer.anchor->alignment == "right"
            ? chronon3d::TextAlign::Right
            : layer.anchor->alignment == "center"
                ? chronon3d::TextAlign::Center : chronon3d::TextAlign::Left;
    }
    if (layer.font_asset) {
        definition.style.font.font_path = layer.font_asset->asset;
        if (!layer.font_asset->family.empty())
            definition.style.font.font_family = layer.font_asset->family;
        if (layer.font_asset->weight)
            definition.style.font.font_weight = *layer.font_asset->weight;
    }
    if (!layer.font.empty()) definition.style.font.font_path = layer.font;
    if (layer.font_size) definition.style.font.font_size = *layer.font_size;
}

}  // namespace
}  // namespace chronon3d::render_plan
