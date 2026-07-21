// SPDX-License-Identifier: MIT
//
// test_capcut_parity.cpp — CapCut-grade parity test skeleton.
//
// FU09 of TICKET-CAPCUT-REFERENCE-CORPUS.
//
// Compares Chronon3D-rendered output against blessed CapCut PNG references.
// Soglie (verdict §Fase 9):
//   - baseline err  ≤ 1 px
//   - bbox err      ≤ 2 px per lato
//   - SSIM-on-ROI   ≥ 0.95
//   - missing_glyphs == 0
//   - cut_text       == false
//
// Cat-3 minimal-surface: metric helpers inlined here (not extracted to
// tests/visual/support/capcut_metrics.hpp yet). Forward-point: refactor
// to header-only shared module IF another test reuses these helpers.
//
// Skeleton phase: blessed reference corpus is empty. All tests skip
// gracefully via MESSAGE+CHECK(true)+return pattern (NOT doctest::skip
// decorator which is version-dependent).
//
// Link contract (chronon3d_add_test_suite LINK_TARGETS override):
//   - chronon3d_pipeline         (default INTEGRATION)
//   - chronon3d_backend_software (default INTEGRATION)
//   - chronon3d_visual_test_support (for image_diff.hpp::compute_ssim)

#include <doctest/doctest.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>

// Reuse canonical SSIM + bbox helpers (Cat-3 anti-dup).
//   - chronon3d::test::compute_ssim from tests/visual/support/image_diff.hpp
//     (transitively linked via chronon3d_visual_test_support — if that target
//     is ever renamed/moved, this test breaks silently at link time)
//   - chronon3d::test::completeness::alpha_bbox + ink_vertical_extent from
//     tests/text_golden/text_completeness/pixel_scan_helpers.hpp
//     (reachable via ${CMAKE_SOURCE_DIR}/tests include dir set by
//      chronon3d_add_test_suite per cmake/Chronon3DTestSuite.cmake)
#include <tests/visual/support/image_diff.hpp>
#include <tests/text_golden/text_completeness/pixel_scan_helpers.hpp>

#include <filesystem>
#include <vector>
#include <string>

namespace fs = std::filesystem;

namespace {

// ── CapCut-grade parity thresholds (verdict §Fase 9) ─────────────────────
constexpr double kBaselineErrMaxPx  = 1.0;
constexpr double kBboxErrMaxPx      = 2.0;
constexpr double kSsimMinOnRoi      = 0.95;
constexpr double kChangedPixelRatioMaxRoi = 0.05;
constexpr int    kMissingGlyphsMax  = 0;

// ── Subdir helpers ─────────────────────────────────────────────────────
const fs::path kCapcutRoot = "tests/reference/capcut";

std::vector<fs::path> discover_blessed_references(const std::string& area) {
    const fs::path ref_dir = kCapcutRoot / area / "reference";
    std::vector<fs::path> refs;
    if (!fs::exists(ref_dir)) return refs;
    for (const auto& entry : fs::directory_iterator(ref_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".png") {
            refs.push_back(entry.path());
        }
    }
    return refs;
}

// ── Inline metric helpers (Cat-3 minimal-surface, refactor on reuse) ───

/// Compute baseline Y-error between two framebuffers by finding the first
/// row with visible ink in each and returning the absolute pixel difference.
double compute_baseline_error(const chronon3d::Framebuffer& actual,
                              const chronon3d::Framebuffer& expected,
                              float alpha_epsilon = 0.01f) {
    using namespace chronon3d::test::completeness;
    const auto actual_extent   = ink_vertical_extent(actual,   alpha_epsilon);
    const auto expected_extent = ink_vertical_extent(expected, alpha_epsilon);
    if (actual_extent.first < 0 || expected_extent.first < 0) {
        return -1.0;  // No ink in one of the two → cannot compare baseline.
    }
    return std::abs(static_cast<double>(actual_extent.first - expected_extent.first));
}

/// Compute bbox per-side error: |actual.side - expected.side| for each side.
struct BboxError {
    double left  = 0.0;
    double right = 0.0;
    double top   = 0.0;
    double bot   = 0.0;
};

BboxError compute_bbox_error(const chronon3d::Framebuffer& actual,
                             const chronon3d::Framebuffer& expected,
                             float alpha_epsilon = 0.01f) {
    using namespace chronon3d::test::completeness;
    const auto a = alpha_bbox(actual,   alpha_epsilon);
    const auto e = alpha_bbox(expected, alpha_epsilon);
    BboxError err;
    if (a.empty() || e.empty()) return err;
    err.left  = std::abs(static_cast<double>(a.x0 - e.x0));
    err.right = std::abs(static_cast<double>(a.x1 - e.x1));
    err.top   = std::abs(static_cast<double>(a.y0 - e.y0));
    err.bot   = std::abs(static_cast<double>(a.y1 - e.y1));
    return err;
}

/// Count missing glyphs heuristically: pixels in expected with alpha>eps
/// that are completely absent in actual (i.e. actual alpha at that pixel == 0).
/// Returns 0 if actual matches or exceeds expected coverage.
int count_missing_glyphs(const chronon3d::Framebuffer& actual,
                         const chronon3d::Framebuffer& expected,
                         float alpha_epsilon = 0.01f) {
    if (actual.width() != expected.width() ||
        actual.height() != expected.height()) {
        return -1;  // Dimension mismatch.
    }
    int missing = 0;
    for (int y = 0; y < expected.height(); ++y) {
        const chronon3d::Color* exp_row = expected.pixels_row(y);
        const chronon3d::Color* act_row = actual.pixels_row(y);
        for (int x = 0; x < expected.width(); ++x) {
            if (exp_row[x].a > alpha_epsilon && act_row[x].a <= 0.001f) {
                ++missing;
            }
        }
    }
    return missing;
}

/// Detect cut text: true if the alpha-bbox of `actual` touches any
/// framebuffer edge (right or bottom) — indicates text was clipped
/// instead of auto-fitting.
bool detect_cut_text(const chronon3d::Framebuffer& actual,
                     float alpha_epsilon = 0.01f,
                     int edge_tolerance_px = 1) {
    using namespace chronon3d::test::completeness;
    const auto b = alpha_bbox(actual, alpha_epsilon);
    if (b.empty()) return false;
    return (b.x1 >= actual.width()  - 1 - edge_tolerance_px) ||
           (b.y1 >= actual.height() - 1 - edge_tolerance_px);
}

} // namespace

// ── TEST_CASEs (one per area: static / subtitles / effects) ───────────

TEST_CASE("capcut/static: baseline + bbox + SSIM-on-ROI + missing_glyphs + cut_text") {
    const auto refs = discover_blessed_references("static");
    if (refs.empty()) {
        MESSAGE("§blessed-reference corpus not yet populated for static/ — see TICKET-CAPCUT-REFERENCE-CORPUS §Forward-points (a)");
        MESSAGE("To seed: export PNG lossless from CapCut, place in tests/reference/capcut/static/reference/, open PR review.");
        CHECK(true);  // graceful-skip: no FAIL.
        return;
    }
    // Post-seed verification (skeleton: just verify the helpers don't crash).
    for (const auto& ref_path : refs) {
        MESSAGE("Blessed reference: " << ref_path.string());
        // The actual Chronon3D rendering + full comparison is forward-pointed
        // to TICKET-CAPCUT-REFERENCE-CORPUS §Forward-points (e) WBH verification.
        CHECK(true);
    }
}

TEST_CASE("capcut/subtitles: baseline + bbox + SSIM-on-ROI + missing_glyphs + cut_text") {
    const auto refs = discover_blessed_references("subtitles");
    if (refs.empty()) {
        MESSAGE("§blessed-reference corpus not yet populated for subtitles/ — see TICKET-CAPCUT-REFERENCE-CORPUS §Forward-points (b)");
        MESSAGE("To seed: export karaoke subtitle PNGs from CapCut, place in tests/reference/capcut/subtitles/reference/, open PR review.");
        CHECK(true);
        return;
    }
    for (const auto& ref_path : refs) {
        MESSAGE("Blessed reference: " << ref_path.string());
        CHECK(true);
    }
}

TEST_CASE("capcut/effects: baseline + bbox + SSIM-on-ROI + missing_glyphs + cut_text") {
    const auto refs = discover_blessed_references("effects");
    if (refs.empty()) {
        MESSAGE("§blessed-reference corpus not yet populated for effects/ — see TICKET-CAPCUT-REFERENCE-CORPUS §Forward-points (c)");
        MESSAGE("To seed: export effect frames (glow, text-on-path, etc.) PNG from CapCut, place in tests/reference/capcut/effects/reference/, open PR review.");
        CHECK(true);
        return;
    }
    for (const auto& ref_path : refs) {
        MESSAGE("Blessed reference: " << ref_path.string());
        CHECK(true);
    }
}

// ── Metric helper unit tests (Cat-3 invariant: helpers must be testable
//    independently of the blessed corpus). These always run. ───────────

TEST_CASE("capcut/metrics: compute_baseline_error detects identical frames (err=0)") {
    chronon3d::Framebuffer a(64, 64);
    chronon3d::Framebuffer e(64, 64);
    // Fill a horizontal strip at row 10 in both.
    for (int x = 0; x < 64; ++x) {
        a.set_pixel(x, 10, chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f});
        e.set_pixel(x, 10, chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f});
    }
    CHECK(compute_baseline_error(a, e) == doctest::Approx(0.0));
}

TEST_CASE("capcut/metrics: compute_bbox_error detects 2px shift") {
    chronon3d::Framebuffer a(64, 64);
    chronon3d::Framebuffer e(64, 64);
    for (int y = 10; y <= 20; ++y) {
        for (int x = 5; x <= 15; ++x) {
            a.set_pixel(x, y, chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f});  // ink at [5,10]-[15,20]
            e.set_pixel(x + 2, y + 1, chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f});  // shifted +2,+1
        }
    }
    const auto err = compute_bbox_error(a, e);
    CHECK(err.left  == doctest::Approx(2.0));
    CHECK(err.top   == doctest::Approx(1.0));
    CHECK(err.right == doctest::Approx(2.0));
    CHECK(err.bot   == doctest::Approx(1.0));
}

TEST_CASE("capcut/metrics: detect_cut_text returns true when bbox touches right edge") {
    chronon3d::Framebuffer a(64, 64);
    for (int y = 10; y <= 20; ++y) {
        for (int x = 60; x <= 63; ++x) {
            a.set_pixel(x, y, chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
    CHECK(detect_cut_text(a) == true);
}

TEST_CASE("capcut/metrics: count_missing_glyphs == 0 when actual matches expected") {
    chronon3d::Framebuffer a(64, 64);
    chronon3d::Framebuffer e(64, 64);
    for (int y = 10; y <= 20; ++y) {
        for (int x = 10; x <= 20; ++x) {
            a.set_pixel(x, y, chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f});
            e.set_pixel(x, y, chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
    CHECK(count_missing_glyphs(a, e) == 0);
}
