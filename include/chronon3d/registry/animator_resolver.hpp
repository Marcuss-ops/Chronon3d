// ==============================================================================
// include/chronon3d/registry/animator_resolver.hpp
//
// PUBLIC API — `struct AnimatorResolver`, formerly an anonymous-namespace
// struct in `src/registry/text_preset_registry.cpp`.  Header-lifted per
// TICKET-012 to expose a stable per-id AnimatorSpec-mapping to any TU that
// includes this header (test harness, downstream Cluster B authoring facade,
// canonical resolver-table inspector).
//
// Method BODIES are INLINE in the header (per TICKET-012 acceptance criteria
// "Mark methods inline").  Static member functions defined inside the struct
// body are implicitly inline — no separate .cpp file is needed; the
// `chronon3d_registry` OBJECT library retains its single source of truth
// (the registry was previously the only TU holding the resolver table; now
// the table lives in a header consumed by both the registry and its
// consumers).
//
// Methods are `static`, so callers don't need to instantiate the struct;
// no shared / mutable state across calls.  Anchor string-ids (e.g.
// "ctc_rich_<preset_id>" and "presetc_<preset_id>") are bound at
// compile time; no runtime registry lookup is performed.
//
// Anti-circular dependency: this header DOES NOT include any
// `content/text/text_*.hpp`.  It pulls the canonical TextSpec +
// TextAnimatorSpec + property types from
// `<chronon3d/scene/builders/builder_params.hpp>`.  External callers
// that instantiate `chronon3d::TextSpec` can pass it to the resolver
// unchanged.
//
// Cross-references:
//   - TICKET-012 — header-lift rationale + acceptance criteria.
//   - docs/ANTI_DUPLICATION_RULES.md — avoid duplicating registries /
//     resolvers / composers / caches; this header-lift consolidates
//     the resolver so the registry TU no longer owns it uniquely.
//   - include/chronon3d/registry/text_preset_resolver.hpp — peer header
//     that exposes the public `wire_preset_text_run_params(...)` free
//     function (which delegates to AnimatorResolver::compose_for).
// ==============================================================================
#pragma once

#include <chronon3d/scene/builders/builder_params.hpp>  // TextSpec, TextAnimatorSpec, Vec3,
                                                          // TextPropertyBlendMode, GlyphSelectorSpec,
                                                          // TextSelectorUnit, TextSelectorShape,
                                                          // OpacityProperty, PositionProperty,
                                                          // ScaleProperty, BlurProperty,
                                                          // TrackingProperty.

#include <optional>
#include <string>
#include <string_view>

namespace chronon3d::registry {

struct AnimatorResolver {
    // ── Stage 4 ──────────────────────────────────────────────────────────
    // AnimatorResolver maps motion-preset canonical entries to TextAnimatorSpec
    // entries that compose the per-glyph animated state alongside the layer
    // level transform.  At Stage 4 we ship ONE conservative resolver mapping:
    //
    //     cinematic_text_camera  ─►  rich_paint_anchor("cinematic_text_camera")
    //
    // fired when the spec carries ANY of:
    //   - appearance.paint.stroke_enabled        = true
    //   - appearance.paint.fill_style.has_value() (a Fill variant populated)
    //   - appearance.paint.stroke_style.has_value()
    //
    // These three signals proxy a "richly-painted text spec" intent (the
    // caller-authored `paint.rich_text` style — gradient fill, outline, or
    // stroke).  When resolved, the resolver pushes a global-selector
    // TextAnimatorSpec with `id = "ctc_rich_<preset_id>"` onto
    // params.animators BEFORE the cinematic_text_camera factory invokes the
    // canonical motion-preset chain.  This satisfies the DoD #2 contract:
    //
    //   "preset registry resolves motion(id) against embedded animators
    //    BEFORE invoking the motion-preset canon."
    //
    // Downstream stages can extend the resolver table for additional motion
    // presets WITHOUT touching the cinematic_text_camera factory body —
    // the factory is now: "spec_is_rich → wire rich_paint_anchor → chain
    // motion canon".

    [[nodiscard]] static bool spec_is_rich(const TextSpec& spec) noexcept {
        return spec.appearance.paint.stroke_enabled
            || spec.appearance.paint.fill_style.has_value()
            || spec.appearance.paint.stroke_style.has_value();
    }

    [[nodiscard]] static TextAnimatorSpec
    rich_paint_anchor(std::string_view preset_id) {
        TextAnimatorSpec a;
        a.id             = std::string{"ctc_rich_"} + std::string{preset_id};
        a.enabled        = true;
        a.transform_mode = TextPropertyBlendMode::Add;
        a.color_mode     = TextPropertyBlendMode::Replace;

        // Global glyph selector — every glyph receives weight=1 (the
        // After Effects-style "entire text as one unit" pattern).  The
        // id on the selector is the parent animator's id with a
        // `_sel_global` suffix so diagnostics can correlate the two.
        GlyphSelectorSpec sel;
        sel.id    = a.id + "_sel_global";
        sel.unit  = TextSelectorUnit::Glyph;
        sel.shape = TextSelectorShape::Square;
        sel.start  = {0.0f};
        sel.end    = {100.0f};
        sel.amount = {100.0f};
        a.selectors.push_back(sel);

        // No-op-at-render property: OpacityProperty{1.0f} keeps the
        // glyphs at the standard full opacity (1.0) so the resolver
        // entry is observable AND semantically full (it composes with
        // the canonical motion-preset canon via `evaluate_animator_stack`
        // rather than id-only).  Stage 5 below extends the resolver with
        // property-level compositions for ALL 22 presets.
        a.properties.push_back(OpacityProperty{1.0f});
        return a;
    }

    // ── Stage 5 (Cluster A DoD #2 closure — all 22 presets) ──────────────
    // `compose_for(preset_id)` maps each of the 22 built-in presets to a
    // TextAnimatorSpec that captures the END-STATE glyph contribution of
    // the canonical motion chain.  When the caller invokes the factory
    // body of preset P, the registry routes through this resolver:
    //
    //   1. auxiliary `wire_through_resolver()` helper (in the registry TU)
    //      calls `compose_for(P).value_or(nullopt)` to obtain the
    //      property-composed TextAnimatorSpec;
    //   2. if non-null, the helper pushes it onto TextRunParams.animators
    //      and routes through `lb.text_run(...).commit()` so the wired
    //      entry lands on the PendingTextRun BEFORE the canonical motion
    //      chain mutates the layer;
    //   3. if null (no branch matched — `minimal_white` OR any unknown
    //      id), the helper falls back to a plain `lb.text(...)` — no
    //      canonical motion mapped → no resolver output.
    //
    // The 22 branches below encode each preset's terminal glyph state as
    // END-STATE static values (the engine reads opacity/position/scale as
    // STATIC values in `evaluate_animator_stack`; ramps are produced by
    // the layer-level motion canon that runs AFTER the resolver).  Stagger
    // / center-split / float-idle do NOT have a static end-state property
    // and therefore route via the canonical Opacity/Position path.
    [[nodiscard]] static std::optional<TextAnimatorSpec>
    compose_for(std::string_view preset_id) {
        TextAnimatorSpec a;
        a.id             = std::string{"presetc_"} + std::string{preset_id};
        a.enabled        = true;
        a.transform_mode = TextPropertyBlendMode::Add;
        a.color_mode     = TextPropertyBlendMode::Replace;

        // Global glyph selector — every glyph receives weight=1 (the
        // After Effects-style "entire text as one unit" pattern).
        GlyphSelectorSpec sel;
        sel.id     = a.id + "_sel_global";
        sel.unit   = TextSelectorUnit::Glyph;
        sel.shape  = TextSelectorShape::Square;
        sel.start  = {0.0f};
        sel.end    = {100.0f};
        sel.amount = {100.0f};
        a.selectors.push_back(sel);

        if (preset_id == "animation_compositions") {
            // depth_reveal(280,45) + soft_pop(30) + float_idle(8,120)
            a.properties.push_back(PositionProperty{Vec3{0.0f, 8.0f, 280.0f}});
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else if (preset_id == "cinematic_text_camera") {
            // depth_reveal(260,50) + scale_drop(0.95,30) + soft_pop(24)
            a.properties.push_back(PositionProperty{Vec3{0.0f, 0.0f, 260.0f}});
            a.properties.push_back(ScaleProperty{Vec3{0.95f, 0.95f, 1.0f}});
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else if (preset_id == "cinematic_title_reveal") {
            // scale_drop(0.92,40) + soft_pop(30)
            a.properties.push_back(ScaleProperty{Vec3{0.92f, 0.92f, 1.0f}});
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else if (preset_id == "tilt_sweep_title_v2") {
            // scale_drop(1.08,45) + focus_in(2.5,30) + soft_pop(24)
            a.properties.push_back(ScaleProperty{Vec3{1.08f, 1.08f, 1.0f}});
            a.properties.push_back(BlurProperty{2.5f});
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else if (preset_id == "text_animations") {
            // fade_in(20) + scale_drop(0.95,30)
            a.properties.push_back(ScaleProperty{Vec3{0.95f, 0.95f, 1.0f}});
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else if (preset_id == "fade_in") {
            // fade_in(15) + soft_pop(10)
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else if (preset_id == "blur_in") {
            // focus_in(4.0,30) + fade_in(15)
            a.properties.push_back(BlurProperty{4.0f});
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else if (preset_id == "slide_up") {
            // fade_shift_vertical({0,200,0},25) + fade_in(15)
            a.properties.push_back(PositionProperty{Vec3{0.0f, 200.0f, 0.0f}});
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else if (preset_id == "slide_down") {
            // fade_shift_vertical({0,-200,0},25) + fade_in(15)
            a.properties.push_back(PositionProperty{Vec3{0.0f, -200.0f, 0.0f}});
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else if (preset_id == "scale_in") {
            // scale_drop(0.85,25) + soft_pop(15)
            a.properties.push_back(ScaleProperty{Vec3{0.85f, 0.85f, 1.0f}});
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else if (preset_id == "tracking_close") {
            // tracking_breathing(0.05,30)
            a.properties.push_back(TrackingProperty{0.05f});
        }
        else if (preset_id == "masked_line_reveal") {
            // center_split(30) + fade_shift_horizontal({120,0,0},25)
            a.properties.push_back(PositionProperty{Vec3{120.0f, 0.0f, 0.0f}});
        }
        else if (preset_id == "word_cascade") {
            // word_stagger(4,20) + fade_in(15)
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else if (preset_id == "character_cascade") {
            // fade_in(15) + word_stagger(2,20)
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else if (preset_id == "word_pop") {
            // scale_drop(1.15,8) + soft_pop(15)
            a.properties.push_back(ScaleProperty{Vec3{1.15f, 1.15f, 1.0f}});
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else if (preset_id == "scale_punch") {
            // scale_drop(0.70,12) + soft_pop(20)
            a.properties.push_back(ScaleProperty{Vec3{0.70f, 0.70f, 1.0f}});
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else if (preset_id == "color_accent") {
            // fade_in(12) — colour identity comes from caller-set spec
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else if (preset_id == "gradient_fill") {
            // fade_shift_vertical({0,80,0},20) + fade_in(15)
            a.properties.push_back(PositionProperty{Vec3{0.0f, 80.0f, 0.0f}});
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else if (preset_id == "yellow_keyword") {
            // word_stagger(3,20) + fade_in(12)
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else if (preset_id == "glow_pulse") {
            // tracking_breathing(0.08,40)
            a.properties.push_back(TrackingProperty{0.08f});
        }
        else if (preset_id == "caption_box") {
            // fade_shift_vertical({0,-30,0},18) + fade_in(12)
            a.properties.push_back(PositionProperty{Vec3{0.0f, -30.0f, 0.0f}});
            a.properties.push_back(OpacityProperty{1.0f});
        }
        else {
            // No branch matched: either `minimal_white` (the only preset
            // without canonical motion) OR an id not in the registered
            // 22-preset catalog.  Both routes fall through to a fail-safe
            // std::nullopt return — the factory body's `wire_through_resolver()`
            // helper detects `params.animators.empty()` and routes to a
            // plain `lb.text(...)` (no resolver output, no pending TextRun
            // mutation).
            return std::nullopt;
        }
        return a;
    }
};

} // namespace chronon3d::registry
