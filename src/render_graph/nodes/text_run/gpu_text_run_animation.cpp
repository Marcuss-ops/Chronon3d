// SPDX-License-Identifier: MIT
//
// gpu_text_run_animation.cpp — GPU text-run animator lowering and sampling.
//
// Split out of gpu_text_run.cpp: the predicates that decide whether an animator
// stack can be lowered to a single runtime TextRunDynamic record, plus the
// samplers that fill that record.  No atlas, surface or backend work lives
// here.  See gpu_text_run.hpp for the contract.

#include "gpu_text_run.hpp"

#include <chronon3d/text/animation/text_animator_properties.hpp>
#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/text/text_color_packing.hpp>
#include <chronon3d/runtime/gpu_text_selector_metadata.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <variant>

namespace chronon3d::graph::text_run {

namespace {

bool is_full_run_selector(const TextRunShape& shape,
                          const TextAnimatorSpec& animator) noexcept {
    if (!shape.layout || shape.layout->units.word_count != 1 ||
        animator.selectors.size() != 1) {
        return false;
    }
    const auto& selector = animator.selectors.front();
    const auto initial = bind_selector_runtime(
        selector, 1u, SampleTime::from_frame(0.0, FrameRate{30, 1}));
    return selector.unit == TextSelectorUnit::Word &&
           (selector.shape == TextSelectorShape::Square ||
            selector.shape == TextSelectorShape::Smooth) &&
           selector.order == TextSelectorOrder::Forward &&
           selector.combine == SelectorCombineMode::Replace &&
           !selector.randomize_order && !selector.offset.is_time_dependent() &&
           !selector.start.has_expression() &&
           !selector.end.has_expression() &&
           !selector.amount.has_expression() &&
           std::abs(selector.ease_low) <= 1e-6f &&
           std::abs(selector.ease_high - 100.0f) <= 1e-6f &&
           std::abs(initial.start) <= 1e-6f &&
           std::abs(initial.end - 100.0f) <= 1e-6f;
}

float sample_full_run_selector_weight(
    const GlyphSelectorSpec& selector,
    SampleTime sample_time) noexcept {
    const SelectorWeightParams params =
        bind_selector_runtime(selector, 1u, sample_time);
    return selector_weight(params, 0u);
}

} // namespace

bool can_lower_gpu_run_level_animation(const TextRunShape& shape) noexcept {
    if (!shape.layout || shape.animated_doc || shape.animators.size() != 1) {
        return false;
    }
    const auto& animator = shape.animators.front();
    if (!animator.enabled || !is_full_run_selector(shape, animator)) return false;

    bool saw_dynamic_property = false;
    for (const auto& property : animator.properties) {
        if (const auto* position = std::get_if<PositionProperty>(&property)) {
            if (position->value.has_expression()) return false;
            saw_dynamic_property = true;
        } else if (const auto* scale = std::get_if<ScaleProperty>(&property)) {
            if (scale->value.has_expression()) return false;
            saw_dynamic_property = true;
        } else if (const auto* opacity = std::get_if<OpacityProperty>(&property)) {
            if (opacity->value.has_expression()) return false;
            saw_dynamic_property = true;
        } else if (std::holds_alternative<FillColorProperty>(property)) {
            saw_dynamic_property = true;
        } else {
            return false;
        }
    }
    return saw_dynamic_property;
}

void sample_gpu_run_level_animation(
    const TextRunShape& shape,
    SampleTime sample_time,
    runtime::TextRunDynamic& run) noexcept {
    if (!can_lower_gpu_run_level_animation(shape)) return;
    const auto& animator = shape.animators.front();
    const auto& selector = animator.selectors.front();
    const float selector_weight =
        sample_full_run_selector_weight(selector, sample_time);
    for (const auto& property : animator.properties) {
        if (const auto* position = std::get_if<PositionProperty>(&property)) {
            const auto value = position->value.evaluate(sample_time);
            run.tx += value.x * selector_weight;
            run.ty += value.y * selector_weight;
        } else if (const auto* scale = std::get_if<ScaleProperty>(&property)) {
            const auto value = scale->value.evaluate(sample_time);
            run.sx *= 1.0f + (value.x - 1.0f) * selector_weight;
            run.sy *= 1.0f + (value.y - 1.0f) * selector_weight;
        } else if (const auto* opacity = std::get_if<OpacityProperty>(&property)) {
            const auto value = opacity->value.evaluate(sample_time);
            run.opacity *= 1.0f + (value - 1.0f) * selector_weight;
        } else if (const auto* fill = std::get_if<FillColorProperty>(&property)) {
            const auto unpack = [](std::uint32_t packed, int shift) {
                return static_cast<float>((packed >> shift) & 0xFFu) / 255.0f;
            };
            Color base{
                unpack(run.color, 0), unpack(run.color, 8),
                unpack(run.color, 16), unpack(run.color, 24)};
            base.r += (fill->color.r - base.r) * selector_weight;
            base.g += (fill->color.g - base.g) * selector_weight;
            base.b += (fill->color.b - base.b) * selector_weight;
            base.a += (fill->color.a - base.a) * selector_weight;
            run.color = ::chronon3d::text::pack_text_color(base);
        }
    }
}

bool can_lower_gpu_text_animation(const TextRunShape& shape) noexcept {
    if (!shape.layout || shape.animated_doc ||
        shape.layout->font_spans.size() > 1 ||
        shape.paint.fill_style || shape.paint.stroke_style ||
        shape.material.enabled || shape.shadows.size() > 1 ||
        shape.glyphs.size() != shape.layout->placed.glyphs.size()) {
        return false;
    }
    if (shape.paint.stroke_enabled &&
        (shape.paint.stroke_width < 0.0f || shape.paint.stroke_width > 16.0f)) {
        return false;
    }
    for (const auto& shadow : shape.shadows) {
        if (!shadow.enabled) continue;
        if (shadow.blur < 0.0f || shadow.blur > 64.0f ||
            std::abs(shadow.offset.x) + shadow.blur > 64.0f ||
            std::abs(shadow.offset.y) + shadow.blur > 64.0f) {
            return false;
        }
    }
    if (shape.animators.empty()) return true;
    if (shape.animators.size() != 1) return false;
    const auto& animator = shape.animators.front();
    if (!animator.enabled || animator.selectors.size() != 1 ||
        animator.properties.empty()) {
        return false;
    }
    if (can_lower_gpu_run_level_animation(shape)) return true;
    const auto& selector = animator.selectors.front();
    // This is the canonical per-glyph GPU text contract. The whole shaped
    // run remains resident; only this small dynamic record changes per frame.
    if ((selector.unit != TextSelectorUnit::Glyph &&
         selector.unit != TextSelectorUnit::Character &&
         selector.unit != TextSelectorUnit::Grapheme &&
         selector.unit != TextSelectorUnit::Word &&
         selector.unit != TextSelectorUnit::Line) ||
        (selector.shape != TextSelectorShape::Square &&
         selector.shape != TextSelectorShape::Smooth) ||
        (selector.order != TextSelectorOrder::Forward &&
         selector.order != TextSelectorOrder::Reverse &&
         selector.order != TextSelectorOrder::FromCenter &&
         selector.order != TextSelectorOrder::ToCenter) ||
        selector.combine != SelectorCombineMode::Replace ||
        selector.randomize_order || selector.offset.is_time_dependent() ||
        std::abs(selector.ease_low) > 1e-6f ||
        std::abs(selector.ease_high - 100.0f) > 1e-6f ||
        std::abs(bind_selector_runtime(
                     selector, shape.layout->units.unit_count(selector.unit),
                     SampleTime::from_frame(0.0, FrameRate{30, 1})).offset) > 1e-6f ||
        std::abs(bind_selector_runtime(
                     selector, shape.layout->units.unit_count(selector.unit),
                     SampleTime::from_frame(0.0, FrameRate{30, 1})).amount - 100.0f) > 1e-6f) {
        return false;
    }
    // Word and line indexes share the reserved upper bits of each prepared
    // MTSDF glyph flag. Keep the sentinel outside the valid unit range.
    if ((selector.unit == TextSelectorUnit::Word &&
         shape.layout->units.word_count > runtime::kMtsdfGlyphWordIndexMask) ||
        (selector.unit == TextSelectorUnit::Line &&
         shape.layout->units.line_count > runtime::kMtsdfGlyphWordIndexMask)) {
        return false;
    }
    bool saw_supported_property = false;
    for (const auto& property : animator.properties) {
        if (const auto* opacity = std::get_if<OpacityProperty>(&property)) {
            // The tiled text shader already folds animator_opacity with the
            // per-glyph selector weight.  Keep this profile GPU-native rather
            // than routing the canonical typewriter reveal through the
            // legacy software glyph loop.
            if (opacity->value.has_expression()) return false;
            saw_supported_property = true;
        } else if (const auto* position = std::get_if<PositionProperty>(&property)) {
            if (position->value.has_expression()) return false;
            saw_supported_property = true;
        } else if (const auto* scale = std::get_if<ScaleProperty>(&property)) {
            if (scale->value.has_expression()) return false;
            saw_supported_property = true;
        } else if (const auto* tracking = std::get_if<TrackingProperty>(&property)) {
            if (tracking->pixels.has_expression() ||
                selector.unit == TextSelectorUnit::Word) return false;
            saw_supported_property = true;
        } else if (const auto* blur = std::get_if<BlurProperty>(&property)) {
            if (blur->radius.has_expression()) return false;
            saw_supported_property = true;
        } else {
            return false;
        }
    }
    return saw_supported_property;
}

void sample_gpu_text_animation(
    const TextRunShape& shape,
    SampleTime sample_time,
    runtime::TextRunDynamic& run) noexcept {
    if (!shape.layout || shape.animators.size() != 1 ||
        !can_lower_gpu_text_animation(shape)) return;
    const auto& animator = shape.animators.front();
    if (animator.selectors.size() != 1) return;
    const auto& selector = animator.selectors.front();
    if (selector.unit != TextSelectorUnit::Glyph &&
        selector.unit != TextSelectorUnit::Character &&
        selector.unit != TextSelectorUnit::Grapheme &&
        selector.unit != TextSelectorUnit::Word &&
        selector.unit != TextSelectorUnit::Line) return;
    if (can_lower_gpu_run_level_animation(shape)) return;

    // flags bit 0 is shaped-plane placement; bits 1/2 are selector metadata.
    // Bit 3 is a host-resolved completed-window flag. The shaders consume the
    // resulting per-glyph weights and never recalculate selector semantics.
    run.flags |= 2u;
    run.flags = runtime::pack_text_selector_metadata(
        run.flags, static_cast<std::uint32_t>(selector.unit),
        static_cast<std::uint32_t>(selector.order));
    const auto bound = bind_selector_runtime(
        selector, shape.layout->units.unit_count(selector.unit), sample_time);
    if (bound.shape == TextSelectorShape::Smooth) run.flags |= 4u;
    if (bound.window_completed) run.flags |= 8u;
    run.visible_glyph_count = std::numeric_limits<std::uint32_t>::max();
    run.selector_start = bound.start;
    run.selector_end = bound.end;
    run.selector_amount = bound.amount;
    run.selector_unit_count = bound.total_units;

    for (const auto& property : animator.properties) {
        if (const auto* opacity = std::get_if<OpacityProperty>(&property)) {
            run.animator_opacity *= opacity->value.evaluate(sample_time);
        } else if (const auto* position = std::get_if<PositionProperty>(&property)) {
            const auto value = position->value.evaluate(sample_time);
            run.animator_position_x += value.x;
            run.animator_position_y += value.y;
        } else if (const auto* scale = std::get_if<ScaleProperty>(&property)) {
            const auto value = scale->value.evaluate(sample_time);
            run.animator_scale_x *= value.x;
            run.animator_scale_y *= value.y;
        } else if (const auto* tracking = std::get_if<TrackingProperty>(&property)) {
            run.animator_tracking += tracking->pixels.evaluate(sample_time);
        } else if (const auto* blur = std::get_if<BlurProperty>(&property)) {
            run.animator_blur += std::max(0.0f, blur->radius.evaluate(sample_time));
        }
    }
}

} // namespace chronon3d::graph::text_run
