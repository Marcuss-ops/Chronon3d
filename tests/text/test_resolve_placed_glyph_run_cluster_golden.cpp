// tests/text/test_resolve_placed_glyph_run_cluster_golden.cpp
//
// Golden equivalence test for resolve_placed_glyph_run() cluster
// aggregation logic.
//
// The optimized implementation in src/backends/text/font_engine_placed_run.cpp
// replaces the O(n*k) nested cluster scan with a single O(n) hash-map
// aggregation. This test verifies the optimized output is byte-for-byte
// identical to the reference O(n*k) implementation.

#include <doctest/doctest.h>

#include <chronon3d/text/font_engine.hpp>

#include <cstdint>
#include <string>
#include <vector>

using namespace chronon3d;

// ── Reference O(n*k) implementation (verbatim from pre-fix code) ────────
//
// This replicates the original nested loop that scanned all glyphs for
// each unique cluster. It is the golden baseline.
static PlacedGlyphRun reference_resolve(const GlyphRun& hb_run,
                                        float tracking,
                                        std::string_view source_text)
{
    PlacedGlyphRun result;
    result.ascent = hb_run.ascent;
    result.descent = hb_run.descent;
    result.baseline = hb_run.baseline;
    result.font_size = hb_run.font_size;

    if (hb_run.glyphs.empty()) return result;

    float pen_x = 0.0f;
    float pen_y = 0.0f;
    result.glyphs.reserve(hb_run.glyphs.size());

    for (size_t i = 0; i < hb_run.glyphs.size(); ++i) {
        const auto& g = hb_run.glyphs[i];
        PlacedGlyph pg;
        pg.glyph_id = g.glyph_id;
        pg.x_offset = g.x_offset;
        pg.y_offset = g.y_offset;
        pg.is_cluster_start = g.is_cluster_start;
        pg.cluster = g.cluster;

        pg.x = pen_x + g.x_offset;
        pg.y = pen_y + g.y_offset;

        pg.raw_advance_x = g.advance_x;
        pg.advance_x = g.advance_x;
        pg.advance_y = g.advance_y;

        if (tracking != 0.0f && i + 1 < hb_run.glyphs.size()) {
            if (hb_run.glyphs[i + 1].is_cluster_start) {
                pg.advance_x += tracking;
            }
        }

        result.glyphs.push_back(pg);
        pen_x += pg.advance_x;
        pen_y += g.advance_y;
    }

    result.total_width = pen_x;
    result.total_height = hb_run.ascent + hb_run.descent;

    if (!source_text.empty()) {
        std::vector<u32> sorted_clusters;
        sorted_clusters.reserve(hb_run.glyphs.size() + 1);
        for (const auto& g : hb_run.glyphs) {
            sorted_clusters.push_back(g.cluster);
        }
        sorted_clusters.push_back(static_cast<u32>(source_text.size()));

        std::sort(sorted_clusters.begin(), sorted_clusters.end());
        sorted_clusters.erase(std::unique(sorted_clusters.begin(), sorted_clusters.end()),
                              sorted_clusters.end());

        result.clusters.reserve(sorted_clusters.size() - 1);
        for (size_t k = 0; k + 1 < sorted_clusters.size(); ++k) {
            const u32 c = sorted_clusters[k];
            const size_t start_byte = c;
            const size_t end_byte = sorted_clusters[k + 1];

            if (end_byte <= start_byte || start_byte >= source_text.size()) continue;

            PlacedGlyphRun::Cluster cl;
            cl.byte_offset = start_byte;
            cl.byte_len = end_byte - start_byte;

            for (size_t gi = 0; gi < result.glyphs.size(); ++gi) {
                if (result.glyphs[gi].cluster == c) {
                    if (cl.start_glyph == 0 && cl.end_glyph == 0) {
                        cl.start_glyph = gi;
                    }
                    cl.end_glyph = gi + 1;
                    cl.advance += result.glyphs[gi].advance_x;
                    cl.raw_advance += result.glyphs[gi].raw_advance_x;
                }
            }

            result.clusters.push_back(cl);

            for (size_t gi = cl.start_glyph; gi < cl.end_glyph; ++gi) {
                if (gi < result.glyphs.size()) {
                    result.glyphs[gi].byte_offset = cl.byte_offset;
                    result.glyphs[gi].byte_len = cl.byte_len;
                }
            }
        }
    }

    return result;
}

// ── Comparison helper ───────────────────────────────────────────────────
static void check_placed_run_equivalence(const PlacedGlyphRun& actual,
                                       const PlacedGlyphRun& reference,
                                       const std::string& label)
{
    INFO("Test category: ", label);

    CHECK(actual.ascent == reference.ascent);
    CHECK(actual.descent == reference.descent);
    CHECK(actual.baseline == reference.baseline);
    CHECK(actual.font_size == reference.font_size);
    CHECK(actual.total_width == reference.total_width);
    CHECK(actual.total_height == reference.total_height);
    REQUIRE(actual.glyphs.size() == reference.glyphs.size());
    REQUIRE(actual.clusters.size() == reference.clusters.size());

    for (size_t i = 0; i < actual.glyphs.size(); ++i) {
        const auto& a = actual.glyphs[i];
        const auto& r = reference.glyphs[i];
        CHECK(a.glyph_id == r.glyph_id);
        CHECK(a.cluster == r.cluster);
        CHECK(a.byte_offset == r.byte_offset);
        CHECK(a.byte_len == r.byte_len);
        CHECK(a.advance_x == r.advance_x);
        CHECK(a.raw_advance_x == r.raw_advance_x);
        CHECK(a.x == r.x);
        CHECK(a.y == r.y);
    }

    for (size_t i = 0; i < actual.clusters.size(); ++i) {
        const auto& a = actual.clusters[i];
        const auto& r = reference.clusters[i];
        CHECK(a.start_glyph == r.start_glyph);
        CHECK(a.end_glyph == r.end_glyph);
        CHECK(a.byte_offset == r.byte_offset);
        CHECK(a.byte_len == r.byte_len);
        CHECK(a.advance == r.advance);
        CHECK(a.raw_advance == r.raw_advance);
    }
}

// ── Synthetic GlyphRun builders ───────────────────────────────────────────
static GlyphRun make_run(const std::vector<u32>& clusters,
                         const std::vector<float>& advances)
{
    GlyphRun run;
    run.font_size = 16.0f;
    run.ascent = 16.0f;
    run.descent = 4.0f;
    run.baseline = 0.0f;
    run.line_height = 20.0f;

    for (size_t i = 0; i < clusters.size(); ++i) {
        GlyphPosition g;
        g.glyph_id = static_cast<u32>(i);
        g.cluster = clusters[i];
        g.is_cluster_start = true;
        g.advance_x = advances[i];
        g.advance_y = 0.0f;
        g.x_offset = 0.0f;
        g.y_offset = 0.0f;
        run.glyphs.push_back(g);
    }

    run.width = 0.0f;
    for (float a : advances) run.width += a;
    return run;
}

// ═════════════════════════════════════════════════════════════════════════
// Test 1: ASCII — one glyph per cluster, monotonic LTR
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("resolve_placed_glyph_run cluster golden: ASCII LTR") {
    const std::string text = "Hello";
    std::vector<u32> clusters(text.size());
    std::vector<float> advances(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        clusters[i] = static_cast<u32>(i);
        advances[i] = 10.0f;
    }
    auto run = make_run(clusters, advances);

    auto actual = resolve_placed_glyph_run(run, 0.0f, text);
    auto reference = reference_resolve(run, 0.0f, text);
    check_placed_run_equivalence(actual, reference, "ASCII LTR");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 2: RTL — clusters monotonically decreasing
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("resolve_placed_glyph_run cluster golden: RTL") {
    const std::string text = "مرحبا";
    std::vector<u32> clusters(text.size());
    std::vector<float> advances(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        clusters[i] = static_cast<u32>(text.size() - 1 - i);
        advances[i] = 10.0f;
    }
    auto run = make_run(clusters, advances);

    auto actual = resolve_placed_glyph_run(run, 0.0f, text);
    auto reference = reference_resolve(run, 0.0f, text);
    check_placed_run_equivalence(actual, reference, "RTL");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 3: Ligature — multiple source chars share one cluster
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("resolve_placed_glyph_run cluster golden: ligature") {
    const std::string text = "ffi ";
    // "ffi" maps to one ligature glyph with cluster=0, then space at cluster=3.
    std::vector<u32> clusters = {0, 3};
    std::vector<float> advances = {30.0f, 10.0f};
    auto run = make_run(clusters, advances);

    auto actual = resolve_placed_glyph_run(run, 0.0f, text);
    auto reference = reference_resolve(run, 0.0f, text);
    check_placed_run_equivalence(actual, reference, "Ligature");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 4: Mixed / duplicate clusters
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("resolve_placed_glyph_run cluster golden: mixed duplicate clusters") {
    const std::string text = "AaBbCc";
    // A and a share cluster 0, B and b share cluster 2, C and c share cluster 4.
    std::vector<u32> clusters = {0, 0, 2, 2, 4, 4};
    std::vector<float> advances = {10.0f, 10.0f, 12.0f, 12.0f, 14.0f, 14.0f};
    auto run = make_run(clusters, advances);

    auto actual = resolve_placed_glyph_run(run, 0.0f, text);
    auto reference = reference_resolve(run, 0.0f, text);
    check_placed_run_equivalence(actual, reference, "Mixed duplicate clusters");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 5: Empty source text
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("resolve_placed_glyph_run cluster golden: empty source text") {
    const std::string text;
    std::vector<u32> clusters = {0, 1, 2};
    std::vector<float> advances = {10.0f, 10.0f, 10.0f};
    auto run = make_run(clusters, advances);

    auto actual = resolve_placed_glyph_run(run, 0.0f, text);
    auto reference = reference_resolve(run, 0.0f, text);
    check_placed_run_equivalence(actual, reference, "Empty source text");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 6: Empty glyphs
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("resolve_placed_glyph_run cluster golden: empty glyphs") {
    const std::string text = "Hello";
    GlyphRun run;
    run.font_size = 16.0f;
    run.ascent = 16.0f;
    run.descent = 4.0f;
    run.baseline = 0.0f;
    run.line_height = 20.0f;
    run.width = 0.0f;

    auto actual = resolve_placed_glyph_run(run, 0.0f, text);
    auto reference = reference_resolve(run, 0.0f, text);
    check_placed_run_equivalence(actual, reference, "Empty glyphs");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 7: Non-zero tracking
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("resolve_placed_glyph_run cluster golden: non-zero tracking") {
    const std::string text = "ABCD";
    std::vector<u32> clusters(text.size());
    std::vector<float> advances(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        clusters[i] = static_cast<u32>(i);
        advances[i] = 10.0f;
    }
    auto run = make_run(clusters, advances);

    auto actual = resolve_placed_glyph_run(run, 5.0f, text);
    auto reference = reference_resolve(run, 5.0f, text);
    check_placed_run_equivalence(actual, reference, "Non-zero tracking");
}

// ═════════════════════════════════════════════════════════════════════════
// Test 8: Non-zero tracking with shared clusters (ligature + tracking)
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("resolve_placed_glyph_run cluster golden: tracking with shared clusters") {
    const std::string text = "ffi ";
    // "ffi" maps to one ligature glyph with cluster=0, then space at cluster=3.
    std::vector<u32> clusters = {0, 3};
    std::vector<float> advances = {30.0f, 10.0f};
    auto run = make_run(clusters, advances);

    auto actual = resolve_placed_glyph_run(run, 7.0f, text);
    auto reference = reference_resolve(run, 7.0f, text);
    check_placed_run_equivalence(actual, reference, "Tracking with shared clusters");
}
