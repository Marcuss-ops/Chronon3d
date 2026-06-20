// ═══════════════════════════════════════════════════════════════════════════
// test_text_run_driver_pr10.cpp — Cache-key closure tests (PR 10)
//
// Covers:
//   1. hash_text_run_shape (legacy, no sample_time) — regression
//   2. hash_text_run_shape with sample_time — static shape returns
//      identical hash regardless of time (no doc fold)
//   3. hash_text_run_shape with animated_doc bound — different active
//      document at different frames produces different hashes
//   4. hash_text_run_shape with Scramble — mid-transition frames
//      produce DIFFERENT hashes due to per-frame transition_text fold
//   5. hash_text_run_shape with Morph — morph_map fold invalidates
//      when the map changes
//   6. prewarm_text_run_layout_for_frame — no-op when shape.animated_doc
//      is null OR when shape.engine is null
//
// These tests are font-independent (string-only assertions on
// transition_text and morph_map); they skip the actual HarfBuzz
// shaping round-trip so they pass on CI without DejaVu Sans installed.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/animated_text_document.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/text_run_driver.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <doctest/doctest.h>

#include <memory>
#include <string>

using namespace chronon3d;

namespace {

/// Build a tiny TextRunShape with no font work (null layout / no engine).
/// Suitable for hash-only tests.
std::shared_ptr<TextRunShape> make_hash_only_shape() {
    auto shape = std::make_shared<TextRunShape>();
    // No layout, no glyphs, no animators — pure hash-fold test.
    return shape;
}

/// Build a 2-keyframe AnimatedTextDocument that swaps from "Alpha" to
/// "Beta" with a Hold transition.  At frame 0 the active is "Alpha";
/// at frame 60 the active becomes "Beta".
std::shared_ptr<AnimatedTextDocument> make_hold_doc() {
    auto doc = std::make_shared<AnimatedTextDocument>();
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = "Alpha";
    doc->add_keyframe(kf0);

    SourceTextKeyframe kf60;
    kf60.frame = Frame{60};
    kf60.document.utf8 = "Beta";
    kf60.transition = SourceTextTransition::Cut;
    doc->add_keyframe(kf60);
    return doc;
}

/// Same shape, but with a Scramble transition between "Hello" → "World".
std::shared_ptr<AnimatedTextDocument> make_scramble_doc() {
    auto doc = std::make_shared<AnimatedTextDocument>();
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = "Hello";
    doc->add_keyframe(kf0);

    SourceTextKeyframe kf60;
    kf60.frame = Frame{60};
    kf60.transition = SourceTextTransition::Scramble;
    kf60.document.utf8 = "World";
    kf60.scramble_params.seed = 42;
    doc->add_keyframe(kf60);
    return doc;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. Legacy hash — sanity regression for the no-time overload
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Hash PR10: legacy overload always returns same hash for identical shape") {
    auto a = make_hash_only_shape();
    auto b = make_hash_only_shape();
    // Engine null is acceptable for hash-only test (no layout → base hash is 0).
    CHECK(hash_text_run_shape(*a) == hash_text_run_shape(*b));
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Sample-time overload — static shape returns identical hash for any time
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Hash PR10: static shape (no animated_doc) returns identical hash at any frame") {
    auto shape = make_hash_only_shape();

    const u64 h0  = hash_text_run_shape(*shape, Frame{0});
    const u64 h60 = hash_text_run_shape(*shape, Frame{60});
    const u64 h120 = hash_text_run_shape(*shape, Frame{120});
    const u64 h9999 = hash_text_run_shape(*shape, Frame{9999});

    CHECK(h0 == h60);
    CHECK(h0 == h120);
    CHECK(h0 == h9999);  // any frame produces the same hash when no animated_doc is bound
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. AnimatedTextDocument with Hold/Cut — different frames differ
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Hash PR10: Hold/Cut boundaries produce distinct hashes") {
    auto shape = make_hash_only_shape();
    shape->animated_doc = make_hold_doc();

    const u64 h0   = hash_text_run_shape(*shape, Frame{0});
    const u64 h30  = hash_text_run_shape(*shape, Frame{30});
    const u64 h60  = hash_text_run_shape(*shape, Frame{60});
    const u64 h120 = hash_text_run_shape(*shape, Frame{120});

    // Different active docs change the hash:
    CHECK(h0 != h60);   // Alpha vs Beta
    CHECK(h30 != h60);  // pre-boundary vs post-boundary
    CHECK(h60 == h120); // post-boundary settled tail is stable (same active doc)

    // Sanity — the legacy overload stays the same regardless of doc.
    CHECK(hash_text_run_shape(*shape) == hash_text_run_shape(*shape));
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Scramble — mid-transition frames differ from each other AND from endpoints
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Hash PR10: Scramble mid-transition frames produce distinct hashes") {
    auto shape = make_hash_only_shape();
    // IMPORTANT: Scramble transition is governed by the OUTGOING keyframe
    // (kf0.transition = Scramble), not the destination (kf60.transition).
    // See the convention documented on SourceTextKeyframe::transition.
    auto doc = std::make_shared<AnimatedTextDocument>();
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = "Hello";
    kf0.transition = SourceTextTransition::Scramble;   // <-- outgoing
    kf0.scramble_params.seed = 42;
    doc->add_keyframe(kf0);
    SourceTextKeyframe kf60;
    kf60.frame = Frame{60};
    // kf60.transition default = Hold (irrelevant; no successor).
    kf60.document.utf8 = "World";
    doc->add_keyframe(kf60);
    shape->animated_doc = doc;

    // Capture hash at the scrambled midpoint (frame 30).  At frame 30
    // sample_at returns transition = Scramble with non-empty
    // transition_text → all three doc-state folds contribute.
    const u64 h30a = hash_text_run_shape(*shape, Frame{30});
    const u64 h30b = hash_text_run_shape(*shape, Frame{30});
    // Same frame + same doc → same hash (determinism).
    CHECK(h30a == h30b);

    // A different mid-frame must differ (scramble text is deterministic
    // per frame; different frame → different bytes → different hash).
    const u64 h31 = hash_text_run_shape(*shape, Frame{31});
    CHECK(h30a != h31);

    // Endpoints (pre-transition and post-transition) become stable
    // (transition_text empty; only active->utf8 contributes).
    const u64 h0  = hash_text_run_shape(*shape, Frame{0});
    const u64 h60 = hash_text_run_shape(*shape, Frame{60});
    CHECK(h0 != h30a);   // scrambling branch differs from pre-branching
    CHECK(h60 != h30a);  // scrambling branch differs from post-scramble

    // Determinism check: same doc + same frame = same hash, even when
    // built from multiple independent doc instances (the hash function
    // is pure; nothing reads from a global state).
    auto alt_doc = std::make_shared<AnimatedTextDocument>();
    {
        SourceTextKeyframe k0;
        k0.frame = Frame{0};
        k0.document.utf8 = "Hello";
        k0.transition = SourceTextTransition::Scramble;
        k0.scramble_params.seed = 42;
        alt_doc->add_keyframe(k0);
        SourceTextKeyframe k6;
        k6.frame = Frame{60};
        k6.document.utf8 = "World";
        alt_doc->add_keyframe(k6);
    }
    shape->animated_doc = alt_doc;
    const u64 h30_alt = hash_text_run_shape(*shape, Frame{30});
    CHECK(h30a == h30_alt);
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. Morph — morph_map fold invalidates when topology changes
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Hash PR10: Morph transit produces distinct hashes per frame") {
    auto shape = make_hash_only_shape();

    // Morph transition is governed by the OUTGOING keyframe
    // (kf0.transition = Morph), not the destination.
    auto doc = std::make_shared<AnimatedTextDocument>();
    SourceTextKeyframe kf0;
    kf0.frame = Frame{0};
    kf0.document.utf8 = "ABC";
    kf0.transition = SourceTextTransition::Morph;   // <-- outgoing
    doc->add_keyframe(kf0);

    SourceTextKeyframe kf60;
    kf60.frame = Frame{60};
    // kf60.transition default = Hold (irrelevant; no successor).
    kf60.document.utf8 = "XYZ";
    doc->add_keyframe(kf60);

    shape->animated_doc = doc;

    const u64 h10 = hash_text_run_shape(*shape, Frame{10});
    const u64 h20 = hash_text_run_shape(*shape, Frame{20});
    const u64 h50 = hash_text_run_shape(*shape, Frame{50});
    // Each mid-transition frame differs from the others (different
    // transition_text bytes + different morph_map topology + different mix).
    CHECK(h10 != h20);
    CHECK(h20 != h50);
    CHECK(h10 != h50);
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. prewarm — no-op guard rails
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Prewarm PR10: returns false when shape.animated_doc is null") {
    auto shape = make_hash_only_shape();
    // No animated_doc, no engine.
    const bool stored = prewarm_text_run_layout_for_frame(*shape, Frame{30});
    CHECK(stored == false);
}

TEST_CASE("Prewarm PR10: returns false when shape.animated_doc set but engine is null") {
    auto shape = make_hash_only_shape();
    shape->animated_doc = make_hold_doc();
    // engine still null.
    const bool stored = prewarm_text_run_layout_for_frame(*shape, Frame{30});
    CHECK(stored == false);
}

TEST_CASE("Prewarm PR10: idempotent — calling twice with the same args is safe") {
    // Without a real FontEngine we cannot exercise the cache-write
    // path; this test asserts the no-op guards don't trip on a shape
    // that *would* be prewarmeable.  Repeated calls are safe (the
    // shared cache is internally idempotent on the same key).
    auto shape = make_hash_only_shape();
    shape->animated_doc = make_hold_doc();
    // Both calls return false because engine is null; idempotency
    // means we don't crash on the second call.
    for (int i = 0; i < 3; ++i) {
        const bool stored = prewarm_text_run_layout_for_frame(*shape, Frame{30});
        CHECK(stored == false);
    }
}
