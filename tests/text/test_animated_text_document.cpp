// ═══════════════════════════════════════════════════════════════════════════
// test_animated_text_document.cpp — AnimatedTextDocument tests
//
// PR 6 covers:
//   1. Empty document — sample_at returns nullptr
//   2. Single keyframe — held before and after its frame
//   3. Hold transition — previous document visible until next keyframe
//   4. Cut transition — instant switch to next document after keyframe
//   5. CrossfadeLayouts transition — both documents active with mix factor
//   6. Determinism
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/animated_text_document.hpp>
#include <doctest/doctest.h>

using namespace chronon3d;

// ── Helper: make a simple TextDocument ────────────────────────────────────

namespace {

TextDocument make_doc(const std::string& text) {
    TextDocument doc;
    doc.utf8 = text;
    return doc;
}

SourceTextKeyframe make_kf(int frame, const std::string& text,
                           SourceTextTransition transition = SourceTextTransition::Hold) {
    SourceTextKeyframe kf;
    kf.frame = Frame{frame};
    kf.document = make_doc(text);
    kf.transition = transition;
    return kf;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. Empty document
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AnimatedTextDocument: empty keyframes returns null active") {
    AnimatedTextDocument anim;
    auto state = anim.sample_at(Frame{42});
    CHECK(state.active == nullptr);
    CHECK(state.crossfade_from == nullptr);
    CHECK(state.mix == 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Single keyframe
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AnimatedTextDocument: single keyframe holds") {
    AnimatedTextDocument anim;
    anim.add_keyframe(make_kf(30, "Hello"));

    // Before the keyframe: holds the first (only) document.
    auto state = anim.sample_at(Frame{0});
    CHECK(state.active != nullptr);
    CHECK(state.active->utf8 == "Hello");
    CHECK(state.transition == SourceTextTransition::Hold);
    CHECK(state.mix == 0.0f);

    // At the keyframe: same document.
    state = anim.sample_at(Frame{30});
    CHECK(state.active->utf8 == "Hello");

    // After the keyframe: still the same (only) document.
    state = anim.sample_at(Frame{300});
    CHECK(state.active->utf8 == "Hello");
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. Hold transition
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AnimatedTextDocument: Hold keeps previous document visible") {
    AnimatedTextDocument anim;
    anim.add_keyframe(make_kf(0,  "First", SourceTextTransition::Hold));
    anim.add_keyframe(make_kf(60, "Second", SourceTextTransition::Hold));

    // At frame 0: "First"
    auto state = anim.sample_at(Frame{0});
    CHECK(state.active->utf8 == "First");
    CHECK(state.transition == SourceTextTransition::Hold);

    // At frame 30 (between keyframes): still "First" (Hold)
    state = anim.sample_at(Frame{30});
    CHECK(state.active->utf8 == "First");
    CHECK(state.mix == 0.0f);

    // At frame 60 (exactly at next keyframe): switches to "Second"
    state = anim.sample_at(Frame{60});
    CHECK(state.active->utf8 == "Second");
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Cut transition
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AnimatedTextDocument: Cut switches to next immediately") {
    AnimatedTextDocument anim;
    anim.add_keyframe(make_kf(0,  "DocA", SourceTextTransition::Cut));
    anim.add_keyframe(make_kf(60, "DocB", SourceTextTransition::Hold));

    // At frame 0: "DocA"
    auto state = anim.sample_at(Frame{0});
    CHECK(state.active->utf8 == "DocA");

    // Frame 0 + 1: already "DocB" (Cut means instant switch after prev frame)
    state = anim.sample_at(Frame{1});
    CHECK(state.active->utf8 == "DocB");
    CHECK(state.transition == SourceTextTransition::Cut);

    // At frame 30: still "DocB"
    state = anim.sample_at(Frame{30});
    CHECK(state.active->utf8 == "DocB");
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. CrossfadeLayouts transition
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AnimatedTextDocument: CrossfadeLayouts provides mix factor") {
    AnimatedTextDocument anim;
    anim.add_keyframe(make_kf(30, "Source", SourceTextTransition::CrossfadeLayouts));
    anim.add_keyframe(make_kf(90, "Target", SourceTextTransition::Hold));

    // At prev keyframe (30): active=Target (incoming), crossfade_from=Source, mix=0
    auto state = anim.sample_at(Frame{30});
    CHECK(state.active->utf8 == "Target");
    CHECK(state.crossfade_from != nullptr);
    CHECK(state.crossfade_from->utf8 == "Source");
    CHECK(state.transition == SourceTextTransition::CrossfadeLayouts);
    CHECK(state.mix == doctest::Approx(0.0f));

    // Midway (60): active=Target, crossfade_from=Source, mix=0.5
    state = anim.sample_at(Frame{60});
    CHECK(state.active->utf8 == "Target");
    CHECK(state.crossfade_from != nullptr);
    CHECK(state.crossfade_from->utf8 == "Source");
    CHECK(state.transition == SourceTextTransition::CrossfadeLayouts);
    CHECK(state.mix == doctest::Approx(0.5f));

    // Near end (85): mix ~0.917
    state = anim.sample_at(Frame{85});
    CHECK(state.active->utf8 == "Target");
    CHECK(state.mix == doctest::Approx(55.0f / 60.0f).epsilon(0.01f));

    // Exactly at next keyframe (90): active=Target, no crossfade_from (done)
    state = anim.sample_at(Frame{90});
    CHECK(state.active->utf8 == "Target");
    CHECK(state.crossfade_from == nullptr);
    CHECK(state.mix == doctest::Approx(0.0f));
}

TEST_CASE("AnimatedTextDocument: CrossfadeLayouts zero-duration gap") {
    AnimatedTextDocument anim;
    anim.add_keyframe(make_kf(30, "A", SourceTextTransition::CrossfadeLayouts));
    anim.add_keyframe(make_kf(30, "B", SourceTextTransition::Hold));

    // Same frame overwrites to "B" — only one keyframe.
    CHECK(anim.size() == 1);
    auto state = anim.sample_at(Frame{30});
    CHECK(state.active->utf8 == "B");
    CHECK(state.mix == 0.0f);
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. Mixed transitions
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AnimatedTextDocument: mixed Hold → Crossfade → Cut sequence") {
    AnimatedTextDocument anim;
    anim.add_keyframe(make_kf(0,  "Intro", SourceTextTransition::Hold));
    anim.add_keyframe(make_kf(30, "FadeIn", SourceTextTransition::CrossfadeLayouts));
    anim.add_keyframe(make_kf(90, "Body", SourceTextTransition::Cut));

    // Frame 0-29: Hold "Intro"
    auto state = anim.sample_at(Frame{15});
    CHECK(state.active->utf8 == "Intro");
    CHECK(state.transition == SourceTextTransition::Hold);

    // Frame 30-89: crossfade Intro→FadeIn then FadeIn alone?
    // Wait: the transition is on the PREVIOUS keyframe. At frame 30:
    // prev_kf = Intro (Hold), so it's Hold until frame 30.
    // At frame 30: next_kf = FadeIn → active = FadeIn.
    state = anim.sample_at(Frame{30});
    CHECK(state.active->utf8 == "FadeIn");

    // Frame 30+: prev_kf = FadeIn (CrossfadeLayouts), next_kf = Body
    // So FadeIn crossfades into Body between 30 and 90.
    state = anim.sample_at(Frame{60});
    CHECK(state.active->utf8 == "Body");
    CHECK(state.crossfade_from != nullptr);
    CHECK(state.crossfade_from->utf8 == "FadeIn");
    CHECK(state.mix == doctest::Approx(0.5f));

    // Frame 90: fully at Body, crossfade complete
    state = anim.sample_at(Frame{90});
    CHECK(state.active->utf8 == "Body");
    CHECK(state.crossfade_from == nullptr);
    CHECK(state.mix == doctest::Approx(0.0f));

    // Frame 91+: Cut from Body onward — Body stays (no next kf)
    state = anim.sample_at(Frame{120});
    CHECK(state.active->utf8 == "Body");
}

// ═══════════════════════════════════════════════════════════════════════════
// 7. Overwriting keyframes
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AnimatedTextDocument: duplicate frame overwrites") {
    AnimatedTextDocument anim;
    anim.add_keyframe(make_kf(60, "Old"));
    anim.add_keyframe(make_kf(60, "New"));

    CHECK(anim.size() == 1);
    auto state = anim.sample_at(Frame{60});
    CHECK(state.active->utf8 == "New");
}

// ═══════════════════════════════════════════════════════════════════════════
// 8. Determinism
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AnimatedTextDocument: same input produces same output") {
    AnimatedTextDocument anim;
    anim.add_keyframe(make_kf(0,  "One", SourceTextTransition::Cut));
    anim.add_keyframe(make_kf(50, "Two", SourceTextTransition::CrossfadeLayouts));
    anim.add_keyframe(make_kf(100, "Three", SourceTextTransition::Hold));

    auto a = anim.sample_at(Frame{75});
    auto b = anim.sample_at(Frame{75});

    CHECK(a.active->utf8 == b.active->utf8);
    CHECK((a.crossfade_from != nullptr) == (b.crossfade_from != nullptr));
    if (a.crossfade_from) {
        CHECK(a.crossfade_from->utf8 == b.crossfade_from->utf8);
    }
    CHECK(a.mix == doctest::Approx(b.mix));
    CHECK(a.transition == b.transition);
}
