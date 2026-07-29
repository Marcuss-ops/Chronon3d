// ═══════════════════════════════════════════════════════════════════════════
// src/text/word_emphasis_animators.cpp
//
// Implementation of `parse_emphasis_prefix` + `make_word_emphasis_animators`.
// Composition uses exclusively the canonical primitives from
// `chronon3d/animation`, `chronon3d/text/animation`, and
// `chronon3d/text/glyph_selector_spec` — no new singletons / registries /
// caches.
//
// Iteration v3 (post v2-review fixes):
//   * Selector windows are epsilon-shimmed so adjacent-letter windows
//     share no endpoint — Square shape treats `end` as inclusive, so
//     a shared endpoint would otherwise double-apply the selector at
//     the boundary glyph. The last letter keeps the full 100.0f
//     boundary so coverage is preserved.
//   * Selector windows + spec/selector ids + cat-5 ticket unchanged
//     from v2 (already correct).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/presets/text/word_emphasis_animators.hpp>

#include <string>

namespace chronon3d::presets::text {

namespace {

// Lengths of the recognised prefix tokens (without trailing colon).
constexpr std::string_view kNamePrefix{"name:"};
constexpr std::string_view kTitlePrefix{"title:"};
constexpr std::string_view kEmphPrefix{"emph:"};
constexpr std::string_view kBasePrefix{"base:"};
constexpr std::string_view kPhrasePrefix{"phrase:"};
constexpr std::string_view kWordPrefix{"word:"};

constexpr std::size_t kNamePrefixLen = 5;   // "name:"  (n,a,m,e,:)
constexpr std::size_t kTitlePrefixLen = 6;  // "title:" (t,i,t,l,e,:)
constexpr std::size_t kEmphPrefixLen = 5;   // "emph:"  (e,m,p,h,:)
constexpr std::size_t kBasePrefixLen = 5;
constexpr std::size_t kPhrasePrefixLen = 7;
constexpr std::size_t kWordPrefixLen = 5;

// Selector-window epsilon: shaves a tiny amount off each per-letter
// window's `end` (except the last letter's) so adjacent windows do not
// share an exact endpoint.  Without this, the canonical Square shape
// (which treats `end` as inclusive) would double-apply the selector at
// the boundary glyph between letter `i` and letter `i+1`.  1e-4 (0.0001
// normalised units, i.e. 0.01% of the full 0..100 range) is small enough
// to be invisible to any real glyph's unit-position (which carries far
// more precision than that) yet large enough to be visible to a `f32`
// comparison at the boundary.
constexpr f32 kSelectorEpsilon = 1e-4f;

// Stable, grep-discoverable token for `WordEmphasisKind`. Mirrors the
// canonical parse prefix tokens so a downstream grep for "name:" /
// "title:" / "emph:" finds BOTH the parser input and the per-spec ids.
// The unreachable-fallback return preserves well-defined behaviour on
// a forward-declared future enum value (kept for forward-compat
// extension, in line with the project's C++20 baseline — `std::unreachable`
// is C++23 and not yet adopted).
constexpr std::string_view kind_token(WordEmphasisKind k) noexcept {
    switch (k) {
        case WordEmphasisKind::Name:  return kNamePrefix;   // "name:"
        case WordEmphasisKind::Phrase: return kPhrasePrefix;
        case WordEmphasisKind::Word:  return kWordPrefix;
        case WordEmphasisKind::Base:  return kBasePrefix;
        case WordEmphasisKind::Title: return kTitlePrefix;
        case WordEmphasisKind::Emph:  return kEmphPrefix;
        case WordEmphasisKind::None:  return "none:";
    }
    return "none:";
}

} // namespace

EmphasisParseResult parse_emphasis_prefix(std::string_view semantic_id) {
    if (semantic_id.empty()) {
        return {WordEmphasisKind::None, semantic_id};
    }
    if (semantic_id.size() >= kNamePrefixLen &&
        semantic_id.compare(0, kNamePrefixLen, kNamePrefix) == 0) {
        return {WordEmphasisKind::Name, semantic_id.substr(kNamePrefixLen)};
    }
    if (semantic_id.size() >= kPhrasePrefixLen &&
        semantic_id.compare(0, kPhrasePrefixLen, kPhrasePrefix) == 0) {
        return {WordEmphasisKind::Phrase, semantic_id.substr(kPhrasePrefixLen)};
    }
    if (semantic_id.size() >= kTitlePrefixLen &&
        semantic_id.compare(0, kTitlePrefixLen, kTitlePrefix) == 0) {
        return {WordEmphasisKind::Title, semantic_id.substr(kTitlePrefixLen)};
    }
    if (semantic_id.size() >= kWordPrefixLen &&
        semantic_id.compare(0, kWordPrefixLen, kWordPrefix) == 0) {
        return {WordEmphasisKind::Word, semantic_id.substr(kWordPrefixLen)};
    }
    if (semantic_id.size() >= kEmphPrefixLen &&
        semantic_id.compare(0, kEmphPrefixLen, kEmphPrefix) == 0) {
        return {WordEmphasisKind::Emph, semantic_id.substr(kEmphPrefixLen)};
    }
    if (semantic_id.size() >= kBasePrefixLen &&
        semantic_id.compare(0, kBasePrefixLen, kBasePrefix) == 0) {
        return {WordEmphasisKind::Base, semantic_id.substr(kBasePrefixLen)};
    }
    return {WordEmphasisKind::None, semantic_id};
}

std::vector<TextAnimatorSpec> make_word_emphasis_animators(
    WordEmphasisKind kind,
    std::optional<Color> accent,
    Frame start_frame,
    std::size_t letter_count) {

    if (kind == WordEmphasisKind::None || letter_count == 0) {
        return {};
    }

    std::vector<TextAnimatorSpec> specs;
    specs.reserve(letter_count);

    // Drop the trailing colon from the kind token to keep spec ids
    // slug-like (`word_emphasis_name_letter_0`).
    const std::string kind_slug(kind_token(kind).substr(0, kind_token(kind).size() - 1));

    // ── Per-letter timeline constants ──────────────────────────────────
    // Two-frame stagger between consecutive letters for cascading impact;
    // six frames up + six frames down gives a clearly readable bounce
    // without crowding neighbouring words. Opacity ramps over the
    // first 4 frames with Easing::OutCubic.
    const i64 stagger_step = 2;
    const i64 up_duration  = 6;
    const i64 settle_dur   = 6;
    const f32 start_scale  = 0.7f;
    const f32 peak_scale   = 1.15f;
    const f32 end_scale    = 1.0f;
    // `Frame::integral()` returns i64; the practical range of `base_frame`
    // is bounded by the renderer's timeline (≤ ~2^24 frames at 60 fps for
    // any reasonable production clip).  Stagger_step ≤ 4 keeps the
    // per-letter arithmetic well below 2^62.  Future-proofing via a
    // checked_add helper is deferred (no such helper exists in the
    // canonical Frame type today).
    const i64 base_frame   = start_frame.integral();

    // Width of each per-letter selector window in normalised 0..100 units.
    // Always > 0 (we early-returned when letter_count == 0).  With Five
    // evenly-spaced letters, each window is exactly 20 units wide.
    const f32 per_letter_unit = 100.0f / static_cast<f32>(letter_count);

    for (std::size_t i = 0; i < letter_count; ++i) {
        TextAnimatorSpec spec;
        spec.id = "word_emphasis_" + kind_slug + "_letter_" + std::to_string(i);
        spec.enabled = true;
        spec.transform_mode = TextPropertyBlendMode::Add;
        spec.color_mode = TextPropertyBlendMode::Replace;

        // Selector: target ONLY the i-th Character unit in normalised 0..100.
        // Square shape guarantees crisp 1.0 / 0.0 boundary at window edges —
        // Smooth would smear the per-letter animation onto adjacent letters.
        // Epsilon-shim on `end` (except for the last letter) prevents the
        // canonical Square shape's inclusive-end semantics from creating
        // a double-apply rot at the boundary glyph between letter `i`
        // and letter `i+1`.  The last letter gets the full 100.0f
        // boundary so trailing-edge coverage is preserved.
        const f32 sel_start = static_cast<f32>(i) * per_letter_unit;
        const f32 sel_end   = (i + 1 == letter_count)
            ? 100.0f
            : sel_start + per_letter_unit - kSelectorEpsilon;

        GlyphSelectorSpec sel;
        sel.id              = "sel_" + kind_slug + "_letter_" + std::to_string(i);
        sel.unit            = TextSelectorUnit::Character;
        sel.shape           = TextSelectorShape::Square;     // CRISP per-letter
        sel.order           = TextSelectorOrder::Forward;
        sel.combine         = SelectorCombineMode::Replace;
        sel.start           = AnimatedValue<f32>(sel_start);
        sel.end             = AnimatedValue<f32>(sel_end);
        sel.amount          = AnimatedValue<f32>(100.0f);
        sel.exclude_spaces  = false;       // letters are not whitespace anyway
        spec.selectors.push_back(std::move(sel));

        // ── Scale ramp: 0.7 → 1.15 (OutBack) → 1.0 (InOutSine settle) ───
        const Frame up_start   = Frame{base_frame + static_cast<i64>(i) * stagger_step};
        const Frame up_end     = up_start + Frame{up_duration};
        const Frame settle_end = up_end + Frame{settle_dur};

        ScaleProperty scale;
        scale.value.clear();
        scale.value.add_keyframe(up_start, Vec3{start_scale, start_scale, start_scale});
        scale.value.add_keyframe(
            up_end,
            Vec3{peak_scale, peak_scale, peak_scale},
            EasingCurve{Easing::OutBack});
        scale.value.add_keyframe(
            settle_end,
            Vec3{end_scale, end_scale, end_scale},
            EasingCurve{Easing::InOutSine});
        spec.properties.push_back(scale);

        // ── Opacity ramp: 0 → 1 over first 4 frames (OutCubic) ────────
        OpacityProperty opacity;
        opacity.value.clear();
        opacity.value.add_keyframe(up_start, 0.0f);
        opacity.value.add_keyframe(
            up_start + Frame{4},
            1.0f,
            EasingCurve{Easing::OutCubic});
        spec.properties.push_back(opacity);

        // ── Accent fill (per-letter): only emitted when caller supplied
        //    an `accent` color; transform_mode=Replace so the last spec
        //    painted wins. With the same color on each letter this is
        //    visually identical to a single shared color, but emitting
        //    it here keeps the color_mode contract local to each spec.
        if (accent.has_value()) {
            FillColorProperty fill;
            fill.color = *accent;
            spec.properties.push_back(fill);
        }

        specs.push_back(std::move(spec));
    }

    return specs;
}

std::vector<TextAnimatorSpec> make_light_text_animators(
    WordEmphasisKind kind,
    std::optional<Color> accent,
    Frame start_frame,
    std::size_t span_index,
    std::size_t span_count) {
    if (span_count == 0 || span_index >= span_count) return {};
    if (kind == WordEmphasisKind::None) kind = WordEmphasisKind::Base;

    TextAnimatorSpec spec;
    spec.enabled = true;
    spec.id = "text_v1_" + std::string(kind_token(kind).substr(0, kind_token(kind).size() - 1))
        + "_span_" + std::to_string(span_index);

    const f32 unit = 100.0f / static_cast<f32>(span_count);
    GlyphSelectorSpec selector;
    selector.id = spec.id + "_selector";
    selector.unit = TextSelectorUnit::Word;
    selector.shape = TextSelectorShape::Square;
    selector.start = AnimatedValue<f32>{static_cast<f32>(span_index) * unit};
    selector.end = AnimatedValue<f32>{static_cast<f32>(span_index + 1) * unit};
    selector.amount = AnimatedValue<f32>{100.0f};
    selector.exclude_spaces = true;
    spec.selectors.push_back(std::move(selector));

    const Frame f0 = start_frame;
    const Frame f1 = f0 + Frame{2};
    const Frame f2 = f0 + Frame{6};
    auto scale = [&](f32 from, f32 peak) {
        ScaleProperty p;
        p.value.clear();
        p.value.add_keyframe(f0, Vec3{from, from, 1.0f});
        p.value.add_keyframe(f1, Vec3{peak, peak, 1.0f}, EasingCurve{Easing::OutBack});
        p.value.add_keyframe(f2, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::InOutSine});
        spec.properties.push_back(std::move(p));
    };
    auto opacity = [&](Frame end) {
        OpacityProperty p;
        p.value.clear();
        p.value.add_keyframe(f0, 0.0f);
        p.value.add_keyframe(end, 1.0f, EasingCurve{Easing::OutCubic});
        spec.properties.push_back(std::move(p));
    };

    switch (kind) {
        case WordEmphasisKind::Base: {
            PositionProperty p;
            p.value.clear();
            p.value.add_keyframe(f0, Vec3{0.0f, 10.0f, 0.0f});
            p.value.add_keyframe(f2, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
            spec.properties.push_back(std::move(p));
            opacity(f2);
            break;
        }
        case WordEmphasisKind::Phrase:
        case WordEmphasisKind::Title:
            scale(0.96f, 1.02f);
            opacity(f2);
            break;
        case WordEmphasisKind::Name:
            scale(0.94f, 1.04f);
            opacity(f2);
            if (accent) spec.properties.push_back(FillColorProperty{*accent});
            break;
        case WordEmphasisKind::Word:
        case WordEmphasisKind::Emph:
            scale(0.90f, 1.08f);
            opacity(f1);
            if (accent) spec.properties.push_back(FillColorProperty{*accent});
            break;
        case WordEmphasisKind::None:
            break;
    }
    return {std::move(spec)};
}

} // namespace chronon3d::presets::text
