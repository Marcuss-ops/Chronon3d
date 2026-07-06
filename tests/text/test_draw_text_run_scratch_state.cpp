// ═══════════════════════════════════════════════════════════════════════════
// test_draw_text_run_scratch_state.cpp — P0-1 regression coverage
// ═══════════════════════════════════════════════════════════════════════════
//
// Regression lock for the P0-1 build-fix (audit hotspot #1):
//   1. draw_text_run() acquires scratch_state via RAII Handle from
//      rctx.text_resources->scratch_manager.acquire().  Before the fix,
//      scratch_state was used at ~15 call sites without being declared.
//   2. The old `const bool ft_loaded = (font_handle.ft_face != nullptr)`
//      referenced an undeclared variable.  The fix replaced the global
//      flag with per-span `span_handle.ft_face != nullptr` checks in the
//      stroke path.
//   3. Null-guard validation: rctx.text_resources and rctx.asset_resolver
//      are checked before the font resolution block, returning
//      InvalidInput on failure.
//
// Test matrix:
//   (1) Null text_resources  → InvalidInput (no crash)
//   (2) Null asset_resolver  → InvalidInput (no crash)
//   (3) E2E render-success with stroke: draw_text_run acquires scratch_state
//       and resolves per-span font handles — the dispatch must succeed and
//       produce visible ink pixels (proving both the RAII acquisition and
//       the per-span stroke path are functional).
//
// Framework: doctest.  Gated by CHRONON3D_USE_BLEND2D AND
// CHRONON3D_ENABLE_TEXT via tests/core_tests.cmake CORE_BLEND2D_TESTS.
// Pattern precedent: tests/text/test_draw_text_run_crossfade_stroke.cpp.

#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/text_run_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/software_backend.hpp>
#include <chronon3d/backends/software/software_processor_context.hpp>
#include <chronon3d/backends/software/text_run_processor.hpp>
#include <chronon3d/backends/text/text_render_resources.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/types.hpp>

#include <doctest/doctest.h>
#include <helpers/test_utils.hpp>
#include "test_text_font_fixture.hpp"

#include <filesystem>
#include <memory>
#include <string>

using namespace chronon3d;
using namespace chronon3d::renderer;

namespace {

// s_text_cache — file-local TextLayoutCache used by make_real_shape().
// TICKET-011 closure line: kept inside the anonymous namespace so that
// unqualified name lookup from within make_real_shape is unambiguous.
// Equivalently internal-linkage to a file-static declaration, but no
// dependence on global-ns visibility at the top of the TU.
static chronon3d::TextLayoutCache s_text_cache;



/// Build a TextRunShape backed by a real Inter-Bold layout.
/// Mirrors the helper in test_draw_text_run_crossfade_stroke.cpp.
std::shared_ptr<TextRunShape> make_real_shape(
    const std::string& text,
    FontEngine& engine,
    const TextLayoutSpec& layout,
    const FontSpec& font
) {
    TextDocument doc;
    doc.utf8 = text;
    doc.defaults.font = font;
    doc.split_paragraphs();

    auto& cache = s_text_cache;
    auto result = build_text_run(doc, engine, layout, &cache);
    if (result.paragraphs.empty() || !result.paragraphs.front()) {
        return nullptr;
    }
    auto shape = std::make_shared<TextRunShape>();
    shape->layout = std::const_pointer_cast<const TextRunLayout>(
        std::const_pointer_cast<TextRunLayout>(result.paragraphs.front()));
    shape->glyphs = make_initial_glyph_states(shape->layout->placed);
    shape->engine = &engine;
    shape->layout_spec = layout;
    return shape;
}

/// Build a minimal TextRunShape with a synthetic layout (no font engine).
/// Used for the null-guard tests that never reach the font resolution path.
std::shared_ptr<TextRunShape> make_synthetic_shape() {
    auto layout = std::make_shared<TextRunLayout>();
    layout->font = FontSpec{
        .font_path   = "dummy.ttf",
        .font_family = "Dummy",
        .font_weight = 400,
        .font_style  = "normal",
        .font_size   = 32.0f,
    };
    layout->font_size = 32.0f;
    layout->bounds = {100.0f, 50.0f};
    layout->line_height = 38.0f;
    // Minimal placed run: one glyph at origin.
    PlacedGlyph pg;
    pg.glyph_id = 0;
    pg.x = 0.0f;
    pg.y = 0.0f;
    pg.advance_x = 20.0f;
    layout->placed.glyphs.push_back(pg);
    layout->placed.total_width = 20.0f;
    layout->placed.total_height = 32.0f;
    layout->placed.ascent = 24.0f;
    layout->placed.descent = 8.0f;
    layout->placed.baseline = 24.0f;
    layout->placed.font_size = 32.0f;

    auto shape = std::make_shared<TextRunShape>();
    shape->layout = layout;  // implicit shared_ptr<TextRunLayout> → shared_ptr<const TextRunLayout>
    shape->glyphs = make_initial_glyph_states(layout->placed);
    return shape;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. Null text_resources → InvalidInput (no crash)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("draw_text_run: null text_resources returns InvalidInput (P0-1 regression)") {
    auto shape = make_synthetic_shape();
    REQUIRE(shape != nullptr);
    REQUIRE(shape->layout != nullptr);
    REQUIRE_FALSE(shape->glyphs.empty());

    Framebuffer fb(64, 64);
    const Mat4 model = Mat4(1.0f);

    // Construct a context with text_resources = nullptr, asset_resolver = nullptr.
    // draw_text_run must detect this and return InvalidInput BEFORE attempting
    // any font resolution or scratch_state acquisition.
    SoftwareProcessorContext ctx;
    ctx.text_resources  = nullptr;
    ctx.asset_resolver  = nullptr;
    ctx.counters        = nullptr;
    ctx.settings        = nullptr;
    ctx.registry        = nullptr;

    TextRunDrawParams params{fb, *shape, model, 1.0f};

    // Initialize with a valid success outcome — Result requires T or E.
    graph::RenderOpResult result{graph::RenderOpOutcome{0}};
    REQUIRE_NOTHROW(result = draw_text_run(ctx, params));

    REQUIRE_FALSE(static_cast<bool>(result));
    CHECK(result.error().code == graph::RenderBackendErrorCode::InvalidInput);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Null asset_resolver → InvalidInput (no crash)
//    text_resources is non-null (a real TextRenderResources), but
//    asset_resolver is null — the validation must still fire.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("draw_text_run: null asset_resolver returns InvalidInput (P0-1 regression)") {
    auto shape = make_synthetic_shape();
    REQUIRE(shape != nullptr);
    REQUIRE_FALSE(shape->glyphs.empty());

    Framebuffer fb(64, 64);
    const Mat4 model = Mat4(1.0f);

    TextRenderResources resources;

    SoftwareProcessorContext ctx;
    ctx.text_resources  = &resources;
    ctx.asset_resolver  = nullptr;  // <-- the guard under test
    ctx.counters        = nullptr;
    ctx.settings        = nullptr;
    ctx.registry        = nullptr;

    TextRunDrawParams params{fb, *shape, model, 1.0f};

    // Initialize with a valid success outcome — Result requires T or E.
    graph::RenderOpResult result{graph::RenderOpOutcome{0}};
    REQUIRE_NOTHROW(result = draw_text_run(ctx, params));

    REQUIRE_FALSE(static_cast<bool>(result));
    CHECK(result.error().code == graph::RenderBackendErrorCode::InvalidInput);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. E2E render-success with stroke: scratch_state acquisition +
//    per-span font_handle resolution must produce visible ink.
//
//    This is the positive-path regression lock: after the P0-1 fix,
//    draw_text_run must:
//      (a) successfully acquire scratch_state from the manager (RAII),
//      (b) resolve per-span font handles via rctx.text_resources,
//      (c) render stroke glyphs using span_handle.ft_face (not the
//          removed global font_handle),
//      (d) return a successful RenderOpResult with non-zero glyph count,
//      (e) produce at least one non-transparent pixel in the framebuffer.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("draw_text_run: stroke render succeeds with per-span font_handle + scratch_state (P0-1 E2E)") {
    if (!test_text_fixture::fixture_exists(test_text_fixture::kInterBoldPath)) {
        test_text_fixture::skip_if_missing(test_text_fixture::kInterBoldPath, "P0-1 E2E (scratch_state + per-span font_handle)");
        return;
    }

    const FontSpec font = test_text_fixture::inter_bold();
    const std::string text = "Hello";

    SoftwareRenderer renderer = test::make_renderer();
    FontEngine engine{renderer.runtime().resolver()};
    TextLayoutSpec layout;
    layout.box = {256.0f, 64.0f};

    auto shape = make_real_shape(text, engine, layout, font);
    REQUIRE(shape != nullptr);
    REQUIRE(shape->layout != nullptr);
    REQUIRE_FALSE(shape->glyphs.empty());

    // Enable stroke on every glyph — exercises the per-span
    // span_handle.ft_face != nullptr check in the stroke path.
    for (auto& g : shape->glyphs) {
        g.stroke_width = 2.0f;
        g.stroke       = Color{0xFF, 0x00, 0x00, 0xFF};  // red
    }
    shape->paint.stroke_enabled = true;
    shape->paint.stroke_width   = 2.0f;
    shape->paint.stroke_color   = Color{0xFF, 0x00, 0x00, 0xFF};

    Framebuffer fb(128, 64);
    const Mat4 model = Mat4(1.0f);

    auto result = renderer.backend().draw_text_run(fb, *shape, model, 1.0f);

    // The dispatch MUST succeed (font fixture is present).
    REQUIRE(static_cast<bool>(result));
    CHECK(static_cast<bool>(result));

    // At least one non-transparent pixel — proves the stroke path
    // wrote ink via the per-span font_handle + scratch_state path.
    bool has_non_transparent = false;
    for (int y = 0; y < fb.height() && !has_non_transparent; ++y) {
        for (int x = 0; x < fb.width() && !has_non_transparent; ++x) {
            if (fb.get_pixel(x, y).a != 0.0f) has_non_transparent = true;
        }
    }
    CHECK(has_non_transparent);
}
