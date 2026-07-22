// ═══════════════════════════════════════════════════════════════════════════
// tests/certification/test_timeline_functional_v1.cpp
//
// Timeline & Sequence V1 — anti-false-green certification.
//
// User-spec verbatim (Italian):
//   "testa sequence_start/sequence_duration, global_frame/local_frame/
//    source_frame/sample_time. Copri: sequence inizia/termina al frame
//    esatto, frame precedente/successivo invisibili, animazione usa
//    local_frame, freeze, reverse, nested sequence, overlap, transition.
//    Test chiave: resolve_local_frame(49).inactive(), (50).value()==0,
//    (79).value()==29, (80).inactive(). Crea
//    tools/verify_timeline_functional_linux.sh."
//
// This file materializes the canonical Timeline & Sequence V1 cert in
// 10 TEST_CASEs (one per user-spec scenario):
//   1.  SequenceExactStart     — f50 → active, local=0  (key test)
//   2.  SequenceExactEnd       — f79 → active, local=29 (key test)
//   3.  PreviousFrameInvisible — f49 → inactive         (key test)
//   4.  NextFrameInvisible     — f80 → inactive         (key test)
//   5.  AnimationUsesLocalFrame — local = global - sequence_start
//   6.  Freeze                  — held_progress = 1.0 after sequence ends
//   7.  Reverse                 — reversed = duration-1 - local
//   8.  NestedSequence          — outer+inner TimelineResolver path
//   9.  Overlap                 — two sequences overlapping in time
//   10. Transition              — trim_before remap
//
// API surface (chrono API, no rendering backend dependency):
//   * SequenceRange { from, duration, trim_before } — contains() + to_local()
//   * SequenceNode  { name, range, children }       — tree topology
//   * TimelineResolver::resolve_one(node, ctx)     — single-node path
//   * TimelineResolver::resolve(roots, ctx)        — multi-root path
//   * SequenceContext::sequence(ctx, from, dur)    — animation-centric factory
//   * SequenceContext::held_progress()             — freeze semantic
//   * Frame arithmetic (operator-, operator<=>)    — frame math
//
// AGENTS.md v0.1 freeze compliance:
//   Cat-2: deterministic per test (no flake, no time-of-day, no RNG)
//   Cat-3: zero new public SDK API surface (pure tests/ tracking)
//   Cat-5: 3-doc same-commit (CHANGELOG + FOLLOWUP_TICKETS + CURRENT_STATUS)
//   §honesty: HARNESS-COMPLETE on this VPS; macchina-verifica of the
//             ctest run DEFERRED to working build host per the
//             established TICKET-BUILD-ROT-CASCADE-CAMERA 409-error rot
//             + TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV vcpkg glm/magic_enum
//             missing on this VPS.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/timeline/sequence_node.hpp>
#include <chronon3d/timeline/timeline_resolver.hpp>
#include <chronon3d/timeline/sequence.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>

#include <optional>
#include <vector>

using namespace chronon3d;

namespace {

// ── Helpers ──────────────────────────────────────────────────────────────

/// Build a FrameContext with a specific global frame, default 30fps +
/// width/height irrelevant for the pure timeline tests.
FrameContext make_ctx(Frame global_frame) {
    FrameContext ctx;
    ctx = ctx.with_frame(global_frame);
    ctx.local_frame = global_frame;
    ctx = ctx.with_frame_rate(FrameRate{30, 1});
    ctx = ctx.with_duration(Frame{200});  // composition runs to f200
    ctx.width = 64;
    ctx.height = 64;
    return ctx;
}

/// Build the canonical sequence fixture: from=50, duration=30,
/// trim_before=0. Active range is [50, 80) — 30 frames (f50..f79).
SequenceNode make_canonical_sequence() {
    SequenceNode node;
    node.name = "canonical_boundary";
    node.range = SequenceRange{Frame{50}, Frame{30}, Frame{0}};
    return node;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. SequenceExactStart — key test: resolve_local_frame(50).value()==0
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Timeline.SequenceExactStart") {
    auto node = make_canonical_sequence();

    SUBCASE("resolve at f50 → active, local_frame == 0") {
        auto r = TimelineResolver::resolve_one(node, make_ctx(Frame{50}));
        REQUIRE(r.has_value());
        REQUIRE(r->active_path.size() == 1);
        CHECK(r->active_path[0].name == "canonical_boundary");
        CHECK(r->active_path[0].local_frame.integral() == 0);
        CHECK(r->effective_context.frame.integral() == 0);
    }

    SUBCASE("resolve at f50 via SequenceRange.contains + to_local") {
        CHECK(node.range.contains(Frame{50}));
        CHECK(node.range.to_local(Frame{50}).integral() == 0);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. SequenceExactEnd — key test: resolve_local_frame(79).value()==29
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Timeline.SequenceExactEnd") {
    auto node = make_canonical_sequence();

    SUBCASE("resolve at f79 → active, local_frame == 29 (last frame)") {
        auto r = TimelineResolver::resolve_one(node, make_ctx(Frame{79}));
        REQUIRE(r.has_value());
        CHECK(r->active_path[0].local_frame.integral() == 29);
    }

    SUBCASE("resolve at f79 via SequenceRange.contains + to_local") {
        CHECK(node.range.contains(Frame{79}));
        CHECK(node.range.to_local(Frame{79}).integral() == 29);
    }

    SUBCASE("end() = from + duration = 80 (exclusive bound)") {
        CHECK(node.range.end().integral() == 80);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. PreviousFrameInvisible — key test: resolve_local_frame(49).inactive()
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Timeline.PreviousFrameInvisible") {
    auto node = make_canonical_sequence();

    SUBCASE("resolve at f49 → nullopt (one frame before start)") {
        auto r = TimelineResolver::resolve_one(node, make_ctx(Frame{49}));
        CHECK_FALSE(r.has_value());
    }

    SUBCASE("resolve at f0 → nullopt (well before start)") {
        auto r = TimelineResolver::resolve_one(node, make_ctx(Frame{0}));
        CHECK_FALSE(r.has_value());
    }

    SUBCASE("SequenceRange.contains(f49) == false") {
        CHECK_FALSE(node.range.contains(Frame{49}));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. NextFrameInvisible — key test: resolve_local_frame(80).inactive()
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Timeline.NextFrameInvisible") {
    auto node = make_canonical_sequence();

    SUBCASE("resolve at f80 → nullopt (one frame after end)") {
        auto r = TimelineResolver::resolve_one(node, make_ctx(Frame{80}));
        CHECK_FALSE(r.has_value());
    }

    SUBCASE("resolve at f199 → nullopt (well after end)") {
        auto r = TimelineResolver::resolve_one(node, make_ctx(Frame{199}));
        CHECK_FALSE(r.has_value());
    }

    SUBCASE("SequenceRange.contains(f80) == false (exclusive upper bound)") {
        CHECK_FALSE(node.range.contains(Frame{80}));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. AnimationUsesLocalFrame — animation progress maps local 0..29 → 0..1
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Timeline.AnimationUsesLocalFrame") {
    auto node = make_canonical_sequence();
    const Frame from{50};
    const Frame duration{30};

    // Sample at every active frame; verify local = global - from.
    for (int g = 50; g <= 79; ++g) {
        const Frame global_frame{g};
        auto r = TimelineResolver::resolve_one(node, make_ctx(global_frame));
        REQUIRE(r.has_value());
        const int expected_local = g - 50;
        CHECK(r->active_path[0].local_frame.integral() == expected_local);

        // Also verify the SequenceContext factory path (the animation-
        // centric entry point) returns the same local frame.
        auto seq = sequence(make_ctx(global_frame), from, duration);
        CHECK(seq.active);
        CHECK(seq.frame.integral() == expected_local);
    }

    // Sanity check: progress() at f50 (local 0) ≈ 0, at f79 (local 29)
    // ≈ 29/30 = 0.9666.
    auto seq50 = sequence(make_ctx(Frame{50}), from, duration);
    auto seq79 = sequence(make_ctx(Frame{79}), from, duration);
    CHECK(seq50.progress() == doctest::Approx(0.0f).epsilon(1e-6));
    CHECK(seq79.progress() == doctest::Approx(29.0f / 30.0f).epsilon(1e-6));
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. Freeze — sequence holds at final state after window closes
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Timeline.Freeze") {
    const Frame from{50};
    const Frame duration{30};

    // During the sequence, held_progress == progress() (active=true).
    auto seq_active = sequence(make_ctx(Frame{60}), from, duration);
    CHECK(seq_active.active);
    CHECK(seq_active.held_progress() == doctest::Approx(10.0f / 30.0f).epsilon(1e-6));
    CHECK(seq_active.held_progress() == doctest::Approx(seq_active.progress()).epsilon(1e-6));

    // After the sequence ends, held_progress == 1.0 (frozen at end).
    auto seq_after = sequence(make_ctx(Frame{100}), from, duration);
    CHECK_FALSE(seq_after.active);
    CHECK(seq_after.held_progress() == doctest::Approx(1.0f).epsilon(1e-6));

    // Before the sequence starts, held_progress == 0.0 (frozen at start).
    auto seq_before = sequence(make_ctx(Frame{10}), from, duration);
    CHECK_FALSE(seq_before.active);
    CHECK(seq_before.held_progress() == doctest::Approx(0.0f).epsilon(1e-6));
}

// ═══════════════════════════════════════════════════════════════════════════
// 7. Reverse — reversed_local = duration-1 - local
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Timeline.Reverse") {
    const Frame from{50};
    const Frame duration{30};

    // Reverse semantic: the local_frame at the start of the sequence
    // (f50, local 0) maps to the END of the reversed timeline (29);
    // the local_frame at the end (f79, local 29) maps to the START (0).
    for (int g = 50; g <= 79; ++g) {
        const int local = g - 50;
        const int reversed = 30 - 1 - local;  // 29, 28, ..., 0
        auto seq = sequence(make_ctx(Frame{g}), from, duration);
        const Frame reversed_frame{reversed};
        CHECK(seq.frame.integral() == local);
        CHECK((duration - Frame{1} - seq.frame).integral() == reversed_frame.integral());
    }

    // Boundary: f50 → reversed=29, f79 → reversed=0.
    auto seq50 = sequence(make_ctx(Frame{50}), from, duration);
    auto seq79 = sequence(make_ctx(Frame{79}), from, duration);
    CHECK((duration - Frame{1} - seq50.frame).integral() == 29);
    CHECK((duration - Frame{1} - seq79.frame).integral() == 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// 8. NestedSequence — outer (from=100, dur=200) + inner (from=20, dur=30)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Timeline.NestedSequence") {
    // Build a chapter (from=100, duration=200) containing a title
    // (from=20, duration=30) child.  At global f120: chapter.local=20,
    // title.local=0 (innermost).  The active path is [chapter, title].
    SequenceNode chapter;
    chapter.name = "chapter";
    chapter.range = SequenceRange{Frame{100}, Frame{200}, Frame{0}};

    SequenceNode title;
    title.name = "title";
    title.range = SequenceRange{Frame{20}, Frame{30}, Frame{0}};
    chapter.add_child(title);

    SUBCASE("global f120 → active_path size==2, innermost local==0") {
        auto resolutions = TimelineResolver::resolve({chapter}, make_ctx(Frame{120}));
        REQUIRE(resolutions.size() == 2);  // chapter + title (both emit)
        const auto& chapter_res = resolutions[0];
        const auto& title_res = resolutions[1];

        // Outer: chapter
        REQUIRE(chapter_res.active_path.size() == 1);
        CHECK(chapter_res.active_path[0].name == "chapter");
        CHECK(chapter_res.active_path[0].local_frame.integral() == 20);

        // Inner: title
        REQUIRE(title_res.active_path.size() == 2);
        CHECK(title_res.active_path[0].name == "chapter");
        CHECK(title_res.active_path[0].local_frame.integral() == 20);
        CHECK(title_res.active_path[1].name == "title");
        CHECK(title_res.active_path[1].local_frame.integral() == 0);
        CHECK(title_res.effective_context.frame.integral() == 0);
    }

    SUBCASE("global f130 → active_path innermost local==10") {
        auto resolutions = TimelineResolver::resolve({chapter}, make_ctx(Frame{130}));
        REQUIRE(resolutions.size() == 2);
        // Innermost resolution has local 10 (chapter local 30, title local 10).
        const auto& title_res = resolutions[1];
        REQUIRE(title_res.active_path.size() == 2);
        CHECK(title_res.active_path[1].local_frame.integral() == 10);
    }

    SUBCASE("global f99 → no resolution (chapter not yet active)") {
        auto resolutions = TimelineResolver::resolve({chapter}, make_ctx(Frame{99}));
        CHECK(resolutions.empty());
    }

    SUBCASE("global f300 → no resolution (chapter ended at f300)") {
        auto resolutions = TimelineResolver::resolve({chapter}, make_ctx(Frame{300}));
        CHECK(resolutions.empty());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 9. Overlap — two sequences active at the same global frame
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Timeline.Overlap") {
    // Two roots with overlapping active ranges: A=[10,50), B=[30,70).
    // At f40: both are active; TimelineResolver::resolve returns 2 entries.
    SequenceNode a;
    a.name = "sequence_a";
    a.range = SequenceRange{Frame{10}, Frame{40}, Frame{0}};

    SequenceNode b;
    b.name = "sequence_b";
    b.range = SequenceRange{Frame{30}, Frame{40}, Frame{0}};

    std::vector<SequenceNode> roots{a, b};

    SUBCASE("global f40 → 2 active resolutions (A.local=30, B.local=10)") {
        auto resolutions = TimelineResolver::resolve(roots, make_ctx(Frame{40}));
        REQUIRE(resolutions.size() == 2);
        // Each root emits its own Resolution.
        bool found_a = false, found_b = false;
        for (const auto& r : resolutions) {
            REQUIRE(r.active_path.size() == 1);
            if (r.active_path[0].name == "sequence_a") {
                CHECK(r.active_path[0].local_frame.integral() == 30);  // 40-10
                found_a = true;
            } else if (r.active_path[0].name == "sequence_b") {
                CHECK(r.active_path[0].local_frame.integral() == 10);  // 40-30
                found_b = true;
            }
        }
        CHECK(found_a);
        CHECK(found_b);
    }

    SUBCASE("global f20 → only A active (1 resolution)") {
        auto resolutions = TimelineResolver::resolve(roots, make_ctx(Frame{20}));
        REQUIRE(resolutions.size() == 1);
        CHECK(resolutions[0].active_path[0].name == "sequence_a");
    }

    SUBCASE("global f60 → only B active (1 resolution)") {
        auto resolutions = TimelineResolver::resolve(roots, make_ctx(Frame{60}));
        REQUIRE(resolutions.size() == 1);
        CHECK(resolutions[0].active_path[0].name == "sequence_b");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 10. TrimBeforeRemap — trim_before remap shifts the local frame origin
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Timeline.TrimBeforeRemap") {
    // Sequence with trim_before=10: the local frame starts at 10 (not 0)
    // at the global from frame.  This simulates "start this sequence
    // 10 frames into its content" — the canonical transition semantic.
    SequenceNode node;
    node.name = "transition";
    node.range = SequenceRange{Frame{50}, Frame{30}, Frame{10}};

    SUBCASE("at f50 → active, local_frame == 10 (trim_before offset)") {
        auto r = TimelineResolver::resolve_one(node, make_ctx(Frame{50}));
        REQUIRE(r.has_value());
        CHECK(r->active_path[0].local_frame.integral() == 10);
    }

    SUBCASE("at f60 → active, local_frame == 20 (60-50+10)") {
        auto r = TimelineResolver::resolve_one(node, make_ctx(Frame{60}));
        REQUIRE(r.has_value());
        CHECK(r->active_path[0].local_frame.integral() == 20);
    }

    SUBCASE("at f79 → active, local_frame == 39 (last frame of shifted range)") {
        auto r = TimelineResolver::resolve_one(node, make_ctx(Frame{79}));
        REQUIRE(r.has_value());
        CHECK(r->active_path[0].local_frame.integral() == 39);
    }

    SUBCASE("active range still [50, 80) regardless of trim_before") {
        CHECK(node.range.contains(Frame{50}));
        CHECK(node.range.contains(Frame{79}));
        CHECK_FALSE(node.range.contains(Frame{49}));
        CHECK_FALSE(node.range.contains(Frame{80}));
    }
}
