// ═══════════════════════════════════════════════════════════════════════════
// tests/text/test_text_preset_subtitle.cpp — Phase-2.1 P0 split
//
// 2 Subtitle-tier presets × 8 sentinels + 2 e2e pipelines.
//
//   minimal_white · yellow_keyword              (2 subtitle presets)
//
// The 2 e2e tests (TextE2E: render_frame with text + TextE2E:
// materialize + draw_text_run) live in this TU because they exercise
// the canonical text-pipeline end-to-end and were historically grouped
// with the Subtitle tier in the monolithic file:
//
//   minimal_white's static text + the full `chronon3d::authoring::`
//   path are the closest integration smoke we have for Subtitle
//   presentational text.  caption_box + glow_pulse are TICKET-A4.1
//   (cinematic tier deferred to A4.0 once bit-stable per
//   test_text_preset_cinematic.cpp).
//
// ═══════════════════════════════════════════════════════════════════════════

#include <cmath>

#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/text/text_run.hpp>
#include <content/text/text_helpers.hpp>
#include <chronon3d/text/text_definition.hpp>  // F2.C — from_text_definition()

#include <tests/text/visual/text_visual_fixture.hpp>
#include <tests/text/visual/text_visual_sentinels.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/helpers/render_fixtures.hpp>

#include <glm/gtc/matrix_transform.hpp>

// ── Subtitle (2 — caption_box + glow_pulse deferred to A4.1) ─────────────

TEST_CASE("VRTextPreset/MinimalWhite") {
    auto renderer = chronon3d::test::make_renderer();
    // minimal_white has no entrance animation — text is static and always visible.
    emit_preset_gate(renderer, "minimal_white", AspectRatio::k16x9, 0,  kRefTextPresMinimalWhite_169_F000, "MinimalWhite_169_F000", VisualExpectation::Visible);
    emit_preset_gate(renderer, "minimal_white", AspectRatio::k16x9, 20, kRefTextPresMinimalWhite_169_F020, "MinimalWhite_169_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "minimal_white", AspectRatio::k16x9, 30, kRefTextPresMinimalWhite_169_F030, "MinimalWhite_169_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "minimal_white", AspectRatio::k16x9, 40, kRefTextPresMinimalWhite_169_F040, "MinimalWhite_169_F040", VisualExpectation::Visible);
    emit_preset_gate(renderer, "minimal_white", AspectRatio::k9x16, 0,  kRefTextPresMinimalWhite_916_F000, "MinimalWhite_916_F000", VisualExpectation::Visible);
    emit_preset_gate(renderer, "minimal_white", AspectRatio::k9x16, 20, kRefTextPresMinimalWhite_916_F020, "MinimalWhite_916_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "minimal_white", AspectRatio::k9x16, 30, kRefTextPresMinimalWhite_916_F030, "MinimalWhite_916_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "minimal_white", AspectRatio::k9x16, 40, kRefTextPresMinimalWhite_916_F040, "MinimalWhite_916_F040", VisualExpectation::Visible);
}

TEST_CASE("VRTextPreset/YellowKeyword") {
    auto renderer = chronon3d::test::make_renderer();
    emit_preset_gate(renderer, "yellow_keyword", AspectRatio::k16x9, 0,  kRefTextPresYellowKeyword_169_F000, "YellowKeyword_169_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "yellow_keyword", AspectRatio::k16x9, 20, kRefTextPresYellowKeyword_169_F020, "YellowKeyword_169_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "yellow_keyword", AspectRatio::k16x9, 30, kRefTextPresYellowKeyword_169_F030, "YellowKeyword_169_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "yellow_keyword", AspectRatio::k16x9, 40, kRefTextPresYellowKeyword_169_F040, "YellowKeyword_169_F040", VisualExpectation::Visible);
    emit_preset_gate(renderer, "yellow_keyword", AspectRatio::k9x16, 0,  kRefTextPresYellowKeyword_916_F000, "YellowKeyword_916_F000", VisualExpectation::Transparent);
    emit_preset_gate(renderer, "yellow_keyword", AspectRatio::k9x16, 20, kRefTextPresYellowKeyword_916_F020, "YellowKeyword_916_F020", VisualExpectation::Visible);
    emit_preset_gate(renderer, "yellow_keyword", AspectRatio::k9x16, 30, kRefTextPresYellowKeyword_916_F030, "YellowKeyword_916_F030", VisualExpectation::Visible);
    emit_preset_gate(renderer, "yellow_keyword", AspectRatio::k9x16, 40, kRefTextPresYellowKeyword_916_F040, "YellowKeyword_916_F040", VisualExpectation::Visible);
}

// =============================================================================
// Minimal test: text through full render_frame() pipeline (near-e2e)
// Exercises the entire compositor → graph → text_run_node path.
// (Mechanical split-off from monolithic test_text_preset_visual.cpp;
//  bundled under Subtitle tier because both presets in that tier are
//  closest to the static-text integration smoke role.)
// =============================================================================

TEST_CASE("TextE2E: render_frame with text produces visible ink pixels") {
    auto renderer = chronon3d::test::make_renderer();

    chronon3d::content::text::CenterTextOptions opts;
    opts.text = "HELLO";
    opts.font_path = "assets/fonts/Poppins-Bold.ttf";
    opts.font_size = 48.0f;
    opts.color = chronon3d::Color::white();
    opts.box = chronon3d::Vec2{400.0f, 100.0f};

    auto comp = chronon3d::composition(
        {.name = "e2e_render_frame",
         .width = 640, .height = 200,
         .frame_rate = chronon3d::FrameRate{30, 1},
         .duration = 60},
        [opts = std::move(opts), &renderer](const chronon3d::FrameContext& ctx) -> chronon3d::Scene {
            chronon3d::SceneBuilder s(ctx);
            s.font_engine(&renderer.font_engine());
            auto text_spec = chronon3d::content::text::centered_text(opts);
            s.layer("hero", [&s, text_spec](chronon3d::LayerBuilder& l) mutable {
                l.text("k", std::move(text_spec));
            });
            return s.build();
        });

    auto fb = renderer.render(comp, chronon3d::Frame{0});
    REQUIRE(fb != nullptr);

    int ink = 0;
    for (int y = 0; y < fb->height(); ++y)
        for (int x = 0; x < fb->width(); ++x)
            if (fb->get_pixel(x, y).a > 0.05f) ++ink;

    CHECK(ink > 0);
    MESSAGE("ink_pixels=" << ink);
}

// =============================================================================
// End-to-end isolation test: FontEngine + materialize + draw_text_run.
// Bypasses the render graph entirely to verify the core text pipeline
// produces visible pixels in isolation.
// =============================================================================

TEST_CASE("TextE2E: materialize + draw_text_run produces visible ink pixels") {
    auto renderer = chronon3d::test::make_renderer();
    chronon3d::FontEngine& engine = renderer.font_engine();

    // Build TextSpec for centered text
    chronon3d::content::text::CenterTextOptions opts;
    opts.text = "HELLO";
    opts.font_path = "assets/fonts/Poppins-Bold.ttf";
    opts.font_size = 48.0f;
    opts.color = chronon3d::Color::white();
    opts.box = chronon3d::Vec2{400.0f, 100.0f};
    auto spec = chronon3d::content::text::centered_text(std::move(opts));

    // Materialize the text shape via the canonical helper
    chronon3d::TextRunParams params;
    params.text = std::move(from_text_definition(spec));
    chronon3d::SampleTime st = chronon3d::SampleTime::from_frame_int(0, chronon3d::FrameRate{30, 1});
    auto shape = chronon3d::materialize_text_run_shape(params, &engine, st);
    REQUIRE(shape != nullptr);
    REQUIRE(shape->glyphs.size() > 0);
    MESSAGE("shaped glyphs: " << shape->glyphs.size());

    // Render directly to a framebuffer via the software backend,
    // bypassing the render graph entirely.
    const int w = 640, h = 200;
    chronon3d::Framebuffer fb(w, h, true);  // cleared to transparent

    chronon3d::Mat4 model = glm::translate(chronon3d::Mat4(1.0f),
                                            chronon3d::Vec3(w * 0.5f, h * 0.5f, 0.0f));

    auto& backend = renderer.backend();
    auto result = backend.draw_text_run(fb, *shape, model, 1.0f);
    REQUIRE(result);
    CHECK(result.value().items_drawn > 0);
    MESSAGE("items_drawn=" << result.value().items_drawn);

    // Count ink pixels
    int ink = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (fb.get_pixel(x, y).a > 0.05f) ++ink;

    CHECK(ink > 0);
    MESSAGE("ink_pixels=" << ink);
}
