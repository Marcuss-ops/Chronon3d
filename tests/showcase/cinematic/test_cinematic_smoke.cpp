// ═══════════════════════════════════════════════════════════════════════════
//  test_cinematic_smoke.cpp — AGENT 4 / TICKET-A4 (gates A4.1 + A4.2)
//
//  Phase-2.2 mechanical split.  Verbatim extraction of:
//
//    • TEST_CASE("AGENT4: A4.1 every keyframe non-empty")
//    • TEST_CASE("AGENT4: A4.2 camera motion across keyframes")
//
//  from the monolithic tests/showcase/test_cinematic_camera_showcase.cpp  // drift-allow: stale-ref
//  (was 771 LOC).  Behaviour preserved bit-identical: same hash, same
//  REQUIRE/CHECK ordering, same MESSAGE forensic lines, same
//  smoke-mode-tolerant motion-bound threshold (12/15 in full mode;
//  1 in smoke).  Helper code is now sourced from
//  cinematic_showcase_fixture.{hpp,cpp} + config.hpp.
//
//  A4.1 / A4.2 are the daily-on-push smoke gates: no PNG emission,
//  no telemetry envelope; they run on a 2-frame / 1-comp slice whenever
//  CHRONON3D_CINEMATIC_FRAME_COUNT and/or CHRONON3D_CINEMATIC_COMP_COUNT
//  are reduced (verified nightly by the full 6/5 default).
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include "cinematic_showcase_fixture.hpp"

// Exhibit the cinematic_text_camera compositions directly (no registry hop
// required for these).  Phase-2.2-fix: the umbrella header lives at
// content/showcases/cinematic/cinematic_text_camera.hpp (the old
// content/anims/compositions/ path was deleted by the 24388800
// content/ directory restructuring commit; only the showcase
// sub-directory survived).
#include "content/showcases/cinematic/cinematic_text_camera.hpp"

using namespace chronon3d;
using namespace chronon3d::testing::cinematic;
using chronon3d::test::make_renderer;
using namespace chronon3d::content::anims;

// ─────────────────────────────────────────────────────────────────────
//  A4.1 — Every key frame is non-empty: visible ink + valid alpha + no NaN.
// ─────────────────────────────────────────────────────────────────────
TEST_CASE("AGENT4: A4.1 every keyframe non-empty") {
    const auto& kfs = runtime_kf();
    const int kf_count = static_cast<int>(kfs.size());
    REQUIRE(kf_count >= 1);

    auto renderer = make_renderer();
    const auto comp = deep_parallax_cascade();
    const auto cache = render_frames(renderer, comp);

    int non_empty = 0;
    for (int f : kfs) {
        const auto& m = cache.at(f).first;

        // Compose a single MESSAGE row for forensic tracing.  Cheap; printed
        // by doctest only on failure / `-DCTEST_VERBOSE`.
        MESSAGE(stamped(f)
                << " hash=" << hash_to_hex(m.hash)
                << " ink_px=" << m.ink_pixels
                << " alpha_cov=" << m.alpha_coverage
                << " mean_lum=" << m.mean_luminance
                << " render_ms=" << m.render_ms);

        // Frames at the boundary of the composition (F179 is post-hero
        // fade) reduce to backdrop-only ink — still has SOME ink because
        // bg_halo is always rendered.  We assert ink_pixels > 0 AND no NaN
        // in any of the four scalar telemetry fields.
        CHECK(m.ink_pixels > 0);
        CHECK(m.alpha_coverage > 0.0f);
        CHECK_FALSE(std::isnan(m.mean_luminance));
        CHECK_FALSE(std::isnan(m.alpha_coverage));
        CHECK_FALSE(std::isnan(m.render_ms));
        CHECK_FALSE(std::isinf(m.mean_luminance));
        CHECK_FALSE(std::isinf(m.alpha_coverage));

        if (m.ink_pixels > 0) ++non_empty;
    }
    CHECK(non_empty == kf_count);
    MESSAGE("A4.1 OK — " << non_empty << "/" << kf_count
            << " keyframes have visible ink + valid alpha");
}

// ─────────────────────────────────────────────────────────────────────
//  A4.2 — Camera motion: hashes between frames span multiple distinct
//          values, validating real motion across the timeline.  The
//          threshold is kf_count-agnostic: at minimum one distinct pair
//          (smoke) or 12-of-15 (full, empirical lower bound that
//          tolerates bg_halo + held-key collisions).
// ─────────────────────────────────────────────────────────────────────
TEST_CASE("AGENT4: A4.2 camera motion across keyframes") {
    const auto& kfs = runtime_kf();
    const int kf_count = static_cast<int>(kfs.size());
    REQUIRE(kf_count >= 2);  // motion needs ≥2 frames

    auto renderer = make_renderer();
    const auto comp = deep_parallax_cascade();
    const auto cache = render_frames(renderer, comp);

    // Smoke-grade invariant: first vs second frame must differ.
    const auto h_first  = cache.at(kfs[0]).first.hash;
    const auto h_second = cache.at(kfs[1]).first.hash;
    CHECK(h_first != h_second);

    // Pairwise-distinct counting (informational).
    int diffs = 0;
    const int total_pairs = kf_count * (kf_count - 1) / 2;
    for (int i = 0; i < kf_count; ++i) {
        for (int j = i + 1; j < kf_count; ++j) {
            if (cache.at(kfs[i]).first.hash != cache.at(kfs[j]).first.hash) {
                ++diffs;
            }
        }
    }
    const int motion_lower_bound = (kf_count >= 6) ? 12 : 1;
    CHECK(diffs >= motion_lower_bound);
    MESSAGE("A4.2 OK — pairwise-distinct keyframes: "
            << diffs << " / " << total_pairs
            << " (F[0] hash=" << hash_to_hex(h_first)
            << " F[1] hash=" << hash_to_hex(h_second) << ")");
}
