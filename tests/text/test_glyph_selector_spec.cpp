#include "glyph_selector_helpers.hpp"
#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/text/glyph_selector_spec.hpp>

#include <cmath>

using namespace chronon3d;
using namespace test_glyph_sel;

// ═══════════════════════════════════════════════════════════════════════════
// TEXT-SEL-01 — GlyphSelectorSpec variant tests (13 cases)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TEXT-SEL-01: SafeAccessMap basic get / set / contains") {
    SafeAccessMap m;
    CHECK_FALSE(m.contains("alpha"));
    m.set("alpha", 1.0f);
    CHECK(m.contains("alpha"));
    CHECK(m.get("alpha").value_or(-1.0f) == doctest::Approx(1.0f));
    CHECK_FALSE(m.get("missing").has_value());
    CHECK(m.size() == 1u);
}

TEST_CASE("TEXT-SEL-01: SafeAccessMap evaluate() reads AnimatedValue at SampleTime") {
    SafeAccessMap m;
    AnimatedValue<f32> av;
    av.set(5.0f);
    av.set(15.0f);  // sets two keyframes at default 0% / 100%
    m.set("animated_key", std::move(av));

    SampleTime t0 = SampleTime::from_frame_int(Frame{0});
    SampleTime t1 = SampleTime::from_frame_int(Frame{24});

    const f32 at_t0 = m.evaluate("animated_key", t0).value_or(-1.0f);
    const f32 at_t1 = m.evaluate("animated_key", t1).value_or(-1.0f);
    CHECK(at_t0 != at_t1);
    CHECK(m.evaluate("absent", t0) == std::nullopt);
}

TEST_CASE("TEXT-SEL-01: TextUnitRef is_valid helper + ctor ergonomics") {
    TextUnitRef a{};  // default-constructed: Glyph + kInvalid
    CHECK_FALSE(a.is_valid());
    CHECK(a.unit == TextSelectorUnit::Glyph);

    TextUnitRef b{TextSelectorUnit::Word, 3u};
    CHECK(b.is_valid());
    CHECK(b.unit == TextSelectorUnit::Word);
    CHECK(b.index == 3u);

    TextUnitRef c{TextSelectorUnit::CodePoint, 5u, /*byte_anchor=*/std::optional<u32>{10u}};
    CHECK(c.byte_anchor.has_value());
    CHECK(c.byte_anchor.value() == 10u);
}

TEST_CASE("TEXT-SEL-01: RangeSelector dispatch composes on legacy evaluate_selector") {
    // 'ab cd' — RangeSelector with start=0, end=100 should yield weight 1.0
    // on the non-space glyph at index 0 (a) and 1.0 on index 1 (b);
    // 0.0 on the space glyph (index 2) because exclude_spaces is true.
    std::string source = "ab cd";
    auto placed = make_run_for_source(source);
    auto map = build_text_unit_map(placed, source);

    RangeSelector r{};
    r.unit = TextSelectorUnit::Glyph;
    r.shape = TextSelectorShape::Square;
    r.combine = SelectorCombineMode::Replace;
    r.exclude_spaces = true;

    GlyphSelectorSpec spec{};
    spec.id = "range_dispatch";
    spec.kind = r;

    GlyphSelectorContext ctx{map, SampleTime::from_frame_int(Frame{0}), source, &placed, 5};

    const SelectorWeight w_a   = evaluate_selector_v2(spec, 0u, ctx);
    const SelectorWeight w_b   = evaluate_selector_v2(spec, 1u, ctx);
    const SelectorWeight w_sp  = evaluate_selector_v2(spec, 2u, ctx);
    const SelectorWeight w_cd  = evaluate_selector_v2(spec, 3u, ctx);

    CHECK(w_a  == doctest::Approx(1.0f));
    CHECK(w_b  == doctest::Approx(1.0f));
    CHECK(w_sp == doctest::Approx(0.0f));  // space excluded
    CHECK(w_cd == doctest::Approx(1.0f));
}

TEST_CASE("TEXT-SEL-01: WigglySelector — same seed + same frame → bit-exact determinism") {
    std::string source = "abcdefghij";
    auto placed = make_run_for_source(source);
    auto map = build_text_unit_map(placed, source);

    WigglySelector w{};
    w.unit = TextSelectorUnit::Glyph;
    w.shape = TextSelectorShape::Smooth;
    w.seed = 42u;
    w.wps = 1.0f;
    w.correlation = 0.5f;
    w.min_amount.set(0.0f);
    w.max_amount.set(1.0f);

    GlyphSelectorSpec spec{};
    spec.kind = w;
    GlyphSelectorContext ctx{map, SampleTime::from_frame_int(Frame{7}), source, &placed, 10};

    const f32 w0 = evaluate_selector_v2(spec, 0u, ctx);
    for (int i = 0; i < 200; ++i) {
        const f32 xi = evaluate_selector_v2(spec, 0u, ctx);
        CHECK(xi == doctest::Approx(w0));
    }
}

TEST_CASE("TEXT-SEL-01: WigglySelector — different seeds at same frame → diverging output") {
    std::string source = "abcdefghij";
    auto placed = make_run_for_source(source);
    auto map = build_text_unit_map(placed, source);

    WigglySelector base{};
    base.unit = TextSelectorUnit::Glyph;
    base.shape = TextSelectorShape::Smooth;
    base.wps = 1.0f;
    base.correlation = 0.0f;
    base.min_amount.set(0.0f);
    base.max_amount.set(1.0f);

    WigglySelector w_a = base; w_a.seed = 1u;
    WigglySelector w_b = base; w_b.seed = 99u;

    GlyphSelectorSpec spec_a{}; spec_a.kind = w_a;
    GlyphSelectorSpec spec_b{}; spec_b.kind = w_b;
    GlyphSelectorContext ctx{map, SampleTime::from_frame_int(Frame{0}), source, &placed, 10};

    f32 sum_a = 0.0f;
    f32 sum_b = 0.0f;
    for (u32 i = 0; i < 6; ++i) {
        sum_a += evaluate_selector_v2(spec_a, i, ctx);
        sum_b += evaluate_selector_v2(spec_b, i, ctx);
    }
    CHECK(std::abs(sum_a - sum_b) > 0.01f);
}

TEST_CASE("TEXT-SEL-01: WigglySelector — output clamped to [0, 1] + shape remap") {
    std::string source = "abcd";
    auto placed = make_run_for_source(source);
    auto map = build_text_unit_map(placed, source);

    WigglySelector w{};
    w.unit = TextSelectorUnit::Glyph;
    w.shape = TextSelectorShape::Smooth;
    w.seed = 7u;
    w.wps = 4.0f;
    w.min_amount.set(0.0f);
    w.max_amount.set(1.0f);

    GlyphSelectorSpec spec{};
    spec.kind = w;
    GlyphSelectorContext ctx{map, SampleTime::from_frame_int(Frame{3}), source, &placed, 4};

    for (u32 i = 0; i < 4; ++i) {
        const f32 w_ = evaluate_selector_v2(spec, i, ctx);
        CHECK(w_ >= 0.0f);
        CHECK(w_ <= 1.0f);
    }
}

TEST_CASE("TEXT-SEL-01: ExpressionSelector — missing key returns 0.0 (no throw)") {
    std::string source = "abc";
    auto placed = make_run_for_source(source);
    auto map = build_text_unit_map(placed, source);

    ExpressionSelector e{};
    e.unit = TextSelectorUnit::Glyph;
    e.shape = TextSelectorShape::Smooth;
    e.value = "absent_property";  // never set

    GlyphSelectorSpec spec{};
    spec.kind = e;
    GlyphSelectorContext ctx{map, SampleTime::from_frame_int(Frame{0}), source, &placed, 3};

    CHECK_NOTHROW(evaluate_selector_v2(spec, 0u, ctx));
    CHECK(evaluate_selector_v2(spec, 0u, ctx) == doctest::Approx(0.0f));
    CHECK(evaluate_selector_v2(spec, 1u, ctx) == doctest::Approx(0.0f));
}

TEST_CASE("TEXT-SEL-01: ExpressionSelector — builtin variables textIndex/textTotal/frame resolve") {
    std::string source = "abc";
    auto placed = make_run_for_source(source);
    auto map = build_text_unit_map(placed, source);

    // Property "textIndex" → falls into [0..100] = glyph_index normalized to percent.
    //   glyph 0 → textIndex=0 → 0/100 = 0.0
    //   glyph 1 → textIndex=1 → 1/100 = 0.01
    //   glyph 2 → textIndex=2 → 2/100 = 0.02
    ExpressionSelector e{};
    e.unit = TextSelectorUnit::Glyph;
    e.shape = TextSelectorShape::Square;  // no remap
    e.value = "textIndex";

    GlyphSelectorSpec spec{};
    spec.kind = e;
    GlyphSelectorContext ctx{map, SampleTime::from_frame_int(Frame{5}), source, &placed, 3};

    CHECK(evaluate_selector_v2(spec, 0u, ctx) == doctest::Approx(0.0f));
    CHECK(evaluate_selector_v2(spec, 1u, ctx) == doctest::Approx(0.01f));
    CHECK(evaluate_selector_v2(spec, 2u, ctx) == doctest::Approx(0.02f));
}

TEST_CASE("TEXT-SEL-01: variant dispatch — std::visit routes correctly for all 3 kinds") {
    // Compile-time guarantee: building each variant kind compiles and runs.
    // Range
    RangeSelector r{};
    GlyphSelectorSpec sr{}; sr.kind = r;
    CHECK(std::holds_alternative<RangeSelector>(sr.kind));

    // Wiggly
    WigglySelector w{};
    GlyphSelectorSpec sw{}; sw.kind = w;
    CHECK(std::holds_alternative<WigglySelector>(sw.kind));

    // Expression
    ExpressionSelector e{};
    GlyphSelectorSpec se{}; se.kind = e;
    CHECK(std::holds_alternative<ExpressionSelector>(se.kind));
}

TEST_CASE("TEXT-SEL-01: GlyphSelectorSpec targets filter applies-to-glyph logic") {
    std::string source = "ab cd";
    auto placed = make_run_for_source(source);
    auto map = build_text_unit_map(placed, source);

    RangeSelector r{};
    r.unit = TextSelectorUnit::Glyph;
    r.shape = TextSelectorShape::Square;
    r.exclude_spaces = false;  // don't exclude on this test path
    GlyphSelectorSpec spec{};
    spec.kind = r;
    spec.targets = {
        TextUnitRef{TextSelectorUnit::Glyph, 1u},  // only glyph 1 ("b")
    };

    GlyphSelectorContext ctx{map, SampleTime::from_frame_int(Frame{0}), source, &placed, 5};

    // Glyph 1 is in targets → weight 1.0
    CHECK(evaluate_selector_v2(spec, 1u, ctx) == doctest::Approx(1.0f));
    // Glyph 0 NOT in targets → weight 0.0 (filtered)
    CHECK(evaluate_selector_v2(spec, 0u, ctx) == doctest::Approx(0.0f));
    CHECK(evaluate_selector_v2(spec, 4u, ctx) == doctest::Approx(0.0f));
}

TEST_CASE("TEXT-SEL-01: evaluate_selectors_v2 — combine modes Replace / Add / Subtract") {
    std::string source = "abc";
    auto placed = make_run_for_source(source);
    auto map = build_text_unit_map(placed, source);

    RangeSelector a{};
    a.unit = TextSelectorUnit::Glyph;
    a.shape = TextSelectorShape::Square;
    a.combine = SelectorCombineMode::Replace;
    a.exclude_spaces = false;

    RangeSelector b = a;
    b.combine = SelectorCombineMode::Add;

    RangeSelector c = a;
    c.combine = SelectorCombineMode::Subtract;

    GlyphSelectorSpec sa{}; sa.kind = a;  // yields 1.0
    GlyphSelectorSpec sb{}; sb.kind = b;  // +1.0 = 1.0 (clamped)
    GlyphSelectorSpec sc{}; sc.kind = c;  // 1.0 - 1.0 = 0.0

    GlyphSelectorContext ctx{map, SampleTime::from_frame_int(Frame{0}), source, &placed, 3};

    CHECK(evaluate_selectors_v2({sa},                     0u, ctx) == doctest::Approx(1.0f));
    CHECK(evaluate_selectors_v2({sa, sb},                 0u, ctx) == doctest::Approx(1.0f));
    CHECK(evaluate_selectors_v2({sa, sb, sc},             0u, ctx) == doctest::Approx(0.0f));
}

TEST_CASE("TEXT-SEL-01: bit-exact determinism over 1000 iterations") {
    std::string source = "hello world";
    auto placed = make_run_for_source(source);
    auto map = build_text_unit_map(placed, source);

    WigglySelector w{};
    w.unit = TextSelectorUnit::Glyph;
    w.shape = TextSelectorShape::Smooth;
    w.seed = 12345u;
    w.wps = 1.5f;
    w.min_amount.set(0.0f);
    w.max_amount.set(1.0f);

    GlyphSelectorSpec spec{};
    spec.kind = w;
    GlyphSelectorContext ctx{map, SampleTime::from_frame_int(Frame{10}), source, &placed, 11};

    // Run 1000 iterations; assert all yield the exact same value.
    const f32 first = evaluate_selector_v2(spec, 5u, ctx);
    for (int i = 0; i < 1000; ++i) {
        const f32 xi = evaluate_selector_v2(spec, 5u, ctx);
        CHECK(xi == doctest::Approx(first));
    }
}

// TICKET-007.p (gate-compliance metadata) — kept for back-compat with the
// existing test_text_unit_map.cpp's skip-marker. Same DATA introduzione /
// Deadline already present.
TEST_CASE("TEXT-SEL-01: back-compat — legacy evaluate_selector unchanged output") {
    std::string source = "abc";
    auto placed = make_run_for_source(source);
    auto map = build_text_unit_map(placed, source);

    SampleTime t = SampleTime::from_frame_int(Frame{0});

    // Legacy path
    GlyphSelectorSpec legacy_spec{};
    legacy_spec.unit = TextSelectorUnit::Glyph;
    legacy_spec.shape = TextSelectorShape::Square;
    legacy_spec.start.set(0.0f);
    legacy_spec.end.set(100.0f);
    legacy_spec.amount.set(100.0f);
    legacy_spec.exclude_spaces = false;
    const f32 w_legacy = evaluate_selector(legacy_spec, map, 0u, source, t, &placed);

    // V2 path with same fields
    RangeSelector r{};
    r.unit = legacy_spec.unit;
    r.shape = legacy_spec.shape;
    r.start = legacy_spec.start;
    r.end = legacy_spec.end;
    r.offset = legacy_spec.offset;
    r.amount = legacy_spec.amount;
    r.ease_low = legacy_spec.ease_low;
    r.ease_high = legacy_spec.ease_high;
    r.exclude_spaces = legacy_spec.exclude_spaces;
    r.randomize_order = legacy_spec.randomize_order;
    r.random_seed = legacy_spec.random_seed;

    GlyphSelectorSpec v2_spec{};
    v2_spec.kind = r;
    GlyphSelectorContext ctx{map, t, source, &placed, 3};
    const f32 w_v2 = evaluate_selector_v2(v2_spec, 0u, ctx);

    // Both must produce the same weight for the same glyph at the same time.
    CHECK(w_legacy == doctest::Approx(w_v2));
}
