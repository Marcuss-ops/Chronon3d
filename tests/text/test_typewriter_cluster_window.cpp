// tests/text/test_typewriter_cluster_window.cpp
//
// Unit test for the O(chars + clusters) cluster-window helper used by
// typewriter_build. Verifies that the two-pointer scan produces the same
// overlapping cluster range as the brute-force scan.

#include <doctest/doctest.h>

#include <content/text/text_helpers_typewriter.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <utility>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::content::text;

// ── Synthetic cluster builder ───────────────────────────────────────────────
static std::vector<PlacedGlyphRun::Cluster> make_clusters(
    const std::vector<std::pair<size_t, size_t>>& ranges)
{
    std::vector<PlacedGlyphRun::Cluster> clusters;
    clusters.reserve(ranges.size());
    for (const auto& [off, len] : ranges) {
        PlacedGlyphRun::Cluster cl;
        cl.byte_offset = off;
        cl.byte_len = len;
        cl.start_glyph = 0;
        cl.end_glyph = 0;
        cl.advance = 0.0f;
        cl.raw_advance = 0.0f;
        clusters.push_back(cl);
    }
    return clusters;
}

// ── Reference brute-force scan: returns the list of overlapping cluster indices
static std::vector<size_t> reference_overlapping_clusters(
    const std::vector<PlacedGlyphRun::Cluster>& clusters,
    size_t char_start,
    size_t char_end)
{
    std::vector<size_t> result;
    for (size_t ci = 0; ci < clusters.size(); ++ci) {
        const auto& cl = clusters[ci];
        const size_t cl_start = cl.byte_offset;
        const size_t cl_end = cl.byte_offset + cl.byte_len;
        if (cl_start < char_end && cl_end > char_start) {
            result.push_back(ci);
        }
    }
    return result;
}

// ── Test: LTR one-to-one mapping ──────────────────────────────────────────
TEST_CASE("typewriter cluster window: LTR one-to-one") {
    auto clusters = make_clusters({{0, 1}, {1, 1}, {2, 1}, {3, 1}});
    size_t first_cl = 0;
    size_t end_cl = 0;

    detail::advance_cluster_window(clusters, 0, 1, first_cl, end_cl);
    CHECK(first_cl == 0);
    CHECK(end_cl == 1);

    detail::advance_cluster_window(clusters, 1, 2, first_cl, end_cl);
    CHECK(first_cl == 1);
    CHECK(end_cl == 2);

    detail::advance_cluster_window(clusters, 2, 3, first_cl, end_cl);
    CHECK(first_cl == 2);
    CHECK(end_cl == 3);

    detail::advance_cluster_window(clusters, 3, 4, first_cl, end_cl);
    CHECK(first_cl == 3);
    CHECK(end_cl == 4);
}

// ── Test: ligature spans multiple source bytes ────────────────────────────
TEST_CASE("typewriter cluster window: ligature spans multiple bytes") {
    // Cluster 0 covers bytes [0, 3), cluster 1 covers [3, 1).
    auto clusters = make_clusters({{0, 3}, {3, 1}});
    size_t first_cl = 0;
    size_t end_cl = 0;

    detail::advance_cluster_window(clusters, 0, 1, first_cl, end_cl);
    CHECK(first_cl == 0);
    CHECK(end_cl == 1);

    detail::advance_cluster_window(clusters, 2, 3, first_cl, end_cl);
    CHECK(first_cl == 0);
    CHECK(end_cl == 1);

    detail::advance_cluster_window(clusters, 3, 4, first_cl, end_cl);
    CHECK(first_cl == 1);
    CHECK(end_cl == 2);
}

// ── Test: empty overlap between characters and clusters ──────────────────
TEST_CASE("typewriter cluster window: empty overlap") {
    auto clusters = make_clusters({{0, 1}, {2, 1}});
    size_t first_cl = 0;
    size_t end_cl = 0;

    detail::advance_cluster_window(clusters, 1, 2, first_cl, end_cl);
    CHECK(first_cl == 1);
    CHECK(end_cl == 1);
}

// ── Test: golden equivalence against brute-force over monotonic sequence ───
TEST_CASE("typewriter cluster window: golden equivalence vs brute-force") {
    auto clusters = make_clusters({{0, 2}, {2, 3}, {5, 1}, {6, 4}});
    size_t first_cl = 0;
    size_t end_cl = 0;

    // Simulate contiguous characters that partition [0, 10).
    std::vector<std::pair<size_t, size_t>> chars = {
        {0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5},
        {5, 6}, {6, 7}, {7, 8}, {8, 9}, {9, 10}
    };

    for (const auto& [char_start, char_end] : chars) {
        detail::advance_cluster_window(clusters, char_start, char_end,
                                      first_cl, end_cl);

        auto ref = reference_overlapping_clusters(clusters, char_start, char_end);
        if (ref.empty()) {
            CHECK(first_cl == end_cl);
        } else {
            CHECK(first_cl == ref.front());
            CHECK(end_cl == ref.back() + 1);
        }
    }
}

// ── Test: variable-length characters still monotonic ───────────────────────
TEST_CASE("typewriter cluster window: monotonic variable-length characters") {
    auto clusters = make_clusters({{0, 2}, {2, 3}, {5, 1}, {6, 4}});
    size_t first_cl = 0;
    size_t end_cl = 0;

    std::vector<std::pair<size_t, size_t>> chars = {
        {0, 2}, {2, 4}, {4, 5}, {5, 7}, {7, 10}
    };

    for (const auto& [char_start, char_end] : chars) {
        detail::advance_cluster_window(clusters, char_start, char_end,
                                      first_cl, end_cl);

        auto ref = reference_overlapping_clusters(clusters, char_start, char_end);
        if (ref.empty()) {
            CHECK(first_cl == end_cl);
        } else {
            CHECK(first_cl == ref.front());
            CHECK(end_cl == ref.back() + 1);
        }
    }
}
