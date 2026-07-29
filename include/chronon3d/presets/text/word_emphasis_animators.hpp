// ═══════════════════════════════════════════════════════════════════════════
// presets/text/word_emphasis_animators.hpp
//
// Canonical semantic text-emphasis factories.
//
// Two surfaces intentionally share the same parser, selector model and
// TextAnimatorSpec primitives:
//   1. make_lightweight_emphasis_animator() — one animator per semantic span;
//      this is the preferred production path for maximum throughput.
//   2. make_word_emphasis_animators() — the existing optional per-letter
//      stagger path retained for callers that explicitly want that look.
//
// No registry, resolver, sampler or cache is duplicated here. Both factories
// feed the canonical text animator evaluator unchanged.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/text/animation/text_animator_spec.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::presets::text {

// Semantic role carried by TimedWord::semantic_id or direct authoring code.
// Existing enum names remain stable:
//   Title == important phrase
//   Emph  == important word
//
enum class WordEmphasisKind {
    None,
    Base,   // `base:` — lightweight neutral entrance
    Name,   // `name:` — special person/place/product name
    Title,  // `title:` or `phrase:` — important phrase / heading
    Emph    // `emph:` or `word:` — important keyword
};

struct EmphasisParseResult {
    WordEmphasisKind kind{WordEmphasisKind::None};
    std::string_view remainder{};
};

// Recognised prefixes are case-sensitive ASCII:
//   base:
//   name:
//   title:  / phrase:
//   emph:   / word:
//
// Aliases map to the existing enum values, so old semantic data remains valid.
[[nodiscard]] EmphasisParseResult parse_emphasis_prefix(std::string_view semantic_id);

[[nodiscard]] inline bool is_emphasis_kind(WordEmphasisKind kind) noexcept {
    return kind != WordEmphasisKind::None;
}

[[nodiscard]] inline std::string strip_emphasis_prefix(std::string_view semantic_id) {
    return std::string{parse_emphasis_prefix(semantic_id).remainder};
}

// Preferred production factory: emits at most ONE animator for the supplied
// semantic span. The caller supplies the canonical selector, which may target
// a complete cue, phrase, name or single word.
//
// Profiles:
//   Base  — short fade + 10px rise
//   Title — soft phrase scale reveal
//   Name  — clean name pop
//   Emph  — fast keyword punch
//
// Each profile uses one selector and no more than three properties. Optional
// accent colour is added as a static FillColorProperty without introducing a
// second colour animation path.
[[nodiscard]] std::optional<TextAnimatorSpec> make_lightweight_emphasis_animator(
    WordEmphasisKind kind,
    std::optional<Color> accent,
    Frame start_frame,
    GlyphSelectorSpec selector,
    std::string_view id_suffix = {});

// Existing expressive per-letter factory. Name / Title / Emph retain the
// stagger-bounce behaviour for explicit callers. Base uses the lightweight
// whole-span implementation so its cost remains O(1) even for long text.
[[nodiscard]] std::vector<TextAnimatorSpec> make_word_emphasis_animators(
    WordEmphasisKind kind,
    std::optional<Color> accent,
    Frame start_frame,
    std::size_t letter_count);

} // namespace chronon3d::presets::text
