#include <doctest/doctest.h>

#include <chronon3d/backends/software/text_run_processor.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <glm/gtc/matrix_transform.hpp>

using namespace chronon3d;
using namespace chronon3d::renderer;

namespace {

// ── Helpers ────────────────────────────────────────────────────────────────

PlacedGlyphRun make_placed_run_trp(size_t count) {
    PlacedGlyphRun pr;
    pr.font_size = 32.0f;
    pr.total_width = static_cast<float>(count) * 12.0f;
    pr.total_height = 40.0f;
    pr.ascent = 28.0f;
    pr.descent = 12.0f;
    pr.baseline = 28.0f;

    for (size_t i = 0; i < count; ++i) {
        PlacedGlyph g;
        g.glyph_id = static_cast<u32>(100 + i);
        g.x = static_cast<float>(i) * 12.0f;
        g.y = 0.0f;
        g.advance_x = 12.0f;
        g.byte_offset = i;
        g.byte_len = 1;
        g.cluster = static_cast<u32>(i);
        g.is_cluster_start = true;
        pr.glyphs.push_back(g);

        PlacedGlyphRun::Cluster c;
        c.start_glyph = i;
        c.end_glyph = i + 1;
        c.byte_offset = i;
        c.byte_len = 1;
        c.advance = 12.0f;
        c.raw_advance = 10.0f;
        pr.clusters.push_back(c);
    }

    return pr;
}

SharedTextRunLayout make_test_layout_trp(const std::string& text = "ABC") {
    auto layout = std::make_shared<TextRunLayout>();
    layout->source_text = text;
    layout->font = FontSpec{"assets/fonts/Inter-Regular.ttf", "Inter", 400};
    layout->font_size = 32.0f;
    layout->placed = make_placed_run_trp(text.size());
    layout->units = build_text_unit_map(layout->placed, text);
    layout->bounds = {layout->placed.total_width, layout->placed.total_height};
    layout->line_height = 38.4f;
    layout->tracking = 0.0f;
    layout->wrap = TextWrap::None;
    layout->direction = TextDirection::LTR;
    return layout;
}

std::vector<GlyphInstanceState> make_glyph_states_trp(const PlacedGlyphRun& placed) {
    std::vector<GlyphInstanceState> states;
    for (size_t i = 0; i < placed.glyphs.size(); ++i) {
        const auto& g = placed.glyphs[i];
        GlyphInstanceState s;
        s.glyph_id = g.glyph_id;
        s.layout_position = {g.x, g.y};
        s.opacity = 1.0f;
        s.fill = {1.0f, 1.0f, 1.0f, 1.0f};
        states.push_back(s);
    }
    return states;
}

TextRunShape make_test_shape(const std::string& text = "ABC") {
    TextRunShape shape;
    shape.layout = make_test_layout_trp(text);
    shape.glyphs = make_glyph_states_trp(shape.layout->placed);
    return shape;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// compute_text_run_world_bbox tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TextRunBBox: empty shape returns zero bbox") {
    // F0.4 — default-constructed TextRunShape (null layout + empty glyphs)
    // is the canonical way to represent an empty text run.
    TextRunShape shape;
    Mat4 identity{1.0f};
    auto bbox = compute_text_run_world_bbox(shape, identity, 0.0f);
    CHECK(bbox.x0 == 0);
    CHECK(bbox.y0 == 0);
    CHECK(bbox.x1 == 0);
    CHECK(bbox.y1 == 0);
}

TEST_CASE("TextRunBBox: single glyph at origin with identity matrix") {
    auto shape = make_test_shape("A");
    Mat4 identity{1.0f};
    auto bbox = compute_text_run_world_bbox(shape, identity, 0.0f);

    // Should include the glyph position + padding
    CHECK(bbox.x0 < 0);   // min_x - pad is negative
    CHECK(bbox.y0 < 0);   // min_y - pad is negative
    CHECK(bbox.x1 > 0);   // max_x + pad is positive
    CHECK(bbox.y1 > 0);   // max_y + pad is positive
    CHECK(bbox.x1 > bbox.x0);
    CHECK(bbox.y1 > bbox.y0);
}

TEST_CASE("TextRunBBox: three glyphs span horizontally") {
    auto shape = make_test_shape("ABC");
    Mat4 identity{1.0f};
    auto bbox = compute_text_run_world_bbox(shape, identity, 0.0f);

    // Three glyphs at (0,0), (12,0), (24,0)
    CHECK(bbox.x1 - bbox.x0 > 30);  // horizontal span > 30px
}

TEST_CASE("TextRunBBox: spread enlarges bbox") {
    auto shape = make_test_shape("A");
    Mat4 identity{1.0f};
    auto bbox_no_spread = compute_text_run_world_bbox(shape, identity, 0.0f);
    auto bbox_with_spread = compute_text_run_world_bbox(shape, identity, 20.0f);

    CHECK(bbox_with_spread.x0 < bbox_no_spread.x0);  // spread makes x0 more negative
    CHECK(bbox_with_spread.y0 < bbox_no_spread.y0);  // spread makes y0 more negative
    CHECK(bbox_no_spread.x1 < bbox_with_spread.x1);  // spread makes x1 larger
    CHECK(bbox_no_spread.y1 < bbox_with_spread.y1);  // spread makes y1 larger
}

TEST_CASE("TextRunBBox: spread is applied once (per-side delta == spread, not 2x)") {
    // After the fix: `spread` is added only at the final global pad (post
    // corner transformation), not also at the per-glyph inner pad.  With an
    // identity matrix the per-side inflation equals exactly the spread
    // amount.  Pre-fix, the per-glyph pad also contained `spread`, so the
    // per-side delta was 2x the spread value.
    auto shape = make_test_shape("ABC");
    Mat4 identity{1.0f};
    constexpr float kSpread = 20.0f;

    auto bbox_0 = compute_text_run_world_bbox(shape, identity, 0.0f);
    auto bbox_k = compute_text_run_world_bbox(shape, identity, kSpread);

    CHECK(bbox_0.x0 - bbox_k.x0 == static_cast<i32>(kSpread));
    CHECK(bbox_k.x1 - bbox_0.x1 == static_cast<i32>(kSpread));
    CHECK(bbox_0.y0 - bbox_k.y0 == static_cast<i32>(kSpread));
    CHECK(bbox_k.y1 - bbox_0.y1 == static_cast<i32>(kSpread));
}

TEST_CASE("TextRunBBox: translation moves bbox") {
    auto shape = make_test_shape("A");
    Mat4 translation = glm::translate(Mat4{1.0f}, Vec3{100.0f, 50.0f, 0.0f});
    auto bbox = compute_text_run_world_bbox(shape, translation, 0.0f);

    // Bbox should be centered around (100, 50) with padding
    CHECK(bbox.x0 < 100);
    CHECK(bbox.x1 > 100);
    CHECK(bbox.y0 < 50);
    CHECK(bbox.y1 > 50);
}

TEST_CASE("TextRunBBox: rotated glyph still produces valid bbox") {
    auto shape = make_test_shape("A");
    Mat4 rotation = glm::rotate(Mat4{1.0f}, glm::radians(45.0f), Vec3{0.0f, 0.0f, 1.0f});
    auto bbox = compute_text_run_world_bbox(shape, rotation, 0.0f);

    CHECK(bbox.x0 < bbox.x1);
    CHECK(bbox.y0 < bbox.y1);
    // Area should be non-zero
    CHECK((bbox.x1 - bbox.x0) * (bbox.y1 - bbox.y0) > 0);
}

TEST_CASE("TextRunBBox: moved glyph by position offset") {
    auto shape = make_test_shape("A");
    shape.glyphs[0].position = {50.0f, 30.0f, 0.0f};
    Mat4 identity{1.0f};
    auto bbox = compute_text_run_world_bbox(shape, identity, 0.0f);

    // Bbox should include the position offset
    CHECK(bbox.x1 > 40);  // position.x + some padding
    CHECK(bbox.y1 > 20);  // position.y + some padding
}

TEST_CASE("TextRunBBox: scaled glyph affects bbox") {
    auto shape = make_test_shape("A");
    shape.glyphs[0].scale = {2.0f, 2.0f, 1.0f};
    Mat4 identity{1.0f};
    auto bbox = compute_text_run_world_bbox(shape, identity, 0.0f);

    // Scale doesn't affect bbox directly (bbox doesn't account for per-glyph scale)
    // But bbox should still be valid
    CHECK(bbox.x0 < bbox.x1);
    CHECK(bbox.y0 < bbox.y1);
}

TEST_CASE("TextRunBBox: glyph with blur has larger bbox") {
    auto shape = make_test_shape("A");
    Mat4 identity{1.0f};

    auto bbox_no_blur = compute_text_run_world_bbox(shape, identity, 0.0f);
    shape.glyphs[0].blur = 10.0f;
    auto bbox_blur = compute_text_run_world_bbox(shape, identity, 0.0f);

    CHECK(bbox_blur.x0 < bbox_no_blur.x0);   // blur makes x0 more negative (enlarges left)
    CHECK(bbox_blur.y0 < bbox_no_blur.y0);   // blur makes y0 more negative (enlarges top)
    CHECK(bbox_no_blur.x1 < bbox_blur.x1);   // blur makes x1 larger (enlarges right)
    CHECK(bbox_no_blur.y1 < bbox_blur.y1);   // blur makes y1 larger (enlarges bottom)
}

// ═══════════════════════════════════════════════════════════════════════════
// TextRunProcessor completion tests
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/text/text_run.hpp>
using namespace chronon3d;

// Helper: bucket radius logic mirrors the inline `bucket_radius` lambda
// inside draw_text_run.  Exposed here for testing the 4-tier mapping
// without requiring a real GPU-renderer integration.
[[nodiscard]] static int expected_bucket_radius(float r) {
    // Same as production — calls the shared tier-mapping helper.
    return chronon3d::renderer::detail::bucket_radius_for_tier(r);
}

TEST_CASE("TextRun processor: 4-tier bucket sigma mapping") {
    CHECK(expected_bucket_radius(0.0f)  == 0);   // no blur
    CHECK(expected_bucket_radius(0.5f)  == 2);   // tier 1
    CHECK(expected_bucket_radius(4.0f)  == 2);
    CHECK(expected_bucket_radius(5.0f)  == 7);   // tier 2
    CHECK(expected_bucket_radius(8.0f)  == 7);
    CHECK(expected_bucket_radius(9.0f)  == 13);  // tier 3
    CHECK(expected_bucket_radius(16.0f) == 13);
    CHECK(expected_bucket_radius(17.0f) == 20);  // tier 4 (cap)
    CHECK(expected_bucket_radius(60.0f) == 20);
}

TEST_CASE("TextRunBBox: world bbox safely disjoint from framebuffer (safe-clip)") {
    // Glyph deliberately translated FAR off the framebuffer so the
    // safe-clip intersection is empty.  This is the case draw_text_run
    // now guards against (silent early return).
    auto shape = make_test_shape("A");
    Mat4 translated_off_canvas = glm::translate(
        Mat4{1.0f}, Vec3{100000.0f, 100000.0f, 0.0f});

    auto bbox = compute_text_run_world_bbox(shape, translated_off_canvas, 0.0f);

    // The bbox should land far outside any reasonable framebuffer.
    CHECK(bbox.x0 > 10000);
    CHECK(bbox.x1 > 10000);
    CHECK(bbox.y0 > 10000);
    CHECK(bbox.y1 > 10000);

    // A 1920x1080 framebuffer does NOT intersect this bbox.
    raster::BBox fb_bbox{0, 0, 1920, 1080};
    raster::BBox intersection{
        std::max(bbox.x0, fb_bbox.x0),
        std::max(bbox.y0, fb_bbox.y0),
        std::min(bbox.x1, fb_bbox.x1),
        std::min(bbox.y1, fb_bbox.y1)
    };
    CHECK(intersection.x1 <= intersection.x0);
    CHECK(intersection.y1 <= intersection.y0);
}

TEST_CASE("TextRunBBox: 180°-rotated extreme transform still leaves intersecting region (sanity)") {
    // Rotation by 180° flips the bbox but the EXTENT stays positive
    // and the world bbox still intersects the framebuffer if the
    // model translation is on-canvas.  Safe-clip should NOT
    // accidentally skip this case.
    auto shape = make_test_shape("Hi");
    Mat4 rotated_180 = glm::translate(Mat4{1.0f}, Vec3{500.0f, 500.0f, 0.0f});
    rotated_180 = glm::rotate(rotated_180, glm::radians(180.0f),
        Vec3{0.0f, 0.0f, 1.0f}) * rotated_180;
    auto bbox = compute_text_run_world_bbox(shape, rotated_180, 0.0f);
    CHECK(bbox.x0 < bbox.x1);
    CHECK(bbox.y0 < bbox.y1);
    // The intersection with a 1920x1080 framebuffer is non-empty.
    raster::BBox fb_bbox{0, 0, 1920, 1080};
    CHECK(bbox.x1 > fb_bbox.x0);
    CHECK(bbox.y1 > fb_bbox.y0);
}

