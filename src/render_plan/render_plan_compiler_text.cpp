#include "render_plan_compiler_detail.hpp"

#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/effects/effect_params.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace chronon3d::render_plan::detail {

void apply_text_glow(chronon3d::LayerBuilder& builder, const LayerPlan& layer) {
    if (!layer.style || !layer.style->glow) return;

    const auto& spec = *layer.style->glow;
    const auto color = parse_hex_color(spec.color);
    if (!color)
        throw std::runtime_error("text layer '" + layer.id + "' has invalid glow color");
    builder.effect(chronon3d::GlowParams{
        .radius = spec.radius,
        .intensity = spec.intensity,
        .color = *color,
    });
}

chronon3d::TextDefinition materialize_text(
    const LayerPlan& layer, const chronon3d::CanvasInfo& canvas) {
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
        // Keep colour alpha and shadow opacity as separate factors.  The
        // canonical text renderer applies opacity once to the blurred union
        // mask; baking it into the colour here would multiply it twice.
        const auto shadow_color = parse_hex_color(layer.style->shadow->color);
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
        : chronon3d::Vec2{static_cast<float>(canvas.width),
                          static_cast<float>(canvas.height)};
    definition.frame.anchor = chronon3d::TextAnchor::Center;
    definition.frame.align = chronon3d::TextAlign::Center;
    definition.frame.vertical_align = chronon3d::VerticalAlign::Middle;
    definition.frame.placement = chronon3d::TextPlacement{
        chronon3d::TextPlacementKind::CanvasCenter, {0.0f, 0.0f}};
    return definition;
}

}  // namespace chronon3d::render_plan::detail
