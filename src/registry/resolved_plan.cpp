// ─── resolved_plan.cpp — resolved_plan diagnostic implementation ──────────
//
// Composes VisualPresetRegistry → StyleResolver → OverlayLayoutResolver into
// a single concrete projection (`ResolvedPlan`) plus its JSON diagnostic
// form.  No rendering, no TextDefinition, no backends — pure and
// deterministic.

#include <chronon3d/registry/resolved_plan.hpp>

#include <chronon3d/registry/visual_preset_registry.hpp>
#include <chronon3d/render_plan/render_plan.hpp>  // LayerStylePlan, AnchorPlan

#include <nlohmann/json.hpp>

namespace chronon3d::registry {

ResolvedPlan resolve_plan_diagnostic(
    const VisualPresetRegistry& registry,
    std::string_view preset_id,
    std::string_view semantic_role,
    float canvas_width,
    float canvas_height,
    float box_width,
    float box_height,
    const render_plan::LayerStylePlan* style_overrides,
    const render_plan::AnchorPlan* anchor_override) {
    const auto& preset = registry.get(preset_id);

    ResolvedPlan plan;
    plan.preset_id = preset.id;
    plan.semantic_role = std::string{semantic_role};

    // Style: preset defaults + job overrides = ResolvedVisualStyle.
    plan.style = VisualStyleResolver{}.resolve(preset.style, style_overrides);

    // Layout: the preset's preferred intent resolves through the anchor
    // resolver (canvas → safe area → content bounds → collision → fallback).
    // This mirrors render_plan_compiler.cpp `resolve_visual_layout`: the
    // preset's anchor type is the layout intent; a per-job anchor override
    // contributes its safe margin.
    chronon3d::layout::OverlayLayoutRequest request;
    request.id = preset.id;
    request.intent = preset.anchor.type;
    request.fallback_intents = preset.fallback_anchors;
    request.width = box_width;
    request.height = box_height;
    request.safe_margin = anchor_override ? anchor_override->safe_margin
                                          : preset.anchor.safe_margin;

    const auto resolved =
        chronon3d::layout::OverlayLayoutResolver{}.solve(
            canvas_width, canvas_height, {std::move(request)});
    plan.layout = resolved.empty()
                      ? chronon3d::layout::ResolvedOverlayLayout{}
                      : resolved.front();
    if (plan.layout.id.empty()) plan.layout.id = preset.id;

    // Animation: the preset's motion intent passes through unchanged.
    plan.animation = preset.animation;
    return plan;
}

nlohmann::json to_json(const ResolvedPlan& plan) {
    nlohmann::json style;
    if (!plan.style.font_family.empty()) style["font_family"] = plan.style.font_family;
    if (!plan.style.font_asset.empty()) style["font_asset"] = plan.style.font_asset;
    if (plan.style.font_weight) style["font_weight"] = *plan.style.font_weight;
    if (plan.style.font_size) style["font_size"] = *plan.style.font_size;
    if (!plan.style.fill.empty()) style["fill"] = plan.style.fill;

    if (plan.style.stroke_enabled) {
        style["stroke"] = {
            {"color", plan.style.stroke_color},
            {"width", plan.style.stroke_width.value_or(0.0f)},
        };
    }
    if (plan.style.shadow_enabled) {
        style["shadow"] = {
            {"color", plan.style.shadow_color},
            {"opacity", plan.style.shadow_opacity.value_or(1.0f)},
            {"blur", plan.style.shadow_blur.value_or(0.0f)},
            {"offset", plan.style.shadow_offset
                           ? nlohmann::json::array(
                                 {(*plan.style.shadow_offset)[0],
                                  (*plan.style.shadow_offset)[1]})
                           : nlohmann::json::array({0.0f, 0.0f})},
        };
    }
    if (plan.style.background_enabled) {
        style["background"] = {
            {"color", plan.style.background_color},
            {"opacity", plan.style.background_opacity.value_or(1.0f)},
            {"radius", plan.style.radius.value_or(0.0f)},
            {"padding", plan.style.padding
                            ? nlohmann::json::array(
                                  {(*plan.style.padding)[0],
                                   (*plan.style.padding)[1]})
                            : nlohmann::json::array({0.0f, 0.0f})},
        };
    }

    return nlohmann::json{
        {"preset", plan.preset_id},
        {"semantic_role", plan.semantic_role},
        {"resolved_style", std::move(style)},
        {"resolved_layout",
         {{"intent", plan.layout.intent},
          {"x", plan.layout.x},
          {"y", plan.layout.y},
          {"valid", plan.layout.valid},
          {"warning", plan.layout.warning}}},
        {"resolved_animation",
         {{"preset", plan.animation.preset},
          {"unit", plan.animation.unit},
          {"enter_duration_frames", plan.animation.enter_duration_frames.value_or(0)},
          {"exit_duration_frames", plan.animation.exit_duration_frames.value_or(0)}}},
    };
}

} // namespace chronon3d::registry
