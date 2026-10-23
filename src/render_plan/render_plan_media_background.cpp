// ── media background card pass ─────────────────────────────────────────────
//
// A media layer (video/image) may declare the rounded, padded plate behind its
// box through style.background — the very field text layers already use for
// their card, declared by the same published style contract. This pass
// materialises it as a real Shape layer placed immediately BEFORE the media
// layer, so the plate is composited under the clip:
//
//   - the plate is a colour layer carrying the border colour and radius_px
//     (a flat colour plate honours the top-level `radius` corner contract), so
//     it composites through the proven centred-plate geometry;
//   - the plate's size is the layer's DISPLAYED box (its declared size, or the
//     canvas when it declares none) plus 2*padding: the padding IS the visible
//     frame thickness;
//   - its corner radius is style.background.radius (the frame's outer radius),
//     while the media layer keeps its own `radius` (the concentric inner one);
//   - the shadow moves to the plate. The plate IS the frame, so a shadow kept
//     on the clip underneath would be painted over by the opaque plate and
//     leak past it by exactly the frame thickness; the single shadow the
//     contract declares belongs to the card;
//   - the media layer keeps size/fit/opacity/radius untouched.
//   - the plate follows the media layer's resolved box placement; a full-canvas
//     source clip's centered scale and explicit positioned media both retain
//     their authored transform semantics.
//
// Runs before any per-layer index table (resolved media paths, prepared
// effects, content fingerprint) is built, so plan.layers stays the single
// index space for the rest of the compile.

#include "render_plan_compiler_detail.hpp"

#include <chronon3d/render_plan/color_utils.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {

void materialize_media_background_cards(RenderPlan& plan) {
    bool any_card = false;
    for (const auto& layer : plan.layers) {
        if ((layer.type == LayerType::Video || layer.type == LayerType::Image) &&
            layer.style && layer.style->background) {
            any_card = true;
            break;
        }
    }
    if (!any_card) return;

    std::vector<LayerPlan> expanded;
    expanded.reserve(plan.layers.size() * 2);
    for (auto& layer : plan.layers) {
        const bool media = layer.type == LayerType::Video || layer.type == LayerType::Image;
        if (!media || !layer.style || !layer.style->background) {
            expanded.push_back(std::move(layer));
            continue;
        }
        const auto& background = *layer.style->background;
        const auto parsed_color = parse_hex_color(background.color, 1.0f);
        if (!parsed_color)
            throw std::runtime_error("layer '" + layer.id +
                "' background colour '" + background.color + "' is not #RRGGBB");
        const float opacity = background.opacity.value_or(1.0f);
        if (!std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f)
            throw std::runtime_error(
                "layer '" + layer.id + "' background opacity is outside [0,1]");
        const float padding_x = background.padding_dimensions == 2
            ? background.padding[0] : 0.0f;
        const float padding_y = background.padding_dimensions == 2
            ? background.padding[1] : 0.0f;
        if (!std::isfinite(padding_x) || !std::isfinite(padding_y) ||
            padding_x < 0.0f || padding_y < 0.0f)
            throw std::runtime_error("layer '" + layer.id +
                "' background padding must be finite and non-negative");

        const float scale_x = std::abs(layer.scale[0]) > 1e-6f
            ? std::abs(layer.scale[0]) : 1.0f;
        const float scale_y = std::abs(layer.scale[1]) > 1e-6f
            ? std::abs(layer.scale[1]) : 1.0f;
        const float box_w = (layer.size_dimensions == 2
            ? layer.size[0] : static_cast<float>(plan.canvas.width)) * scale_x;
        const float box_h = (layer.size_dimensions == 2
            ? layer.size[1] : static_cast<float>(plan.canvas.height)) * scale_y;
        const float card_w = box_w + 2.0f * padding_x;
        const float card_h = box_h + 2.0f * padding_y;
        if (!(card_w > 0.0f) || !(card_h > 0.0f) ||
            !std::isfinite(card_w) || !std::isfinite(card_h))
            throw std::runtime_error("layer '" + layer.id +
                "' background plate has a non-positive size");

        LayerPlan card;
        card.id = layer.id + "__frame";
        card.type = LayerType::Color;
        card.start_frame = layer.start_frame;
        card.duration_frames = layer.duration_frames;
        card.size = {card_w, card_h};
        card.size_dimensions = 2;
        // The plate geometry is centered around its transform, while a video
        // source's local surface starts at its top-left. Resolve the plate's
        // center from the same origin-to-center conversion used by the video
        // builder so scaled/positioned media and its frame remain aligned.
        // Its size has already incorporated scale, so the plate stays
        // unit-scaled.
        card.position = layer.position;
        card.position_dimensions = layer.position_dimensions;
        card.rotation = layer.rotation;
        card.rotation_dimensions = layer.rotation_dimensions;
        card.enable_3d = layer.enable_3d;
        card.opacity = layer.opacity;
        if (layer.type == LayerType::Video && layer.position_dimensions >= 2) {
            const float displayed_w = layer.size_dimensions == 2
                ? layer.size[0] : static_cast<float>(plan.canvas.width) * scale_x;
            const float displayed_h = layer.size_dimensions == 2
                ? layer.size[1] : static_cast<float>(plan.canvas.height) * scale_y;
            card.position[0] += displayed_w * 0.5f -
                static_cast<float>(plan.canvas.width) * 0.5f;
            card.position[1] += displayed_h * 0.5f -
                static_cast<float>(plan.canvas.height) * 0.5f;
        }
        // The frame plate is a sibling layer, so it must follow the media's
        // transform and visibility tracks explicitly. Its base size above
        // already includes the media's initial scale; normalize animated
        // scales around that base to avoid applying the initial scale twice.
        if (layer.animation) {
            AnimationPlan plate_animation;
            for (const auto& source_track : layer.animation->tracks) {
                if (source_track.property != "position" && source_track.property != "rotation" &&
                    source_track.property != "scale" && source_track.property != "opacity")
                    continue;
                auto track = source_track;
                if (track.property == "scale") {
                    for (auto& keyframe : track.keyframes) {
                        for (std::size_t axis = 0; axis < keyframe.value.size() && axis < 3; ++axis) {
                            const float base_scale = layer.scale[axis];
                            if (!std::isfinite(base_scale) || std::abs(base_scale) <= 1e-6f)
                                throw std::runtime_error("layer '" + layer.id +
                                    "' has an invalid base scale for its animated media frame");
                            keyframe.value[axis] /= base_scale;
                        }
                    }
                }
                plate_animation.tracks.push_back(std::move(track));
            }
            if (!plate_animation.tracks.empty())
                card.animation = std::move(plate_animation);
        }
        card.color = {parsed_color->r, parsed_color->g, parsed_color->b,
                      parsed_color->a * opacity};
        card.radius = std::min(background.radius.value_or(0.0f),
                               std::min(card_w, card_h) * 0.5f);
        if (layer.style->shadow || !layer.style->shadows.empty()) {
            LayerStylePlan card_style;
            card_style.shadow = layer.style->shadow;
            card_style.shadows = layer.style->shadows;
            card.style = std::move(card_style);
        }
        // The plate owns the frame and the shadow from here on: leaving the
        // declaration on the media layer would materialize a second plate on
        // any later pass over the same plan.
        layer.style->background.reset();
        layer.style->shadow.reset();
        layer.style->shadows.clear();
        expanded.push_back(std::move(card));
        expanded.push_back(std::move(layer));
    }
    plan.layers = std::move(expanded);
}

} // namespace chronon3d::render_plan::detail
