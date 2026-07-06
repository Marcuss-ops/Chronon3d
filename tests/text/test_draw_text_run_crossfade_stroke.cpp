// ═══════════════════════════════════════════════════════════════════════════
// test_draw_text_run_crossfade_stroke.cpp — TICKET-068 E2E regression coverage
// ═══════════════════════════════════════════════════════════════════════════
//
// TICKET-068 close-out (P1#5 / Bug #5 / Fase 1#5) was achieved by the
// property-only `tests/text/test_crossfade_stroke.cpp`: it asserts the
// OOB-precondition data shape (crossfade_layout->placed.glyphs.size()
// > shape->layout->placed.glyphs.size()) and the post-gap slot-clear
// lifecycle, but does NOT actually exercise the dispatch path.
//
// Bug #5's source fix is at commit 0d32e049
// `fix(text): crossfade stroke uses source_placed glyph_id (Bug #5)`
// on `src/backends/software/processors/text_run/text_run_processor.cpp`:
// the stroke branch in `draw_run_layer()` now reads
// `source_placed.glyphs[gi].glyph_id` (ACTIVE layout, bounded) instead
// of `layout.placed.glyphs[gi].glyph_id` (OUTGOING layout, OOB on
// longer outgoing text).  This file CLOSES the E2E gap: it actually
// invokes `SoftwareBackend::draw_text_run` with a stroke-enabled
// shape carrying the OOB-precondition crossfade data — the exact
// conditions that would re-trigger Bug #5 if it regressed.
//
// Test invariant: the dispatch path runs to completion (or returns
// a structured `RenderOpResult::err` with a known error code) without
// segfaulting.  Bug #5's regression symptom was use-after-free / OOB
// read on `layout.placed[gi]`, so a successful no-crash dispatch is
// the strongest machine check we can perform at TU level.  The test
// does NOT assert pixel-level determinism (the BL rasterizer is
// environment-sensitive); only the no-crash invariant.
//
// Framework: doctest.  Gated by `CHRONON3D_USE_BLEND2D AND
// CHRONON3D_ENABLE_TEXT` via `tests/core_tests.cmake` `CORE_BLEND2D_TESTS`.
// Skips with `MESSAGE("Skipping...")` when the font fixture is missing.
// Pattern precedent:
//   * `tests/text/test_crossfade_stroke.cpp` — same data-shape helpers
//   * `tests/helpers/test_utils.hpp::make_renderer()` — wires backend
//   * `tests/text/test_freetype_face_cache_concurrency.cpp` — skip policy
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/animated_text_document.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/text_run_builder.hpp>
#include <chronon3d/text/text_run_driver.hpp>
#include <chronon3d/text/text_layout_cache.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/software_backend.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/types/frame.hpp>

#include <doctest/doctest.h>
#include <helpers/test_utils.hpp>
#include "test_text_font_fixture.hpp"

#include <filesystem>
#include <memory>
#include <string>

static chronon3d::TextLayoutCache s_text_cache;

using namespace chronon3d;

using namespace test_text_fixture;

namespace {

// Build a TextRunShape backed by a real Inter-Bold layout.  Mirrors the
// helper used in `tests/text/test_crossfade_stroke.cpp`.
std::shared_ptr<TextRunShape> make_real_shape_for_test(
    const std::string& text,
    FontEngine& engine,
    const TextLayoutSpec& layout,
    FontSpec font
) {
    TextDocument doc;
    doc.utf8 = text;
    doc.defaults.font = font;
    doc.split_paragraphs();

    auto& cache = s_text_cache;
    auto result = build_text_run(doc, engine, layout, &cache);
    if (result.paragraphs.empty() || !result.paragraphs.front()) {
        return nullptr;  // font load failed in the test env
    }
    auto shape = std::make_shared<TextRunShape>();
    shape->layout = std::const_pointer_cast<const TextRunLayout>(
        std::const_pointer_cast<TextRunLayout>(result.paragraphs.front()));
    shape->glyphs = make_initial_glyph_states(shape->layout->placed);
    shape->engine = &engine;
    shape->layout_spec = layout;
    return shape;
}

// Build a 2-keyframe AnimatedTextDocument where the OUTGOING text is
// intentionally LONGER than the ACTIVE text — the OOB-triggering data
// shape that Bug #5 defends against.
std::shared_ptr<AnimatedTextDocument> make_crossfade_longer_outgoing_doc(
    const std::string& outgoing_text,
    const std::string& active_text,
    const FontSpec& font
) {
    auto doc = std::make_shared<AnimatedTextDocument>();
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = outgoing_text;
    kf0.transition = SourceTextTransition::CrossfadeLayouts;
    kf0.document.defaults.font = font;
    doc->add_keyframe(kf0);

    SourceTextKeyframe kf60;
    kf60.frame = Frame{60};
    kf60.document.utf8 = active_text;
    kf60.document.defaults.font = font;
    doc->add_keyframe(kf60);

    return doc;
}

// Forgive any of the documented `RenderBackendErrorCode` outcomes that
// the dispatch path may produce in a CI environment (font load may fail
// for missing fixtures, Blend2D may be disabled, etc.).  Anything outside
// this set is a regression.  Note: `RenderBackendErrorCode::Success` is
// INTENTIONALLY absent from this allow-list — the success path is
// handled by the inline `if (result.has_value())` branch and never
// re-enters this helper (it's only called from the `else` arm).
bool is_known_render_op_error(graph::RenderBackendErrorCode code) noexcept {
    switch (code) {
        case graph::RenderBackendErrorCode::InvalidInput:
        case graph::RenderBackendErrorCode::UnsupportedCapability:
        case graph::RenderBackendErrorCode::ExecutionFailure:
            return true;
        default:
            return false;
    }
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════
// 1. NO-CRASH — draw_text_run with crossfade + stroke + longer outgoing
//              text must not crash (Bug #5 OOB-defence regression).
//
//              Accepts ANY return outcome (success or documented error)
//              as long as the dispatch does not segfault / OOB-read.
//              This is PURELY a crash-regression lock — NOT an E2E
//              rendering-success gate.
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("draw_text_run: crossfade + stroke with longer outgoing text does not crash (no-crash regression)") {
    if (!test_text_fixture::fixture_exists(kInterBoldPath)) {
        test_text_fixture::skip_if_missing(kInterBoldPath, "TICKET-068 E2E (draw_text_run crossfade+stroke)");
        return;
    }

    const FontSpec font = test_text_fixture::inter_bold();
    const std::string active_text    = "ABc";                       // 3 glyphs
    const std::string outgoing_text  =
        "ABcdefghijklmnopqrSTuvWXyZ1234";                            // 33 glyphs (LONG)

    // make_renderer() wires the entire services bundle via
    // make_software_backend(...).value() — necessary because the
    // draw_text_run post-construction loud-fails (TICKET-046 +
    // Fase 1 services-validation follow-up) on any null REQUIRED
    // service.  Chincy constructors (e.g., SoftwareBackend ctor with a
    // default-constructed bundles) abort in debug builds.
    SoftwareRenderer renderer = test::make_renderer();
    FontEngine engine{renderer.runtime().resolver()};
    TextLayoutSpec layout;
    layout.box = {128.0f, 64.0f};

    // ── Build the active shape with stroke enabled ───────────────────
    auto shape = make_real_shape_for_test(active_text, engine, layout, font);
    REQUIRE(shape != nullptr);
    REQUIRE(shape->layout != nullptr);
    REQUIRE_FALSE(shape->glyphs.empty());

    // Force the stroke branch in `draw_run_layer()` by setting
    // stroke_width > 0 + non-transparent stroke color on EVERY active
    // glyph.  Shape-level paint.stroke acts as fallback for any glyph
    // whose own stroke_width is left at 0.
    for (auto& g : shape->glyphs) {
        g.stroke_width = 2.0f;
        g.stroke       = Color{0xFF, 0x00, 0xFF, 0xFF};  // magenta
    }
    shape->paint.stroke_enabled = true;
    shape->paint.stroke_width   = 2.0f;
    shape->paint.stroke_color   = Color{0xFF, 0x00, 0xFF, 0xFF};

    // ── Attach the OOB-precondition crossfade doc ────────────────────
    shape->animated_doc = make_crossfade_longer_outgoing_doc(
        outgoing_text, active_text, font);

    // Sample at frame 30 — middle of the gap, mix strictly in (0, 1).
    const auto state = shape->animated_doc->sample_at(Frame{30});
    REQUIRE(state.transition == SourceTextTransition::CrossfadeLayouts);
    REQUIRE(state.crossfade_from != nullptr);
    REQUIRE(state.mix > 0.0f);
    REQUIRE(state.mix < 1.0f);

    // apply_active_state_to_text_run_shape populates shape.crossfade_layout
    // + shape.crossfade_glyphs from state.crossfade_from — this is the
    // PR 11 hook that establishes the OOB-precondition data shape.
    REQUIRE(apply_active_state_to_text_run_shape(*shape, state, engine, layout));

    // ── OOB precondition established ─────────────────────────────────
    REQUIRE(shape->crossfade_layout != nullptr);
    REQUIRE_FALSE(shape->crossfade_glyphs.empty());
    const size_t active_glyph_count  = shape->layout->placed.glyphs.size();
    const size_t outgoing_glyph_count = shape->crossfade_layout->placed.glyphs.size();
    MESSAGE("active_glyph_count   = " << active_glyph_count);
    MESSAGE("outgoing_glyph_count = " << outgoing_glyph_count);
    CHECK(outgoing_glyph_count > active_glyph_count);
    CHECK(shape->crossfade_glyphs.size() == outgoing_glyph_count);

    // ── INVOKE draw_text_run end-to-end ──────────────────────────────
    // The "does not crash" invariant is the primary regression check.
    // Bug #5's symptom was OOB-read on `layout.placed[gi]` — a clean
    // dispatch here proves the glyph_id read path stays inside the
    // ACTIVE bounds, irrespective of how long the OUTGOING text is.
    Framebuffer fb(64, 64);
    const Mat4 model = Mat4(1.0f);  // identity
    graph::RenderOpResult result{
        graph::RenderBackendError{
            graph::RenderBackendErrorCode::InvalidInput, "uninitialised"}};
    REQUIRE_NOTHROW(result = renderer.backend().draw_text_run(fb, *shape, model, 1.0f));

    if (result.has_value()) {
        // Success path: dispatch did not crash + returned an outcome.
        // We do not assert pixel-output determinism (BL rasterizer is
        // environment-sensitive) — only that the dispatch returned.
        CHECK(static_cast<bool>(result));
    } else {
        // Error path is acceptable IF it is a documented error code.
        // Anything outside `is_known_render_op_error(...)` would mean
        // a regression in the error contract, not a runtime crash.
        const auto code = result.error().code;
        MESSAGE("draw_text_run error.code = " << static_cast<int>(code));
        CHECK(is_known_render_op_error(code));
    }
}

// ═════════════════════════════════════════════════════════════════════════
// 2. NO-CRASH STRESS — draw_text_run with crossfade + stroke + 33:3
//        active:outgoing ratio must not OOB under deflated ratio.
//
//        Same no-crash contract as test 1: ANY return outcome is
//        acceptable as long as the dispatch completes without
//        segfault / OOB-read.  Stress variant with extreme asymmetry.
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("draw_text_run: crossfade + stroke with 33:3 ratio does not crash (no-crash regression)") {
    if (!test_text_fixture::fixture_exists(kInterBoldPath)) {
        test_text_fixture::skip_if_missing(kInterBoldPath, "TICKET-068 E2E stress (33:3 ratio)");
        return;
    }

    const FontSpec font = test_text_fixture::inter_bold();
    const std::string active_text    = "ABc";                       // 3 glyphs
    const std::string outgoing_text  =
        "abcdefghijklmnopqrstuvwxyz0123456";                         // 33 glyphs (LONG)

    SoftwareRenderer renderer = test::make_renderer();
    FontEngine engine{renderer.runtime().resolver()};
    TextLayoutSpec layout;
    layout.box = {256.0f, 64.0f};

    auto shape = make_real_shape_for_test(active_text, engine, layout, font);
    REQUIRE(shape != nullptr);
    REQUIRE(shape->layout != nullptr);
    REQUIRE_FALSE(shape->glyphs.empty());

    for (auto& g : shape->glyphs) {
        g.stroke_width = 3.0f;
        g.stroke       = Color{0x00, 0xFF, 0x00, 0xFF};  // green
    }
    shape->paint.stroke_enabled = true;
    shape->paint.stroke_width   = 3.0f;
    shape->paint.stroke_color   = Color{0x00, 0xFF, 0x00, 0xFF};

    shape->animated_doc = make_crossfade_longer_outgoing_doc(
        outgoing_text, active_text, font);

    const auto state = shape->animated_doc->sample_at(Frame{30});
    REQUIRE(state.transition == SourceTextTransition::CrossfadeLayouts);
    REQUIRE(apply_active_state_to_text_run_shape(*shape, state, engine, layout));

    REQUIRE(shape->crossfade_layout != nullptr);
    REQUIRE(shape->crossfade_layout->placed.glyphs.size() > shape->layout->placed.glyphs.size());

    Framebuffer fb(128, 64);
    const Mat4 model = Mat4(1.0f);
    graph::RenderOpResult result{
        graph::RenderBackendError{
            graph::RenderBackendErrorCode::InvalidInput, "uninitialised"}};
    REQUIRE_NOTHROW(result = renderer.backend().draw_text_run(fb, *shape, model, 1.0f));

    if (result.has_value()) {
        CHECK(static_cast<bool>(result));
    } else {
        const auto code = result.error().code;
        MESSAGE("draw_text_run error.code = " << static_cast<int>(code));
        CHECK(is_known_render_op_error(code));
    }
}

// ═════════════════════════════════════════════════════════════════════════
// 3. RENDER-SUCCESS — draw_text_run with crossfade + stroke must
//        produce a successful render (NOT just avoid crashing).
//
//        This is the E2E rendering-success gate: a properly-configured
//        environment (fonts installed, Blend2D present) MUST produce a
//        non-error RenderOpResult.  Skips gracefully when the font
//        fixture is missing.  Contrast with tests 1-2 which accept
//        ExecutionFailure — this test does NOT.
// ═════════════════════════════════════════════════════════════════════════
//
// Uses the same OOB-precondition crossfade data shape as test 1, but
// adds a RENDER-SUCCESS assertion: draw_text_run MUST return
// result.has_value() == true when fonts are available.  This closes
// the audit P0 #6 gap where the no-crash E2E smoke was counted as
// proof of rendering correctness.

TEST_CASE("draw_text_run: crossfade + stroke produces successful render (E2E render-success gate)") {
    if (!test_text_fixture::fixture_exists(kInterBoldPath)) {
        test_text_fixture::skip_if_missing(kInterBoldPath, "TICKET-068 E2E render-success");
        return;
    }

    const FontSpec font = test_text_fixture::inter_bold();
    const std::string active_text    = "Hello";                     // 5 glyphs
    const std::string outgoing_text  = "HelloWorld";                  // 10 glyphs (LONGER)

    SoftwareRenderer renderer = test::make_renderer();
    FontEngine engine{renderer.runtime().resolver()};
    TextLayoutSpec layout;
    layout.box = {256.0f, 64.0f};

    auto shape = make_real_shape_for_test(active_text, engine, layout, font);
    REQUIRE(shape != nullptr);
    REQUIRE(shape->layout != nullptr);
    REQUIRE_FALSE(shape->glyphs.empty());

    // Enable stroke on every active glyph.
    for (auto& g : shape->glyphs) {
        g.stroke_width = 2.0f;
        g.stroke       = Color{0x00, 0x80, 0xFF, 0xFF};  // blue
    }
    shape->paint.stroke_enabled = true;
    shape->paint.stroke_width   = 2.0f;
    shape->paint.stroke_color   = Color{0x00, 0x80, 0xFF, 0xFF};

    // Attach OOB-precondition crossfade doc.
    shape->animated_doc = make_crossfade_longer_outgoing_doc(
        outgoing_text, active_text, font);

    const auto state = shape->animated_doc->sample_at(Frame{30});
    REQUIRE(state.transition == SourceTextTransition::CrossfadeLayouts);
    REQUIRE(state.crossfade_from != nullptr);
    REQUIRE(apply_active_state_to_text_run_shape(*shape, state, engine, layout));

    REQUIRE(shape->crossfade_layout != nullptr);
    REQUIRE_FALSE(shape->crossfade_glyphs.empty());
    REQUIRE(shape->crossfade_layout->placed.glyphs.size() > shape->layout->placed.glyphs.size());

    // ── RENDER-SUCCESS assertion ────────────────────────────────────
    // Unlike tests 1-2 (no-crash), this test REQUIRES the render to
    // succeed — result.has_value() must be true.  ExecutionFailure,
    // InvalidInput, or UnsupportedCapability are NOT acceptable here.
    Framebuffer fb(128, 64);
    const Mat4 model = Mat4(1.0f);
    auto result = renderer.backend().draw_text_run(fb, *shape, model, 1.0f);
    REQUIRE(result.has_value());
    CHECK(static_cast<bool>(result));

    // ── Output sanity: framebuffer must not be all-transparent ───────
    // After a successful stroke render, at least one pixel should be
    // non-transparent.  This is a weak but useful smoke check that the
    // renderer actually wrote pixels (not just returned Ok with no-op).
    bool has_non_transparent = false;
    for (int y = 0; y < fb.height() && !has_non_transparent; ++y) {
        for (int x = 0; x < fb.width() && !has_non_transparent; ++x) {
            const auto px = fb.get_pixel(x, y);
            if (px.a != 0.0f) has_non_transparent = true;
        }
    }
    CHECK(has_non_transparent);
}
