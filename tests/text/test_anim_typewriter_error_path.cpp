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
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_error.hpp>
#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <content/text/text_helpers_typewriter.hpp>

using namespace chronon3d;
using namespace chronon3d::content::text;
using test_text_quality::require_font;

// ─── Azione 18 — Runtime regression lock ───────────────────────────────────

TEST_CASE("Azione 18: typewriter_build with valid text returns Ok(true)") {
    chronon3d::Config cfg;
    chronon3d::runtime::RenderRuntime runtime(cfg);
    FontEngine engine{runtime.resolver()};
    if (!require_font(engine)) return;

    FrameContext ctx{.frame = Frame{0}, .width = 1920, .height = 1080};
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
    chronon3d::runtime::RenderRuntime runtime(cfg);
    FontEngine engine{runtime.resolver()};
    if (!require_font(engine)) return;

    FrameContext ctx{.frame = Frame{0}, .width = 1920, .height = 1080};
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
    chronon3d::runtime::RenderRuntime runtime(cfg);
    FontEngine engine{runtime.resolver()};
    if (!require_font(engine)) return;

    FrameContext ctx{.frame = Frame{0}, .width = 1920, .height = 1080};
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
    auto good_result = typewriter_build(s, "tw_good", good_opts, Frame{0}, engine);
    REQUIRE(good_result.has_value());
    CHECK(good_result.value() == true);

    // Scene is still buildable after the error + recovery.
    auto scene = s.build();
    CHECK(scene != nullptr);
}
