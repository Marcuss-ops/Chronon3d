#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// animated_text_document.hpp — Animated text document with keyframe
// transitions (Hold, Cut, DissolveLayouts)
//
// PR 6 of the text pipeline.  Enables animating text content by switching
// between TextDocument snapshots at keyframe boundaries, with optional
// layout crossfade during the transition gap.
//
// Usage:
//   AnimatedTextDocument anim;
//   anim.add_keyframe(Frame{0},   doc_a, SourceTextTransition::Hold);
//   anim.add_keyframe(Frame{60},  doc_b, SourceTextTransition::DissolveLayouts);
//   anim.add_keyframe(Frame{120}, doc_c, SourceTextTransition::Cut);
//
//   ActiveTextState state = anim.sample_at(Frame{90});
//   // state.active → doc_b (crossfading in)
//   // state.dissolve_from → doc_a (fading out)
//   // state.mix → 0.5 (halfway through the gap)
//   // state.transition → DissolveLayouts
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/text/text_document.hpp>
#include <chronon3d/animation/easing/easing.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d {

// ── SourceTextTransition — how text changes at a keyframe boundary ─────

/// Determines the behaviour between two consecutive SourceTextKeyframes.
///
/// Hold:               The previous document remains visible until the next
///                      keyframe arrives, then the new document replaces it
///                      instantly (step function).  During the gap, only the
///                      previous document is active.
///
/// Cut:                The previous document stays visible until the next
///                      keyframe boundary, then the document instantly
///                      switches to the new value.  No crossfade.
///                      Semantically identical to Hold for the visible
///                      text, but signals the intent of an editorial cut
///                      (useful for render-graph optimisation — no need
///                      to keep the outgoing layout alive).
///
/// DissolveLayouts:    During the gap between keyframes, both the outgoing
///                      and incoming documents are active.  The renderer
///                      dissolves from the outgoing to the incoming layout
///                      using a `mix` factor from 0 (fully previous) to 1
///                      (fully next).  This is a pure alpha dissolve, not
///                      a positional morph.
///
/// Scramble:           During the gap, characters are randomly substituted
///                      using a deterministic seed.  At mix=0 the text is
///                      the previous document; at mix=1 it settles into the
///                      next document.  Intermediate frames contain pseudo-
///                      random substitutions from a configurable character
///                      pool (alphanumeric by default).  The active state
///                      carries `scrambled_text` with the per-frame output.
///
/// Morph:              Characters common to both documents persist at their
///                      positions; new characters enter from the sides;
///                      removed characters exit.  The `morphed_text` in
///                      ActiveTextState contains the per-frame interpolated
///                      text, and `morph_map` maps positions from the
///                      previous layout to the next.
enum class SourceTextTransition : u8 {
    Hold,
    Cut,
    DissolveLayouts,
    Scramble,
    Morph,
};

// ── SourceTextKeyframe — a TextDocument snapshot at a specific frame ────

/// Parameters for Scramble transition.
struct ScrambleParams {
    /// Deterministic seed for the pseudo-random substitution.
    /// Same seed + same frame + same glyph index = same substitution.
    u64 seed{12345};

    /// Character pool used for random substitution (default: alphanumeric).
    /// Characters are drawn from this pool uniformly.  Must not be empty.
    std::string char_pool{"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"};

    /// How fast characters scramble (higher = more substitutions per frame).
    /// A value of 1.0 means each character has a 100% chance of substitution
    /// at the midpoint of the transition.  Values > 1.0 saturate faster.
    f32 intensity{1.5f};
};

/// Parameters for Morph transition.
struct MorphParams {
    /// Characters enter from the left, right, or both sides depending
    /// on alignment relative to the text layout.
    enum class EnterDirection : u8 {
        FromRight,   // new chars slide in from the right (LTR default)
        FromLeft,    // new chars slide in from the left (RTL default)
        FromCenter,  // new chars expand outward from the center
    };

    EnterDirection direction{EnterDirection::FromRight};

    /// Easing curve applied to per-character entry/exit timing.
    EasingCurve easing{EasingCurve{Easing::OutCubic}};
};

struct SourceTextKeyframe {
    /// Frame index when this document becomes active.
    Frame frame{0};

    /// The text document bound to this keyframe.
    /// Copied by value — AnimatedTextDocument owns the full document tree.
    TextDocument document;

    /// How to transition FROM this keyframe TO the next one.
    /// Default is Hold (step function).
    SourceTextTransition transition{SourceTextTransition::Hold};

    /// Parameters for Scramble transition (only meaningful when
    /// transition == Scramble).
    ScrambleParams scramble_params{};

    /// Parameters for Morph transition (only meaningful when
    /// transition == Morph).
    MorphParams morph_params{};

    /// For std::sort / std::lower_bound.
    bool operator<(const SourceTextKeyframe& other) const {
        return frame < other.frame;
    }
    bool operator<(Frame f) const { return frame < f; }
};

// ── ActiveTextState — result of sampling AnimatedTextDocument ───────────

/// Returned by AnimatedTextDocument::sample_at().  Describes which
/// document(s) are active at a given frame and how to blend them.
struct ActiveTextState {
    /// The primary document to render (always present when keyframes exist).
    const TextDocument* active{nullptr};

    /// When transition == DissolveLayouts and we're inside the crossfade
    /// gap, this points to the outgoing (previous) document.  Null otherwise.
    const TextDocument* dissolve_from{nullptr};

    /// 0.0 = fully dissolve_from, 1.0 = fully active.
    /// Only meaningful when transition == DissolveLayouts, Scramble, or Morph;
    /// zero otherwise.
    float mix{0.0f};

    /// The transition type in effect at this frame.
    SourceTextTransition transition{SourceTextTransition::Hold};

    /// ── Scramble / Morph: per-frame generated text ────────────────────
    /// During a Scramble transition, contains the pseudo-randomly
    /// substituted text for this frame (character-by-character replacement
    /// from prev toward next).  Empty when not in Scramble mode.
    ///
    /// During a Morph transition, contains the per-frame interpolated
    /// text (common characters persist, new ones enter, removed ones exit).
    /// Empty when not in Morph mode.
    std::string transition_text;

    /// ── Morph: character position map ─────────────────────────────────
    /// Maps character positions from the previous layout to the next.
    /// Each entry is (prev_index, next_index) where the character at
    /// prev_index in dissolve_from maps to next_index in active.
    /// -1 means the position has no counterpart (new or removed char).
    /// Only populated when transition == Morph.
    std::vector<std::pair<int, int>> morph_map;
};

// ── AnimatedTextDocument — keyframe-driven text document animation ─────

/// Container of SourceTextKeyframes that can be sampled at any frame to
/// produce the active document state.
///
/// Keyframes are sorted by frame index.  The first keyframe is active from
/// frame 0 up to its frame; between keyframes, the transition specified by
/// the EARLIER keyframe governs behaviour.
///
/// Thread-safe for concurrent reads after all keyframes have been added.
class AnimatedTextDocument {
public:
    AnimatedTextDocument() = default;

    /// Add a keyframe.  Keyframes are kept sorted by frame after insertion.
    /// Duplicate frames overwrite the previous keyframe at that frame.
    void add_keyframe(SourceTextKeyframe kf);

    /// Sample the active text state at the given frame.
    /// Returns an empty state (active = nullptr) if no keyframes exist.
    [[nodiscard]] ActiveTextState sample_at(Frame frame) const;

    /// Number of keyframes.
    [[nodiscard]] size_t size() const { return m_keyframes.size(); }

    /// Read-only access to keyframes.
    [[nodiscard]] const std::vector<SourceTextKeyframe>& keyframes() const {
        return m_keyframes;
    }

    /// Remove all keyframes.
    void clear() { m_keyframes.clear(); }

private:
    std::vector<SourceTextKeyframe> m_keyframes;
};

} // namespace chronon3d
