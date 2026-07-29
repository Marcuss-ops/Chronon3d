// ═══════════════════════════════════════════════════════════════════════════
// tests/text/test_word_emphasis_animators.cpp
//
// Unit test for chronon3d::presets::text::word_emphasis_animators
// (Variant A — TICKET-WORD-EMPHASIS / TICKET-WORD-EMPHASIS-ANIMATORS).
//
// Iteration v3 (post v2-review fixes):
//   * Selector boundary contract: assert strict disjointness between
//     adjacent windows (epsilon shim) and `end == 100.0f` for the
//     last letter.  Locks the v3 epsilon-shim fix.
//   * Per-kind id slugs asserted for ALL three kinds (Name, Title,
//     Emph), not just Name — closes the v2-review coverage gap.
//   * Existing v2 checks (keyframes() inspection, parse matrix,
//     is_emphasis_kind, strip helper, structural parity, accent
//     wiring, end-to-end roundtrip) unchanged.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/presets/text/word_emphasis_animators.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::presets::text;

// Mirror the canonical epsilon constant exposed by the impl (kept in
// sync by hand — see src/text/word_emphasis_animators.cpp).
namespace {
constexpr f32 kSelectorEpsilon = 1e-4f;
}

// ── parse_emphasis_prefix ────────────────────────────────────────────────

TEST_CASE("parse_emphasis_prefix — recognised prefixes") {
    SUBCASE("name prefix") {
        const auto r = parse_emphasis_prefix("name:Marco");
        CHECK(r.kind == WordEmphasisKind::Name);
        CHECK(r.remainder == std::string_view{"Marco"});
    }
    SUBCASE("title prefix") {
        const auto r = parse_emphasis_prefix("title:Cinque Stelle");
        CHECK(r.kind == WordEmphasisKind::Title);
        CHECK(r.remainder == std::string_view{"Cinque Stelle"});
    }
    SUBCASE("emph prefix") {
        const auto r = parse_emphasis_prefix("emph:critical");
        CHECK(r.kind == WordEmphasisKind::Emph);
        CHECK(r.remainder == std::string_view{"critical"});
    }
    SUBCASE("non-prefix input stays None with full remainder") {
        const auto r = parse_emphasis_prefix("regular");
        CHECK(r.kind == WordEmphasisKind::None);
        CHECK(r.remainder == std::string_view{"regular"});
    }
    SUBCASE("empty input is None with empty remainder") {
        const auto r = parse_emphasis_prefix("");
        CHECK(r.kind == WordEmphasisKind::None);
        CHECK(r.remainder == std::string_view{""});
    }
    SUBCASE("nested colon — only the first prefix is stripped") {
        const auto r = parse_emphasis_prefix("emph:foo:bar");
        CHECK(r.kind == WordEmphasisKind::Emph);
        CHECK(r.remainder == std::string_view{"foo:bar"});
    }
    SUBCASE("partial-prefix match is rejected (only 'name' without colon)") {
        const auto r = parse_emphasis_prefix("name");
        CHECK(r.kind == WordEmphasisKind::None);
        CHECK(r.remainder == std::string_view{"name"});
    }
    SUBCASE("uppercase prefix is rejected (case-sensitive)") {
        const auto r = parse_emphasis_prefix("NAME:Marco");
        CHECK(r.kind == WordEmphasisKind::None);
        CHECK(r.remainder == std::string_view{"NAME:Marco"});
    }
    SUBCASE("V1 profile prefixes") {
        CHECK(parse_emphasis_prefix("base:hello").kind == WordEmphasisKind::Base);
        CHECK(parse_emphasis_prefix("phrase:hello").kind == WordEmphasisKind::Phrase);
        CHECK(parse_emphasis_prefix("word:hello").kind == WordEmphasisKind::Word);
    }
}

TEST_CASE("make_light_text_animators — span budget and canonical selectors") {
    const auto base = make_light_text_animators(WordEmphasisKind::Base, std::nullopt, Frame{0});
    REQUIRE(base.size() == 1);
    CHECK(base.front().selectors.size() == 1);
    CHECK(base.front().selectors.front().unit == TextSelectorUnit::Word);
    CHECK(base.front().properties.size() == 2);

    const auto name = make_light_text_animators(
        WordEmphasisKind::Name, Color{1.0f, 0.2f, 0.1f, 1.0f}, Frame{10}, 1, 3);
    REQUIRE(name.size() == 1);
    CHECK(name.front().selectors.size() == 1);
    CHECK(name.front().properties.size() == 3);
    CHECK(name.front().selectors.front().start.default_value() == doctest::Approx(33.3333f));
    CHECK(name.front().selectors.front().end.default_value() == doctest::Approx(66.6667f));

    const auto word = make_light_text_animators(WordEmphasisKind::Word, std::nullopt, Frame{0});
    REQUIRE(word.size() == 1);
    CHECK(word.front().selectors.front().unit == TextSelectorUnit::Word);
    CHECK(word.front().properties.size() == 2);
    CHECK(make_light_text_animators(WordEmphasisKind::Word, std::nullopt, Frame{0}, 3, 3).empty());
}

// ── is_emphasis_kind ─────────────────────────────────────────────────────

TEST_CASE("is_emphasis_kind — only None returns false") {
    CHECK(is_emphasis_kind(WordEmphasisKind::None) == false);
    CHECK(is_emphasis_kind(WordEmphasisKind::Name) == true);
    CHECK(is_emphasis_kind(WordEmphasisKind::Title) == true);
    CHECK(is_emphasis_kind(WordEmphasisKind::Emph) == true);
}

// ── strip_emphasis_prefix ────────────────────────────────────────────────

TEST_CASE("strip_emphasis_prefix — inverse of parse") {
    CHECK(strip_emphasis_prefix("name:Marco") == "Marco");
    CHECK(strip_emphasis_prefix("title:Hello World") == "Hello World");
    CHECK(strip_emphasis_prefix("emph:key") == "key");
    CHECK(strip_emphasis_prefix("plain") == "plain");
    CHECK(strip_emphasis_prefix("") == "");
    CHECK(strip_emphasis_prefix("emph:foo:bar") == "foo:bar");
}

// ── make_word_emphasis_animators: trivial inputs ─────────────────────────

TEST_CASE("make_word_emphasis_animators — None returns empty") {
    const auto specs = make_word_emphasis_animators(
        WordEmphasisKind::None, std::nullopt, Frame{0}, 5);
    CHECK(specs.empty());
}

TEST_CASE("make_word_emphasis_animators — zero letters returns empty") {
    const auto specs = make_word_emphasis_animators(
        WordEmphasisKind::Name, std::nullopt, Frame{0}, 0);
    CHECK(specs.empty());
}

// ── make_word_emphasis_animators: structural contract ────────────────────

TEST_CASE("make_word_emphasis_animators — Names returns N specs with per-letter characterisation") {
    constexpr std::size_t N = 5;
    constexpr Frame START{120};

    const auto specs = make_word_emphasis_animators(
        WordEmphasisKind::Name, std::nullopt, START, N);

    REQUIRE(specs.size() == N);

    SUBCASE("each spec is enabled and has canonical blend modes") {
        for (const auto& s : specs) {
            CHECK(s.enabled);
            CHECK(s.transform_mode == TextPropertyBlendMode::Add);
            CHECK(s.color_mode == TextPropertyBlendMode::Replace);
        }
    }

    SUBCASE("spec ids encode the kind token for grep-discoverability") {
        // Each id must contain the kind token "name" so a forward
        // forensic grep "rg 'word_emphasis_name_letter_'" finds them.
        for (std::size_t i = 0; i < N; ++i) {
            CHECK(specs[i].id == "word_emphasis_name_letter_" + std::to_string(i));
        }
    }

    SUBCASE("selectors target ONLY letter i with disjoint Square windows") {
        std::vector<float> starts;
        std::vector<float> ends;
        starts.reserve(N);
        ends.reserve(N);
        for (const auto& s : specs) {
            REQUIRE(s.selectors.size() == 1);
            const auto& sel = s.selectors.front();
            CHECK(sel.unit == TextSelectorUnit::Character);
            CHECK(sel.shape == TextSelectorShape::Square); // CRISP boundary
            CHECK_FALSE(sel.exclude_spaces);
            // AnimatedValue<f32> has a single static value (no keyframes
            // added); default_value() returns that constant directly.
            starts.push_back(sel.start.default_value());
            ends.push_back(sel.end.default_value());
        }
        // Windows are strictly disjoint (epsilon shim v3) and span 0..100.
        // Last letter ends exactly at 100.0f; intermediate letters end
        // epsilon below their respective window edge.
        std::sort(starts.begin(), starts.end());
        std::sort(ends.begin(), ends.end());
        for (std::size_t i = 0; i < N; ++i) {
            CHECK(starts[i] == doctest::Approx(static_cast<float>(i) * 20.0f));
            if (i + 1 == N) {
                // Last letter: end == 100.0f (full boundary coverage).
                CHECK(ends[i] == doctest::Approx(100.0f));
            } else {
                // Intermediate letters: end == (i+1)*20 - epsilon.
                CHECK(ends[i] == doctest::Approx(
                    static_cast<float>(i + 1) * 20.0f - kSelectorEpsilon));
            }
        }
        // Strict disjointness: end[i] < start[i+1] for sorted windows.
        for (std::size_t i = 0; i + 1 < N; ++i) {
            CHECK(ends[i] < starts[i + 1]);
        }
        // Selector ids also carry the kind token.
        for (std::size_t i = 0; i < N; ++i) {
            CHECK(specs[i].selectors.front().id
                  == "sel_name_letter_" + std::to_string(i));
        }
    }

    SUBCASE("each spec carries Scale (3 keyframes) + Opacity (2 keyframes)") {
        for (const auto& s : specs) {
            // 2 properties expected (no accent supplied ⇒ no FillColor).
            REQUIRE(s.properties.size() == 2);
            bool saw_scale = false;
            bool saw_opacity = false;
            for (const auto& p : s.properties) {
                if (std::holds_alternative<ScaleProperty>(p)) {
                    saw_scale = true;
                    const auto& sp = std::get<ScaleProperty>(p);
                    REQUIRE(sp.value.keyframes().size() == 3);
                    CHECK(sp.value.keyframes()[0].value.x
                          == doctest::Approx(0.7f).epsilon(1e-3));
                    CHECK(sp.value.keyframes()[0].value.y
                          == doctest::Approx(0.7f).epsilon(1e-3));
                    CHECK(sp.value.keyframes()[0].value.z
                          == doctest::Approx(0.7f).epsilon(1e-3));
                    CHECK(sp.value.keyframes()[1].value.x
                          == doctest::Approx(1.15f).epsilon(1e-3));
                    CHECK(sp.value.keyframes()[2].value.x
                          == doctest::Approx(1.0f).epsilon(1e-3));
                }
                if (std::holds_alternative<OpacityProperty>(p)) {
                    saw_opacity = true;
                    const auto& op = std::get<OpacityProperty>(p);
                    REQUIRE(op.value.keyframes().size() == 2);
                    CHECK(op.value.keyframes()[0].value
                          == doctest::Approx(0.0f).epsilon(1e-3));
                    CHECK(op.value.keyframes()[1].value
                          == doctest::Approx(1.0f).epsilon(1e-3));
                }
            }
            CHECK(saw_scale);
            CHECK(saw_opacity);
        }
    }

    SUBCASE("per-letter stagger shifts the scale up-start frame by 2 per index") {
        for (std::size_t i = 0; i < N; ++i) {
            const auto& s = specs[i];
            REQUIRE(s.properties.size() >= 1);
            REQUIRE(std::holds_alternative<ScaleProperty>(s.properties.front()));
            const auto& sp = std::get<ScaleProperty>(s.properties.front());
            REQUIRE(sp.value.keyframes().size() == 3);
            const i64 expected_start =
                START.integral() + static_cast<i64>(i) * 2;
            CHECK(sp.value.keyframes()[0].frame.integral() == expected_start);
        }
    }

    SUBCASE("opacity ramp ends 4 frames after the scale up-start") {
        for (std::size_t i = 0; i < N; ++i) {
            const auto& s = specs[i];
            REQUIRE(s.properties.size() == 2);
            const auto& scale = std::get<ScaleProperty>(s.properties[0]);
            const auto& opacity = std::get<OpacityProperty>(s.properties[1]);
            const i64 scale_start = scale.value.keyframes()[0].frame.integral();
            const i64 opacity_start = opacity.value.keyframes()[0].frame.integral();
            const i64 opacity_end   = opacity.value.keyframes()[1].frame.integral();
            CHECK(opacity_start == scale_start);
            CHECK(opacity_end - opacity_start == 4);
        }
    }
}

// ── make_word_emphasis_animators: per-kind id-slug coverage ──────────────
// v2-review coverage gap: Title + Emph slugs were never asserted.  This
// subcase locks them so a regression in `kind_token()` (e.g. a typo on
// the Title mapping) is caught at the unit-test level rather than
// silently passing through CI.

TEST_CASE("make_word_emphasis_animators — per-kind id slugs (Name / Title / Emph)") {
    SUBCASE("Name slug") {
        const auto specs = make_word_emphasis_animators(
            WordEmphasisKind::Name, std::nullopt, Frame{0}, 2);
        REQUIRE(specs.size() == 2);
        CHECK(specs[0].id == "word_emphasis_name_letter_0");
        CHECK(specs[0].selectors.front().id == "sel_name_letter_0");
        CHECK(specs[1].id == "word_emphasis_name_letter_1");
        CHECK(specs[1].selectors.front().id == "sel_name_letter_1");
    }
    SUBCASE("Title slug") {
        const auto specs = make_word_emphasis_animators(
            WordEmphasisKind::Title, std::nullopt, Frame{0}, 2);
        REQUIRE(specs.size() == 2);
        CHECK(specs[0].id == "word_emphasis_title_letter_0");
        CHECK(specs[0].selectors.front().id == "sel_title_letter_0");
        CHECK(specs[1].id == "word_emphasis_title_letter_1");
        CHECK(specs[1].selectors.front().id == "sel_title_letter_1");
    }
    SUBCASE("Emph slug") {
        const auto specs = make_word_emphasis_animators(
            WordEmphasisKind::Emph, std::nullopt, Frame{0}, 2);
        REQUIRE(specs.size() == 2);
        CHECK(specs[0].id == "word_emphasis_emph_letter_0");
        CHECK(specs[0].selectors.front().id == "sel_emph_letter_0");
        CHECK(specs[1].id == "word_emphasis_emph_letter_1");
        CHECK(specs[1].selectors.front().id == "sel_emph_letter_1");
    }
}

// ── make_word_emphasis_animators: per-kind structural parity ─────────────

TEST_CASE("make_word_emphasis_animators — Title and Emph variants behave identically to Name") {
    for (auto kind : {WordEmphasisKind::Name,
                      WordEmphasisKind::Title,
                      WordEmphasisKind::Emph}) {
        SUBCASE("kind = " + std::string{[kind] {
            switch (kind) {
                case WordEmphasisKind::Name:  return "Name";
                case WordEmphasisKind::Title: return "Title";
                case WordEmphasisKind::Emph:  return "Emph";
                case WordEmphasisKind::None:  return "None";
            }
            return "?";
        }()}) {
            const auto specs = make_word_emphasis_animators(
                kind, std::nullopt, Frame{0}, 4);
            REQUIRE(specs.size() == 4);
            for (const auto& s : specs) {
                CHECK(s.enabled);
                REQUIRE(s.selectors.size() == 1);
                CHECK(s.selectors.front().unit == TextSelectorUnit::Character);
                CHECK(s.selectors.front().shape == TextSelectorShape::Square);
            }
        }
    }
}

// ── accent wiring ────────────────────────────────────────────────────────

TEST_CASE("make_word_emphasis_animators — accent colour wires a FillColor property") {
    constexpr std::size_t N = 3;
    const Color accent{0.9f, 0.2f, 0.1f, 1.0f};

    const auto specs = make_word_emphasis_animators(
        WordEmphasisKind::Emph, accent, Frame{0}, N);

    REQUIRE(specs.size() == N);
    for (const auto& s : specs) {
        // Scale + Opacity + FillColor = 3 properties when accent present.
        REQUIRE(s.properties.size() == 3);
        bool saw_fill = false;
        for (const auto& p : s.properties) {
            if (std::holds_alternative<FillColorProperty>(p)) {
                saw_fill = true;
                const auto& fp = std::get<FillColorProperty>(p);
                CHECK(fp.color.r == doctest::Approx(0.9f).epsilon(1e-3));
                CHECK(fp.color.g == doctest::Approx(0.2f).epsilon(1e-3));
                CHECK(fp.color.b == doctest::Approx(0.1f).epsilon(1e-3));
                CHECK(fp.color.a == doctest::Approx(1.0f).epsilon(1e-3));
            }
        }
        CHECK(saw_fill);
    }
}

// ── End-to-end: label roundtrip ──────────────────────────────────────────

TEST_CASE("e2e — name-prefixed semantic_id roundtrips through the helper chain") {
    constexpr std::string_view kRawSemanticId = "name:Giulia";
    const auto parsed = parse_emphasis_prefix(kRawSemanticId);
    REQUIRE(parsed.kind == WordEmphasisKind::Name);
    REQUIRE(parsed.remainder == "Giulia");

    const auto specs = make_word_emphasis_animators(
        parsed.kind, std::nullopt, Frame{0}, parsed.remainder.size());
    CHECK(specs.size() == parsed.remainder.size());

    CHECK(strip_emphasis_prefix(kRawSemanticId) == "Giulia");
}
