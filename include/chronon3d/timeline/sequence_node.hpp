// ── include/chronon3d/timeline/sequence_node.hpp
//
// Sequence V2 — tree-structured sequence model with nested timeline support.
//
// Design:
//   SequenceRange  — temporal window (from / duration / trim_before)
//   SequenceNode   — recursive tree node (name + range + children)
//   ResolvedSequence — output of resolving one node at a given frame
//
// These types are the data model consumed by TimelineResolver (in
// timeline_resolver.hpp).  They do NOT modify the existing SequenceContext
// or SceneBuilder::sequence() — those remain backward-compatible.
//
// The key insight: a Sequence is NOT a layer.  It is a temporal scope
// that maps a parent frame to a local frame.  Layers are built by the
// SceneBuilder using the resolved local FrameContext.

#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <algorithm>
#include <string>
#include <vector>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// SequenceRange — temporal window for a sequence
// ═══════════════════════════════════════════════════════════════════════════
//
// Unlike TimeRange (start/end model in time.hpp), SequenceRange uses
// the from/duration model with an optional trim_before offset for
// local frame remapping.
//
// Example:
//   global frame 50, sequence from=30, duration=60, trim_before=0
//   → contains(50) = true
//   → to_local(50) = 50 - 30 + 0 = 20
//
// Example with trim_before:
//   global frame 50, sequence from=30, duration=60, trim_before=10
//   → to_local(50) = 50 - 30 + 10 = 30
//   (useful for "start this sequence 10 frames into its content")
//
struct SequenceRange {
    Frame from{0};
    Frame duration{0};
    Frame trim_before{0};

    /// Returns true if parent_frame falls within [from, from + duration).
    /// A zero-duration sequence never contains any frame.
    [[nodiscard]] constexpr bool contains(Frame parent_frame) const {
        if (parent_frame < from) return false;
        if (duration <= Frame{0}) return false;
        if (parent_frame >= from + duration) return false;
        return true;
    }

    /// Maps a parent frame to the local frame within this sequence.
    /// Precondition: contains(parent_frame) should be true for meaningful results.
    [[nodiscard]] constexpr Frame to_local(Frame parent_frame) const {
        return parent_frame - from + trim_before;
    }

    /// The exclusive end frame (from + duration).
    [[nodiscard]] constexpr Frame end() const {
        return from + duration;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// SequenceNode — a node in the recursive sequence tree
// ═══════════════════════════════════════════════════════════════════════════
//
// Sequences can be nested:
//   sequence("chapter", {from=100, duration=200})
//     sequence("title", {from=0, duration=30})
//     sequence("body",  {from=30, duration=150})
//
// Each child's range is relative to the parent's LOCAL frame.
// At global frame 120: chapter.local=20, title would contain(20)=false
//   (title is from=0..30, so it contains frame 20) → title.local=20
//
// Layers are NOT stored here — the tree is purely temporal.
// The SceneBuilder uses resolved contexts to build layers.
//
struct SequenceNode {
    std::string name;
    SequenceRange range;
    std::vector<SequenceNode> children;

    /// Add a child sequence and return a reference for chaining.
    SequenceNode& add_child(std::string child_name, SequenceRange child_range) {
        children.push_back({std::move(child_name), child_range, {}});
        return children.back();
    }

    /// Add a child SequenceNode (move).
    void add_child(SequenceNode child) {
        children.push_back(std::move(child));
    }

    /// Returns true if this node has no children.
    [[nodiscard]] bool is_leaf() const noexcept {
        return children.empty();
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// ResolvedSequence — result of resolving one SequenceNode at a frame
// ═══════════════════════════════════════════════════════════════════════════
//
// Represents one level in the active path through the sequence tree.
// The TimelineResolver produces a vector of these (outermost to innermost).
//
struct ResolvedSequence {
    std::string name;
    Frame local_frame{0};
    Frame duration{0};
    f32 progress{0.0f};
};

} // namespace chronon3d
