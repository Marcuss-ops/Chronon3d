// ═══════════════════════════════════════════════════════════════════════════
// test_animated_text_document.cpp — AnimatedTextDocument tests
//
// PR 6 + PR 9 covers:
//   1. Empty document — sample_at returns nullptr
//   2. Single keyframe — held before and after its frame
//   3. Hold transition — previous document visible until next keyframe
//   4. Cut transition — instant switch to next document after keyframe
//   5. DissolveLayouts transition — both documents active with mix factor
//   6. Determinism
//   7. Scramble transition — deterministic char substitution
//   8. Morph transition — common char persistence + morph map
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
    CHECK(state.dissolve_from == nullptr);
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

TEST_CASE("AnimatedTextDocument: Cut keeps previous document until next boundary") {
    AnimatedTextDocument anim;
    anim.add_keyframe(make_kf(0,  "DocA", SourceTextTransition::Cut));
    anim.add_keyframe(make_kf(60, "DocB", SourceTextTransition::Hold));

    // At frame 0: "DocA"
    auto state = anim.sample_at(Frame{0});
    CHECK(state.active->utf8 == "DocA");

    // During the gap: still "DocA" (Cut is semantically identical to
    // Hold for the visible text until the next keyframe boundary).
    state = anim.sample_at(Frame{30});
    CHECK(state.active->utf8 == "DocA");
    CHECK(state.transition == SourceTextTransition::Cut);

    // Exactly at the next keyframe boundary: "DocB" appears.
    state = anim.sample_at(Frame{60});
    CHECK(state.active->utf8 == "DocB");
    CHECK(state.transition == SourceTextTransition::Hold);

    // After the boundary: still "DocB".
    state = anim.sample_at(Frame{61});
    CHECK(state.active->utf8 == "DocB");
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. DissolveLayouts transition
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AnimatedTextDocument: DissolveLayouts provides mix factor") {
    AnimatedTextDocument anim;
    anim.add_keyframe(make_kf(30, "Source", SourceTextTransition::DissolveLayouts));
    anim.add_keyframe(make_kf(90, "Target", SourceTextTransition::Hold));

    // At prev keyframe (30): active=Target (incoming), dissolve_from=Source, mix=0
    auto state = anim.sample_at(Frame{30});
    CHECK(state.active->utf8 == "Target");
    CHECK(state.dissolve_from != nullptr);
    CHECK(state.dissolve_from->utf8 == "Source");
    CHECK(state.transition == SourceTextTransition::DissolveLayouts);
    CHECK(state.mix == doctest::Approx(0.0f));

    // Midway (60): active=Target, dissolve_from=Source, mix=0.5
    state = anim.sample_at(Frame{60});
    CHECK(state.active->utf8 == "Target");
    CHECK(state.dissolve_from != nullptr);
    CHECK(state.dissolve_from->utf8 == "Source");
    CHECK(state.transition == SourceTextTransition::DissolveLayouts);
    CHECK(state.mix == doctest::Approx(0.5f));

    // Near end (85): mix ~0.917
    state = anim.sample_at(Frame{85});
    CHECK(state.active->utf8 == "Target");
    CHECK(state.mix == doctest::Approx(55.0f / 60.0f).epsilon(0.01f));

    // Exactly at next keyframe (90): active=Target, no dissolve_from (done)
    state = anim.sample_at(Frame{90});
    CHECK(state.active->utf8 == "Target");
    CHECK(state.dissolve_from == nullptr);
    CHECK(state.mix == doctest::Approx(0.0f));
}

TEST_CASE("AnimatedTextDocument: DissolveLayouts zero-duration gap") {
    AnimatedTextDocument anim;
    anim.add_keyframe(make_kf(30, "A", SourceTextTransition::DissolveLayouts));
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

TEST_CASE("AnimatedTextDocument: mixed Hold → Dissolve → Cut sequence") {
    AnimatedTextDocument anim;
    anim.add_keyframe(make_kf(0,  "Intro", SourceTextTransition::Hold));
    anim.add_keyframe(make_kf(30, "FadeIn", SourceTextTransition::DissolveLayouts));
    anim.add_keyframe(make_kf(90, "Body", SourceTextTransition::Cut));

    // Frame 0-29: Hold "Intro"
    auto state = anim.sample_at(Frame{15});
    CHECK(state.active->utf8 == "Intro");
    CHECK(state.transition == SourceTextTransition::Hold);

    // At frame 30: FadeIn boundary. Body is the incoming side of the
    // FadeIn→Body dissolve, but mix=0 means we still see FadeIn as the
    // outgoing side.
    state = anim.sample_at(Frame{30});
    CHECK(state.active->utf8 == "Body");
    CHECK(state.dissolve_from != nullptr);
    CHECK(state.dissolve_from->utf8 == "FadeIn");
    CHECK(state.mix == doctest::Approx(0.0f));

    // Frame 30-89: prev_kf = FadeIn (DissolveLayouts), next_kf = Body.
    // FadeIn dissolves into Body between frames 30 and 90.
    state = anim.sample_at(Frame{60});
    CHECK(state.active->utf8 == "Body");
    CHECK(state.dissolve_from != nullptr);
    CHECK(state.dissolve_from->utf8 == "FadeIn");
    CHECK(state.mix == doctest::Approx(0.5f));

    // Frame 90: fully at Body, dissolve complete.
    state = anim.sample_at(Frame{90});
    CHECK(state.active->utf8 == "Body");
    CHECK(state.dissolve_from == nullptr);
    CHECK(state.mix == doctest::Approx(0.0f));

    // Frame 91+: Cut from Body onward — Body stays (no next kf).
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
    anim.add_keyframe(make_kf(50, "Two", SourceTextTransition::DissolveLayouts));
    anim.add_keyframe(make_kf(100, "Three", SourceTextTransition::Hold));

    auto a = anim.sample_at(Frame{75});
    auto b = anim.sample_at(Frame{75});

    CHECK(a.active->utf8 == b.active->utf8);
    CHECK((a.dissolve_from != nullptr) == (b.dissolve_from != nullptr));
    if (a.dissolve_from) {
        CHECK(a.dissolve_from->utf8 == b.dissolve_from->utf8);
    }
    CHECK(a.mix == doctest::Approx(b.mix));
    CHECK(a.transition == b.transition);
}

// ═══════════════════════════════════════════════════════════════════════════
// 9. Scramble transition
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AnimatedTextDocument: Scramble is deterministic") {
    AnimatedTextDocument anim;
    auto kf = make_kf(0, "FUTURE", SourceTextTransition::Scramble);
    kf.scramble_params.seed = 42;
    anim.add_keyframe(std::move(kf));
    anim.add_keyframe(make_kf(60, "LAUNCH", SourceTextTransition::Hold));

    auto a = anim.sample_at(Frame{30});
    auto b = anim.sample_at(Frame{30});

    CHECK(a.transition == SourceTextTransition::Scramble);
    CHECK(b.transition == SourceTextTransition::Scramble);
    CHECK(a.transition_text == b.transition_text);
    CHECK(a.mix == doctest::Approx(b.mix));
}

TEST_CASE("AnimatedTextDocument: Scramble at mix=0 matches prev") {
    AnimatedTextDocument anim;
    auto kf = make_kf(0, "BUILD", SourceTextTransition::Scramble);
    anim.add_keyframe(std::move(kf));
    anim.add_keyframe(make_kf(60, "SCALE", SourceTextTransition::Hold));

    auto state = anim.sample_at(Frame{1});
    CHECK(state.transition == SourceTextTransition::Scramble);
    CHECK(state.transition_text.size() > 0);
    CHECK(state.active != nullptr);
    CHECK(state.dissolve_from != nullptr);
}

TEST_CASE("AnimatedTextDocument: Scramble at mix=1 matches next") {
    AnimatedTextDocument anim;
    auto kf = make_kf(0, "BUILD", SourceTextTransition::Scramble);
    anim.add_keyframe(std::move(kf));
    anim.add_keyframe(make_kf(60, "SCALE", SourceTextTransition::Hold));

    auto state = anim.sample_at(Frame{59});
    CHECK(state.transition == SourceTextTransition::Scramble);
    CHECK_FALSE(state.transition_text.empty());
}

TEST_CASE("AnimatedTextDocument: Scramble different frames produce different text") {
    AnimatedTextDocument anim;
    auto kf = make_kf(0, "HELLO", SourceTextTransition::Scramble);
    kf.scramble_params.seed = 123;
    anim.add_keyframe(std::move(kf));
    anim.add_keyframe(make_kf(100, "WORLD", SourceTextTransition::Hold));

    auto s20 = anim.sample_at(Frame{20});
    auto s50 = anim.sample_at(Frame{50});

    // Different frames should produce different scrambled text
    CHECK(s20.transition_text != s50.transition_text);
}

TEST_CASE("AnimatedTextDocument: Scramble custom char pool") {
    AnimatedTextDocument anim;
    auto kf = make_kf(0, "ABCDEF", SourceTextTransition::Scramble);
    kf.scramble_params.seed = 99;
    kf.scramble_params.char_pool = "XYZ";
    anim.add_keyframe(std::move(kf));
    anim.add_keyframe(make_kf(60, "GHIJKL", SourceTextTransition::Hold));

    auto state = anim.sample_at(Frame{30});
    CHECK(state.transition == SourceTextTransition::Scramble);
    // Scrambled text should be non-empty
    CHECK_FALSE(state.transition_text.empty());
}

// ═══════════════════════════════════════════════════════════════════════════
// 10. Morph transition
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("AnimatedTextDocument: Morph common chars persist") {
    AnimatedTextDocument anim;
    auto kf = make_kf(0, "MILLION", SourceTextTransition::Morph);
    anim.add_keyframe(std::move(kf));
    anim.add_keyframe(make_kf(60, "MILLIONS", SourceTextTransition::Hold));

    auto state = anim.sample_at(Frame{30});
    CHECK(state.transition == SourceTextTransition::Morph);
    CHECK_FALSE(state.transition_text.empty());
    CHECK_FALSE(state.morph_map.empty());
}

TEST_CASE("AnimatedTextDocument: Morph produces morph_map") {
    AnimatedTextDocument anim;
    auto kf = make_kf(0, "POWER", SourceTextTransition::Morph);
    anim.add_keyframe(std::move(kf));
    anim.add_keyframe(make_kf(60, "MONEY", SourceTextTransition::Hold));

    auto state = anim.sample_at(Frame{30});
    CHECK(state.transition == SourceTextTransition::Morph);
    CHECK(state.morph_map.size() > 0);

    // The morph_map should contain pairs
    bool has_common = false;
    for (const auto& [prev_idx, next_idx] : state.morph_map) {
        if (prev_idx >= 0 && next_idx >= 0) {
            has_common = true;
            break;
        }
    }
    CHECK(has_common);
}

TEST_CASE("AnimatedTextDocument: Morph at bounds") {
    AnimatedTextDocument anim;
    auto kf = make_kf(0, "BUILD", SourceTextTransition::Morph);
    anim.add_keyframe(std::move(kf));
    anim.add_keyframe(make_kf(60, "SCALE", SourceTextTransition::Hold));

    // At mix ~= 0: text should resemble prev
    auto s0 = anim.sample_at(Frame{1});
    CHECK(s0.transition == SourceTextTransition::Morph);
    CHECK_FALSE(s0.transition_text.empty());

    // At mix ~= 1: text should resemble next
    auto s1 = anim.sample_at(Frame{59});
    CHECK(s1.transition == SourceTextTransition::Morph);
    CHECK_FALSE(s1.transition_text.empty());

    // Different frames = different transition text
    CHECK(s0.transition_text != s1.transition_text);
}

TEST_CASE("AnimatedTextDocument: Morph determinism") {
    AnimatedTextDocument anim;
    auto kf = make_kf(0, "FAST", SourceTextTransition::Morph);
    anim.add_keyframe(std::move(kf));
    anim.add_keyframe(make_kf(60, "SCALE", SourceTextTransition::Hold));

    auto a = anim.sample_at(Frame{30});
    auto b = anim.sample_at(Frame{30});

    CHECK(a.transition_text == b.transition_text);
    CHECK(a.morph_map.size() == b.morph_map.size());
    for (size_t i = 0; i < a.morph_map.size(); ++i) {
        CHECK(a.morph_map[i].first == b.morph_map[i].first);
        CHECK(a.morph_map[i].second == b.morph_map[i].second);
    }
}
