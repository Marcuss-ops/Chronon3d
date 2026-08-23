// ─── test_resolved_plan_contract.cpp — resolved_plan diagnostic (ADR-029 e/g)
//
// Locks three guarantees from ADR-029:
//   1. CONTRACT — every visual preset round-trips through RenderPlan (id only
//      travels, defaults stay in the registry) and re-resolves without field
//      loss: preset defaults == ResolvedVisualStyle.
//   2. GOLDEN/PARITY — the resolved style + animation are resolution-
//      independent (only layout moves with the canvas).
//   3. GOLDEN/PARITY — layout stays valid, inside the safe area, and
//      deterministic across resolutions and content lengths.

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/registry/resolved_plan.hpp>
#include <chronon3d/registry/visual_preset_registry.hpp>
#include <chronon3d/render_plan/animation_intent.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/text/animation/text_animator_properties.hpp>  // OpacityProperty

#include <nlohmann/json.hpp>

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using chronon3d::registry::ResolvedPlan;
using chronon3d::registry::ResolvedVisualStyle;
using chronon3d::registry::VisualLayerKind;
using chronon3d::registry::VisualStyle;
using chronon3d::registry::builtin_visual_preset_registry;
using chronon3d::registry::resolve_plan_diagnostic;
using chronon3d::registry::to_json;

namespace {

std::string layer_type_json(VisualLayerKind kind) {
    switch (kind) {
        case VisualLayerKind::Image: return "image";
        case VisualLayerKind::Video: return "video";
        case VisualLayerKind::Text:  return "text";
        case VisualLayerKind::Color: return "color";
    }
    return "text";
}

// Minimal render plan for one preset: only id + type + text travel; the
// preset's full style/anchor/animation defaults stay in the registry.
nlohmann::json plan_json(std::string_view preset_id, VisualLayerKind kind) {
    return nlohmann::json{
        {"schema", "chronon.render-plan"},
        {"version", 1},
        {"job_id", "contract"},
        {"canvas",
         {{"width", 1920}, {"height", 1080}, {"fps_num", 30}, {"fps_den", 1},
          {"duration_frames", 120}}},
        {"layers",
         nlohmann::json::array(
             {{{"id", "l0"},
               {"type", layer_type_json(kind)},
               {"text", "HELLO"},
               {"preset", std::string{preset_id}}}})},
        {"output", {{"path", "out.mp4"}, {"format", "mp4"}, {"codec", "h264"}}},
    };
}

// Every non-empty/non-null preset default must survive resolution unchanged.
void check_style_preserved(const VisualStyle& def,
                           const ResolvedVisualStyle& res) {
    CHECK(res.font_family == def.font_family);
    CHECK(res.font_asset == def.font_asset);
    CHECK(res.font_weight == def.font_weight);
    CHECK(res.font_size == def.font_size);
    CHECK(res.fill == def.fill);

    CHECK(res.stroke_enabled ==
          (!def.stroke_color.empty() || def.stroke_width.has_value()));
    CHECK(res.stroke_color == def.stroke_color);
    CHECK(res.stroke_width == def.stroke_width);

    CHECK(res.shadow_enabled ==
          (!def.shadow_color.empty() || def.shadow_opacity.has_value() ||
           def.shadow_blur.has_value()));
    CHECK(res.shadow_color == def.shadow_color);
    CHECK(res.shadow_opacity == def.shadow_opacity);
    CHECK(res.shadow_blur == def.shadow_blur);
    CHECK(res.shadow_offset == def.shadow_offset);

    CHECK(res.background_enabled ==
          (!def.background_color.empty() || def.background_opacity.has_value() ||
           def.radius.has_value() || def.padding.has_value()));
    CHECK(res.background_color == def.background_color);
    CHECK(res.background_opacity == def.background_opacity);
    CHECK(res.radius == def.radius);
    CHECK(res.padding == def.padding);
}

} // namespace

TEST_CASE("resolved_plan: contract — every preset → RenderPlan → resolve without field loss") {
    const auto& registry = builtin_visual_preset_registry();
    for (const auto& preset : registry.list()) {
        CAPTURE(preset.id);

        // 1. preset → RenderPlan (only the id + type travel).
        const auto json = plan_json(preset.id, preset.supported_layer);
        auto decoded = chronon3d::render_plan::decode_render_plan(json);
        REQUIRE(decoded);
        const auto& plan = decoded.value();
        REQUIRE(plan.layers.size() == 1);
        CHECK(plan.layers.front().preset == preset.id);

        // 2. resolve → preset defaults re-derived, no field lost.
        const auto resolved =
            resolve_plan_diagnostic(registry, preset.id, /*semantic_role=*/"",
                                    /*canvas=*/1920.0f, 1080.0f,
                                    /*box=*/540.0f, 104.0f);
        CHECK(resolved.preset_id == preset.id);
        check_style_preserved(preset.style, resolved.style);
        CHECK(resolved.animation.preset == preset.animation.preset);
        CHECK(resolved.animation.unit == preset.animation.unit);
        CHECK(resolved.animation.enter_duration_frames ==
              preset.animation.enter_duration_frames);
        CHECK(resolved.animation.exit_duration_frames ==
              preset.animation.exit_duration_frames);
    }
}

TEST_CASE("resolved_plan: contract — every text preset's animation intent lowers to a per-unit animator") {
    const auto& registry = builtin_visual_preset_registry();
    for (const auto& preset : registry.list()) {
        if (preset.supported_layer != VisualLayerKind::Text) continue;
        CAPTURE(preset.id);

        // The preset's unit + enter/exit durations must survive as a valid
        // per-unit reveal animator (the text-pipeline connection):
        //   unit → TextSelectorUnit → GlyphSelectorSpec
        //   enter/exit → opacity keyframes over the window.
        const auto unit =
            chronon3d::render_plan::selector_unit(preset.animation.unit);
        const std::optional<chronon3d::Frame> enter =
            preset.animation.enter_duration_frames
                ? std::optional<chronon3d::Frame>{
                      chronon3d::Frame{*preset.animation.enter_duration_frames}}
                : std::nullopt;
        const std::optional<chronon3d::Frame> exit =
            preset.animation.exit_duration_frames
                ? std::optional<chronon3d::Frame>{
                      chronon3d::Frame{*preset.animation.exit_duration_frames}}
                : std::nullopt;

        const auto animator = chronon3d::render_plan::build_unit_reveal_animator(
            preset.animation.unit, chronon3d::Frame{0}, chronon3d::Frame{60},
            enter, exit);

        REQUIRE(animator.selectors.size() == 1u);
        CHECK(animator.selectors.front().unit == unit);
        REQUIRE(animator.properties.size() == 1u);
        REQUIRE(std::holds_alternative<chronon3d::OpacityProperty>(
            animator.properties.front()));
        const auto& opacity =
            std::get<chronon3d::OpacityProperty>(animator.properties.front());
        CHECK(opacity.value.first_keyframe_time() == chronon3d::Frame{0});
        CHECK(opacity.value.last_keyframe_time() == chronon3d::Frame{60});
        CHECK(opacity.value.evaluate(0.0) == doctest::Approx(0.0f));
        CHECK(opacity.value.evaluate(30.0) == doctest::Approx(1.0f));
        CHECK(opacity.value.evaluate(60.0) == doctest::Approx(0.0f));
    }
}

TEST_CASE("resolved_plan: golden/parity — style + animation are resolution-independent") {
    const auto& registry = builtin_visual_preset_registry();
    const std::array<std::pair<float, float>, 3> resolutions = {
        std::pair{1920.0f, 1080.0f},
        std::pair{1280.0f, 720.0f},
        std::pair{1080.0f, 1920.0f},
    };
    for (const auto& preset : registry.list()) {
        if (preset.supported_layer != VisualLayerKind::Text) continue;
        CAPTURE(preset.id);

        nlohmann::json baseline_style;
        nlohmann::json baseline_animation;
        bool first = true;
        for (const auto [w, h] : resolutions) {
            const auto resolved =
                resolve_plan_diagnostic(registry, preset.id, "", w, h,
                                        w * 0.4f, h * 0.1f);
            if (first) {
                baseline_style = to_json(resolved)["resolved_style"];
                baseline_animation = to_json(resolved)["resolved_animation"];
                first = false;
                continue;
            }
            // Only the layout may differ with the canvas; the resolved paint
            // recipe and motion intent must be byte-identical.
            CHECK(to_json(resolved)["resolved_style"] == baseline_style);
            CHECK(to_json(resolved)["resolved_animation"] == baseline_animation);
        }
    }
}

TEST_CASE("resolved_plan: golden/parity — layout valid + deterministic across resolutions and content lengths") {
    const auto& registry = builtin_visual_preset_registry();
    const std::array<std::pair<float, float>, 3> resolutions = {
        std::pair{1920.0f, 1080.0f},
        std::pair{1280.0f, 720.0f},
        std::pair{1080.0f, 1920.0f},
    };
    // short / medium / long content, as canvas fractions so every box fits
    // the safe area (safe_margin 0.06 → 0.88 of the canvas per side).
    const std::array<float, 3> content_fractions = {0.25f, 0.5f, 0.75f};

    for (const auto& preset : registry.list()) {
        if (preset.supported_layer != VisualLayerKind::Text) continue;
        CAPTURE(preset.id);

        for (const auto [w, h] : resolutions) {
            const float inset_x = preset.anchor.safe_margin * w;
            const float inset_y = preset.anchor.safe_margin * h;
            for (const float frac : content_fractions) {
                const float box_w = w * frac;
                const float box_h = h * 0.1f;
                CAPTURE(w);
                CAPTURE(h);
                CAPTURE(frac);

                const auto a = resolve_plan_diagnostic(
                    registry, preset.id, "", w, h, box_w, box_h);
                const auto b = resolve_plan_diagnostic(
                    registry, preset.id, "", w, h, box_w, box_h);

                // Determinism: two resolves are bit-identical.
                CHECK(a.layout.x == b.layout.x);
                CHECK(a.layout.y == b.layout.y);
                CHECK(a.layout.intent == b.layout.intent);
                CHECK(a.layout.valid == b.layout.valid);

                // Validity + safe-area containment.
                REQUIRE(a.layout.valid);
                CHECK(a.layout.x >= inset_x);
                CHECK(a.layout.y >= inset_y);
                CHECK(a.layout.x + box_w <= w - inset_x);
                CHECK(a.layout.y + box_h <= h - inset_y);
            }
        }
    }
}

TEST_CASE("resolved_plan: to_json emits the diagnostic shape") {
    const auto& registry = builtin_visual_preset_registry();
    const auto resolved = resolve_plan_diagnostic(
        registry, "lower_third_safe", "PERSON", 1920.0f, 1080.0f, 540.0f, 104.0f);
    const auto json = to_json(resolved);

    CHECK(json["preset"] == "lower_third_safe");
    CHECK(json["semantic_role"] == "PERSON");
    // Canonical registry contract (ADR-029): Poppins asset + registered
    // Chronon layer motion `focus_in` for lower_third_safe.
    CHECK(json["resolved_style"]["font_asset"] == "assets/fonts/Poppins-Bold.ttf");
    CHECK(json["resolved_style"]["font_size"] == doctest::Approx(58.0f));
    CHECK(json["resolved_style"].contains("stroke"));
    CHECK(json["resolved_style"].contains("shadow"));
    CHECK(json["resolved_style"].contains("background"));
    CHECK(json["resolved_layout"]["valid"] == true);
    CHECK_FALSE(json["resolved_layout"]["intent"].get<std::string>().empty());
    CHECK(json["resolved_animation"]["preset"] == "focus_in");
    CHECK(json["resolved_animation"]["unit"] == "line");
}
