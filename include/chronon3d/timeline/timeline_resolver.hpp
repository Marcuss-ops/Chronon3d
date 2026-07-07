// ── include/chronon3d/timeline/timeline_resolver.hpp
//
// Sequence V2 — TimelineResolver: resolves the sequence tree at a given
// global frame, producing local FrameContexts for each active sequence.
//
// Design:
//   The resolver walks the SequenceNode tree and produces:
//     1. A list of active ResolvedSequence entries (the active path)
//     2. A FrameContext for each active node (with correct local_frame)
//
// This is the single resolver that the render pipeline uses to determine
// which sequences are active and what local frame each sees.
//
// Example:
//   sequence("chapter", from=100, duration=200)
//     sequence("title", from=20, duration=60)
//
//   At global frame 120:
//     chapter.local = 120 - 100 = 20
//     title.local = 20 - 20 = 0
//     → active_path = [ {chapter, local=20, progress=0.1}, {title, local=0, progress=0.0} ]
//     → effective_context.frame = 0
//
// Nested sequences guarantee:
//   global 120 → chapter local=20 → title local=0
//   (the innermost context sees frame 0, not 120)

#pragma once

#include <chronon3d/timeline/sequence_node.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <algorithm>
#include <optional>
#include <vector>

namespace chronon3d {

class TimelineResolver {
public:
    // ── Resolution result ────────────────────────────────────────────────
    struct Resolution {
        /// Active path from outermost to innermost sequence.
        std::vector<ResolvedSequence> active_path;

        /// The innermost FrameContext with correct local_frame, duration,
        /// and preserved sub-frame fraction.
        FrameContext effective_context;
    };

    // ── Primary entry point ──────────────────────────────────────────────

    /// Resolve the full sequence tree against a global FrameContext.
    /// Returns one Resolution per active leaf/node in the tree.
    ///
    /// For a flat tree (one level), returns one Resolution per active child.
    /// For nested trees, returns one Resolution per active path through
    /// the tree (leaf-level contexts).
    ///
    /// Non-leaf active nodes also emit a Resolution so the caller can
    /// use intermediate contexts if needed.
    [[nodiscard]] static std::vector<Resolution> resolve(
        const std::vector<SequenceNode>& roots,
        const FrameContext& ctx
    ) {
        std::vector<Resolution> out;
        std::vector<ResolvedSequence> path;
        for (const auto& node : roots) {
            resolve_node(node, ctx, path, out);
        }
        return out;
    }

    // ── Convenience: resolve a single node ───────────────────────────────

    /// Resolve a single SequenceNode.  Returns the first active Resolution
    /// or nullopt if the node is not active at the given frame.
    [[nodiscard]] static std::optional<Resolution> resolve_one(
        const SequenceNode& node,
        const FrameContext& ctx
    ) {
        std::vector<Resolution> all = resolve({node}, ctx);
        if (all.empty()) return std::nullopt;
        return all.front();
    }

    // ── Convenience: check if any sequence is active ─────────────────────

    /// Returns true if any node in the tree is active at the given frame.
    [[nodiscard]] static bool any_active(
        const std::vector<SequenceNode>& roots,
        const FrameContext& ctx
    ) {
        for (const auto& node : roots) {
            if (node_active(node, ctx)) return true;
        }
        return false;
    }

private:
    // ── Recursive resolver ───────────────────────────────────────────────

    static void resolve_node(
        const SequenceNode& node,
        const FrameContext& parent_ctx,
        std::vector<ResolvedSequence>& current_path,
        std::vector<Resolution>& out
    ) {
        // Active check: parent's current frame must be within [from, from+duration)
        if (!node.range.contains(parent_ctx.frame)) {
            return;
        }

        // Compute local frame
        const Frame local = node.range.to_local(parent_ctx.frame);

        // Build local FrameContext
        FrameContext local_ctx = parent_ctx;
        local_ctx.frame = local;
        local_ctx.local_frame = local;
        local_ctx.duration = node.range.duration;
        // Preserve sub-frame fraction from parent
        local_ctx.frame_time = parent_ctx.frame_time;

        // Compute progress
        const f32 progress = (node.range.duration > Frame{0})
            ? std::clamp(
                static_cast<f32>(local) / static_cast<f32>(node.range.duration),
                0.0f, 1.0f)
            : 0.0f;

        // Push onto active path
        current_path.push_back(ResolvedSequence{
            .name = node.name,
            .local_frame = local,
            .duration = node.range.duration,
            .progress = progress
        });

        // Emit resolution for this node (every active node emits,
        // not just leaves — the caller decides what to do)
        out.push_back(Resolution{
            .active_path = current_path,
            .effective_context = local_ctx
        });

        // Recurse into children (they use the LOCAL context)
        for (const auto& child : node.children) {
            resolve_node(child, local_ctx, current_path, out);
        }

        // Pop from active path (backtrack)
        current_path.pop_back();
    }

    // ── Quick active check (no allocation) ───────────────────────────────
    // Returns true if the node is active at the given frame.
    // If the node itself is not active, no descendant can be active either
    // (children's frames are relative to the parent's local frame, which
    // is undefined when the parent is inactive).

    [[nodiscard]] static bool node_active(
        const SequenceNode& node,
        const FrameContext& ctx
    ) {
        return node.range.contains(ctx.frame);
    }
};

} // namespace chronon3d
