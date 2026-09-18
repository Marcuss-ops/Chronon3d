#pragma once
// ==============================================================================
// chronon3d/motion/contract_bridge.hpp
//
// ADR-032 — the "zona franca": the single C++ boundary between the motion
// side (decides WHERE and WHEN) and the content side (decides WHAT).
//
// This header is the ONLY place where internal scene/motion types are
// converted into the `chronon/abi.h` contract-zone PODs. The conversion is
// pure, allocation-free and deterministic: the same inputs in any evaluation
// order produce bit-identical `chronon_layer_state` values.
//
// BOUNDARY (docs/architecture/chronon-c-abi-boundary.md):
//   - No camera authoring type crosses: the caller supplies the final
//     clip_from_local product (projection·view·world·local).
//   - No content payload crosses: only the opaque id + measured metrics.
//   - No STL / virtual / scene-graph type exists on the wire side.
// ==============================================================================

#include <chronon/abi.h>

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/layer/content_ref.hpp>

#include <cstdint>

namespace chronon3d::motion {

// ── Contract version ────────────────────────────────────────────────────────
/// Contract-zone version implemented by this bridge. Independent of the
/// engine ABI major; bumps whenever a contract-zone record changes shape.
inline constexpr uint32_t kContractZoneVersion = 1u;

/// Contract-zone version check: true when the C symbol matches this header.
/// A mismatch means the loaded library and this header disagree on the
/// wire format — consumers must fail closed.
[[nodiscard]] bool contract_version_matches() noexcept;

// ── Metrics handshake (Chronon → Motion3D) ─────────────────────────────────
/// Pack the measured content facts into the contract POD. `ContentRef` maps
/// its kind axis 1:1 onto `chronon_content_kind` (values are ABI-frozen);
/// `NoContent` yields `CHRONON_CONTENT_NONE` with id 0.
[[nodiscard]] chronon_content_metrics make_content_metrics(ContentKind kind,
                                                            ContentAssetId id,
                                                            uint32_t width_px,
                                                            uint32_t height_px,
                                                            float anchor_x,
                                                            float anchor_y,
                                                            uint64_t fingerprint) noexcept;

// ── Layer-state handshake (Motion3D → Chronon) ─────────────────────────────
/// One layer's final compiled state at one frame. Every input is already
/// resolved: the caller composed the hierarchy (parent ⊗ local) and folded
/// camera projection + view before calling. This function never computes
/// animation and never looks anything up.
struct LayerStateInput {
    chronon_layer_id layer{0};
    chronon_content_id content{0};
    /// Final transform: projection · view · world · local (column-vector
    /// GLM convention, as produced by math::camera_* helpers).
    Mat4 clip_from_local{1.0f};
    float opacity{1.0f};
    bool visible{true};
    /// Forward depth of the layer origin in NDC (0..1). Optional:
    /// recomputed from clip_from_local when left negative.
    float depth{-1.0f};
};

/// Pack the compiled layer state into the contract POD. Non-finite matrix /
/// scalar inputs are sanitized to zero-rotation identity behaviour by the
/// caller's responsibility; this function clamps opacity into [0,1] and
/// recomputes `depth` from the matrix when the input depth is negative.
[[nodiscard]] chronon_layer_state make_layer_state(const LayerStateInput& in) noexcept;

/// Convenience overload for the scene model: derives the content id/kind
/// from the ADR-031 `ContentRef` taxonomy.
[[nodiscard]] chronon_layer_state make_layer_state(const LayerStateInput& in,
                                                   const ContentRef& content) noexcept;

// ── Converters ─────────────────────────────────────────────────────────────
/// Row-major ABI storage from the column-vector GLM convention.
[[nodiscard]] chronon_layer_state with_clip_from_local(const chronon_layer_state& base,
                                                       const Mat4& clip_from_local) noexcept;

/// ContentRef → contract kind (NoContent → CHRONON_CONTENT_NONE).
[[nodiscard]] chronon_content_kind to_contract_kind(ContentKind kind) noexcept;

} // namespace chronon3d::motion
