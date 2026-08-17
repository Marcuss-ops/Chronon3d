// ─── style_resolver.cpp — StyleResolver implementation ─────────────────────
//
// Implements the canonical field-level merge declared in
// `include/chronon3d/registry/style_resolver.hpp`:
//
//   preset defaults (VisualStyle) + job overrides (LayerStylePlan)
//       = ResolvedVisualStyle
//
// Merge rule per field: override wins when present, else preset default,
// else "leave base" (empty string / absent optional).  This is the single
// place that knows how a render-plan `style` block combines with a
// VisualPresetRegistry default, so no caller re-implements the recipe.

#include <chronon3d/registry/style_resolver.hpp>

#include <chronon3d/registry/visual_preset_registry.hpp>
#include <chronon3d/render_plan/render_plan.hpp>  // LayerStylePlan (full def)

namespace chronon3d::registry {

ResolvedVisualStyle VisualStyleResolver::resolve(
    const VisualStyle& defaults,
    const render_plan::LayerStylePlan* overrides) const {
    const auto* o = overrides;  // may be null → defaults-only.

    ResolvedVisualStyle out;

    // ── Typography + fill ────────────────────────────────────────────────
    out.font_family = (o && !o->font_family.empty()) ? o->font_family
                                                     : defaults.font_family;
    // The font asset (byte-identity anchor) has no LayerStylePlan override:
    // the render-plan's `font` / `font_asset.asset` are top-level LayerPlan
    // fields applied by the compiler AFTER the resolved style, so here it is
    // the preset default carried through unchanged.
    out.font_asset = defaults.font_asset;
    out.font_weight = (o && o->font_weight) ? o->font_weight : defaults.font_weight;
    out.font_size   = (o && o->font_size)   ? o->font_size   : defaults.font_size;
    out.fill        = (o && !o->fill.empty()) ? o->fill : defaults.fill;

    // ── Stroke ───────────────────────────────────────────────────────────
    const bool default_stroke =
        !defaults.stroke_color.empty() || defaults.stroke_width.has_value();
    const bool override_stroke = o && o->stroke.has_value();
    out.stroke_enabled = override_stroke || default_stroke;
    out.stroke_color = (override_stroke && !o->stroke->color.empty())
                           ? o->stroke->color
                           : defaults.stroke_color;
    out.stroke_width = (override_stroke && o->stroke->width)
                           ? o->stroke->width
                           : defaults.stroke_width;

    // ── Shadow ───────────────────────────────────────────────────────────
    const bool default_shadow = !defaults.shadow_color.empty() ||
                                defaults.shadow_opacity.has_value() ||
                                defaults.shadow_blur.has_value();
    const bool override_shadow = o && o->shadow.has_value();
    out.shadow_enabled = override_shadow || default_shadow;
    out.shadow_color = (override_shadow && !o->shadow->color.empty())
                           ? o->shadow->color
                           : defaults.shadow_color;
    out.shadow_opacity = (override_shadow && o->shadow->opacity)
                             ? o->shadow->opacity
                             : defaults.shadow_opacity;
    out.shadow_blur = (override_shadow && o->shadow->blur)
                          ? o->shadow->blur
                          : defaults.shadow_blur;
    out.shadow_offset = (override_shadow && o->shadow->offset_dimensions >= 2)
                            ? std::optional<std::array<float, 2>>{o->shadow->offset}
                            : defaults.shadow_offset;

    // ── Background card ──────────────────────────────────────────────────
    const bool default_background = !defaults.background_color.empty() ||
                                    defaults.background_opacity.has_value() ||
                                    defaults.radius.has_value() ||
                                    defaults.padding.has_value();
    const bool override_background = o && o->background.has_value();
    out.background_enabled = override_background || default_background;
    out.background_color = (override_background && !o->background->color.empty())
                               ? o->background->color
                               : defaults.background_color;
    out.background_opacity = (override_background && o->background->opacity)
                                 ? o->background->opacity
                                 : defaults.background_opacity;
    out.radius = (override_background && o->background->radius)
                     ? o->background->radius
                     : defaults.radius;
    out.padding = (override_background && o->background->padding_dimensions >= 2)
                      ? std::optional<std::array<float, 2>>{o->background->padding}
                      : defaults.padding;

    return out;
}

ResolvedVisualStyle VisualStyleResolver::resolve_preset(
    const VisualPresetRegistry& registry,
    std::string_view preset_id,
    const render_plan::LayerStylePlan* overrides) const {
    return resolve(registry.get(preset_id).style, overrides);
}

} // namespace chronon3d::registry
