#include <optional>
// ===========================================================================
// tests/text/test_anim_typewriter_error_path.cpp
//
// Azione 18 — regression lock for the silent-failure fix in
// `content/animation_compositions.cpp::anim_typewriter()` (P0 #3
// closed gap, see animation_compositions.cpp:98-103 for the canonical
// spdlog::error emit).
//
// Strategy: RUNTIME test.  Instead of grepping the production source for
// a string literal, this test exercises the actual `typewriter_build()`
// API surface and verifies that:
//   1. A valid text + FontEngine → Ok(true) (scene layers added)
//   2. Empty text → Err(TextErrorCode::EmptyText) (structured error, not
//      silent return)
//   3. The error is propagated correctly (non-fatal best-effort contract)
//
// This replaces the prior cat-2 static-source grep test with a proper
// runtime behavioral test that locks the structured-error contract
// introduced in F0.3.  If the silent-degrade pattern re-emerges
// (typewriter_build silently returning without error), this test fails
// loud.
//
// Gating: requires Blend2D + text enabled (FontEngine + text_layout_engine).
// Skips gracefully when system fonts are unavailable.
// ===========================================================================

#include <doctest/doctest.h>

#include "test_text_quality_helpers.hpp"  // require_font, inter_bold_quality
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_error.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <content/text/text_helpers_typewriter.hpp>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_completeness/pixel_scan_helpers.hpp>

using namespace chronon3d;
using namespace chronon3d::content::text;
using test_text_quality::require_font;

// ─── Azione 18 — Runtime regression lock ───────────────────────────────────

TEST_CASE("Azione 18: typewriter_build with valid text returns Ok(true)") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    FrameContext ctx = make_frame_context(FrameContextParams{
    .global_time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
    .width = 1920,
    .height = 1080
});
    ctx.font_engine = &engine;
    SceneBuilder s(ctx);

    TypewriterBuildOptions opts{
        .text           = "Typewriter",
        .box            = {1200.0f, 240.0f},
        .font_size      = 64.0f,
        .tracking       = 3.0f,
        .chars_per_frame = 0.3f,
    };

    auto result = typewriter_build(s, "tw", opts, Frame{0}, engine);

    // F0.3 — valid text must return Ok (not silent failure).
    REQUIRE(result.has_value());
    CHECK(result.value() == true);
}

TEST_CASE("Azione 18: typewriter_build with empty text returns Err(EmptyText)") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    FrameContext ctx = make_frame_context(FrameContextParams{
    .global_time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
    .width = 1920,
    .height = 1080
});
    ctx.font_engine = &engine;
    SceneBuilder s(ctx);

    TypewriterBuildOptions opts{
        .text           = "",   // empty → must produce structured error
        .box            = {1200.0f, 240.0f},
        .font_size      = 64.0f,
        .chars_per_frame = 0.3f,
    };

    auto result = typewriter_build(s, "tw", opts, Frame{0}, engine);

    // F0.3 — empty text must return Err(EmptyText), NOT silently succeed.
    // This is the core regression lock: if the silent-degrade pattern
    // re-emerges, result would be Ok(true) and this CHECK fails.
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == TextErrorCode::EmptyText);
    CHECK_FALSE(result.error().message.empty());
}

TEST_CASE("Azione 18: typewriter_build error is non-fatal (scene still buildable)") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    FrameContext ctx = make_frame_context(FrameContextParams{
    .global_time = SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1}),
    .width = 1920,
    .height = 1080
});
    ctx.font_engine = &engine;
    SceneBuilder s(ctx);

    // First: a failing typewriter_build (empty text) — this should NOT
    // corrupt the scene builder or prevent subsequent operations.
    TypewriterBuildOptions bad_opts{
        .text           = "",
        .box            = {1200.0f, 240.0f},
        .font_size      = 64.0f,
    };
    auto bad_result = typewriter_build(s, "tw_bad", bad_opts, Frame{0}, engine);
    REQUIRE_FALSE(bad_result.has_value());

    // Second: a valid typewriter_build on the SAME SceneBuilder — this
    // must still succeed, proving the error is non-fatal (best-effort
    // contract documented in animation_compositions.cpp:98-103).
    TypewriterBuildOptions good_opts{
        .text           = "Recovery",
        .box            = {1200.0f, 240.0f},
        .font_size      = 64.0f,
        .chars_per_frame = 0.3f,
    };
    auto good_result = typewriter_build(s, "tw_good", good_opts, Frame{10}, engine);
    REQUIRE(good_result.has_value());
    CHECK(good_result.value() == true);

    // Scene is still buildable after the error + recovery.
    auto scene = s.build();
    CHECK_FALSE(scene.layers().empty());
}

// ─── Frame-by-frame monotonicity — no false-green static pass ───────────────
//
// Builds the typewriter scene for each rendered frame so the reveal is
// evaluated at the current frame.  The visible ink area must be
// monotonically non-decreasing and strictly larger at the final frame
// than at the start, proving the typewriter effect actually reveals
// text over time rather than rendering the whole text at every frame.

TEST_CASE("Azione 18: typewriter frame-by-frame ink area is monotonic") {
    chronon3d::Config cfg;
    auto runtime = chronon3d::runtime::RenderRuntime::create(
            chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
    FontEngine engine{runtime->resolver()};
    if (!require_font(engine)) return;

    auto comp = composition(
        {.name = "TypewriterFrames",
         .width = 1920, .height = 1080,
         .frame_rate = FrameRate{30, 1},
         .duration = 30},
        [&engine](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&engine);

            TypewriterBuildOptions opts{
                .text = "Typewriter",
                .box = {1200.0f, 240.0f},
                .font_size = 64.0f,
                .tracking = 3.0f,
                .chars_per_frame = 1.0f,
            };

            auto result = typewriter_build(s, "tw", opts, ctx.frame(), engine);
            REQUIRE(result.has_value());
            CHECK(result.value() == true);

            return s.build();
        });

    auto renderer = chronon3d::test::make_renderer();

    std::vector<int> visible_area;
    for (Frame f : {Frame{0}, Frame{3}, Frame{6}, Frame{9}, Frame{12}, Frame{15}}) {
        auto fb = renderer.render(comp, f);
        REQUIRE(fb != nullptr);
        visible_area.push_back(chronon3d::test::completeness::count_visible_pixels(*fb));
    }

    INFO("typewriter visible area over frames: ", visible_area);
    for (std::size_t i = 1; i < visible_area.size(); ++i) {
        CHECK(visible_area[i] >= visible_area[i - 1]);
    }
    CHECK(visible_area.back() > visible_area.front());
}
