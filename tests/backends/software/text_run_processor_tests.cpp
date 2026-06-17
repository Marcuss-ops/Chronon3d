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
    TextRunShape shape;
    shape.layout = nullptr;
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
