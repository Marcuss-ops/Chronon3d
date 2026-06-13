#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// composite_operator.hpp — Extended compositing operators for Chronon3D
//
// These operators control how a source layer interacts with the backdrop
// beyond the standard SourceOver (Porter-Duff "over") blend.
//
// Stencil:    a fully opaque source masks the backdrop (backdrop *= source alpha)
// Silhouette: the inverse — backdrop *= 1 - source alpha
//
// These are conceptually similar to track mattes but operate at the composite
// level rather than as a separate layer node.
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/core/types/types.hpp>

namespace chronon3d {

enum class CompositeOperator : uint8_t {
    SourceOver      = 0,  // Standard Porter-Duff "over"
    StencilAlpha    = 1,  // backdrop *= source alpha
    StencilLuma     = 2,  // backdrop *= source luma
    SilhouetteAlpha = 3,  // backdrop *= 1 - source alpha
    SilhouetteLuma  = 4,  // backdrop *= 1 - source luma
};

/// Returns true if the composite operator is a stencil or silhouette variant
/// (i.e., it modifies the backdrop rather than blending source over it).
inline bool is_stencil_or_silhouette(CompositeOperator op) {
    return op != CompositeOperator::SourceOver;
}

} // namespace chronon3d
