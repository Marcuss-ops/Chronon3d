// SPDX-License-Identifier: MIT
//
// tests/cli/test_render_job_write_frame_sanity.cpp
//
// TICKET-RENDER-PIPELINE-INTEGRITY — layer 1 unit test.
//
// Locks the new pre-write sanity contract:
//   * `scan_framebuffer_sanity` rejects a framebuffer whose sampled
//     alpha-zero fraction exceeds the threshold (CODE-REVIEW M2 fix
//     raised it from 0.60 to 0.85 so legitimate minimalist fade-in
//     animations are not falsely flagged as empty).
//   * `scan_framebuffer_sanity` rejects a framebuffer whose sampled
//     NaN/Inf fraction exceeds the (unchanged) 0.05 threshold.
//   * A clean framebuffer (solid color) is accepted.
//
// The test exercises the header-only inline implementation in
// render_job_write_frame_sanity.hpp via the chronon3d_cli_render
// link target which has the .cpp file as a source.

#include <doctest/doctest.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>

// TICKET-RENDER-PIPELINE-INTEGRITY: include the public testability surface
// (framebuffer sanity types + scan function).  The .cpp file
// `render_job_write_frame.cpp` also includes this same header; the
// function is `inline` so ODR is satisfied.
//
// The cli_tests target adds `${CMAKE_SOURCE_DIR}/apps/chronon3d_cli` to
// the include path (see tests/cli_tests.cmake), so this include must
// use the `utils/job/` subpath to resolve to
// `apps/chronon3d_cli/utils/job/render_job_write_frame_sanity.hpp`.
#include "utils/job/render_job_write_frame_sanity.hpp"

#include <cmath>
#include <limits>

using namespace chronon3d;
using chronon3d::cli::scan_framebuffer_sanity;
using chronon3d::cli::FramebufferSanityReport;
using chronon3d::cli::kAlphaZeroRejectFraction;
using chronon3d::cli::kNanInfRejectFraction;
using chronon3d::cli::kSanityScanStride;

namespace {

// Make a 4x4 framebuffer where every pixel is the same color.  Helper
// for control cases.  With kSanityScanStride=4 and 4x4, only (0,0) is
// sampled — so 100% of the sampled pixels are the same color.
Framebuffer solid_framebuffer(const Color& c) {
    Framebuffer fb(4, 4);
    fb.clear(c);
    return fb;
}

// Poison a single pixel with NaN/Inf.  Mirrors the test helper in
// test_image_writer_throw.cpp.
void poison_pixel(Framebuffer& fb, int x, int y, float r, float g, float b, float a) {
    Color c;
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    fb.set_pixel(x, y, c);
}

} // namespace

TEST_CASE("render_job_write_frame::sanity: clean solid-color framebuffer is accepted") {
    // Control case — solid red, alpha=1.0 throughout.  Should be ok=true
    // and produce non-zero sample counts with no bad pixels.
    auto fb = solid_framebuffer(Color::red());

    const auto r = scan_framebuffer_sanity(fb);

    CHECK(r.ok);
    CHECK(r.sampled_pixels    > 0);
    CHECK(r.alpha_zero_pixels == 0);
    CHECK(r.nan_or_inf_pixels == 0);
    CHECK(r.first_bad_x       == -1);
    CHECK(r.first_bad_y       == -1);
}

TEST_CASE("render_job_write_frame::sanity: fully transparent framebuffer is rejected (alpha-zero > 0.85)") {
    // All pixels alpha=0.0.  Sampled: 100% alpha-zero > 0.85 threshold.
    // Reject.
    auto fb = solid_framebuffer(Color::transparent());

    const auto r = scan_framebuffer_sanity(fb);

    CHECK_FALSE(r.ok);
    CHECK(r.sampled_pixels    > 0);
    CHECK(r.alpha_zero_pixels == r.sampled_pixels);   // 100% alpha-zero
    CHECK(r.nan_or_inf_pixels == 0);
}

TEST_CASE("render_job_write_frame::sanity: NaN pixel in a 4x4 framebuffer is rejected (NaN/Inf > 0.05)") {
    // With kSanityScanStride=4, a 4x4 framebuffer has only (0,0) sampled.
    // Poisoning (0,0) with NaN gives 100% NaN fraction, far above the
    // 5% threshold.  Reject.
    auto fb = solid_framebuffer(Color::red());
    poison_pixel(fb, 0, 0,
                 std::numeric_limits<float>::quiet_NaN(),
                 0.0f, 0.0f, 1.0f);

    const auto r = scan_framebuffer_sanity(fb);

    CHECK_FALSE(r.ok);
    CHECK(r.sampled_pixels    > 0);
    CHECK(r.nan_or_inf_pixels > 0);
    CHECK(r.first_bad_x       == 0);
    CHECK(r.first_bad_y       == 0);
}

TEST_CASE("render_job_write_frame::sanity: Inf pixel in a 4x4 framebuffer is rejected (NaN/Inf > 0.05)") {
    // Same shape as the NaN test, but with Inf.  Both should be caught by
    // the same code path (std::isnan || std::isinf).
    auto fb = solid_framebuffer(Color::red());
    poison_pixel(fb, 0, 0,
                 std::numeric_limits<float>::infinity(),
                 0.0f, 0.0f, 1.0f);

    const auto r = scan_framebuffer_sanity(fb);

    CHECK_FALSE(r.ok);
    CHECK(r.nan_or_inf_pixels > 0);
}

TEST_CASE("render_job_write_frame::sanity: thresholds are correct (regression lock for M2 fix)") {
    // CODE-REVIEW M2 raised the alpha-zero threshold from 0.60 to 0.85
    // to avoid false-rejecting legitimate minimalist fade-in animations.
    // This test locks the constant value so a future change is caught
    // by the test corpus and not by surprise regressions.
    CHECK(kAlphaZeroRejectFraction == doctest::Approx(0.85f));
    CHECK(kNanInfRejectFraction    == doctest::Approx(0.05f));
    CHECK(kSanityScanStride        == 4);
}

TEST_CASE("render_job_write_frame::sanity: empty framebuffer (0x0) returns ok=true (no scan performed)") {
    // Defensive: the upstream null-check path is the single source of
    // truth for "no framebuffer at all".  An empty framebuffer (0x0)
    // should pass the sanity scan trivially so callers that pre-allocate
    // but don't yet have geometry don't trip the guard.
    Framebuffer fb(0, 0);
    const auto r = scan_framebuffer_sanity(fb);
    CHECK(r.ok);
    CHECK(r.sampled_pixels == 0);
}
