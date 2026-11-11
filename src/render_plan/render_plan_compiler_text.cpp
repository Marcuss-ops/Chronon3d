#include "render_plan_compiler_detail.hpp"
#include "render_plan_compiler_animation_internal.hpp"

#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/text/animated_text_document.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_run_builder.hpp>
#include <chronon3d/scene/builders/text_run_materialization.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace chronon3d::render_plan::detail {

PreparedTextTransitions build_text_transitions(
    const std::string& layer_id, const chronon3d::TextDefinition& base_definition,
    const TextTransitionsPlan& transitions, FrameRate fps) {
    chronon3d::AnimatedTextDocument animated;
    chronon3d::TextDefinition final_definition = base_definition;
    for (const auto& keyframe : transitions.keyframes) {
        auto definition_at_frame = base_definition;
        definition_at_frame.content.value = keyframe.text;
        // A transition keyframe owns the ranges authored against its own
        // text.  The layer-level range block remains mutually exclusive with
        // text_transitions, so there is no ambiguity about which string a
        // range addresses.
        definition_at_frame.spans = keyframe.spans;
        definition_at_frame.glyph_paint_ranges = keyframe.glyph_paint_ranges;
        chronon3d::SourceTextKeyframe source_key;
        source_key.frame = keyframe.frame;
        source_key.document = chronon3d::to_text_document(definition_at_frame);
        if (keyframe.transition == "cut") {
            source_key.transition = chronon3d::SourceTextTransition::Cut;
        } else if (keyframe.transition == "dissolve_layouts") {
            source_key.transition = chronon3d::SourceTextTransition::DissolveLayouts;
        } else if (keyframe.transition == "scramble") {
            source_key.transition = chronon3d::SourceTextTransition::Scramble;
            source_key.scramble_params.seed = keyframe.scramble.seed;
            source_key.scramble_params.char_pool = keyframe.scramble.char_pool;
            source_key.scramble_params.intensity = keyframe.scramble.intensity;
        } else if (keyframe.transition == "morph") {
            source_key.transition = chronon3d::SourceTextTransition::Morph;
            source_key.morph_params.direction =
                keyframe.morph.direction == "from_left"
                    ? chronon3d::MorphParams::EnterDirection::FromLeft
                    : keyframe.morph.direction == "from_center"
                        ? chronon3d::MorphParams::EnterDirection::FromCenter
                        : chronon3d::MorphParams::EnterDirection::FromRight;
            source_key.morph_params.easing = track_easing(keyframe.morph.easing);
        } else if (keyframe.transition == "typewriter") {
            source_key.transition = chronon3d::SourceTextTransition::Typewriter;
            source_key.typewriter_params.chars_per_frame =
                keyframe.typewriter.chars_per_frame;
            source_key.typewriter_params.easing =
                track_easing(keyframe.typewriter.easing);
        }
        final_definition = definition_at_frame;
        animated.add_keyframe(std::move(source_key));
    }
    if (animated.size() == 0)
        throw std::runtime_error(
            "text layer '" + layer_id + "' has no text_transitions keyframes");
    return {std::make_shared<const chronon3d::AnimatedTextDocument>(
                std::move(animated)),
            std::move(final_definition)};
}

PreparedTextLayers prepare_text_layers(
    const RenderPlan& plan, chronon3d::assets::AssetResolver& resolver,
    const chronon3d::CanvasInfo& canvas,
    const std::shared_ptr<chronon3d::FontEngine>& font_engine) {
    std::vector<std::optional<chronon3d::PreparedText>> prepared_texts(
        plan.layers.size());
    std::vector<std::shared_ptr<const chronon3d::AnimatedTextDocument>> prepared_text_counters(
        plan.layers.size());
    std::vector<std::optional<chronon3d::Vec2>> prepared_text_bounds(
        plan.layers.size());
    const auto assets_root = resolver.mount_root();
    chronon3d::TextCompileServices text_compile_services{
        font_engine.get(), nullptr,
        assets_root.empty() ? std::filesystem::path{}
                            : assets_root / "fonts"};
    for (std::size_t index = 0; index < plan.layers.size(); ++index) {
        const auto& layer = plan.layers[index];
        if (layer.type != LayerType::Text) continue;
        if (!font_engine) {
            throw std::runtime_error(
                "FontEngineUnavailable: text layer '" + layer.id +
                "' cannot resolve fonts without a FontEngine");
        }
        auto definition = materialize_text(layer, canvas);
        if (!definition.style.font.font_path.empty()) {
            const auto resolved = resolver.resolve_logical(
                std::filesystem::path(definition.style.font.font_path));
            if (!resolved) {
                throw std::runtime_error(
                    "font asset was not prepared: " +
                    definition.style.font.font_path);
            }
            definition.style.font.font_path = resolved->string();
        }
        if (layer.text_counter && layer.text_transitions)
            throw std::runtime_error(
                "text layer '" + layer.id + "' cannot combine text_counter and text_transitions");
        if (layer.text_transitions &&
            (!layer.text_spans.empty() || !layer.svg_glyph_paint_ranges.empty()))
            throw std::runtime_error(
                "text layer '" + layer.id +
                "' cannot combine text_transitions with styled text spans");
        if (layer.text_counter) {
            const auto& counter = layer.text_counter->counter;
            AnimationTrackPlan value_track;
            value_track.property = "text_counter";
            value_track.easing = counter.easing;
            value_track.keyframes = {
                {counter.start_frame, {counter.from}},
                {counter.start_frame + counter.duration_frames, {counter.to}}};
            chronon3d::AnimatedTextDocument animated;
            const auto replace_token = [&](float value) {
                double magnitude = value;
                std::string compact_suffix;
                if (counter.format == "percent") magnitude *= 100.0;
                if (counter.format == "compact") {
                    const double absolute = std::abs(magnitude);
                    if (absolute >= 1.0e9) { magnitude /= 1.0e9; compact_suffix = "B"; }
                    else if (absolute >= 1.0e6) { magnitude /= 1.0e6; compact_suffix = "M"; }
                    else if (absolute >= 1.0e3) { magnitude /= 1.0e3; compact_suffix = "K"; }
                }
                std::ostringstream formatted;
                formatted.imbue(std::locale::classic());
                formatted << std::fixed << std::setprecision(counter.decimals) << magnitude;
                std::string number = formatted.str();
                if (counter.decimals == 0)
                    number = std::to_string(static_cast<std::int64_t>(std::llround(magnitude)));
                if (counter.format == "percent") number += "%";
                else if (counter.format == "currency") {
                    const std::string symbol = counter.currency == "USD" ? "$" :
                        counter.currency == "EUR" ? "€" : counter.currency == "GBP" ? "£" : counter.currency + " ";
                    number = symbol + number;
                } else if (counter.format == "compact") number += compact_suffix;
                return counter.prefix + number + counter.suffix;
            };
            const auto fill_value = [&](float value) {
                auto target = definition;
                const auto found = target.content.value.find(layer.text_counter->token);
                target.content.value.replace(found, layer.text_counter->token.size(), replace_token(value));
                const auto token_start = found;
                const auto token_end = found + layer.text_counter->token.size();
                const auto replacement_size = target.content.value.size() -
                    (definition.content.value.size() - layer.text_counter->token.size());
                const auto delta = static_cast<std::int64_t>(replacement_size) -
                    static_cast<std::int64_t>(layer.text_counter->token.size());
                for (auto& span : target.spans) {
                    if (span.byte_end <= token_start) continue;
                    if (span.byte_start < token_end)
                        throw std::runtime_error("text_counter token cannot overlap styled text spans");
                    const auto shifted_start = static_cast<std::int64_t>(span.byte_start) + delta;
                    const auto shifted_end = static_cast<std::int64_t>(span.byte_end) + delta;
                    if (shifted_start < 0 || shifted_end < shifted_start)
                        throw std::runtime_error("text_counter span shift overflow");
                    span.byte_start = static_cast<std::size_t>(shifted_start);
                    span.byte_end = static_cast<std::size_t>(shifted_end);
                }
                return target;
            };
            for (std::int64_t offset = 0;
                 offset <= counter.duration_frames.integral(); ++offset) {
                const Frame frame = counter.start_frame + Frame{offset};
                const float value = detail::evaluate_scalar_track(
                    value_track, static_cast<double>(frame.integral()),
                    counter.from, plan.canvas.fps);
                const auto definition_at_frame = fill_value(value);
                chronon3d::SourceTextKeyframe key;
                key.frame = frame;
                key.document = chronon3d::to_text_document(definition_at_frame);
                animated.add_keyframe(std::move(key));
            }
            const auto to_definition = fill_value(counter.to);
            prepared_text_counters[index] =
                std::make_shared<const chronon3d::AnimatedTextDocument>(std::move(animated));
            definition = to_definition;
        }
        if (layer.text_transitions) {
            auto transitioned = build_text_transitions(
                layer.id, definition, *layer.text_transitions, plan.canvas.fps);
            prepared_text_counters[index] = std::move(transitioned.document);
            definition = std::move(transitioned.final_definition);
        }
        prepared_texts[index] = chronon3d::prepare_text(definition);
        if ((layer.style && layer.style->background) || layer.safe_insets) {
            auto measured = chronon3d::compile_text_layout(
                *prepared_texts[index], text_compile_services);
            if (!measured)
                throw std::runtime_error(
                    "text card layout failed for layer '" + layer.id + "': " +
                    measured.error().message);
            prepared_text_bounds[index] = measured.value()->bounds;
        }
    }
    return {std::move(prepared_texts), std::move(prepared_text_counters),
            std::move(prepared_text_bounds)};
}

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
    if (!layer.style || (layer.font.empty() && layer.style->font_family.empty()) || !layer.style->font_size ||
        layer.style->fill.empty()) {
        throw std::runtime_error("text layer '" + layer.id +
                                 "' is missing concrete font/font_size/fill style");
    }

    chronon3d::TextDefinition definition;
    definition.content.value = layer.text;
    definition.spans = layer.text_spans;
    definition.glyph_paint_ranges = layer.svg_glyph_paint_ranges;
    definition.style.font.font_path = layer.font;
    definition.style.font.font_family = layer.style->font_family;
    definition.style.font.font_weight = layer.style->font_weight;
    definition.style.font.font_style = layer.style->font_style;
    definition.style.font.font_size = *layer.style->font_size;
    if (!layer.style->variation_axes.empty()) {
        std::size_t axis_begin = 0;
        while (axis_begin <= layer.style->variation_axes.size()) {
            const auto axis_end = layer.style->variation_axes.find(',', axis_begin);
            const auto token = layer.style->variation_axes.substr(
                axis_begin, axis_end == std::string::npos
                    ? std::string::npos : axis_end - axis_begin);
            definition.paragraph.variable_axes.push_back(chronon3d::VariableAxis{
                token.substr(0, 4), std::stof(token.substr(5))});
            if (axis_end == std::string::npos) break;
            axis_begin = axis_end + 1;
        }
    }
    definition.paragraph.direction = layer.style->text_direction == "rtl"
        ? TextDirection::RTL : layer.style->text_direction == "ltr"
            ? TextDirection::LTR : TextDirection::Auto;
    auto fill = parse_hex_color(layer.style->fill);
    if (!fill)
        throw std::runtime_error("text layer '" + layer.id + "' has invalid fill color");
    fill->a *= layer.style->fill_opacity.value_or(1.0f);
    definition.style.color = *fill;
    definition.style.paint.fill = *fill;
    definition.style.paint.quality = layer.style->quality == "high"
        ? TextQuality::High : TextQuality::Fast;
    definition.style.paint.shadow_quality =
        layer.style->shadow_quality == "canonical"
            ? TextShadowQuality::Canonical : TextShadowQuality::Fast;
    definition.style.paint.stroke_mode = layer.style->stroke_mode == "distance"
        ? TextStrokeMode::Distance
        : layer.style->stroke_mode == "vector"
            ? TextStrokeMode::Vector : TextStrokeMode::Auto;
    if (layer.style->gel_core) {
        definition.style.material = chronon3d::TextMaterial::gel_core();
    } else if (layer.style->gel_surface) {
        definition.style.material = chronon3d::TextMaterial::gel();
        definition.style.material.gel_soft_fusion =
            layer.style->gel_soft_fusion.value_or(true);
        definition.style.material.gel_transmission_enabled =
            layer.style->gel_transmission_enabled;
        definition.style.material.gel_reference_20261107 =
            layer.style->gel_material_profile == "reference_20261107";
        definition.style.material.gel_debug_mode = layer.style->gel_debug_mode;
        if (layer.style->gel_light_azimuth_radians)
            definition.style.material.gel_light_azimuth_radians =
                *layer.style->gel_light_azimuth_radians;
        if (layer.style->gel_light_elevation)
            definition.style.material.gel_light_elevation =
                *layer.style->gel_light_elevation;
    }
    if (layer.style->extruded_text) {
        definition.style.material = chronon3d::TextMaterial::extruded_3d(
            layer.style->extrusion_depth);
    }
    definition.style.paint.fill_pattern = layer.style->fill_pattern;
    if (layer.style->fill_gradient) {
        auto gradient = *layer.style->fill_gradient;
        const float opacity = layer.style->fill_opacity.value_or(1.0f);
        for (auto& stop : gradient.color_stops) stop.color.a *= opacity;
        for (auto& stop : gradient.opacity_stops) stop.opacity *= opacity;
        graphics::FillStyle fill_style;
        fill_style.gradient = std::move(gradient);
        definition.style.paint.fill_style = fill_style.to_fill();
    }

    if (layer.style->stroke) {
        auto stroke_color = parse_hex_color(layer.style->stroke->color);
        if (!stroke_color)
            throw std::runtime_error("text layer '" + layer.id + "' has invalid stroke color");
        stroke_color->a *= layer.style->stroke->opacity.value_or(1.0f);
        definition.style.paint.stroke_enabled = true;
        definition.style.paint.stroke_color = *stroke_color;
        definition.style.paint.stroke_width = layer.style->stroke->width.value_or(1.0f);
        const auto& stroke = *layer.style->stroke;
        definition.style.paint.stroke_cap = stroke.cap == "round" ? LineCap::Round
            : stroke.cap == "square" ? LineCap::Square : LineCap::Butt;
        definition.style.paint.stroke_join = stroke.join == "round" ? LineJoin::Round
            : stroke.join == "bevel" ? LineJoin::Bevel : LineJoin::Miter;
        definition.style.paint.stroke_miter_limit = stroke.miter_limit;
        definition.style.paint.stroke_dash_array = stroke.dash_array;
        definition.style.paint.stroke_dash_offset = stroke.dash_offset;
        if (stroke.gradient) {
            auto gradient = *stroke.gradient;
            const float opacity = stroke.opacity.value_or(1.0f);
            for (auto& stop : gradient.color_stops) stop.color.a *= opacity;
            for (auto& stop : gradient.opacity_stops) stop.opacity *= opacity;
            graphics::FillStyle fill_style;
            fill_style.gradient = std::move(gradient);
            definition.style.paint.stroke_style = fill_style.to_fill();
        }
        definition.style.paint.stroke_pattern = layer.style->stroke_pattern;
    }
    const auto append_shadow = [&](const ShadowStyle& shadow_style) {
        // Keep colour alpha and shadow opacity as separate factors.  The
        // canonical text renderer applies opacity once to the blurred union
        // mask; baking it into the colour here would multiply it twice.
        const auto shadow_color = parse_hex_color(shadow_style.color);
        if (!shadow_color)
            throw std::runtime_error("text layer '" + layer.id + "' has invalid shadow color");
        chronon3d::TextShadow shadow;
        shadow.enabled = true;
        shadow.color = *shadow_color;
        shadow.opacity = shadow_style.opacity.value_or(1.0f);
        shadow.blur = shadow_style.blur.value_or(0.0f);
        if (shadow_style.offset_dimensions == 2) {
            shadow.offset = {shadow_style.offset[0], shadow_style.offset[1]};
        }
        definition.style.shadows.push_back(shadow);
    };
    if (layer.style->shadow) append_shadow(*layer.style->shadow);
    for (const auto& shadow : layer.style->shadows) append_shadow(shadow);
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
    definition.frame.align = layer.style->text_align == "left"
        ? chronon3d::TextAlign::Left
        : layer.style->text_align == "right"
            ? chronon3d::TextAlign::Right : chronon3d::TextAlign::Center;
    definition.frame.vertical_align = chronon3d::VerticalAlign::Middle;
    definition.frame.auto_fit = layer.style->text_fit_mode != "none";
    definition.frame.grow_and_shrink =
        layer.style->text_fit_mode == "grow_and_shrink";
    definition.frame.min_font_size = layer.style->min_font_size.value_or(12.0f);
    // The authored size is the ceiling: defaulting max_font_size to 160
    // silently capped 180/200 px titles even when they fit the box.
    definition.frame.max_font_size = layer.style->max_font_size.value_or(
        layer.style->font_size.value_or(160.0f));
    definition.frame.placement = chronon3d::TextPlacement{
        chronon3d::TextPlacementKind::CanvasCenter, {0.0f, 0.0f}};
    return definition;
}

}  // namespace chronon3d::render_plan::detail
