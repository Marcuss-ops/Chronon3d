// ═══════════════════════════════════════════════════════════════════════════
//  test_cinematic_determinism.cpp — AGENT 4 / TICKET-A4 (gate A4.4)
//
//  Phase-2.2 mechanical split.  Verbatim extraction of:
//
//    • TEST_CASE("AGENT4: A4.4 determinism run A == run B")
//
//  from the monolithic tests/showcase/test_cinematic_camera_showcase.cpp  // drift-allow: stale-ref
//  (was 771 LOC).  Behaviour preserved bit-identical: same dual-render
//  loop pattern (`run_a / run_b` pairwise compare on `runtime_kf()`),
//  same `matches_a == kf_count` bookkeeping, same MESSAGE forensic
//  lines for both runs.
//
//  Smoke = 2 frames; full = 6 frames.  Per Agent 2 ci-showcase plan
//  Step 2/6, both runs go through the same `render_frames()` helper
//  (definition lives in cinematic_showcase_fixture.cpp) so the
//  timer's start/end wall-clock scan is identical bit-for-bit.
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

using namespace chronon3d::testing::cinematic;

// ─────────────────────────────────────────────────────────────────────
//  A4.4 — Determinism: re-render and assert byte-exact hashes.
//          Smoke = 2 frames; full = 6 frames.
// ─────────────────────────────────────────────────────────────────────
TEST_CASE("AGENT4: A4.4 determinism run A == run B") {
    const auto& kfs = runtime_kf();
    const int kf_count = static_cast<int>(kfs.size());
    REQUIRE(kf_count >= 2);

    auto renderer = make_renderer();
    const auto comp = deep_parallax_cascade();

    const auto run_a = render_frames(renderer, comp);
    int matches_a = 0;
    for (int f : kfs) {
        const auto h = run_a.at(f).first.hash;
        MESSAGE("run A " << stamped(f) << " hash=" << hash_to_hex(h));
        ++matches_a;  // bookkeeping only
    }

    const auto run_b = render_frames(renderer, comp);
    int matches_ab = 0;
    for (int f : kfs) {
        const auto h_a = run_a.at(f).first.hash;
        const auto h_b = run_b.at(f).first.hash;
        MESSAGE("run B " << stamped(f) << " hash=" << hash_to_hex(h_b));
        CHECK(h_a == h_b);
        ++matches_ab;
    }
    CHECK(matches_ab == kf_count);
    CHECK(matches_a   == kf_count);
    MESSAGE("A4.4 OK — " << matches_ab << "/" << kf_count
            << " keyframes are bit-exact-identical across runs");
}
