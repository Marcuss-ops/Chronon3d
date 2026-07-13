// tests/content/test_shaped_glyph_line_cluster_benchmark.cpp
//
// Benchmark: O(n) ShapedGlyphLine::layout() vs. reference O(n²)
// cluster-boundary scan on the 200-glyph stress case.
//
// Run on a working build host with:
//   ctest -R "ShapedGlyphLine cluster benchmark" --output-on-failure
// Or directly:
//   ./build/<preset>/tests/chronon3d_content_tests --test-case="ShapedGlyphLine cluster benchmark"

#include <doctest/doctest.h>

#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <tests/helpers/test_utils.hpp>

#include "content/common/text/glyph_layout.hpp"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::test;
using chronon3d::content::text_reveal::GlyphPos;
using chronon3d::content::text_reveal::ShapedGlyphLine;

// ── Reference O(n²) implementation (verbatim from pre-fix layout()) ───────
static std::vector<GlyphPos> reference_layout_o_n_squared(
    const std::vector<GlyphPosition>& glyphs,
    const std::string& text,
    float tracking,
    float ref_offset_x)
{
    std::vector<GlyphPos> out;
    out.reserve(glyphs.size());

    float cursor = ref_offset_x;
    for (size_t gi = 0; gi < glyphs.size(); ++gi) {
        const auto& g = glyphs[gi];
        size_t start = g.cluster;
        size_t end = text.size();
        for (size_t i = 0; i < glyphs.size(); ++i) {
            const auto& o = glyphs[i];
            if (o.cluster > start) { end = o.cluster; break; }
        }
        std::string ch = text.substr(start, end - start);
        if (ch.empty()) continue;
        out.push_back({ch, cursor + g.advance_x * 0.5f, g.advance_x});
        cursor += g.advance_x + tracking;
    }
    return out;
}

// ── Benchmark: 200-glyph stress case ────────────────────────────────────
TEST_CASE("ShapedGlyphLine cluster benchmark: 200-glyph stress O(n) vs O(n²)") {
    auto renderer = test::make_renderer();
    auto& engine  = renderer.font_engine();

    FontSpec spec{"assets/fonts/Poppins-Regular.ttf", "Poppins", 400};

    // B02 equivalent: 200 glyphs of repeating Latin text.
    std::string text_200;
    text_200.reserve(200);
    const std::string pangram = "THEQUICKBROWNFOXJUMPSOVERTHELAZYDOG";
    while (text_200.size() < 200) text_200 += pangram;
    text_200.resize(200);

    // Shape once and reuse the raw GlyphRun for both implementations.
    auto run_opt = engine.shape_text(text_200, spec, 72.0f);
    REQUIRE(run_opt.has_value());
    REQUIRE(!run_opt->glyphs.empty());

    const auto& glyphs = run_opt->glyphs;
    const size_t n = glyphs.size();
    INFO("glyph count: ", n);
    REQUIRE(n > 0);

    constexpr int iterations = 10000;

    // Warm-up to reduce cache/noise effects.
    for (int i = 0; i < 100; ++i) {
        [[maybe_unused]] auto _ = reference_layout_o_n_squared(glyphs, text_200, 4.0f, 0.0f);
    }

    // Time reference O(n²) implementation.
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        [[maybe_unused]] auto _ = reference_layout_o_n_squared(glyphs, text_200, 4.0f, 0.0f);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    const double ref_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    // Time actual ShapedGlyphLine::layout() (O(n)).
    ShapedGlyphLine line(text_200, 72.0f, spec, 4.0f, 0.0f, engine);
    auto t2 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        [[maybe_unused]] auto _ = line.layout();
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    const double actual_us = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();

    const double speedup = ref_us / actual_us;
    const double ref_per_call_ns   = (ref_us   * 1000.0) / iterations;
    const double actual_per_call_ns = (actual_us * 1000.0) / iterations;

    std::cout << "\n=== ShapedGlyphLine cluster benchmark ===\n"
              << "glyphs: " << n << "\n"
              << "iterations: " << iterations << "\n"
              << "O(n²) reference total: " << ref_us << " us ("
              << ref_per_call_ns << " ns/call)\n"
              << "O(n)  actual    total: " << actual_us << " us ("
              << actual_per_call_ns << " ns/call)\n"
              << "speedup: " << speedup << "x\n"
              << "=========================================\n";

    // The O(n) implementation should be measurably faster.
    // We require at least a 2x speedup on the 200-glyph stress case.
    CHECK(speedup > 2.0);
}
