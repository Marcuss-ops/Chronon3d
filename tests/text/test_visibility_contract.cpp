// test_visibility_contract.cpp — M1.8 §1E / TICKET-SIMPLICITY-VISIBILITY-CONTRACT
//
// Zero-false-negative guarantee test for `TextVisibilityAudit` (FU04).
// Locks the 4 critical invariants + 3 §9 response fields (status, should_disable_tile_pruning,
// expanded_predicted_bbox) so that any regression in the contract computation
// surfaces immediately.
//
// Test surface: 4 TEST_CASEs × ~3 CHECKs each = ~12 CHECK assertions total.
//   #1 FAIL on invalid bbox (world_ink_bbox out of predicted_bbox containment)
//   #2 PASS on nominal bbox (predicted_bbox ⊇ world_ink_bbox)
//   #3 Violation response expands world_ink_bbox by effect_padding
//   #4 Edge cases: empty shape, NaN predicted_bbox, infinite predicted_bbox
//
// AGENTS.md v0.1 freeze compliance: pure math harness, no Blend2D / GPU /
// FontEngine / HarfBuzz. The .cpp file is wrapped in
// `#ifdef CHRONON3D_BUILD_DIAGNOSTICS` because the audit function is gated.
// CMake registration is UNCONDITIONAL (the .cpp self-guards); in
// non-diagnostic builds the test target compiles an empty TU (no TEST_CASE
// to run, but the target still exists for uniform CI configuration).
//
// FU04 honest accounting: this test does NOT exercise the alpha-bbox
// invariants (clip_contains_visible_ink + alpha_bbox non-empty) because
// those require a rendered framebuffer, which is out-of-scope for this
// unit-level contract test. Forward-point (FU06): add a render-tier
// golden test in `tests/text_golden/text_visibility_contract/` that
// exercises the full 6-invariant cascade with a real framebuffer.

#include <doctest/doctest.h>

#include <chronon3d/text/text_visibility_audit.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <cmath>
#include <limits>

#ifdef CHRONON3D_BUILD_DIAGNOSTICS

using namespace chronon3d;

namespace {

// Minimal `TextRunShape` factory for unit testing. Populates only the
// fields the audit reads: `engine` + `layout->placed.glyphs.size()`.
// Pass `with_glyphs=true` to also populate `shape.glyphs` so the canonical
// `compute_text_run_visual_bounds()` returns a real (non-nullopt) local
// bbox — used by the #5 test to verify the audit now reads local_ink_bbox
// from the shape (not zero-rect placeholder).
TextRunShape make_test_shape(bool with_engine = true,
                             std::size_t glyph_count = 1,
                             bool with_glyphs = false) {
    TextRunShape shape{};
    if (with_engine) {
        // Use a placeholder engine pointer. The audit only checks
        // `engine != nullptr`, not the engine's actual contents. The
        // pointer is non-owning (engine lifetime is managed by the
        // composition layer) so the test does not delete it.
        static std::byte placeholder{};
        shape.engine = reinterpret_cast<FontEngine*>(&placeholder);
    }
    auto layout = std::make_shared<TextRunLayout>();
    layout->placed.glyphs.resize(glyph_count);
    layout->font_size = 16.0f;
    layout->placed.ascent = 10.0f;
    layout->placed.descent = 5.0f;
    shape.layout = std::move(layout);

    if (with_glyphs) {
        // Populate the runtime `shape.glyphs` vector so the canonical
        // `compute_text_run_visual_bounds()` returns a real bbox. The
        // values mirror a small 3-glyph run at font_size=16, advance=20,
        // positioned at the origin. The audit will then compute a
        // non-zero local_ink_bbox from this data.
        shape.glyphs.resize(glyph_count);
        for (std::size_t i = 0; i < glyph_count; ++i) {
            shape.glyphs[i].position = {static_cast<float>(i * 20), 0.0f};
            shape.glyphs[i].layout_position = {static_cast<float>(i * 20), 0.0f};
            shape.glyphs[i].rotation = {0.0f, 0.0f, 0.0f};
            shape.glyphs[i].scale = {1.0f, 1.0f, 1.0f};
            shape.glyphs[i].blur = 0.0f;
            shape.glyphs[i].stroke_width = 0.0f;
            shape.layout->placed.glyphs[i].advance_x = 20.0f;
        }
    }
    return shape;
}

} // anonymous namespace

// #1 — FAIL on invalid bbox (world_ink_bbox out of predicted_bbox containment).
//
// Set up: identity world_matrix, zero-rect local_ink_bbox, predicted_bbox
// that does NOT contain the world_ink_bbox. The audit's `predicted_contains_world`
// check fails because the world point (0,0) is not inside the predicted bbox
// (10,10)→(20,20). Status must be FAIL.
TEST_CASE("Visibility contract #1: FAIL on invalid bbox (world out of predicted)") {
    const auto shape = make_test_shape(/*with_engine=*/true, /*glyph_count=*/3);
    const Mat4 identity{};  // identity world matrix
    // Identity matrix transforms (0,0) to (0,0). The world_ink_bbox is
    // therefore a point at (0,0).
    const Rect predicted_bbox{{10.0f, 10.0f}, {10.0f, 10.0f}};  // (10..20) × (10..20)
    const Rect clip_rect    {{0.0f, 0.0f}, {100.0f, 100.0f}};

    const auto audit = audit_text_visibility(
        shape, identity, predicted_bbox, clip_rect, /*rendered_output=*/nullptr,
        /*effect_padding=*/4.0f);

    // Sanity: shaping succeeded
    CHECK(audit.shaping_succeeded);
    CHECK(audit.font_resolved);
    CHECK(audit.glyph_count == 3);
    // Critical: predicted_contains_world is false (world point at 0,0
    // is outside predicted (10..20) box).
    CHECK_FALSE(audit.predicted_contains_world);
    // Status must be FAIL
    CHECK(audit.status == TextVisibilityStatus::FAIL);
    // Violation response: flag set + expansion computed
    CHECK(audit.should_disable_tile_pruning);
    // expanded_predicted_bbox should be the world_ink_bbox (point at 0,0)
    // expanded by effect_padding=4: (0-4, 0-4) size (1+8, 1+8) = (-4, -4, 9, 9)
    CHECK(audit.expanded_predicted_bbox.origin.x == doctest::Approx(-4.0f));
    CHECK(audit.expanded_predicted_bbox.origin.y == doctest::Approx(-4.0f));
    CHECK(audit.expanded_predicted_bbox.size.x == doctest::Approx(9.0f));
    CHECK(audit.expanded_predicted_bbox.size.y == doctest::Approx(9.0f));
}

// #2 — PASS on nominal bbox (predicted_bbox ⊇ world_ink_bbox).
//
// Set up: identity world_matrix, zero-rect local_ink_bbox, predicted_bbox
// that DOES contain the world_ink_bbox (point at 0,0 is inside (0,0)→(10,10)).
// Status must be PASS.
TEST_CASE("Visibility contract #2: PASS on nominal bbox (predicted contains world)") {
    const auto shape = make_test_shape(/*with_engine=*/true, /*glyph_count=*/3);
    const Mat4 identity{};
    const Rect predicted_bbox{{0.0f, 0.0f}, {10.0f, 10.0f}};  // (0..10) × (0..10)
    const Rect clip_rect    {{0.0f, 0.0f}, {100.0f, 100.0f}};

    const auto audit = audit_text_visibility(
        shape, identity, predicted_bbox, clip_rect, /*rendered_output=*/nullptr,
        /*effect_padding=*/4.0f);

    CHECK(audit.shaping_succeeded);
    CHECK(audit.finite);
    CHECK(audit.predicted_contains_world);
    // No framebuffer supplied → alpha-bbox invariants deferred → PASS
    CHECK(audit.status == TextVisibilityStatus::PASS);
    CHECK_FALSE(audit.should_disable_tile_pruning);
    // expanded_predicted_bbox is zero-rect (default) when no violation
    CHECK(audit.expanded_predicted_bbox.size.x == 0.0f);
    CHECK(audit.expanded_predicted_bbox.size.y == 0.0f);
}

// #3 — Violation response expansion: world_ink_bbox padded by effect_padding.
//
// Set up: identity world_matrix, EMPTY shape (no glyphs) so local_ink_bbox
// is zero-rect (compute_text_run_visual_bounds returns std::nullopt for
// empty shape → audit falls back to Rect{} → world_ink_bbox collapses to
// a point at world origin). predicted_bbox does NOT contain the world
// point → violation triggered. The expansion = world_ink_bbox (point)
// padded by effect_padding on all 4 sides. This locks the empty-shape
// fallback path; the non-zero-local path is exercised in #5.
TEST_CASE("Visibility contract #3: violation response expands world_ink_bbox by effect_padding (empty shape)") {
    const auto shape = make_test_shape(/*with_engine=*/true, /*glyph_count=*/5, /*with_glyphs=*/false);
    const Mat4 identity{};

    // Empty shape → audit.local_ink_bbox = Rect{} → world_ink_bbox = point at (0,0).
    const Rect predicted_bbox{{100.0f, 100.0f}, {10.0f, 10.0f}};  // out of range
    const Rect clip_rect    {{0.0f, 0.0f}, {200.0f, 200.0f}};
    const float padding = 16.0f;

    const auto audit = audit_text_visibility(
        shape, identity, predicted_bbox, clip_rect, /*rendered_output=*/nullptr,
        /*effect_padding=*/padding);

    // World point at (0,0) is outside predicted (100..110). Violation triggered.
    CHECK_FALSE(audit.predicted_contains_world);
    CHECK(audit.should_disable_tile_pruning);
    CHECK(audit.status == TextVisibilityStatus::FAIL);
    // Empty shape → local_ink_bbox = Rect{} → world_ink_bbox = point at (0,0).
    CHECK(audit.local_ink_bbox.size.x == 0.0f);
    CHECK(audit.local_ink_bbox.size.y == 0.0f);
    CHECK(audit.world_ink_bbox.size.x == 0.0f);
    CHECK(audit.world_ink_bbox.size.y == 0.0f);
    // expanded_predicted_bbox = (0,0) point + padding 16 on all sides
    // = origin (-16, -16), size (1+32, 1+32) = (33, 33)
    CHECK(audit.expanded_predicted_bbox.origin.x == doctest::Approx(-16.0f));
    CHECK(audit.expanded_predicted_bbox.origin.y == doctest::Approx(-16.0f));
    CHECK(audit.expanded_predicted_bbox.size.x == doctest::Approx(33.0f));
    CHECK(audit.expanded_predicted_bbox.size.y == doctest::Approx(33.0f));
}

// #4 — Edge cases: empty shape (no glyphs) + NaN predicted_bbox + infinite predicted_bbox.
TEST_CASE("Visibility contract #4: edge cases (empty shape, NaN, infinite)") {
    // 4a: empty shape (no engine + no glyphs) → font_resolved=false, shaping_succeeded=false
    {
        const auto shape = make_test_shape(/*with_engine=*/false, /*glyph_count=*/0);
        const Mat4 identity{};
        const Rect predicted_bbox{{0.0f, 0.0f}, {10.0f, 10.0f}};
        const Rect clip_rect    {{0.0f, 0.0f}, {100.0f, 100.0f}};

        const auto audit = audit_text_visibility(
            shape, identity, predicted_bbox, clip_rect, nullptr, 4.0f);

        CHECK_FALSE(audit.font_resolved);
        CHECK_FALSE(audit.shaping_succeeded);
        CHECK(audit.glyph_count == 0);
        CHECK(audit.status == TextVisibilityStatus::FAIL);  // !critical_pass
    }

    // 4b: NaN predicted_bbox → finite=false, status=FAIL
    {
        const auto shape = make_test_shape();
        const Mat4 identity{};
        const Rect predicted_bbox{
            {std::numeric_limits<float>::quiet_NaN(), 0.0f},
            {10.0f, 10.0f}
        };
        const Rect clip_rect{{0.0f, 0.0f}, {100.0f, 100.0f}};

        const auto audit = audit_text_visibility(
            shape, identity, predicted_bbox, clip_rect, nullptr, 4.0f);

        CHECK_FALSE(audit.finite);  // NaN in predicted_bbox breaks finite
        CHECK(audit.status == TextVisibilityStatus::FAIL);
    }

    // 4c: infinite predicted_bbox → finite=false, status=FAIL
    {
        const auto shape = make_test_shape();
        const Mat4 identity{};
        const Rect predicted_bbox{
            {0.0f, 0.0f},
            {std::numeric_limits<float>::infinity(), 10.0f}
        };
        const Rect clip_rect{{0.0f, 0.0f}, {100.0f, 100.0f}};

        const auto audit = audit_text_visibility(
            shape, identity, predicted_bbox, clip_rect, nullptr, 4.0f);

        CHECK_FALSE(audit.finite);
        CHECK(audit.status == TextVisibilityStatus::FAIL);
    }
}

// #5 — Real local_ink_bbox from shape (FU04 contract fix verification).
//
// Set up: 3-glyph shape with populated `shape.glyphs` (advance=20, font=16,
// positioned at (0,0)/(20,0)/(40,0)). The canonical
// `compute_text_run_visual_bounds()` walks the active glyph vector and
// returns a real local bbox with ascent/descent + per-glyph advance + safety
// padding. The audit now delegates to this canonical helper (replacing the
// prior zero-rect PLACEHOLDER) so `audit.local_ink_bbox` is non-zero when
// the shape has glyphs. This locks the FU04 contract: local_ink_bbox is
// computed from the shape, not assumed zero, not inherited from world.
TEST_CASE("Visibility contract #5: real local_ink_bbox from shape (FU04 contract fix)") {
    const auto shape = make_test_shape(/*with_engine=*/true, /*glyph_count=*/3, /*with_glyphs=*/true);
    const Mat4 identity{};

    // Dummy data: 3 glyphs at positions (0,0), (20,0), (40,0); advance=20 each.
    // Canonical compute_text_run_visual_bounds() with font_size=16:
    //   ascent  = max(0, placed.ascent=10, 16*0.8=12.8) = 12.8
    //   descent = max(0, placed.descent=5, 16*0.2=3.2)  = 5
    //   For each glyph i: gx = i*20 + i*20 = i*40 (layout_pos + position are both i*20)
    //     pad = 0 + 0 + 8 (shear + blur + stroke + safety) = 8
    //     scale_x = scale_y = 1.0
    //     advance = 20 (for last glyph, max(1, max(20, 19.2)) = 20; ink floor doesn't change)
    //     min_x candidate: gx - pad = i*40 - 8
    //     max_x candidate: gx + advance + pad = i*40 + 28
    //     min_y = 0 - 12.8 - 8 = -20.8
    //     max_y = 0 + 5 + 8 = 13
    //   Aggregated (3 glyphs):
    //     min_x = min(-8, 32, 72) = -8
    //     max_x = max(28, 68, 108) = 108
    //     min_y = -20.8
    //     max_y = 13
    //   local_ink_bbox = {origin: {-8, -20.8}, size: {116, 33.8}}
    //
    // World (identity): same as local.

    // predicted_bbox that fully contains the world_ink_bbox → PASS
    const Rect predicted_bbox{{-20.0f, -30.0f}, {200.0f, 100.0f}};
    const Rect clip_rect    {{0.0f, 0.0f}, {200.0f, 200.0f}};

    const auto audit = audit_text_visibility(
        shape, identity, predicted_bbox, clip_rect, /*rendered_output=*/nullptr,
        /*effect_padding=*/4.0f);

    // Sanity: shaping succeeded (3 glyphs, real layout)
    CHECK(audit.shaping_succeeded);
    CHECK(audit.font_resolved);
    CHECK(audit.glyph_count == 3);

    // local_ink_bbox is now computed from the shape (NOT zero-rect PLACEHOLDER)
    CHECK(audit.local_ink_bbox.origin.x == doctest::Approx(-8.0f));
    CHECK(audit.local_ink_bbox.origin.y == doctest::Approx(-20.8f));
    CHECK(audit.local_ink_bbox.size.x   == doctest::Approx(116.0f));
    CHECK(audit.local_ink_bbox.size.y   == doctest::Approx(33.8f));

    // world_ink_bbox = transform_aabb(local_ink_bbox, identity) = local_ink_bbox
    CHECK(audit.world_ink_bbox.origin.x == doctest::Approx(-8.0f));
    CHECK(audit.world_ink_bbox.origin.y == doctest::Approx(-20.8f));
    CHECK(audit.world_ink_bbox.size.x   == doctest::Approx(116.0f));
    CHECK(audit.world_ink_bbox.size.y   == doctest::Approx(33.8f));

    // critical invariants: all 4 hold (world_ink_bbox is finite + inside predicted)
    CHECK(audit.finite);
    CHECK(audit.predicted_contains_world);
    // No framebuffer supplied → alpha-bbox deferred → PASS
    CHECK(audit.status == TextVisibilityStatus::PASS);
    CHECK_FALSE(audit.should_disable_tile_pruning);
    CHECK(audit.expanded_predicted_bbox.size.x == 0.0f);
    CHECK(audit.expanded_predicted_bbox.size.y == 0.0f);
}

// #6 — Violation response with non-zero local_ink_bbox (FU04 contract fix).
//
// Set up: 3-glyph shape with populated `shape.glyphs` → real local_ink_bbox.
// predicted_bbox does NOT contain world_ink_bbox → violation triggered.
// The expansion should equal world_ink_bbox padded by effect_padding on all
// 4 sides. This locks the expansion math against a non-trivial world_ink_bbox
// (not the degenerate point at (0,0) used in #1/#3).
TEST_CASE("Visibility contract #6: violation response with non-zero local_ink_bbox (FU04 contract fix)") {
    const auto shape = make_test_shape(/*with_engine=*/true, /*glyph_count=*/3, /*with_glyphs=*/true);
    const Mat4 identity{};

    // predicted_bbox that does NOT contain the world_ink_bbox
    // (world_ink_bbox is at origin: (-8, -20.8) size (116, 33.8); predicted starts at (500, 500))
    const Rect predicted_bbox{{500.0f, 500.0f}, {10.0f, 10.0f}};
    const Rect clip_rect    {{0.0f, 0.0f}, {600.0f, 600.0f}};
    const float padding = 20.0f;

    const auto audit = audit_text_visibility(
        shape, identity, predicted_bbox, clip_rect, /*rendered_output=*/nullptr,
        /*effect_padding=*/padding);

    // Sanity: local_ink_bbox is the real value (not zero-rect)
    CHECK(audit.local_ink_bbox.size.x == doctest::Approx(116.0f));
    CHECK(audit.local_ink_bbox.size.y == doctest::Approx(33.8f));

    // world_ink_bbox = transform_aabb(local_ink_bbox, identity) = local_ink_bbox
    CHECK(audit.world_ink_bbox.size.x == doctest::Approx(116.0f));
    CHECK(audit.world_ink_bbox.size.y == doctest::Approx(33.8f));

    // Violation triggered (world_ink_bbox extends well past predicted_bbox)
    CHECK_FALSE(audit.predicted_contains_world);
    CHECK(audit.should_disable_tile_pruning);
    CHECK(audit.status == TextVisibilityStatus::FAIL);

    // expanded_predicted_bbox = world_ink_bbox padded by effect_padding=20
    // origin: (-8 - 20, -20.8 - 20) = (-28, -40.8)
    // size:   (116 + 40, 33.8 + 40) = (156, 73.8)
    CHECK(audit.expanded_predicted_bbox.origin.x == doctest::Approx(-28.0f));
    CHECK(audit.expanded_predicted_bbox.origin.y == doctest::Approx(-40.8f));
    CHECK(audit.expanded_predicted_bbox.size.x   == doctest::Approx(156.0f));
    CHECK(audit.expanded_predicted_bbox.size.y   == doctest::Approx(73.8f));
}

#else  // !CHRONON3D_BUILD_DIAGNOSTICS

// Diagnostic builds are off. The audit function is gated and cannot be
// called. Emit a single SUCCEED no-op TEST_CASE so the test target still
// has a test count to report (mirrors the pipeline_parity pattern).
TEST_CASE("Visibility contract: SKIPPED — CHRONON3D_BUILD_DIAGNOSTICS=0") {
    SUCCEED("visibility contract test skipped in non-diagnostic build");
}

#endif // CHRONON3D_BUILD_DIAGNOSTICS
