#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// animated_text_document.hpp — Animated text document with keyframe
// transitions (Hold, Cut, CrossfadeLayouts)
//
// PR 6 of the text pipeline.  Enables animating text content by switching
// between TextDocument snapshots at keyframe boundaries, with optional
// layout crossfade during the transition gap.
//
// Usage:
//   AnimatedTextDocument anim;
//   anim.add_keyframe(Frame{0},   doc_a, SourceTextTransition::Hold);
//   anim.add_keyframe(Frame{60},  doc_b, SourceTextTransition::CrossfadeLayouts);
//   anim.add_keyframe(Frame{120}, doc_c, SourceTextTransition::Cut);
//
//   ActiveTextState state = anim.sample_at(Frame{90});
//   // state.active → doc_b (crossfading in)
//   // state.crossfade_from → doc_a (fading out)
//   // state.mix → 0.5 (halfway through the gap)
//   // state.transition → CrossfadeLayouts
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/text/text_document.hpp>

#include <cstddef>
#include <optional>
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
/// Cut:                At the keyframe boundary, the document instantly
///                      switches to the new value.  No crossfade.
///                      Semantically identical to Hold for discrete text,
///                      but signals the intent of an editorial cut (useful
///                      for render-graph optimisation — no need to keep
///                      the outgoing layout alive).
///
/// CrossfadeLayouts:   During the gap between keyframes, both the outgoing
///                      and incoming documents are active.  The renderer can
///                      interpolate glyph positions and opacity to create a
///                      smooth layout transition.  The active state carries
///                      a `mix` factor from 0 (fully previous) to 1 (fully
///                      next).
enum class SourceTextTransition : u8 {
    Hold,
    Cut,
    CrossfadeLayouts,
};

// ── SourceTextKeyframe — a TextDocument snapshot at a specific frame ────

struct SourceTextKeyframe {
    /// Frame index when this document becomes active.
    Frame frame{0};

    /// The text document bound to this keyframe.
    /// Copied by value — AnimatedTextDocument owns the full document tree.
    TextDocument document;

    /// How to transition FROM this keyframe TO the next one.
    /// Default is Hold (step function).
    SourceTextTransition transition{SourceTextTransition::Hold};

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

    /// When transition == CrossfadeLayouts and we're inside the crossfade
    /// gap, this points to the outgoing (previous) document.  Null otherwise.
    const TextDocument* crossfade_from{nullptr};

    /// 0.0 = fully crossfade_from, 1.0 = fully active.
    /// Only meaningful when transition == CrossfadeLayouts; zero otherwise.
    float mix{0.0f};

    /// The transition type in effect at this frame.
    SourceTextTransition transition{SourceTextTransition::Hold};
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
