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
// fields the audit reads: `engine` + `layout.placed.glyphs.size()`.
TextRunShape make_test_shape(bool with_engine = true, std::size_t glyph_count = 1) {
    TextRunShape shape{};
    if (with_engine) {
        // Use a placeholder engine pointer. The audit only checks
        // `engine != nullptr`, not the engine's actual contents. The
        // pointer is non-owning (engine lifetime is managed by the
        // composition layer) so the test does not delete it.
        static std::byte placeholder{};
        shape.engine = reinterpret_cast<FontEngine*>(&placeholder);
    }
    // SharedTextRunLayout is shared_ptr<const TextRunLayout>, so build a
    // mutable layout first, populate its glyphs, then move it into the
    // shared pointer.
    TextRunLayout layout{};
    layout.placed.glyphs.resize(glyph_count);
    shape.layout = std::make_shared<TextRunLayout>(std::move(layout));
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

    const Rect local_ink_bbox{{0.0f, 0.0f}, {0.0f, 0.0f}};
    const auto audit = audit_text_visibility(
        shape, local_ink_bbox, identity, predicted_bbox, clip_rect,
        /*rendered_output=*/nullptr, /*effect_padding=*/4.0f);

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

    const Rect local_ink_bbox{{0.0f, 0.0f}, {0.0f, 0.0f}};
    const auto audit = audit_text_visibility(
        shape, local_ink_bbox, identity, predicted_bbox, clip_rect,
        /*rendered_output=*/nullptr, /*effect_padding=*/4.0f);

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
// Set up: identity world_matrix, non-zero local_ink_bbox (so world_ink_bbox
// is a real rect), predicted_bbox that does NOT contain world_ink_bbox.
// The expansion should equal `world_ink_bbox` padded by `effect_padding`
// on all 4 sides.
TEST_CASE("Visibility contract #3: violation response expands world_ink_bbox by effect_padding") {
    const auto shape = make_test_shape(/*with_engine=*/true, /*glyph_count=*/5);
    const Mat4 identity{};

    // local_ink_bbox = (10, 10, 20, 20) → world_ink_bbox (identity) = (10, 10, 20, 20).
    // The audit now receives the real local_ink_bbox, so we can verify
    // the expansion from a non-zero world_ink_bbox.
    const Rect local_ink_bbox{{10.0f, 10.0f}, {10.0f, 10.0f}};
    const Rect predicted_bbox{{100.0f, 100.0f}, {10.0f, 10.0f}};  // out of range
    const Rect clip_rect    {{0.0f, 0.0f}, {200.0f, 200.0f}};
    const float padding = 16.0f;

    const auto audit = audit_text_visibility(
        shape, local_ink_bbox, identity, predicted_bbox, clip_rect,
        /*rendered_output=*/nullptr, /*effect_padding=*/padding);

    // World bbox (10,10)→(20,20) is outside predicted (100..110). Violation triggered.
    CHECK_FALSE(audit.predicted_contains_world);
    CHECK(audit.should_disable_tile_pruning);
    CHECK(audit.status == TextVisibilityStatus::FAIL);
    // expanded_predicted_bbox = world_ink_bbox (10,10,20,20) + padding 16 on all sides
    // = origin (-6, -6), size (10+32, 10+32) = (42, 42)
    CHECK(audit.expanded_predicted_bbox.origin.x == doctest::Approx(-6.0f));
    CHECK(audit.expanded_predicted_bbox.origin.y == doctest::Approx(-6.0f));
    CHECK(audit.expanded_predicted_bbox.size.x == doctest::Approx(42.0f));
    CHECK(audit.expanded_predicted_bbox.size.y == doctest::Approx(42.0f));
}

// #4 — Edge cases: empty shape (no glyphs) + NaN predicted_bbox + infinite predicted_bbox.
TEST_CASE("Visibility contract #4: edge cases (empty shape, NaN, infinite)") {
    // 4a: empty shape (no engine + no glyphs) → font_resolved=false, shaping_succeeded=false
    {
        const auto shape = make_test_shape(/*with_engine=*/false, /*glyph_count=*/0);
        const Mat4 identity{};
        const Rect local_ink_bbox{{0.0f, 0.0f}, {0.0f, 0.0f}};
        const Rect predicted_bbox{{0.0f, 0.0f}, {10.0f, 10.0f}};
        const Rect clip_rect    {{0.0f, 0.0f}, {100.0f, 100.0f}};

        const auto audit = audit_text_visibility(
            shape, local_ink_bbox, identity, predicted_bbox, clip_rect,
            nullptr, 4.0f);

        CHECK_FALSE(audit.font_resolved);
        CHECK_FALSE(audit.shaping_succeeded);
        CHECK(audit.glyph_count == 0);
        CHECK(audit.status == TextVisibilityStatus::FAIL);  // !critical_pass
    }

    // 4b: NaN predicted_bbox → finite=false, status=FAIL
    {
        const auto shape = make_test_shape();
        const Mat4 identity{};
        const Rect local_ink_bbox{{0.0f, 0.0f}, {0.0f, 0.0f}};
        const Rect predicted_bbox{
            {std::numeric_limits<float>::quiet_NaN(), 0.0f},
            {10.0f, 10.0f}
        };
        const Rect clip_rect{{0.0f, 0.0f}, {100.0f, 100.0f}};

        const auto audit = audit_text_visibility(
            shape, local_ink_bbox, identity, predicted_bbox, clip_rect,
            nullptr, 4.0f);

        CHECK_FALSE(audit.finite);  // NaN in predicted_bbox breaks finite
        CHECK(audit.status == TextVisibilityStatus::FAIL);
    }

    // 4c: infinite predicted_bbox → finite=false, status=FAIL
    {
        const auto shape = make_test_shape();
        const Mat4 identity{};
        const Rect local_ink_bbox{{0.0f, 0.0f}, {0.0f, 0.0f}};
        const Rect predicted_bbox{
            {0.0f, 0.0f},
            {std::numeric_limits<float>::infinity(), 10.0f}
        };
        const Rect clip_rect{{0.0f, 0.0f}, {100.0f, 100.0f}};

        const auto audit = audit_text_visibility(
            shape, local_ink_bbox, identity, predicted_bbox, clip_rect,
            nullptr, 4.0f);

        CHECK_FALSE(audit.finite);
        CHECK(audit.status == TextVisibilityStatus::FAIL);
    }
}

#else  // !CHRONON3D_BUILD_DIAGNOSTICS

// Diagnostic builds are off. The audit function is gated and cannot be
// called. Emit a single SUCCEED no-op TEST_CASE so the test target still
// has a test count to report (mirrors the pipeline_parity pattern).
TEST_CASE("Visibility contract: SKIPPED — CHRONON3D_BUILD_DIAGNOSTICS=0") {
    SUCCEED("visibility contract test skipped in non-diagnostic build");
}

#endif // CHRONON3D_BUILD_DIAGNOSTICS
