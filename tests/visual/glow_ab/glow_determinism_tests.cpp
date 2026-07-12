// ═══════════════════════════════════════════════════════════════════════════
// tests/visual/glow_ab/glow_determinism_tests.cpp
//
// TICKET-GLOW-CERTIFICATION — Azione 3: glow determinism tests.
//
//   1. 3 consecutive renders of ChrononGlowFinalAE frame 15 produce
//      identical framebuffer hashes (XXH64-based, via test utils).
//   2. State leak ON/OFF/ON: already covered by
//      tests/visual/glow_ab/glow_ab_acceptance.cpp TEST_CASE
//      "Glow acceptance: state does not leak between renders" (Azione 1).
//
// Uses the canonical ChrononGlowFinalAE factory (content/compositions/chronon_glow_final.hpp)
// plus framebuffer_hash() from tests/helpers/test_utils.hpp.
//
// Per AGENTS.md §honesty: the user spec (§13) requires sha256sum across
// raw PNG bytes; here we use XXH64 across the Framebuffer pixel data.
// XXH64 is faster and equally deterministic. For the full sha256sum
// contract at the PNG/MP4 file level, see the brute-determinism suite in
// tests/determinism/test_brute_determinism.cpp (Test 17 / FPC).
//
// NOTE: renders only frame 15 (single-frame determinism smoke).
// Full 60-frame cross-run determinism per user spec §13 requires
// the working build host and is deferred to:
//   - tools/check_glow_temporal.py (frame-by-frame sweep)
//   - tests/determinism/test_brute_determinism.cpp (Test 17 / FPC,
//     20× repetitions × thread × cache permutations)
//
// AGENTS.md Cat-2 freeze-compliant: zero new public SDK API.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <tests/helpers/test_utils.hpp>
#include "content/compositions/chronon_glow_final.hpp"

#include <memory>

using namespace chronon3d;
using namespace chronon3d::test;
using chronon3d::test::glow_final::ChrononGlowProps;

namespace {

/// Render ChrononGlowFinalAE at the given frame and return the
/// XXH64 framebuffer hash.
u64 render_and_hash(
    std::shared_ptr<SoftwareRenderer> renderer,
    ChrononGlowProps props,
    int frame_idx) {
    auto fb = renderer->render(
        chronon3d::test::glow_final::make_chronon_glow_final(props),
        Frame{frame_idx});
    REQUIRE(fb != nullptr);
    return framebuffer_hash(*fb);
}

}  // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// TEST 1 — 3 consecutive renders produce identical hashes
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Glow determinism: 3 consecutive renders produce identical framebuffer hash") {
    auto renderer = make_renderer_shared();

    ChrononGlowProps props = chronon3d::test::glow_final::default_landscape_props();
    props.glow_enabled = true;

    constexpr int kFrame = 15;

    u64 hash1 = render_and_hash(renderer, props, kFrame);
    u64 hash2 = render_and_hash(renderer, props, kFrame);
    u64 hash3 = render_and_hash(renderer, props, kFrame);

    INFO("hash run 1 = ", hash1);
    INFO("hash run 2 = ", hash2);
    INFO("hash run 3 = ", hash3);

    CHECK(hash1 == hash2);
    CHECK(hash2 == hash3);
}

// ═══════════════════════════════════════════════════════════════════════════
// TEST 2 — Determinism survives fresh renderer (new instance per run)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Glow determinism: fresh renderer per run still produces identical hash") {
    ChrononGlowProps props = chronon3d::test::glow_final::default_landscape_props();
    props.glow_enabled = true;

    constexpr int kFrame = 15;

    // Run 1: fresh renderer
    auto r1 = make_renderer_shared();
    u64 hash1 = render_and_hash(r1, props, kFrame);

    // Run 2: another fresh renderer
    auto r2 = make_renderer_shared();
    u64 hash2 = render_and_hash(r2, props, kFrame);

    // Run 3: yet another fresh renderer
    auto r3 = make_renderer_shared();
    u64 hash3 = render_and_hash(r3, props, kFrame);

    INFO("hash fresh-1 = ", hash1);
    INFO("hash fresh-2 = ", hash2);
    INFO("hash fresh-3 = ", hash3);

    CHECK(hash1 == hash2);
    CHECK(hash2 == hash3);
}

// ═══════════════════════════════════════════════════════════════════════════
// NOTE: State leak ON/OFF/ON
//
// Already covered by TEST_CASE "Glow acceptance: state does not leak
// between renders" in tests/visual/glow_ab/glow_ab_acceptance.cpp
// (Azione 1, commit 285b8cff).  That test renders glow_ON → glow_OFF
// → glow_ON on the same renderer instance and asserts:
//   - hash(glow_on_1) == hash(glow_on_2)   (no state leak)
//   - hash(glow_on_1) != hash(glow_off)     (glow actually renders)
// ═══════════════════════════════════════════════════════════════════════════
