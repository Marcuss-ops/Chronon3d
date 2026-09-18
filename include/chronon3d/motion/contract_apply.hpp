#pragma once
// ==============================================================================
// chronon3d/motion/contract_apply.hpp
//
// ADR-032 — the consume half of the "zona franca".
//
// contract_bridge.hpp *produces* the wire records; this header is the only
// place that reads them back into a frame the renderer is about to execute.
// Together they are the two ends of the handshake: the motion side decides
// WHERE and WHEN, and the content side decides WHAT — and this is where the
// "where" arrives.
//
// The injection point is deliberate and single: render_pipeline.cpp samples
// FrameExecution::parameters() with TypedProgramSampler (the only frame-varying
// producer on that route) and hands that block to graph execution as
// `frame_parameters`. apply_layer_states() runs on exactly that block, between
// the two, so an override cannot reach one consumer and miss another.
//
// WHAT IS OVERRIDDEN
//   matrix   the full projection·view·world·local product, unpacked from the
//            ABI's row-major storage back into GLM's column-major one
//   opacity  clamped into [0, 1]
//   active   from the visibility bit: the motion side owns WHEN
//
// WHAT IS NOT
//   kind, text_index and payload belong to the content side and are never
//   touched here. `depth` is carried by the record for downstream sorting and
//   has no field in SampledLayerParameters, so this route does not consume it.
//
// THE LAYER BINDING (the one assumption this seam rests on)
//   A record addresses a layer through `chronon_layer_id`, declared opaque in
//   <chronon/abi.h>. The binding is: the motion side authors against the
//   composition's node ids, so the contract id is the NodeId widened to 64 bits.
//   Anything that cannot address a NodeId — 0 (reserved by the ABI, never a
//   valid id) or a value above UINT32_MAX — is refused rather than dropped.
// ==============================================================================

#include <chronon/abi.h>

#include <chronon3d/compiler/frame_parameters.hpp>

#include <cstddef>
#include <cstdint>

namespace chronon3d::motion {

/// What apply_layer_states() did, so a caller can tell a partial frame from a
/// refused one without guessing.
struct ContractApplyReport {
    /// Records that matched a sampled layer and overrode its transform.
    std::uint32_t applied{0};
    /// Of `applied`, how many switched the layer off (visibility bit clear).
    std::uint32_t deactivated{0};
    /// Records that address no sampled layer. Tolerated and reported: a
    /// composition that draws fewer layers than the motion side tracks is a
    /// partial frame, not a malformed one.
    std::uint32_t unmatched{0};
    /// Records refused by the ABI discipline: too-small struct_size, reserved
    /// or out-of-range layer id, a duplicate record for one layer, or a matrix
    /// that is not finite. These are producer bugs, so they fail the call
    /// instead of silently producing a frame nobody can explain.
    std::uint32_t rejected{0};

    [[nodiscard]] bool accepted() const noexcept { return rejected == 0u; }
};

/// Check the ABI discipline of a record set without touching a frame.
///
/// Same predicates as apply_layer_states() — one policy, one implementation —
/// for callers that must refuse a bad frame *before* rendering it (the C ABI
/// maps a refusal to CHRONON_ERROR_ABI_MISMATCH without starting a render).
/// With no block to match against, only `rejected` is meaningful: `applied`,
/// `deactivated` and `unmatched` stay 0 because nothing was matched.
[[nodiscard]] bool validate_layer_states(const chronon_layer_state* states,
                                         std::size_t count,
                                         ContractApplyReport* report = nullptr) noexcept;

/// Override one frame's sampled layer parameters with the motion side's
/// contract records.
///
/// Pure and allocation-free: no session, no registry, no diagnostics. The block
/// is refilled in place (the frame slot owns its vector, so the frame path keeps
/// its steady-state zero-allocation property), and `report` is optional.
///
/// Returns true when every record was accepted — applied or unmatched. Returns
/// false when at least one was rejected, which is the only case the caller has
/// to act on; the frame is still coherent for the records that were accepted, so
/// a caller that renders anyway gets a defined frame rather than a torn one.
[[nodiscard]] bool apply_layer_states(const chronon_layer_state* states,
                                      std::size_t count,
                                      compiler::FrameParameterBlock& block,
                                      ContractApplyReport* report = nullptr) noexcept;

}  // namespace chronon3d::motion
