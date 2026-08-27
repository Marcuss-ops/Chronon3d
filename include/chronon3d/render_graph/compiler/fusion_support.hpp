#pragma once

// ═══════════════════════════════════════════════════════════════════════
// fusion_support.hpp — SINGLE authority for layer-batch fusion capability.
//
// The frame graph compiler decides whether a node may participate in a
// fused GPU layer batch by consuming EXCLUSIVELY `fusion_support()`.
// No parallel fusible lists, no scattered dynamic_cast+kind conditionals:
// when a node kind's support changes, it changes HERE in exactly one row
// of the switch below.
//
// Contract (Bug 1 — fusion must never delete reachable content):
//   StandaloneOnly    → the compiler must emit a standalone operation for
//                       this node. lowered_into_batch stays false forever,
//                       regardless of optimization level or batch state.
//   FusedSource       → may start/accumulate as a fused batch source.
//   FusedContinuation → may extend an ALREADY-OPEN batch; without an open
//                       batch it degrades to StandaloneOnly behaviour.
//
// Current V1 payload gate: the GPU layer batch executor accepts only Image
// payloads, so Source/Video nodes are FusedSource ONLY when their shape
// payload is image-only. TextRun is StandaloneOnly until CompiledLayerBatch
// semantically owns draw_text_run (flip its single row here to change that).
// ═══════════════════════════════════════════════════════════════════════

#include <chronon3d/render_graph/core/node_identity.hpp>
#include <cstdint>

namespace chronon3d::graph {

enum class FusionSupport : std::uint8_t {
    StandaloneOnly,      ///< never enters a fused layer batch
    FusedSource,         ///< may start / accumulate inside a fused batch
    FusedContinuation    ///< may extend an already-open fused batch
};

/// Payload facts consumed alongside the node kind. The compiler derives
/// these from the node's structural metadata; they are DATA, not policy.
struct LayerFusionContext {
    /// True iff the node's render payload is an Image accepted by the GPU
    /// layer batch executor (single-source Image or all-aggregate-items Image).
    bool image_only_payload{false};
};

/// Single classification function. constexpr over the kind switch so the
/// compiler can fold entire rows away for fixed kinds.
[[nodiscard]] constexpr FusionSupport fusion_support(
    RenderGraphNodeKind kind,
    const LayerFusionContext& ctx) noexcept
{
    switch (kind) {
        case RenderGraphNodeKind::Source:
        case RenderGraphNodeKind::Video:
            // Batch executor accepts only Image payloads today.
            return ctx.image_only_payload ? FusionSupport::FusedSource
                                          : FusionSupport::StandaloneOnly;

        case RenderGraphNodeKind::Transform:
        case RenderGraphNodeKind::Composite:
            return FusionSupport::FusedContinuation;

        case RenderGraphNodeKind::TextRun:      // until batch owns draw_text_run
        case RenderGraphNodeKind::Effect:
        case RenderGraphNodeKind::Mask:
        case RenderGraphNodeKind::Precomp:
        case RenderGraphNodeKind::Adjustment:
        case RenderGraphNodeKind::MotionBlur:
        case RenderGraphNodeKind::ColorConvert:
        case RenderGraphNodeKind::TrackMatte:
        case RenderGraphNodeKind::Output:
        case RenderGraphNodeKind::Transition:
        case RenderGraphNodeKind::ClipTransition:
        default:
            return FusionSupport::StandaloneOnly;
    }
}

} // namespace chronon3d::graph
