// ─── visual_preset_materializer.cpp — VisualPresetMaterializer ─────────────
//
// VISUAL-SSOT-02 — the single materializer that consumes the existing
// VisualPresetRegistry and lowers a LayerPlan onto a fully-resolved text
// overlay.  It owns the preset→base-materializer dispatch, the style/font
// resolution, the animation intent resolution and the layout intent; final
// placement stays a separate scene-wide phase in the render-plan compiler.

#include <chronon3d/render_plan/visual_preset_materializer.hpp>

#include <chronon3d/presets/text/text_presets_v1.hpp>
#include <chronon3d/registry/style_resolver.hpp>
#include <chronon3d/registry/visual_preset_registry.hpp>
#include <chronon3d/render_plan/color_utils.hpp>  // parse_hex_color (shared — no per-path drift)
#include <chronon3d/render_plan/render_plan.hpp>  // LayerPlan (full definition)
#include <chronon3d/text/text_placement.hpp>      // TextPlacementKind

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

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
