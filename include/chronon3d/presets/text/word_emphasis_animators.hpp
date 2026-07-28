// ═══════════════════════════════════════════════════════════════════════════
// presets/text/word_emphasis_animators.hpp
//
// Tag-driven per-word emphasis animations (Variant A — TICKET-WORD-EMPHASIS).
//
// Reuses the canonical `TimedWord::semantic_id` field already populated by
// the SRT/VTT/JSON subtitle adapters and adds a tiny prefix parser that
// classifies the semantic_id into `WordEmphasisKind`. A factory function
// emits a `std::vector<TextAnimatorSpec>` (one spec per letter) using the
// canonical `AnimatedValue<Vec3>` + `EasingCurve` primitives — yielding a
// per-letter stagger-bounce animation that slots into the existing
// `evaluate_animator_stack[_into]` evaluator without touching the engine.
//
// Cat-3 anti-duplication: zero new singletons / registries / caches /
// font/color catalogs. All primitives come from existing producers.
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

// ── WordEmphasisKind — semantic class of an emphasised word ─────────────

enum class WordEmphasisKind {
    None,    // no prefix; default; not emphasised
    Name,    // `name:` prefix     → proper nouns / on-screen names
    Title,   // `title:` prefix    → titles / headings
    Emph     // `emph:` prefix     → arbitrary emphasised keywords
};

// ── Prefix parse result ─────────────────────────────────────────────────

struct EmphasisParseResult {
    WordEmphasisKind kind{WordEmphasisKind::None};
    std::string_view remainder{};
};

// ── parse_emphasis_prefix ───────────────────────────────────────────────
//
// Recognised prefixes (case-sensitive, ASCII):
//   "name:"    →  WordEmphasisKind::Name
//   "title:"   →  WordEmphasisKind::Title
//   "emph:"    →  WordEmphasisKind::Emph
// Anything else → WordEmphasisKind::None (remainder is the full input).
//
// Empty input → {None, ""}.
// Nested prefixes (`emph:foo:bar`) strip only the first prefix; the colon
// in the remainder is preserved (the renderer / authoring layer can interpret
// multi-segment semantic_ids as it wishes).

[[nodiscard]] EmphasisParseResult parse_emphasis_prefix(std::string_view semantic_id);

// ── is_emphasis_kind ─────────────────────────────────────────────────────

[[nodiscard]] inline bool is_emphasis_kind(WordEmphasisKind kind) noexcept {
    return kind != WordEmphasisKind::None;
}

// ── strip_emphasis_prefix ────────────────────────────────────────────────
//
// Returns the visible-text form (remainder) of a semantic_id with the
// emphasis prefix removed. Uses parse_emphasis_prefix under the hood.

[[nodiscard]] inline std::string strip_emphasis_prefix(std::string_view semantic_id) {
    return std::string{parse_emphasis_prefix(semantic_id).remainder};
}

// ── make_word_emphasis_animators ─────────────────────────────────────────
//
// Emits one TextAnimatorSpec per letter, each with a per-letter Character
// selector that targets ONLY its own glyph (per-letter window in
// normalised 0..100 range so the canonical evaluator's pre-built unit map
// picks the right glyph).  Each spec carries the canonical `AnimatedValue`
// ramps for the per-letter stagger-bounce (Variant A):
//
//   * ScaleProperty  — 0.7 → 1.15 (Easing::OutBack) → 1.0 (settle),
//                       starting at start_frame + letter_index * stagger_step
//   * OpacityProperty — 0 → 1 over the first 4 frames (Easing::OutCubic)
//
// The accent colour is reserved for a Future FillColorProperty entry that
// can be appended when the renderer should switch from white→accent; the
// parameter is kept now so callers can wire it without touching the API
// again (Cat-5 3-doc rule "no churn retroattivo").
//
// Return value: a std::vector<TextAnimatorSpec> sized `letter_count`
// (empty when kind == None or letter_count == 0).  The vector plugs into
// `evaluate_animator_stack[_into]` unchanged — no renderer-side glue
// needed.

[[nodiscard]] std::vector<TextAnimatorSpec> make_word_emphasis_animators(
    WordEmphasisKind kind,
    std::optional<Color> accent,
    Frame start_frame,
    std::size_t letter_count);

} // namespace chronon3d::presets::text
